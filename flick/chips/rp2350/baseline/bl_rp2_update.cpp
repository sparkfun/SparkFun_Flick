/*
 * bl_rp2_update.cpp - Baseline updates over the air on the RP2040 / RP2350
 * (docs/OTA.md, "RP2040 and RP2350").
 *
 * arduino-pico puts a small OTA stub in front of every image (its ota/ota.c).
 * On every boot, before the app starts, the stub looks for otacommand.bin in
 * LittleFS; if it is there, the stub copies the file it names over the app at
 * 0x10000000, inflating it when it is gzip, then erases the command. A power
 * cut mid-copy leaves the command in place, so the copy simply runs again.
 *
 * Flick's part:
 *   - receive a .bsu (push: UBEGIN/UDATA/UEND; pull: ota_pull_baseline) and
 *     write the gzip stream after its 128-byte header to NEW, checking the
 *     header (board, sizes) and both SHA-256s on the way;
 *   - keep a backup of the running Baseline: the .gz it was installed from
 *     (CUR), else a raw copy of its flash if LittleFS has room;
 *   - after the stub's copy, check the new image's SHA-256 and run it on trial:
 *     it is kept after TRIAL_MS, or before any restart Baseline makes on
 *     purpose (bl_update_confirm). A reset during the trial (a crash, the
 *     watchdog, a power cut) installs the backup the same way.
 * The sketch slots, Baseline's settings and the files are not touched.
 * Bluetooth bonds are: arduino-pico keeps them inside the image (__bluetooth_tlv,
 * zeros in the .bin), so an update clears them, as a USB flash does. The
 * install check counts that range as zeros (flick puts it in the .bsu).
 * A Baseline that dies before it reaches bl_update_boot() can't roll itself
 * back: BOOTSEL and the download page (or the .uf2) bring the board back.
 */
#include "bl_rp2.h"
#include <LittleFS.h>
#include <PicoOTA.h>

#define TRIAL_MS        10000
#define SLACK           (16 * 1024)     /* LittleFS room kept free besides the files */
#define BSU_MAGIC       0x31555342u     /* "BSU1": baseline/src/bsu/bsu_format.h */
#define BSU_HDR_SIZE    (128 + 16)      /* the .bsu header, then the RP extension (flick.py make_bsu) */
#define BSU_COMP_GZIP   1               /* bsu header byte 5 on RP2: extension + gzip stream */
#define RP_EXT_MAGIC    0x31585052u     /* "RPX1" */

#define F_NEW           ".bl-new.gz"    /* the update being installed */
#define F_CUR           ".bl-cur.gz"    /* what the running Baseline was installed from */
#define F_PREV_GZ       ".bl-prev.gz"   /* the backup during an install: the previous CUR */
#define F_PREV_BIN      ".bl-prev.bin"  /*   or a raw copy of the previous Baseline's flash */
#define F_STATE         ".bl-update"
#define F_OTA_CMD       "otacommand.bin"    /* arduino-pico's OTA command (ota_command.h) */

extern "C" uint8_t __flash_binary_end;

enum { ST_IDLE, ST_INSTALL, ST_TRIAL, ST_ROLLBACK };
enum { BK_NONE, BK_GZ, BK_BIN };

#define STATE_MAGIC     0x55464C46u     /* "FLFU" */
struct update_state {
    uint32_t magic;
    uint32_t state;
    uint32_t backup;                    /* BK_* during an install */
    uint32_t raw_size;                  /* the update's image */
    uint8_t raw_sha[32];
    char version[16];
    char from_version[16];
    uint32_t cur_valid;                 /* F_CUR holds the running image (cur_sha) */
    uint8_t cur_sha[32];
    uint32_t tlv_off, tlv_size;         /* the update's Bluetooth bond store (all zeros in the image) */
    uint32_t cur_raw_size, cur_tlv_off, cur_tlv_size;  /* the same for the image in F_CUR */
};
static update_state s_st;
static uint32_t s_trial_ms;             /* millis() when the trial started, 0: none */

