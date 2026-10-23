/* bl_update.cpp - updating Baseline and the sketch over the air (ABI 1.7).
 *
 * Baseline. Every board takes the same package (.bsu, bsu/bsu_format.h):
 *   BL_UPDATE_DUAL   8/16 MB flash: two Baseline slots. The new Baseline is
 *                    written to the slot not running while everything else
 *                    keeps running, and the board restarts into it. It
 *                    confirms itself after CONFIRM_MS; one that resets before
 *                    then is rolled back by the bootloader (IDF app rollback).
 *   BL_UPDATE_STAGED 4 MB flash: one Baseline slot. The package is stored
 *                    over the sketch slots and LittleFS in update mode (below),
 *                    and the bootloader (idf/bootloader_components/bsu_stage)
 *                    inflates it over Baseline on the next start; then this
 *                    file wipes the staging area. Every update erases the
 *                    sketch and the files (ota_pull_baseline can re-install a
 *                    sketch afterwards).
 * Push: flick sends over USB or the WiFi OTA port with UBEGIN/UDATA/UEND
 *   (loader_proto.c): the raw app image to two-slot boards, the package to
 *   staged ones.
 * Pull: the sketch calls ota_pull_baseline(url, sketch_url) or
 *   ota_pull_sketch(url).
 *
 * Update mode: Baseline restarts without starting the sketch (an RTC flag),
 * so nothing runs from the sketch slots while they are overwritten. It
 * reconnects to WiFi with the credentials the WiFi driver saved, then runs the
 * pull whose URLs are in NVS, or waits (UPDATE_WAIT_MS at most) for flick to
 * push, re-opening the OTA port with the settings ota_begin saved.
 */
#include <Arduino.h>
#include "soc/soc_caps.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_flash.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "bsu/bsu_format.h"
#include "bsu/bsu_inflate.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_main.h"
#include "bl_state.h"
#include "bl_update.h"
#include "sketch_loader.h"
#if SOC_WIFI_SUPPORTED
#include <WiFi.h>
#endif

#define NVS_NS              "bl_update"
#define CONFIRM_MS          10000           /* a new Baseline confirms itself after this */
#define UPDATE_WAIT_MS      (5 * 60 * 1000) /* update mode waits this long for a push */
#define WIFI_WAIT_MS        20000
#define UPDATE_TASK_STACK   8192
#define RTC_MAGIC           0xB1DA7E00u
#define SECTOR              4096u

enum { UPD_NONE = 0, UPD_WAIT_USB = 1, UPD_WAIT_WIFI = 2 };
typedef struct {
    uint32_t magic;
    uint32_t mode;
} upd_rtc_t;

static RTC_NOINIT_ATTR upd_rtc_t s_rtc;
static bool s_update_mode;          /* this boot is update mode: no sketch */

extern "C" int bl_ota_start_saved(void);   /* bl_ota.cpp: the OTA port with ota_begin's saved settings */

/* Arduino's initArduino() confirms a new app at once unless this says
 * "later"; Baseline confirms itself after CONFIRM_MS instead (confirm_cb). */
extern "C" bool verifyRollbackLater(void)
{
    return true;
}

/* ---------------------------------------------------------------- helpers */

static uint32_t align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) / a * a;
}

/* Before a deliberate restart (an upload, a command, an update): if this
 * Baseline is new and not yet confirmed, it has worked well enough to get
 * here, so confirm it. A restart while unconfirmed makes the bootloader roll
 * back to the previous Baseline. */
extern "C" void bl_update_confirm(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY &&
        esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        BLOG_I("update: Baseline %s confirmed", BASELINE_VERSION);
    }
}

static void restart_now(void)
{
    bl_update_confirm();
    bl_console_flush();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static bool nvs_get(const char *key, char *buf, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = size;
    bool ok = nvs_get_str(h, key, buf, &len) == ESP_OK && buf[0];
    nvs_close(h);
    return ok;
}

static void nvs_put(const char *key, const char *value)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        if (value && *value) {
            nvs_set_str(h, key, value);
        } else {
            nvs_erase_key(h, key);
        }
        nvs_commit(h);
        nvs_close(h);
    }
}

