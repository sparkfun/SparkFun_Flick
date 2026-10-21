/*
 * sketch_loader.c - maps a sketch image at its fixed address and runs it.
 * The chip-specific MMU work is in the target's bl_port_mmu.c.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "heap_memory_layout.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_port.h"
#include "bl_state.h"
#include "sketch_loader.h"

#define SKETCH_SUBTYPE      ((esp_partition_subtype_t)0x40)
#define MMU_PAGE            0x10000
#define SKETCH_PRIORITY     1
#if CONFIG_FREERTOS_UNICORE
#define SKETCH_CORE         0       /* single-core chips (ESP32-C3, C5, C6) */
#else
#define SKETCH_CORE         1       /* WiFi/BT and the loader run on core 0 */
#endif
#define SKETCH_MAX_STACK    65536

_Static_assert(sizeof(bl_sketch_header_t) == BL_SKETCH_HEADER_SIZE, "header size");
_Static_assert(offsetof(bl_sketch_header_t, image_size) == BL_HDR_OFF_IMAGE_SIZE, "image_size offset");
_Static_assert(offsetof(bl_sketch_header_t, build_id) == BL_HDR_OFF_BUILD_ID, "build_id offset");
_Static_assert(offsetof(bl_sketch_header_t, sha256) == BL_HDR_OFF_SHA256, "sha256 offset");

/* Keep the heap out of the sketch's fixed RAM window. If Baseline's own static
 * data ever grows into this range, heap init aborts at boot with an
 * "overlapping reserved regions" error instead of corrupting memory. */
SOC_RESERVE_MEMORY_REGION(BL_SKETCH_RAM_BASE, BL_SKETCH_RAM_BASE + BL_SKETCH_RAM_SIZE, bl_sketch_ram);

extern char _heap_start;        /* end of Baseline static DRAM (linker) */

static bl_sketch_header_t s_hdr;
static bl_sketch_callbacks_t s_cb;
static TaskHandle_t s_task;
static const char *s_status = "not loaded";

const esp_partition_t *sketch_slot_partition(int slot)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SKETCH_SUBTYPE,
                                    slot ? "sketch_b" : "sketch_a");
}

static bool in_range(uint32_t start, uint32_t end, uint32_t lo, uint32_t hi)
{
    return lo <= start && start <= end && end <= hi;
}

static const char *check_header(const bl_sketch_header_t *h, uint32_t part_size)
{
    if (h->magic != BL_SKETCH_MAGIC) {
        return "empty slot";
    }
    if (h->header_version != BL_SKETCH_HEADER_VERSION) {
        return "unsupported header version";
    }
    if (h->chip_id != BL_TARGET_CHIP_ID) {
        return "sketch built for a different chip (rebuild it for " BL_TARGET_NAME ")";
    }
    if (h->abi_major != BL_ABI_MAJOR) {
        return "ABI major version mismatch: rebuild the sketch with this SDK";
    }
    if (h->abi_minor > BL_ABI_MINOR) {
        return "sketch needs a newer Baseline";
    }
    uint32_t size = h->image_size;
    if (size < BL_SKETCH_HEADER_SIZE || size > part_size || size > BL_SKETCH_MAX_SIZE || (size & 3)) {
        return "bad image size";
    }
    uint32_t code_lo = BL_SKETCH_IBUS_BASE + BL_SKETCH_HEADER_SIZE, code_hi = BL_SKETCH_IBUS_BASE + size;
    uint32_t ro_lo = BL_SKETCH_DBUS_BASE + BL_SKETCH_HEADER_SIZE, ro_hi = BL_SKETCH_DBUS_BASE + size;
    uint32_t ram_lo = BL_SKETCH_RAM_BASE, ram_hi = BL_SKETCH_RAM_BASE + BL_SKETCH_RAM_SIZE;
    if (h->entry < code_lo || h->entry >= code_hi) {
        return "entry point outside image";
    }
    if (!in_range(h->data_start, h->data_end, ram_lo, ram_hi) ||
        !in_range(h->bss_start, h->bss_end, ram_lo, ram_hi)) {
        return ".data/.bss outside sketch RAM";
    }
    if (h->data_end > h->data_start &&
        !in_range(h->data_load, h->data_load + (h->data_end - h->data_start), ro_lo, ro_hi)) {
        return ".data initializers outside image";
    }
    if (h->init_array_end > h->init_array_start &&
        (!in_range(h->init_array_start, h->init_array_end, ro_lo, ro_hi) ||
         ((h->init_array_end - h->init_array_start) & 3))) {
        return "bad init_array";
    }
    if (h->stack_size < BL_SKETCH_MIN_STACK || h->stack_size > SKETCH_MAX_STACK) {
        return "bad stack size";
    }
    return NULL;
}