static void ulog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ulog(const char *fmt, ...)
{
    char text[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    bl_impl_log(3, "update", "%s", text);
}

static bool state_load(void)
{
    File f = LittleFS.open(F_STATE, "r");
    bool ok = f && f.read((uint8_t *)&s_st, sizeof(s_st)) == sizeof(s_st) && s_st.magic == STATE_MAGIC;
    if (f) {
        f.close();
    }
    if (!ok) {
        memset(&s_st, 0, sizeof(s_st));
    }
    return ok;
}

static void state_save(void)
{
    s_st.magic = STATE_MAGIC;
    File f = LittleFS.open(F_STATE, "w");
    if (f) {
        f.write((const uint8_t *)&s_st, sizeof(s_st));
        f.close();
    }
}

static uint32_t fs_free(void)
{
    FSInfo info;
    if (!LittleFS.info(info) || info.totalBytes <= info.usedBytes) {
        return 0;
    }
    return (uint32_t)(info.totalBytes - info.usedBytes);
}

static void remove_file(const char *name)
{
    if (LittleFS.exists(name)) {
        LittleFS.remove(name);
    }
}

static void rename_file(const char *from, const char *to)
{
    remove_file(to);
    LittleFS.rename(from, to);
}

/* SHA-256 of the first len bytes of flash (the app as installed). The bytes
 * from skip_off to skip_off + skip_size count as zeros: arduino-pico's Bluetooth
 * bond store, zeros in the image, which BTstack rewrites at every start. */
static void flash_sha(uint32_t len, uint8_t out[32], uint32_t skip_off = 0, uint32_t skip_size = 0)
{
    static const uint8_t zeros[256] = {0};
    bl_sha256_t c;
    bl_sha256_init(&c);
    const uint8_t *p = (const uint8_t *)XIP_BASE;
    for (uint32_t done = 0; done < len;) {
        uint32_t n;
        if (skip_size && done >= skip_off && done < skip_off + skip_size) {
            n = skip_off + skip_size - done;
            n = n < sizeof(zeros) ? n : sizeof(zeros);
            n = len - done < n ? len - done : n;
            bl_sha256_update(&c, zeros, n);
        } else {
            n = len - done < 65536 ? len - done : 65536;
            if (skip_size && done < skip_off && done + n > skip_off) {
                n = skip_off - done;
            }
            bl_sha256_update(&c, p + done, n);
        }
        done += n;
        watchdog_update();
    }
    bl_sha256_final(&c, out);
}

static uint32_t running_size(void)
{
    return (uint32_t)((uintptr_t)&__flash_binary_end - XIP_BASE);
}

/* Writes the OTA command for file (the stub copies it on the next boot). */
static bool ota_command(const char *file)
{
    picoOTA.begin();
    return picoOTA.addFile(file) && picoOTA.commit();
}

/* ---- Receiving a .bsu ------------------------------------------------------ */
static struct {
    bool active;
    uint32_t total;                     /* package bytes, 0 until the header tells (a pull) */
    uint32_t next;
    bool check_sha;                     /* push: the whole package's SHA-256 is known */
    uint8_t sha[32];
    bl_sha256_t all, comp;
    uint8_t hdr[BSU_HDR_SIZE];
    File file;
    uint32_t wfill;                     /* bytes waiting in s_wbuf */
    char err[96];
} s_rx;

static const char *rx_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static const char *rx_fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_rx.err, sizeof(s_rx.err), fmt, ap);
    va_end(ap);
    if (s_rx.file) {
        s_rx.file.close();
    }
    remove_file(F_NEW);
    s_rx.active = false;
    return s_rx.err;
}

/* Room for a package of size bytes: old backups go first, then CUR (no
 * rollback for this update) if that is what it takes. */