/* The staging area (4 MB boards): from sketch_a to the coredump partition. */
static bool stage_range(uint32_t *addr, uint32_t *len)
{
#if BL_UPDATE_STAGED_BOOTLOADER
    const esp_partition_t *a = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY,
                                                        BSU_LABEL_STAGE_START);
    const esp_partition_t *e = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY,
                                                        BSU_LABEL_STAGE_END);
    if (!a || !e || e->address <= a->address) {
        return false;
    }
    *addr = a->address;
    *len = e->address - a->address;
    return true;
#else
    (void)addr;
    (void)len;
    return false;
#endif
}

extern "C" int bl_update_mode(void)
{
    if (esp_ota_get_next_update_partition(NULL)) {
        return BL_UPDATE_DUAL;
    }
    uint32_t a, l;
    return stage_range(&a, &l) ? BL_UPDATE_STAGED : BL_UPDATE_NONE;
}

extern "C" const char *bl_update_mode_name(void)
{
    switch (bl_update_mode()) {
    case BL_UPDATE_DUAL:   return "dual";
    case BL_UPDATE_STAGED: return "staged";
    default:               return "none";
    }
}

static const char *check_header(const bsu_header_t *h, uint32_t max_raw)
{
    if (h->magic != BSU_MAGIC) {
        return "not a Baseline update (.bsu)";
    }
    if (h->header_version != BSU_HEADER_VERSION) {
        return "unsupported .bsu version";
    }
    if (h->chip_id != BL_TARGET_CHIP_ID) {
        return "update is for a different chip";
    }
    if (strncmp(h->target, BL_TARGET_NAME, sizeof(h->target)) != 0) {
        return "update is for a different board (target)";
    }
    if (h->raw_size == 0 || h->raw_size > max_raw) {
        return "update does not fit the Baseline partition";
    }
    return NULL;
}

/* ------------------------------------------------------------------- HTTP */

typedef struct {
    esp_http_client_handle_t c;
    int64_t length;     /* Content-Length, or -1 */
} http_t;

static const char *http_open(http_t *h, const char *url)
{
    static char err[64];
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 4096;     // GitHub's redirects: long Location headers ...
    cfg.buffer_size_tx = 2048;  // ... and long request lines after them
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    h->c = esp_http_client_init(&cfg);
    if (!h->c) {
        return "out of memory";
    }
    for (int redirects = 0;; redirects++) {
        if (esp_http_client_open(h->c, 0) != ESP_OK) {
            return "could not connect to the server";
        }
        h->length = esp_http_client_fetch_headers(h->c);
        int status = esp_http_client_get_status_code(h->c);
        bool redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (redirect && redirects < 5) {
            // GitHub release downloads redirect to their storage host.
            esp_http_client_set_redirection(h->c);
            char drain[128];
            while (esp_http_client_read(h->c, drain, sizeof(drain)) > 0) {
            }
            continue;
        }
        if (status != 200) {
            snprintf(err, sizeof(err), "HTTP status %d", status);
            return err;
        }
        return NULL;
    }
}

static int http_read(http_t *h, uint8_t *buf, int n)
{
    int got = 0;
    while (got < n) {
        int r = esp_http_client_read(h->c, (char *)buf + got, n - got);
        if (r <= 0) {
            break;
        }
        got += r;
    }
    return got;
}

static void http_close(http_t *h)
{
    if (h->c) {
        esp_http_client_close(h->c);
        esp_http_client_cleanup(h->c);
        h->c = NULL;
    }
}

/* ------------------------------------------------------- staged packages */

static const char *stage_erase(uint32_t stage, uint32_t pkg_size)
{
    uint32_t len = align_up(BSU_STAGE_PKG_OFFSET + pkg_size, SECTOR);
    BLOG_I("update: erasing %u KB of staging area", (unsigned)(len / 1024));
    return esp_flash_erase_region(NULL, stage, len) == ESP_OK ? NULL : "staging erase failed";
}

