/*
 * bl_impl_idf.cpp - ESP-IDF calls for sketches (ABI 1.10: idf_*, update_*).
 * The SDK gives them their ESP-IDF names
 * (sdk/include/compat/esp_heap_caps.h, esp_mac.h, esp_partition.h,
 * esp_task_wdt.h, the mbedtls headers, Update.h).
 */
#include <Arduino.h>
#include <string.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "spi_flash_mmap.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_state.h"
#include "bl_update.h"
#include "sketch_loader.h"

/* ---- heap ---- */

void *bl_impl_idf_heap_malloc(size_t size, uint32_t caps)
{
    return heap_caps_malloc(size, caps);
}

void *bl_impl_idf_heap_calloc(size_t n, size_t size, uint32_t caps)
{
    return heap_caps_calloc(n, size, caps);
}

void *bl_impl_idf_heap_realloc(void *ptr, size_t size, uint32_t caps)
{
    return heap_caps_realloc(ptr, size, caps);
}

size_t bl_impl_idf_heap_info(uint32_t caps, int what)
{
    switch (what) {
    case BL_HEAP_FREE:     return heap_caps_get_free_size(caps);
    case BL_HEAP_LARGEST:  return heap_caps_get_largest_free_block(caps);
    case BL_HEAP_MIN_FREE: return heap_caps_get_minimum_free_size(caps);
    case BL_HEAP_TOTAL:    return heap_caps_get_total_size(caps);
    default:               return 0;
    }
}

int bl_impl_idf_heap_check(uint32_t caps, int print)
{
    if (caps == MALLOC_CAP_INVALID) {
        return heap_caps_check_integrity_all(print != 0);
    }
    return heap_caps_check_integrity(caps, print != 0);
}

void bl_impl_idf_heap_extmem(size_t limit)
{
    heap_caps_malloc_extmem_enable(limit);
}

/* ---- MAC addresses, error names ---- */

int bl_impl_idf_read_mac(uint8_t *mac, int type)
{
    if (!mac) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_read_mac(mac, (esp_mac_type_t)type);
}

const char *bl_impl_idf_err_name(int err)
{
    return esp_err_to_name(err);
}

/* ---- partition table ---- */

int bl_impl_idf_partition(int index, bl_partition_t *out)
{
    if (index < 0 || !out) {
        return BL_ERR_ARG;
    }
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    for (int i = 0; it && i < index; i++) {
        it = esp_partition_next(it);
    }
    if (!it) {
        return BL_ERR_ARG;
    }
    const esp_partition_t *p = esp_partition_get(it);
    memset(out, 0, sizeof(*out));
    out->type = (uint8_t)p->type;
    out->subtype = (uint8_t)p->subtype;
    out->encrypted = p->encrypted;
    out->address = p->address;
    out->size = p->size;
    strncpy(out->label, p->label, sizeof(out->label) - 1);
    esp_partition_iterator_release(it);
    return BL_OK;
}

/* ---- task watchdog ----
 * Baseline keeps the sketch task on the watchdog itself (it pauses it around
 * long calls, and loop() returning feeds it), so for that task add, delete and
 * status leave it as it is and report ESP_OK, and reset feeds it; the sketch
 * takes it off and back on with disableLoopWDT/enableLoopWDT. The sketch's
 * other tasks are added and removed as asked; bl_impl_rtos.cpp takes a task off
 * before deleting it. */