static const char *make_room(uint32_t size)
{
    remove_file(F_PREV_GZ);
    remove_file(F_PREV_BIN);
    if (fs_free() >= size + SLACK) {
        return NULL;
    }
    remove_file(F_CUR);
    s_st.cur_valid = 0;
    state_save();
    if (fs_free() >= size + SLACK) {
        return NULL;
    }
    return rx_fail("not enough room in LittleFS: the update needs %u bytes, %u free", (unsigned)(size + SLACK),
                   (unsigned)fs_free());
}

static const char *rx_begin(uint32_t size, const uint8_t *sha)
{
    if (s_rx.file) {
        s_rx.file.close();
    }
    memset(&s_rx, 0, sizeof(s_rx));
    if (!bl_lfs_mount()) {
        return rx_fail("LittleFS did not mount");
    }
    state_load();
    if (s_st.state != ST_IDLE) {
        return rx_fail("an update is still being installed: restart the board first");
    }
    remove_file(F_NEW);
    remove_file(F_OTA_CMD);
    if (size) {
        if (size <= BSU_HDR_SIZE || size - BSU_HDR_SIZE > BL_BOARD_BASELINE_MAX) {
            return rx_fail("bad size %u", (unsigned)size);
        }
        const char *e = make_room(size - BSU_HDR_SIZE);
        if (e) {
            return e;
        }
    }
    s_rx.file = LittleFS.open(F_NEW, "w");
    if (!s_rx.file) {
        return rx_fail("cannot create %s", F_NEW);
    }
    s_rx.total = size;
    if (sha) {
        memcpy(s_rx.sha, sha, 32);
        s_rx.check_sha = true;
    }
    bl_sha256_init(&s_rx.all);
    bl_sha256_init(&s_rx.comp);
    s_rx.active = true;
    return NULL;
}

static const char *check_header(void)
{
    const uint8_t *h = s_rx.hdr;
    uint32_t magic, raw_size, comp_size;
    uint16_t chip;
    memcpy(&magic, h, 4);
    memcpy(&chip, h + 6, 2);
    memcpy(&raw_size, h + 8, 4);
    memcpy(&comp_size, h + 12, 4);
    char target[33] = {0}, version[17] = {0};
    memcpy(target, h + 80, 32);
    memcpy(version, h + 112, 16);
    if (magic != BSU_MAGIC || h[4] != 1) {
        return rx_fail("not a Baseline update (.bsu)");
    }
    if (chip != BL_TARGET_CHIP_ID || h[5] != BSU_COMP_GZIP) {
        return rx_fail("this .bsu is for another chip");
    }
    if (strcmp(target, BL_TARGET_NAME) != 0) {
        return rx_fail("this .bsu is for %s; this board is %s", target, BL_TARGET_NAME);
    }
    if (raw_size == 0 || raw_size > BL_BOARD_BASELINE_MAX) {
        return rx_fail("the new Baseline (%u bytes) is bigger than its flash area", (unsigned)raw_size);
    }
    if (s_rx.total == 0) {      // a pull: the size comes from the header
        s_rx.total = BSU_HDR_SIZE + comp_size;
        const char *e = make_room(comp_size);
        if (e) {
            return e;
        }
    } else if (comp_size != s_rx.total - BSU_HDR_SIZE) {
        return rx_fail("the .bsu is damaged (sizes don't match)");
    }
    uint32_t ext[4];
    memcpy(ext, h + 128, sizeof(ext));
    if (ext[0] != RP_EXT_MAGIC || ext[1] + ext[2] > raw_size) {
        return rx_fail("the .bsu is damaged (RP extension)");
    }
    s_st.tlv_off = ext[1];
    s_st.tlv_size = ext[2];
    s_st.raw_size = raw_size;
    memcpy(s_st.raw_sha, h + 16, 32);
    strlcpy(s_st.version, version, sizeof(s_st.version));
    return NULL;
}

static uint8_t s_wbuf[4096];

static bool rx_flush(void)
{
    bool ok = !s_rx.wfill || s_rx.file.write(s_wbuf, s_rx.wfill) == s_rx.wfill;
    s_rx.wfill = 0;
    return ok;
}