static const char *check_sha256(const esp_partition_t *p, const bl_sketch_header_t *h)
{
    const size_t chunk = 4096;
    uint8_t *buf = malloc(chunk);
    if (!buf) {
        return "out of memory";
    }
    uint8_t digest[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    const char *err = NULL;
    for (uint32_t off = 0; off < h->image_size; off += chunk) {
        size_t n = h->image_size - off < chunk ? h->image_size - off : chunk;
        if (esp_partition_read(p, off, buf, n) != ESP_OK) {
            err = "flash read failed";
            break;
        }
        if (off == 0) {
            memset(buf + BL_HDR_OFF_SHA256, 0, 32);     // hash covers the header with this field zeroed
        }
        mbedtls_sha256_update(&ctx, buf, n);
    }
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    free(buf);
    if (!err && memcmp(digest, h->sha256, 32) != 0) {
        err = "SHA-256 mismatch (corrupt image)";
    }
    return err;
}

const char *sketch_check(const esp_partition_t *p, bl_sketch_header_t *out)
{
    if (!p) {
        return "sketch partition missing (wrong partition table?)";
    }
    if (esp_partition_read(p, 0, out, sizeof(*out)) != ESP_OK) {
        return "flash read failed";
    }
    const char *err = check_header(out, p->size);
    return err ? err : check_sha256(p, out);
}

static void stable_cb(void *arg)
{
    bl_state_sketch_stable();
    BLOG_I("sketch '%s' is stable", s_hdr.name);
}

static void sketch_task(void *arg)
{
    // Entry first: it sets the sketch's API pointer, which global constructors
    // (a global String, for example) already need.
    ((bl_sketch_entry_t)s_hdr.entry)(&bl_api_table, &s_cb);
    for (uint32_t a = s_hdr.init_array_start; a < s_hdr.init_array_end; a += 4) {
        (*(void (**)(void))a)();
    }
    if (!s_cb.setup || !s_cb.loop) {
        s_status = "sketch has no setup() or loop()";
        BLOG_E("%s", s_status);
        s_task = NULL;
        vTaskDelete(NULL);
    }

    bl_state_sketch_running();
    const esp_timer_create_args_t targs = { .callback = stable_cb, .name = "sk_stable" };
    esp_timer_handle_t timer;
    if (esp_timer_create(&targs, &timer) == ESP_OK) {
        esp_timer_start_once(timer, (uint64_t)BL_STABLE_AFTER_MS * 1000);
    }
    esp_task_wdt_add(NULL);     // a loop() that never returns trips the task watchdog
    s_status = "running";

    s_cb.setup();
    for (;;) {
        s_cb.loop();
        esp_task_wdt_reset();
    }
}

esp_err_t sketch_start(int slot)
{
    const esp_partition_t *p = sketch_slot_partition(slot);
    const char *err = sketch_check(p, &s_hdr);
    if (err) {
        s_status = err;
        BLOG_W("slot %c: %s", 'a' + slot, err);
        return ESP_FAIL;
    }
    s_hdr.name[sizeof(s_hdr.name) - 1] = '\0';

    if (p->address % MMU_PAGE) {
        s_status = "sketch partition not 64 KB aligned";
    } else if ((uint32_t)&_heap_start > BL_SKETCH_RAM_BASE) {
        s_status = "Baseline static RAM overlaps the sketch RAM window";
    } else if (bl_port_window_in_use()) {
        s_status = "sketch address window already in use";
    } else {
        s_status = NULL;
    }
    if (s_status) {
        BLOG_E("cannot load sketch: %s", s_status);
        return ESP_FAIL;
    }

    uint32_t len = (s_hdr.image_size + MMU_PAGE - 1) & ~(MMU_PAGE - 1);
    bl_port_map_window(p->address, len);
    if (memcmp((const void *)BL_SKETCH_DBUS_BASE, &s_hdr, sizeof(s_hdr)) != 0) {
        s_status = "MMU mapping check failed";
        BLOG_E("%s: window does not show the sketch header", s_status);
        return ESP_FAIL;
    }

    memcpy((void *)s_hdr.data_start, (const void *)s_hdr.data_load, s_hdr.data_end - s_hdr.data_start);
    memset((void *)s_hdr.bss_start, 0, s_hdr.bss_end - s_hdr.bss_start);

    BLOG_I("starting sketch '%s' from slot %c (%u bytes, stack %u)",
           s_hdr.name, 'a' + slot, (unsigned)s_hdr.image_size, (unsigned)s_hdr.stack_size);
    s_status = "starting";
    if (xTaskCreatePinnedToCore(sketch_task, "sketch", s_hdr.stack_size, NULL,
                                SKETCH_PRIORITY, &s_task, SKETCH_CORE) != pdPASS) {
        s_status = "could not create sketch task";
        BLOG_E("%s", s_status);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool sketch_is_current_task(void)
{
    return s_task && xTaskGetCurrentTaskHandle() == s_task;
}

bool sketch_wdt_pause(void)
{
    if (!sketch_is_current_task()) {
        return false;
    }
    esp_task_wdt_delete(NULL);
    return true;
}

void sketch_wdt_resume(bool paused)
{
    if (paused) {
        esp_task_wdt_add(NULL);
    }
}

const char *sketch_status(void)
{
    return s_status;
}

const char *sketch_name(void)
{
    return s_hdr.magic == BL_SKETCH_MAGIC ? s_hdr.name : "";
}
