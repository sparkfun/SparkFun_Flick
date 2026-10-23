/*
 * bsu_stage.c - second-stage bootloader hook for 4 MB boards: installs a
 * staged Baseline update (baseline/src/bsu/bsu_format.h).
 *
 * Baseline stores the update package over the sketch slots and LittleFS and
 * writes a record in front of it; this hook, run before the app is loaded,
 * inflates the package over the Baseline partition. It uses one of
 * BSU_STAGE_ATTEMPTS attempts per start, so a power cut mid-copy just starts
 * the copy again (the package is untouched), and erases the record once the
 * copied image's SHA-256 matches. Then the normal boot loads the new Baseline,
 * which wipes the staging area.
 *
 * bsk copies this folder into the IDF project of 4 MB targets (stage_idf),
 * with bsu_inflate.c, bsu_inflate.h and bsu_format.h from baseline/src/bsu.
 */
#include <string.h>
#include "esp_log.h"
#include "esp_flash_partitions.h"
#include "bootloader_flash_priv.h"
#include "bootloader_sha.h"
#include "bootloader_utility.h"
#include "hal/wdt_hal.h"
#include "bsu_format.h"
#include "bsu_inflate.h"

static const char *TAG = "bsu_stage";

/* Tells the linker to keep this file (the hooks are weak in the bootloader). */
void bootloader_hooks_include(void)
{
}

static uint32_t s_src;          /* next compressed byte in flash */
static uint32_t s_left;         /* compressed bytes still to read */
static uint32_t s_dest, s_dest_size;
static bootloader_sha256_handle_t s_sha;
static uint32_t s_scratch[72];  /* 4-byte aligned reads (bootloader_flash_read needs them) */
static bsu_inflate_t s_inflate;

static void feed_wdt(void)
{
    wdt_hal_context_t rwdt = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rwdt);
    wdt_hal_feed(&rwdt);
    wdt_hal_write_protect_enable(&rwdt);
}

/* Read any address and length through the aligned scratch buffer. */
static int flash_read_any(uint32_t addr, uint8_t *dst, uint32_t len)
{
    while (len) {
        uint32_t base = addr & ~3u, skip = addr - base;
        uint32_t n = sizeof(s_scratch) - 4;
        if (n > len) {
            n = len;
        }
        if (bootloader_flash_read(base, s_scratch, (skip + n + 3) & ~3u, false) != ESP_OK) {
            return -1;
        }
        memcpy(dst, (uint8_t *)s_scratch + skip, n);
        addr += n;
        dst += n;
        len -= n;
    }
    return 0;
}

static int read_in(void *ctx, uint8_t *buf, uint32_t n)
{
    if (n > s_left) {
        n = s_left;
    }
    if (!n || flash_read_any(s_src, buf, n) < 0) {
        return n ? -1 : 0;
    }
    s_src += n;
    s_left -= n;
    return (int)n;
}

static int write_out(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len)
{
    feed_wdt();
    if (off + BSU_BLOCK > s_dest_size) {
        return -1;
    }
    uint32_t padded = (len + 3) & ~3u;     // the last block: pad to a word (out[] has room)
    if (padded != len) {
        memset((uint8_t *)buf + len, 0xFF, padded - len);
    }
    if (bootloader_flash_erase_range(s_dest + off, BSU_BLOCK) != ESP_OK ||
        bootloader_flash_write(s_dest + off, (void *)buf, padded, false) != ESP_OK) {
        return -1;
    }
    bootloader_sha256_data(s_sha, buf, len);
    return 0;
}

static int read_back(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    return flash_read_any(s_dest + off, buf, len);
}

static bool find_partition(const char *label, uint32_t *addr)
{
    esp_partition_info_t e;
    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; i++) {
        if (bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET + i * sizeof(e), &e, sizeof(e), false) != ESP_OK ||
            e.magic != ESP_PARTITION_MAGIC) {
            return false;
        }
        if (strncmp((const char *)e.label, label, sizeof(e.label)) == 0) {
            *addr = e.pos.offset;
            return true;
        }
    }
    return false;
}

void bootloader_after_init(void)
{
    uint32_t stage;
    bsu_stage_t rec;
    if (!find_partition(BSU_LABEL_STAGE_START, &stage) ||
        bootloader_flash_read(stage, &rec, sizeof(rec), false) != ESP_OK ||
        rec.magic != BSU_STAGE_MAGIC || rec.state != BSU_STAGE_READY) {
        return;     // nothing staged
    }
    // One attempt per start: clear the lowest set bit of attempts.
    int used = 32 - __builtin_popcount(rec.attempts);
    if (used >= BSU_STAGE_ATTEMPTS) {
        ESP_LOGE(TAG, "staged Baseline update failed %d times; giving up (flash Baseline over USB)", used);
        uint32_t failed = 0;
        bootloader_flash_write(stage + offsetof(bsu_stage_t, state), &failed, 4, false);
        return;
    }
    uint32_t attempts = rec.attempts & (rec.attempts - 1);
    bootloader_flash_write(stage + offsetof(bsu_stage_t, attempts), &attempts, 4, false);

    bsu_header_t hdr;
    if (bootloader_flash_read(stage + BSU_STAGE_PKG_OFFSET, &hdr, sizeof(hdr), false) != ESP_OK ||
        hdr.magic != BSU_MAGIC || hdr.raw_size > rec.dest_size ||
        hdr.comp_size + sizeof(hdr) != rec.pkg_size) {
        ESP_LOGE(TAG, "staged Baseline update: bad package");
        return;
    }
    ESP_LOGI(TAG, "installing staged Baseline %.16s (%u bytes, attempt %d)", hdr.version,
             (unsigned)hdr.raw_size, used + 1);
    s_src = stage + BSU_STAGE_PKG_OFFSET + sizeof(hdr);
    s_left = hdr.comp_size;
    s_dest = rec.dest_addr;
    s_dest_size = rec.dest_size;
    s_sha = bootloader_sha256_start();
    bsu_inflate_io_t io = {read_in, write_out, read_back, NULL};
    int r = bsu_inflate(&s_inflate, &io, hdr.raw_size);
    uint8_t digest[32];
    bootloader_sha256_finish(s_sha, digest);
    if (r != BSU_OK || memcmp(digest, hdr.raw_sha256, 32) != 0) {
        ESP_LOGE(TAG, "install failed (%d); trying again", r);
        bootloader_reset();
    }
    bootloader_flash_erase_range(stage, BSU_BLOCK);    // done: forget the record
    ESP_LOGI(TAG, "staged Baseline installed");
}