static const char *rx_data(const uint8_t *data, uint32_t len)
{
    if (!s_rx.active) {
        return s_rx.err[0] ? s_rx.err : "no update in progress";
    }
    if (s_rx.total && s_rx.next + len > s_rx.total) {
        return rx_fail("data past the declared size");
    }
    bl_sha256_update(&s_rx.all, data, len);
    while (len && s_rx.next < BSU_HDR_SIZE) {
        s_rx.hdr[s_rx.next++] = *data++;
        len--;
        if (s_rx.next == BSU_HDR_SIZE) {
            const char *e = check_header();
            if (e) {
                return e;
            }
        }
    }
    if (len) {
        if (s_rx.next == BSU_HDR_SIZE && (len < 2 || data[0] != 0x1f || data[1] != 0x8b)) {
            return rx_fail("the .bsu holds no gzip stream (made by an older flick?)");
        }
        bl_sha256_update(&s_rx.comp, data, len);
        s_rx.next += len;
        // Written to LittleFS a flash sector (4 KB) at a time: small writes cost
        // LittleFS a lot more flash work.
        while (len) {
            uint32_t n = sizeof(s_wbuf) - s_rx.wfill;
            n = n < len ? n : len;
            memcpy(s_wbuf + s_rx.wfill, data, n);
            s_rx.wfill += n;
            data += n;
            len -= n;
            if (s_rx.wfill == sizeof(s_wbuf) && !rx_flush()) {
                return rx_fail("LittleFS write failed (full?)");
            }
        }
    }
    return NULL;
}

/* A raw copy of the running Baseline's flash, for a rollback. */
static bool copy_running(uint32_t size)
{
    File f = LittleFS.open(F_PREV_BIN, "w");
    if (!f) {
        return false;
    }
    const uint8_t *p = (const uint8_t *)XIP_BASE;
    static uint8_t buf[4096];     // through RAM: LittleFS writes stop flash reads while they run
    bool ok = true;
    for (uint32_t done = 0; ok && done < size;) {
        uint32_t n = size - done < sizeof(buf) ? size - done : sizeof(buf);
        memcpy(buf, p + done, n);
        ok = f.write(buf, n) == n;
        done += n;
        watchdog_update();
    }
    f.close();
    if (!ok) {
        remove_file(F_PREV_BIN);
    }
    return ok;
}

static const char *rx_end(char *note, size_t note_size)
{
    if (!s_rx.active) {
        return s_rx.err[0] ? s_rx.err : "no update in progress";
    }
    if (s_rx.next < BSU_HDR_SIZE || s_rx.next != s_rx.total) {
        return rx_fail("incomplete: got %u of %u bytes", (unsigned)s_rx.next, (unsigned)s_rx.total);
    }
    uint8_t digest[32];
    bl_sha256_final(&s_rx.all, digest);
    if (s_rx.check_sha && memcmp(digest, s_rx.sha, 32) != 0) {
        return rx_fail("transfer corrupted (sha256 mismatch)");
    }
    bl_sha256_final(&s_rx.comp, digest);
    if (memcmp(digest, s_rx.hdr + 48, 32) != 0) {
        return rx_fail("the .bsu is damaged (sha256 of its data)");
    }
    if (!rx_flush()) {
        return rx_fail("LittleFS write failed (full?)");
    }
    s_rx.file.close();
    s_rx.active = false;

    BlWdogLong wdt;     // hashing and copying the running Baseline take a few seconds
    uint32_t run = running_size();
    uint8_t run_sha[32];
    bool cur_runs = false;
    if (s_st.cur_valid && LittleFS.exists(F_CUR)) {
        flash_sha(s_st.cur_raw_size, run_sha, s_st.cur_tlv_off, s_st.cur_tlv_size);
        cur_runs = memcmp(s_st.cur_sha, run_sha, 32) == 0;   // F_CUR is what runs now
    }
    s_st.backup = BK_NONE;
    if (cur_runs) {
        rename_file(F_CUR, F_PREV_GZ);
        s_st.backup = BK_GZ;
    } else {
        remove_file(F_CUR);
        if (fs_free() >= run + SLACK && copy_running(run)) {
            s_st.backup = BK_BIN;
        }
    }
    s_st.cur_valid = 0;
    if (note && note_size) {
        snprintf(note, note_size, "%s", s_st.backup == BK_NONE ? " (no rollback: no room for a backup in LittleFS)" : "");
    }
    strlcpy(s_st.from_version, BASELINE_VERSION, sizeof(s_st.from_version));
    if (!ota_command(F_NEW)) {
        remove_file(F_OTA_CMD);
        return rx_fail("could not write the OTA command");
    }
    s_st.state = ST_INSTALL;
    state_save();
    ulog("Baseline %s staged (%s), installing on restart", s_st.version,
         s_st.backup == BK_GZ ? "backup: the previous update" : s_st.backup == BK_BIN ? "backup: a copy of this one" : "no backup");
    return NULL;
}