/* After the whole package is stored: check it, then write the record that
 * tells the bootloader to install it. */
static const char *stage_commit(uint32_t stage, const bsu_header_t *h, uint32_t pkg_size, const uint8_t comp_sha[32])
{
    const esp_partition_t *dest = esp_ota_get_running_partition();
    const char *err = check_header(h, dest->size);
    if (err) {
        return err;
    }
    if (h->comp_size + sizeof(bsu_header_t) != pkg_size) {
        return "package size does not match its header";
    }
    if (memcmp(comp_sha, h->comp_sha256, 32) != 0) {
        return "package corrupted (SHA-256 mismatch)";
    }
    bsu_stage_t rec = {BSU_STAGE_MAGIC, BSU_STAGE_READY, 0xFFFFFFFFu, pkg_size, dest->address, dest->size};
    if (esp_flash_write(NULL, &rec, stage, sizeof(rec)) != ESP_OK) {
        return "staging write failed";
    }
    nvs_put("staged", "1");
    BLOG_I("update: Baseline %.16s staged; the bootloader installs it on restart", h->version);
    return NULL;
}

/* Boot after a staged update: the bootloader has erased the record when it
 * finished. The staging area still holds the package, which the sketch slots
 * and LittleFS must not mistake for their own data: erase it all. */
static void stage_finish(void)
{
    char flag[4];
    uint32_t stage, len;
    if (!nvs_get("staged", flag, sizeof(flag)) || !stage_range(&stage, &len)) {
        return;
    }
    bsu_stage_t rec;
    esp_flash_read(NULL, &rec, stage, sizeof(rec));
    if (rec.magic == BSU_STAGE_MAGIC) {     // the bootloader erases the record when it succeeds
        BLOG_E("update: the bootloader could not install the staged Baseline");
    } else {
        BLOG_I("update: Baseline %s installed; erasing the sketch slots and LittleFS", BASELINE_VERSION);
    }
    esp_flash_erase_region(NULL, stage, len);
    nvs_put("staged", NULL);
}

/* ------------------------------------------------------------- push (flick) */

enum { KIND_APP = 1, KIND_BSU = 2 };

static struct {
    bool active;
    int kind;
    uint32_t size, next;
    uint8_t want[32];
    mbedtls_sha256_context all, comp;
    bsu_header_t hdr;
    uint32_t hdr_fill;
    const esp_partition_t *part;
    esp_ota_handle_t ota;
    uint32_t stage;
} s_push;

extern "C" void bl_update_push_abort(void)
{
    if (s_push.active) {
        if (s_push.ota) {
            esp_ota_abort(s_push.ota);
        }
        mbedtls_sha256_free(&s_push.all);
        mbedtls_sha256_free(&s_push.comp);
    }
    memset(&s_push, 0, sizeof(s_push));
}

extern "C" const char *bl_update_push_begin(const char *kind, uint32_t size, const uint8_t sha256[32],
                                            bool via_wifi, bool *restart)
{
    *restart = false;
    bl_update_push_abort();
    int mode = bl_update_mode();
    if (!strcmp(kind, "app")) {
        if (mode != BL_UPDATE_DUAL) {
            return mode == BL_UPDATE_STAGED ? "this board takes the .bsu package" : "this board updates Baseline over USB only";
        }
        const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
        if (size > p->size) {
            return "image larger than the Baseline slot";
        }
        if (esp_ota_begin(p, size, &s_push.ota) != ESP_OK) {
            return "could not start writing the free Baseline slot";
        }
        s_push.part = p;
        s_push.kind = KIND_APP;
    } else if (!strcmp(kind, "bsu")) {
        if (mode != BL_UPDATE_STAGED) {
            return mode == BL_UPDATE_DUAL ? "this board takes the app image" : "this board updates Baseline over USB only";
        }
        if (!s_update_mode) {
            // Nothing may run from the sketch slots while they are overwritten.
            s_rtc.magic = RTC_MAGIC;
            s_rtc.mode = via_wifi ? UPD_WAIT_WIFI : UPD_WAIT_USB;
            *restart = true;
            return "restarting into update mode: send again";
        }
        uint32_t stage, len;
        stage_range(&stage, &len);
        if (size < sizeof(bsu_header_t) || BSU_STAGE_PKG_OFFSET + size > len) {
            return "package does not fit the staging area";
        }
        const char *err = stage_erase(stage, size);
        if (err) {
            return err;
        }
        s_push.stage = stage;
        s_push.kind = KIND_BSU;
    } else {
        return "unknown kind (app or bsu)";
    }
    s_push.active = true;
    s_push.size = size;
    memcpy(s_push.want, sha256, 32);
    mbedtls_sha256_init(&s_push.all);
    mbedtls_sha256_starts(&s_push.all, 0);
    mbedtls_sha256_init(&s_push.comp);
    mbedtls_sha256_starts(&s_push.comp, 0);
    BLOG_I("update: receiving %s, %u bytes", kind, (unsigned)size);
    return NULL;
}