int bl_impl_idf_task_wdt(int op, void *task)
{
    if (op == BL_TWDT_LOOP_OFF || op == BL_TWDT_LOOP_ON) {
        sketch_wdt_enable(op == BL_TWDT_LOOP_ON);
        return ESP_OK;
    }
    TaskHandle_t t = task ? (TaskHandle_t)task : xTaskGetCurrentTaskHandle();
    bool sketch = sketch_is_current_task() && t == xTaskGetCurrentTaskHandle();
    if (sketch) {
        if (op == BL_TWDT_RESET) {
            sketch_wdt_feed();
        }
        return op <= BL_TWDT_RESET && op >= 0 ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    switch (op) {
    case BL_TWDT_ADD:    return esp_task_wdt_add(t);
    case BL_TWDT_DELETE: return esp_task_wdt_status(t) == ESP_OK ? esp_task_wdt_delete(t) : ESP_ERR_NOT_FOUND;
    case BL_TWDT_STATUS: return esp_task_wdt_status(t);
    case BL_TWDT_RESET:  return esp_task_wdt_reset();     // ESP-IDF feeds the calling task only
    default:             return ESP_ERR_INVALID_ARG;
    }
}

/* ---- certificate and key checks ---- */

static int fill_random(void *, unsigned char *buf, size_t len)
{
    esp_fill_random(buf, len);
    return 0;
}

int bl_impl_idf_tls_check(int what, const uint8_t *data, size_t len)
{
    if (!data || !len) {
        return BL_ERR_ARG;
    }
    int r;
    if (what == BL_TLS_CERT) {
        mbedtls_x509_crt crt;
        mbedtls_x509_crt_init(&crt);
        r = mbedtls_x509_crt_parse(&crt, data, len);
        mbedtls_x509_crt_free(&crt);
    } else if (what == BL_TLS_KEY) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        r = mbedtls_pk_parse_key(&pk, data, len, NULL, 0, fill_random, NULL);
        mbedtls_pk_free(&pk);
    } else {
        return BL_ERR_ARG;
    }
    return r;
}

/* ---- firmware updates from the sketch (backs Update) ----
 * The first 4 bytes say what is being written: BL_SKETCH_MAGIC, a sketch,
 * goes to the free sketch slot (erased a sector at a time as it fills);
 * 0xE9, an ESP-IDF app image, goes to the free app slot through esp_ota_*
 * (boards with two Baseline slots only). update_end checks the image and makes
 * it the next to start; the sketch restarts the board when it is ready. */

enum { UPD_NONE, UPD_WAITING, UPD_SKETCH, UPD_APP };

static struct {
    int kind;
    uint32_t size, written, erased;
    uint8_t head[4];
    uint32_t head_fill;
    int slot;
    const esp_partition_t *part;
    esp_ota_handle_t ota;
} s_upd;
static char s_upd_error[96];

static int upd_fail(const char *why)
{
    if (s_upd.kind == UPD_APP && s_upd.ota) {
        esp_ota_abort(s_upd.ota);
    }
    memset(&s_upd, 0, sizeof(s_upd));
    strncpy(s_upd_error, why, sizeof(s_upd_error) - 1);
    BLOG_E("update: %s", why);
    return BL_ERR_FAIL;
}

int bl_impl_update_begin(uint32_t size)
{
    if (s_upd.kind == UPD_APP && s_upd.ota) {
        esp_ota_abort(s_upd.ota);
    }
    memset(&s_upd, 0, sizeof(s_upd));
    s_upd_error[0] = '\0';
    s_upd.kind = UPD_WAITING;
    s_upd.size = size;
    return BL_OK;
}

/* The image's first bytes are in: pick where it goes and get it ready. */
static int upd_start(void)
{
    uint32_t magic;
    memcpy(&magic, s_upd.head, 4);
    if (magic == BL_SKETCH_MAGIC) {
        s_upd.slot = bl_state_active_slot() ^ 1;     // never the running sketch
        s_upd.part = sketch_slot_partition(s_upd.slot);
        if (!s_upd.part) {
            return upd_fail("sketch partition missing");
        }
        if (s_upd.size > s_upd.part->size) {
            return upd_fail("sketch larger than the slot");
        }
        s_upd.kind = UPD_SKETCH;
        return BL_OK;
    }
    if (s_upd.head[0] == 0xE9) {
        if (bl_update_mode() != BL_UPDATE_DUAL) {
            return upd_fail("this board has one app slot: Baseline updates over USB only");
        }
        const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
        if (!p) {
            return upd_fail("no free app slot");
        }
        if (s_upd.size > p->size) {
            return upd_fail("image larger than the app slot");
        }
        WdtPause wdt;       // esp_ota_begin erases the image's size up front
        if (esp_ota_begin(p, s_upd.size ? s_upd.size : OTA_SIZE_UNKNOWN, &s_upd.ota) != ESP_OK) {
            return upd_fail("could not start writing the free app slot");
        }
        s_upd.part = p;
        s_upd.kind = UPD_APP;
        return BL_OK;
    }
    return upd_fail("not a sketch (.flick) or an ESP-IDF app image");
}