extern "C" const char *bl_update_begin(uint32_t size, const uint8_t sha256[32])
{
    return rx_begin(size, sha256);
}

extern "C" const char *bl_update_data(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (s_rx.active && offset != s_rx.next) {
        snprintf(s_rx.err, sizeof(s_rx.err), "expected offset %u", (unsigned)s_rx.next);
        return s_rx.err;      // the transfer stays open: the sender may resend
    }
    return rx_data(data, len);
}

extern "C" const char *bl_update_end(char *note, size_t note_size)
{
    return rx_end(note, note_size);
}

extern "C" void bl_update_install(void)
{
    bl_restart_clean();     // the stub copies F_NEW before Baseline starts again
}

/* ---- Boot, trial, rollback ----------------------------------------------- */
static void finish_rollback_files(void)
{
    remove_file(F_NEW);
    remove_file(F_OTA_CMD);
    if (s_st.backup == BK_GZ) {
        rename_file(F_PREV_GZ, F_CUR);      // the running Baseline came from it
        s_st.cur_valid = 1;                 // cur_sha: still the old image's
    } else {
        remove_file(F_PREV_BIN);
        s_st.cur_valid = 0;
    }
    s_st.backup = BK_NONE;
    s_st.state = ST_IDLE;
}

extern "C" void bl_update_boot(void)
{
    if (!bl_lfs_mount()) {
        return;
    }
    if (!state_load() || s_st.state == ST_IDLE) {
        return;
    }
    BlWdogLong wdt;
    uint8_t sha[32];
    switch (s_st.state) {
    case ST_INSTALL:
        flash_sha(s_st.raw_size, sha, s_st.tlv_off, s_st.tlv_size);
        if (memcmp(sha, s_st.raw_sha, 32) == 0) {
            remove_file(F_OTA_CMD);
            s_st.state = ST_TRIAL;
            state_save();
            s_trial_ms = millis() ? millis() : 1;
            ulog("Baseline %s installed (was %s), on trial for %d s", s_st.version, s_st.from_version,
                 TRIAL_MS / 1000);
        } else {
            // The stub did not copy it (its CRC check failed, or the file was damaged).
            ulog("Baseline %s was NOT installed (the flash doesn't match the update); still %s", s_st.version,
                 BASELINE_VERSION);
            finish_rollback_files();
            state_save();
        }
        break;
    case ST_TRIAL:
        // A reset before the trial ended: Baseline confirms itself before every
        // restart it makes, so this was a crash, the watchdog or a power cut.
        if (s_st.backup == BK_NONE) {
            ulog("Baseline %s reset during its trial, and there is no backup to go back to", s_st.version);
            s_st.state = ST_IDLE;
            remove_file(F_NEW);
            state_save();
            break;
        }
        if (!ota_command(s_st.backup == BK_GZ ? F_PREV_GZ : F_PREV_BIN)) {
            ulog("Baseline %s reset during its trial; the rollback could not be started", s_st.version);
            break;      // tried again at the next boot
        }
        s_st.state = ST_ROLLBACK;
        state_save();
        ulog("Baseline %s reset during its trial: going back to %s", s_st.version, s_st.from_version);
        bl_restart_clean();
        break;
    case ST_ROLLBACK:
        ulog("rolled back to Baseline %s (%s did not run %d s)", BASELINE_VERSION, s_st.version, TRIAL_MS / 1000);
        finish_rollback_files();
        state_save();
        break;
    }
}