extern "C" const char *bl_update_push_data(const uint8_t *data, size_t len)
{
    if (!s_push.active) {
        return "no update in progress";
    }
    if (s_push.next + len > s_push.size) {
        return "data past declared size";
    }
    if (s_push.kind == KIND_APP) {
        if (esp_ota_write(s_push.ota, data, len) != ESP_OK) {
            bl_update_push_abort();
            return "flash write failed";
        }
    } else {
        if (esp_flash_write(NULL, data, s_push.stage + BSU_STAGE_PKG_OFFSET + s_push.next, len) != ESP_OK) {
            bl_update_push_abort();
            return "flash write failed";
        }
        size_t i = 0;
        while (i < len && s_push.hdr_fill < sizeof(bsu_header_t)) {
            ((uint8_t *)&s_push.hdr)[s_push.hdr_fill++] = data[i++];
        }
        mbedtls_sha256_update(&s_push.comp, data + i, len - i);
    }
    mbedtls_sha256_update(&s_push.all, data, len);
    s_push.next += len;
    return NULL;
}

extern "C" const char *bl_update_push_end(void)
{
    if (!s_push.active) {
        return "no update in progress";
    }
    if (s_push.next != s_push.size) {
        return "incomplete";
    }
    uint8_t digest[32], comp[32];
    mbedtls_sha256_finish(&s_push.all, digest);
    mbedtls_sha256_finish(&s_push.comp, comp);
    const char *err = NULL;
    if (memcmp(digest, s_push.want, 32) != 0) {
        err = "transfer corrupted (SHA-256 mismatch)";
    } else if (s_push.kind == KIND_APP) {
        esp_err_t e = esp_ota_end(s_push.ota);
        s_push.ota = 0;
        if (e != ESP_OK) {
            err = "not a valid Baseline image for this chip";
        } else if (esp_ota_set_boot_partition(s_push.part) != ESP_OK) {
            err = "could not select the new Baseline";
        } else {
            BLOG_I("update: new Baseline in %s; restarting into it", s_push.part->label);
        }
    } else {
        err = stage_commit(s_push.stage, &s_push.hdr, s_push.size, comp);
    }
    bl_update_push_abort();
    return err;
}

/* -------------------------------------------------------------- downloads */

typedef struct {
    http_t *http;
    uint32_t remaining;             /* compressed bytes still to read */
    mbedtls_sha256_context comp, raw;
    esp_ota_handle_t ota;
    const esp_partition_t *part;
} dl_t;

static int dl_read_in(void *ctx, uint8_t *buf, uint32_t n)
{
    dl_t *d = (dl_t *)ctx;
    if (n > d->remaining) {
        n = d->remaining;
    }
    int got = n ? http_read(d->http, buf, (int)n) : 0;
    if (got > 0) {
        d->remaining -= (uint32_t)got;
        mbedtls_sha256_update(&d->comp, buf, (size_t)got);
    }
    return got;
}