static int upd_put(const uint8_t *data, size_t len)
{
    if (s_upd.kind == UPD_APP) {
        WdtPause wdt;
        return esp_ota_write(s_upd.ota, data, len) == ESP_OK ? BL_OK : upd_fail("flash write failed");
    }
    const esp_partition_t *p = s_upd.part;
    if (s_upd.written + len > p->size) {
        return upd_fail("sketch larger than the slot");
    }
    WdtPause wdt;
    while (s_upd.erased < s_upd.written + len) {
        if (esp_partition_erase_range(p, s_upd.erased, SPI_FLASH_SEC_SIZE) != ESP_OK) {
            return upd_fail("erase failed");
        }
        s_upd.erased += SPI_FLASH_SEC_SIZE;
    }
    return esp_partition_write(p, s_upd.written, data, len) == ESP_OK ? BL_OK : upd_fail("flash write failed");
}

int bl_impl_update_write(const uint8_t *data, size_t len)
{
    if (s_upd.kind == UPD_NONE) {
        strncpy(s_upd_error, "no update in progress (update_begin)", sizeof(s_upd_error) - 1);
        return BL_ERR_STATE;
    }
    if (!data) {
        return BL_ERR_ARG;
    }
    if (s_upd.size && s_upd.written + s_upd.head_fill + len > s_upd.size) {
        return upd_fail("more data than the size given to update_begin");
    }
    size_t done = 0;
    if (s_upd.kind == UPD_WAITING) {
        // Hold the first 4 bytes until the kind is known.
        while (done < len && s_upd.head_fill < 4) {
            s_upd.head[s_upd.head_fill++] = data[done++];
        }
        if (s_upd.head_fill < 4) {
            return (int)len;
        }
        int r = upd_start();
        if (r == BL_OK) {
            r = upd_put(s_upd.head, 4);
        }
        if (r != BL_OK) {
            return r;
        }
        s_upd.written = 4;
    }
    if (done < len) {
        int r = upd_put(data + done, len - done);
        if (r != BL_OK) {
            return r;
        }
        s_upd.written += len - done;
    }
    return (int)len;
}

int bl_impl_update_end(int commit)
{
    if (s_upd.kind == UPD_NONE) {
        strncpy(s_upd_error, "no update in progress (update_begin)", sizeof(s_upd_error) - 1);
        return BL_ERR_STATE;
    }
    if (!commit) {
        if (s_upd.kind == UPD_APP && s_upd.ota) {
            esp_ota_abort(s_upd.ota);
        }
        memset(&s_upd, 0, sizeof(s_upd));
        return BL_OK;
    }
    if (s_upd.kind == UPD_WAITING) {
        return upd_fail("image too short");
    }
    if (s_upd.size && s_upd.written != s_upd.size) {
        return upd_fail("fewer bytes than the size given to update_begin");
    }
    if (s_upd.kind == UPD_SKETCH) {
        bl_sketch_header_t hdr;
        const char *err = sketch_check(s_upd.part, &hdr);
        if (err) {
            return upd_fail(err);
        }
        hdr.name[sizeof(hdr.name) - 1] = '\0';
        bl_state_set_pending(s_upd.slot);
        bl_usb_forget();
        BLOG_I("update: sketch '%s' (%u bytes) installed in slot %c; it starts at the next restart",
               hdr.name, (unsigned)s_upd.written, 'a' + s_upd.slot);
    } else {
        WdtPause wdt;
        esp_err_t e = esp_ota_end(s_upd.ota);
        s_upd.ota = 0;
        if (e != ESP_OK) {
            return upd_fail("not a valid app image for this chip");
        }
        if (esp_ota_set_boot_partition(s_upd.part) != ESP_OK) {
            return upd_fail("could not select the new app");
        }
        BLOG_I("update: app image (%u bytes) in %s; it starts at the next restart",
               (unsigned)s_upd.written, s_upd.part->label);
    }
    memset(&s_upd, 0, sizeof(s_upd));
    return BL_OK;
}

int bl_impl_update_error(char *buf, size_t size)
{
    size_t n = strlen(s_upd_error);
    if (buf && size) {
        strncpy(buf, s_upd_error, size - 1);
        buf[size - 1] = '\0';
    }
    return (int)n;
}