extern "C" void bl_update_confirm(void)
{
    if (s_st.state != ST_TRIAL) {
        return;
    }
    s_trial_ms = 0;
    rename_file(F_NEW, F_CUR);
    memcpy(s_st.cur_sha, s_st.raw_sha, 32);
    s_st.cur_raw_size = s_st.raw_size;
    s_st.cur_tlv_off = s_st.tlv_off;
    s_st.cur_tlv_size = s_st.tlv_size;
    s_st.cur_valid = 1;
    remove_file(F_PREV_GZ);
    remove_file(F_PREV_BIN);
    s_st.backup = BK_NONE;
    s_st.state = ST_IDLE;
    state_save();
    ulog("Baseline %s confirmed", s_st.version);
}

extern "C" void bl_update_poll(void)
{
    if (s_trial_ms && millis() - s_trial_ms >= TRIAL_MS) {
        bl_update_confirm();
    }
}

/* ---- API ------------------------------------------------------------------- */
extern "C" int bl_impl_baseline_update_mode(void)
{
    return BL_UPDATE_DUAL;      // updated in place; the sketch, settings and files are kept
}

#if defined(BL_HAS_WIFI)
/* HTTPS checks certificate dates: the clock has to be set first. */
static void need_time(const char *url, const char *what)
{
    if (strncasecmp(url, "https:", 6) == 0 && !bl_ntp_ensure(10000)) {
        ulog("%s: the clock is not set (no NTP answer); HTTPS will likely fail", what);
    }
}

/* The response body into an update (or a sketch slot), as it arrives. */
class UpdateSink : public Print {
public:
    bool sketch;
    const char *err = nullptr;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t n) override
    {
        if (err) {
            return 0;
        }
        if (sketch) {
            bl_sketch_stream_write(data, n);
        } else {
            err = rx_data(data, n);
        }
        return err ? 0 : n;
    }
};

extern "C" int bl_impl_ota_pull_baseline(const char *url, const char *sketch_url)
{
    (void)sketch_url;       // the sketch is kept on these boards
    if (!url || !*url) {
        return BL_ERR_ARG;
    }
    need_time(url, "ota_pull_baseline");
    const char *e = rx_begin(0, NULL);
    if (e) {
        ulog("ota_pull_baseline: %s", e);
        return BL_ERR_FAIL;
    }
    UpdateSink sink;
    sink.sketch = false;
    int code = bl_http_get_to(url, sink);
    if (code != 200 || sink.err) {
        ulog("ota_pull_baseline: %s", sink.err ? sink.err : code < 0 ? "download failed" : "HTTP error");
        if (code > 0 && code != 200) {
            ulog("ota_pull_baseline: HTTP %d", code);
        }
        rx_fail("download failed");
        return code < 0 ? code : BL_ERR_FAIL;
    }
    e = rx_end(NULL, 0);
    if (e) {
        ulog("ota_pull_baseline: %s", e);
        return BL_ERR_FAIL;
    }
    bl_update_install();
}

extern "C" int bl_impl_ota_pull_sketch(const char *url)
{
    if (!url || !*url) {
        return BL_ERR_ARG;
    }
    need_time(url, "ota_pull_sketch");
    bl_sketch_stream_begin();
    UpdateSink sink;
    sink.sketch = true;
    int code = bl_http_get_to(url, sink);
    if (code != 200) {
        ulog("ota_pull_sketch: %s %d", code < 0 ? "download failed" : "HTTP", code);
        return code < 0 ? code : BL_ERR_FAIL;
    }
    const char *e = bl_sketch_stream_end();     // restarts into the sketch when it is good
    ulog("ota_pull_sketch: %s", e);
    return BL_ERR_FAIL;
}
#endif