static int dl_write_out(void *ctx, uint32_t off, const uint8_t *buf, uint32_t len)
{
    dl_t *d = (dl_t *)ctx;
    (void)off;      // sequential: esp_ota_write appends
    mbedtls_sha256_update(&d->raw, buf, len);
    return esp_ota_write(d->ota, buf, len) == ESP_OK ? 0 : -1;
}

static int dl_read_back(void *ctx, uint32_t off, uint8_t *buf, uint32_t len)
{
    dl_t *d = (dl_t *)ctx;
    return esp_partition_read(d->part, off, buf, len) == ESP_OK ? 0 : -1;
}

/* Two-slot boards: inflate the download straight into the free slot. */
static const char *pull_baseline_dual(const char *url)
{
    http_t h = {};
    const char *err = http_open(&h, url);
    bsu_header_t hdr;
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!err && http_read(&h, (uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        err = "download too short";
    }
    if (!err) {
        err = check_header(&hdr, p->size);
    }
    dl_t d = {};
    bsu_inflate_t *s = NULL;
    if (!err) {
        s = (bsu_inflate_t *)malloc(sizeof(bsu_inflate_t));
        if (!s) {
            err = "out of memory";
        } else if (esp_ota_begin(p, hdr.raw_size, &d.ota) != ESP_OK) {
            err = "could not start writing the free Baseline slot";
        }
    }
    if (!err) {
        BLOG_I("update: downloading Baseline %.16s into %s", hdr.version, p->label);
        d.http = &h;
        d.remaining = hdr.comp_size;
        d.part = p;
        mbedtls_sha256_init(&d.comp);
        mbedtls_sha256_starts(&d.comp, 0);
        mbedtls_sha256_init(&d.raw);
        mbedtls_sha256_starts(&d.raw, 0);
        bsu_inflate_io_t io = {dl_read_in, dl_write_out, dl_read_back, &d};
        int r = bsu_inflate(s, &io, hdr.raw_size);
        uint8_t drain[64];
        while (d.remaining && dl_read_in(&d, drain, sizeof(drain)) > 0) {
        }
        uint8_t comp[32], raw[32];
        mbedtls_sha256_finish(&d.comp, comp);
        mbedtls_sha256_finish(&d.raw, raw);
        mbedtls_sha256_free(&d.comp);
        mbedtls_sha256_free(&d.raw);
        if (r != BSU_OK) {
            err = r == BSU_ERR_INPUT ? "download ended early" : "download corrupted (could not decompress)";
        } else if (memcmp(comp, hdr.comp_sha256, 32) != 0 || memcmp(raw, hdr.raw_sha256, 32) != 0) {
            err = "download corrupted (SHA-256 mismatch)";
        }
        if (err) {
            esp_ota_abort(d.ota);
        } else if (esp_ota_end(d.ota) != ESP_OK) {
            err = "not a valid Baseline image for this chip";
        } else if (esp_ota_set_boot_partition(p) != ESP_OK) {
            err = "could not select the new Baseline";
        }
    }
    free(s);
    http_close(&h);
    return err;
}

/* Staged boards, in update mode: store the download over the staging area. */
static const char *pull_baseline_staged(const char *url)
{
    uint32_t stage, len;
    if (!stage_range(&stage, &len)) {
        return "no staging area";
    }
    http_t h = {};
    const char *err = http_open(&h, url);
    bsu_header_t hdr;
    if (!err && http_read(&h, (uint8_t *)&hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        err = "download too short";
    }
    if (!err) {
        err = check_header(&hdr, esp_ota_get_running_partition()->size);
    }
    uint32_t pkg = sizeof(hdr) + hdr.comp_size;
    if (!err && BSU_STAGE_PKG_OFFSET + pkg > len) {
        err = "package does not fit the staging area";
    }
    if (!err) {
        err = stage_erase(stage, pkg);
    }
    if (!err && esp_flash_write(NULL, &hdr, stage + BSU_STAGE_PKG_OFFSET, sizeof(hdr)) != ESP_OK) {
        err = "staging write failed";
    }
    if (!err) {
        BLOG_I("update: downloading Baseline %.16s (%u KB)", hdr.version, (unsigned)(pkg / 1024));
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        static uint8_t buf[1024];
        uint32_t off = sizeof(hdr);
        while (!err && off < pkg) {
            int want = (int)(pkg - off < sizeof(buf) ? pkg - off : sizeof(buf));
            int got = http_read(&h, buf, want);
            if (got <= 0) {
                err = "download ended early";
            } else if (esp_flash_write(NULL, buf, stage + BSU_STAGE_PKG_OFFSET + off, (size_t)got) != ESP_OK) {
                err = "staging write failed";
            } else {
                mbedtls_sha256_update(&sha, buf, (size_t)got);
                off += (uint32_t)got;
            }
        }
        uint8_t comp[32];
        mbedtls_sha256_finish(&sha, comp);
        mbedtls_sha256_free(&sha);
        if (!err) {
            err = stage_commit(stage, &hdr, pkg, comp);
        }
    }
    http_close(&h);
    return err;
}

/* Download a sketch (.flick) into the free slot and make it the next to run. */
static const char *pull_sketch(const char *url)
{
    int slot = bl_state_active_slot() ^ 1;      // never overwrite the running sketch
    const esp_partition_t *p = sketch_slot_partition(slot);
    if (!p) {
        return "sketch partition missing";
    }
    http_t h = {};
    const char *err = http_open(&h, url);
    if (!err && h.length > (int64_t)p->size) {
        err = "sketch larger than the slot";
    }
    if (!err && esp_partition_erase_range(p, 0, p->size) != ESP_OK) {
        err = "erase failed";
    }
    uint32_t off = 0;
    static uint8_t buf[1024];
    while (!err) {
        int got = http_read(&h, buf, sizeof(buf));
        if (got <= 0) {
            break;
        }
        if (off + (uint32_t)got > p->size) {
            err = "sketch larger than the slot";
        } else if (esp_partition_write(p, off, buf, (size_t)got) != ESP_OK) {
            err = "flash write failed";
        }
        off += (uint32_t)got;
    }
    http_close(&h);
    bl_sketch_header_t hdr;
    if (!err) {
        err = sketch_check(p, &hdr);
    }
    if (err) {
        return err;
    }
    hdr.name[sizeof(hdr.name) - 1] = '\0';
    bl_state_set_pending(slot);
    bl_usb_forget();
    BLOG_I("update: sketch '%s' (%u bytes) installed in slot %c", hdr.name, (unsigned)off, 'a' + slot);
    return NULL;
}

/* ------------------------------------------------------------ the API */

extern "C" int bl_impl_baseline_update_mode(void)
{
    return bl_update_mode();
}

extern "C" int bl_impl_ota_pull_sketch(const char *url)
{
    if (!url || !*url) {
        return BL_ERR_ARG;
    }
    WdtPause wdt;
    const char *err = pull_sketch(url);
    if (err) {
        BLOG_E("ota_pull_sketch: %s", err);
        return BL_ERR_FAIL;
    }
    restart_now();
    return BL_OK;
}

extern "C" int bl_impl_ota_pull_baseline(const char *url, const char *sketch_url)
{
    if (!url || !*url) {
        return BL_ERR_ARG;
    }
    int mode = bl_update_mode();
    if (mode == BL_UPDATE_NONE) {
        BLOG_E("ota_pull_baseline: this board updates Baseline over USB only");
        return BL_ERR_UNSUPPORTED;
    }
    if (mode == BL_UPDATE_STAGED) {
        // The sketch runs from the area the package goes to: restart without it.
        nvs_put("url", url);
        nvs_put("sketch", sketch_url);
        BLOG_I("ota_pull_baseline: restarting into update mode");
        restart_now();
    }
    WdtPause wdt;
    const char *err = pull_baseline_dual(url);
    if (err) {
        BLOG_E("ota_pull_baseline: %s", err);
        return BL_ERR_FAIL;
    }
    nvs_put("sketch", sketch_url);      // installed by the new Baseline before the sketch starts
    BLOG_I("ota_pull_baseline: restarting into the new Baseline");
    restart_now();
    return BL_OK;
}

/* ------------------------------------------------------------- boot */

static void confirm_cb(void *arg)
{
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        BLOG_I("update: Baseline %s confirmed", BASELINE_VERSION);
    }
}

static bool wifi_up(void)
{
#if SOC_WIFI_SUPPORTED
    WiFi.mode(WIFI_STA);
    WiFi.begin();       // the network the WiFi driver saved when the sketch connected
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_WAIT_MS) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        BLOG_I("update mode: WiFi connected, %s", WiFi.localIP().toString().c_str());
        return true;
    }
    BLOG_E("update mode: could not connect to WiFi with the saved network");
#endif
    return false;
}

static char s_url[256], s_sketch_url[256];

static void pull_task(void *arg)
{
    bool baseline = arg != NULL;
    const char *err = NULL;
    if (!wifi_up()) {
        err = "no WiFi";
    } else if (baseline) {
        err = pull_baseline_staged(s_url);
    } else {
        err = pull_sketch(s_sketch_url);
    }
    if (err) {
        // A failed Baseline download may have erased the sketch: the sketch
        // URL, if any, is still in NVS, so the next boot re-installs it.
        BLOG_E("update mode: %s failed: %s", baseline ? "Baseline download" : "sketch install", err);
    }
    restart_now();
}

static void wait_timeout_cb(void *arg)
{
    BLOG_W("update mode: nothing received in %u minutes; restarting", (unsigned)(UPDATE_WAIT_MS / 60000));
    esp_restart();
}

extern "C" bool bl_update_boot(void)
{
    stage_finish();

    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        BLOG_I("update: new Baseline %s in %s; it confirms itself in %u s", BASELINE_VERSION, run->label,
               (unsigned)(CONFIRM_MS / 1000));
        esp_timer_create_args_t a = {};
        a.callback = confirm_cb;
        a.name = "bl_confirm";
        esp_timer_handle_t t;
        if (esp_timer_create(&a, &t) == ESP_OK) {
            esp_timer_start_once(t, (uint64_t)CONFIRM_MS * 1000);
        }
    }

    uint32_t mode = s_rtc.magic == RTC_MAGIC ? s_rtc.mode : (uint32_t)UPD_NONE;
    s_rtc.magic = 0;
    s_rtc.mode = UPD_NONE;

    if (mode == UPD_WAIT_USB || mode == UPD_WAIT_WIFI) {
        s_update_mode = true;
        sketch_set_status("update mode: waiting for flick");
        BLOG_I("update mode: waiting for the Baseline update (sketch not started)");
        if (mode == UPD_WAIT_WIFI && wifi_up()) {
            bl_ota_start_saved();
        }
        esp_timer_create_args_t a = {};
        a.callback = wait_timeout_cb;
        a.name = "bl_upd_wait";
        esp_timer_handle_t t;
        if (esp_timer_create(&a, &t) == ESP_OK) {
            esp_timer_start_once(t, (uint64_t)UPDATE_WAIT_MS * 1000);
        }
        return true;
    }

    bool baseline = nvs_get("url", s_url, sizeof(s_url));
    bool sketch = nvs_get("sketch", s_sketch_url, sizeof(s_sketch_url));
    if (!baseline && !sketch) {
        return false;
    }
    // Forget the request first, so a failure cannot repeat on every boot. A
    // Baseline download keeps the sketch URL for after the new Baseline starts.
    nvs_put("url", NULL);
    if (!baseline) {
        nvs_put("sketch", NULL);
    }
    s_update_mode = true;
    sketch_set_status(baseline ? "update mode: downloading Baseline" : "update mode: installing the sketch");
    xTaskCreatePinnedToCore(pull_task, "bl_update", UPDATE_TASK_STACK, baseline ? (void *)1 : NULL, 2, NULL, 0);
    return true;
}
