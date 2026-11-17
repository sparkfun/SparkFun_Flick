/*
 * bl_rp2_storage.cpp - LittleFS, microSD and Preferences on the RP2350.
 *
 * LittleFS is arduino-pico's, in the flash region flick gives it
 * (BL_BOARD_FS_START..END in the target's bl_board.h). microSD is arduino-pico's
 * SDFS (SdFat) on SPI. Both are fs::FS objects with the same File class (the
 * ESP8266 / ESP32 API), so one handle table serves both, as on the ESP32 family.
 *
 * Preferences have no arduino-pico counterpart; here a namespace is a folder in
 * LittleFS (/.prefs/<namespace>) and a key is a file holding a type byte and
 * the value, as on the Teensy. LittleFS is mounted for them on first use.
 */
#include <LittleFS.h>
#include <SDFS.h>
#include "bl_rp2.h"

#define MAX_FILES       8
#define MAX_PREFS       4
#define PREFS_ROOT      "/.prefs"

SPIClassRP2040 *bl_spi_object(void);     /* bl_rp2_hw.cpp */

static bool s_lfs_mounted;
static int s_sd_bus = -1;       /* BL_SD_SPI while mounted */

static bool lfs_mount(void)
{
    if (!s_lfs_mounted) {
        BlWdogLong wdog;        // formatting a blank region takes seconds
        s_lfs_mounted = LittleFS.begin();       // formats a blank region (autoFormat)
    }
    return s_lfs_mounted;
}

static FS *fs_by_id(int fs)
{
    if (fs == BL_FS_LITTLEFS) {
        return s_lfs_mounted ? &LittleFS : NULL;
    }
    if (fs == BL_FS_SD) {
        return s_sd_bus >= 0 ? &SDFS : NULL;
    }
    return NULL;
}

/* ---------------- files ---------------- */

struct FileSlot {
    File *file;
    char *path;     /* full path, as the sketch opened it */
    int refs;
};
static FileSlot s_files[MAX_FILES];

static File *file_of(int h)
{
    return (h >= 0 && h < MAX_FILES) ? s_files[h].file : NULL;
}

static int file_slot(File f, const char *path)
{
    if (!f) {
        return BL_ERR_FAIL;
    }
    for (int h = 0; h < MAX_FILES; h++) {
        if (!s_files[h].file) {
            s_files[h].file = new File(f);
            s_files[h].path = strdup(path ? path : "");
            s_files[h].refs = 1;
            return h;
        }
    }
    return BL_ERR_NO_MEM;   // too many open files
}

/* Directories open with openDir() in this API; File::openNextFile() works on
 * the File that open() returns for a directory path. */
static int open_in(FS *fs, const char *path, const char *mode)
{
    if (!path) {
        return BL_ERR_ARG;
    }
    return file_slot(fs->open(path, mode && *mode ? mode : "r"), path);
}

static int path_op(FS *fs, int op, const char *path, const char *path2)
{
    if (!fs || !path) {
        return 0;
    }
    switch (op) {
    case BL_FS_EXISTS: return fs->exists(path);
    case BL_FS_REMOVE: return fs->remove(path);
    case BL_FS_RENAME: return path2 ? fs->rename(path, path2) : 0;
    case BL_FS_MKDIR:  return fs->mkdir(path);
    case BL_FS_RMDIR:  return fs->rmdir(path);
    default:           return 0;
    }
}

extern "C" int bl_impl_fs_begin(int format_if_failed)
{
    return lfs_mount() ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_fs_end(void)
{
    if (s_lfs_mounted) {
        LittleFS.end();
        s_lfs_mounted = false;
    }
}

extern "C" int bl_impl_fs_format(void)
{
    BlWdogLong wdog;
    bool ok = LittleFS.format();
    s_lfs_mounted = false;
    return ok && lfs_mount() ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_fs_info(int what)
{
    FSInfo info;
    if (!s_lfs_mounted || !LittleFS.info(info)) {
        return 0;
    }
    return what == BL_FS_TOTAL_BYTES ? (uint32_t)info.totalBytes
         : what == BL_FS_USED_BYTES  ? (uint32_t)info.usedBytes : 0;
}

extern "C" int bl_impl_fs_open(const char *path, const char *mode, int create)
{
    return s_lfs_mounted ? open_in(&LittleFS, path, mode) : BL_ERR_STATE;
}

extern "C" void bl_impl_fs_retain(int h)
{
    if (file_of(h)) {
        s_files[h].refs++;
    }
}

extern "C" void bl_impl_fs_release(int h)
{
    File *f = file_of(h);
    if (f && --s_files[h].refs <= 0) {
        f->close();
        delete f;
        free(s_files[h].path);
        s_files[h] = {};
    }
}

extern "C" int bl_impl_fs_read(int h, uint8_t *buf, size_t size)
{
    File *f = file_of(h);
    return (f && buf) ? (int)f->read(buf, size) : -1;
}

extern "C" int bl_impl_fs_write(int h, const uint8_t *data, size_t len)
{
    File *f = file_of(h);
    return (f && data) ? (int)f->write(data, len) : 0;
}

extern "C" int bl_impl_fs_peek(int h)
{
    File *f = file_of(h);
    return f ? f->peek() : -1;
}

extern "C" int bl_impl_fs_seek(int h, uint32_t pos, int mode)
{
    File *f = file_of(h);
    return (f && mode >= 0 && mode <= 2 && f->seek(pos, (SeekMode)mode)) ? 1 : 0;
}

extern "C" void bl_impl_fs_flush(int h)
{
    File *f = file_of(h);
    if (f) {
        f->flush();
    }
}

extern "C" uint32_t bl_impl_fs_file_info(int h, int what)
{
    File *f = file_of(h);
    if (!f) {
        return 0;
    }
    switch (what) {
    case BL_FS_FILE_POSITION:   return (uint32_t)f->position();
    case BL_FS_FILE_SIZE:       return (uint32_t)f->size();
    case BL_FS_FILE_IS_DIR:     return f->isDirectory() ? 1 : 0;
    case BL_FS_FILE_LAST_WRITE: return (uint32_t)f->getLastWrite();
    default:                    return 0;
    }
}

extern "C" int bl_impl_fs_file_name(int h, int full_path, char *buf, size_t size)
{
    File *f = file_of(h);
    if (!f) {
        return -1;
    }
    const char *s = full_path ? s_files[h].path : f->name();
    if (!s) {
        s = "";
    }
    if (buf && size) {
        strlcpy(buf, s, size);
    }
    return (int)strlen(s);
}

extern "C" int bl_impl_fs_next(int h)
{
    File *f = file_of(h);
    if (!f) {
        return -1;
    }
    File next = f->openNextFile();
    if (!next) {
        return -1;
    }
    char path[256];
    const char *dir = s_files[h].path;
    snprintf(path, sizeof(path), "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] == '/' ? "" : "/", next.name());
    return file_slot(next, path);
}

extern "C" int bl_impl_fs_path_op(int op, const char *path, const char *path2)
{
    return path_op(s_lfs_mounted ? &LittleFS : NULL, op, path, path2);
}

/* ---------------- microSD (SPI) ---------------- */

extern "C" void bl_impl_sd_end(void)
{
    if (s_sd_bus >= 0) {
        SDFS.end();
        s_sd_bus = -1;
    }
}

extern "C" int bl_impl_sd_begin(int bus, int cs, uint32_t freq_hz, int format_if_empty)
{
    if (bus < 0) {
        bus = BL_SD_SPI;
    }
    if (bus != BL_SD_SPI) {
        return BL_ERR_UNSUPPORTED;
    }
#ifdef BL_SD_CS
    if (cs < 0) {
        cs = BL_SD_CS;
    }
#endif
    if (cs < 0) {
        cs = BL_BOARD_SS;
    }
#if defined(BL_SD_SCK)
    int sd_spi = bl_impl_spi_begin(BL_SD_SCK, BL_SD_MISO, BL_SD_MOSI, -1);    // the slot's own SPI pins
#else
    int sd_spi = bl_impl_spi_begin(-1, -1, -1, -1);         // the board's SPI pins
#endif
    if (sd_spi != BL_OK) {
        return BL_ERR_ARG;
    }
    BlWdogLong wdog;        // with no card, SdFat retries for seconds before it gives up
    bl_impl_sd_end();
    SDFS.setConfig(SDFSConfig((uint8_t)cs, freq_hz ? freq_hz : SD_SCK_MHZ(10), *bl_spi_object()));
    bool ok = SDFS.begin();
    s_sd_bus = ok ? bus : -1;
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint64_t bl_impl_sd_info(int what)
{
    FSInfo info;
    if (s_sd_bus < 0 || !SDFS.info(info)) {
        return 0;       // CARD_NONE
    }
    switch (what) {
    case BL_SD_CARD_TYPE:   return 3;       // CARD_SDHC: SdFat doesn't tell the classes apart here
    case BL_SD_CARD_SIZE:   return info.totalBytes;
    case BL_SD_TOTAL_BYTES: return info.totalBytes;
    case BL_SD_USED_BYTES:  return info.usedBytes;
    default:                return 0;
    }
}

extern "C" int bl_impl_fsx_open(int fs, const char *path, const char *mode, int create)
{
    FS *f = fs_by_id(fs);
    return f ? open_in(f, path, mode) : BL_ERR_STATE;
}

extern "C" int bl_impl_fsx_path_op(int fs, int op, const char *path, const char *path2)
{
    return path_op(fs_by_id(fs), op, path, path2);
}

/* ---------------- Preferences ---------------- */

struct PrefsSlot {
    char dir[48];       /* /.prefs/<namespace> */
    bool read_only;
    bool used;
};
static PrefsSlot s_prefs[MAX_PREFS];

static PrefsSlot *prefs_of(int h)
{
    return (h >= 0 && h < MAX_PREFS && s_prefs[h].used) ? &s_prefs[h] : NULL;
}

static bool key_path(PrefsSlot *p, const char *key, char *out, size_t size)
{
    if (!key || !*key || strlen(key) > 15 || strchr(key, '/')) {     // NVS's key limit
        return false;
    }
    snprintf(out, size, "%s/%s", p->dir, key);
    return true;
}

static size_t fixed_size(int type)
{
    switch (type) {
    case BL_PREF_I8: case BL_PREF_U8: case BL_PREF_BOOL: return 1;
    case BL_PREF_I16: case BL_PREF_U16: return 2;
    case BL_PREF_I32: case BL_PREF_U32: case BL_PREF_FLOAT: return 4;
    case BL_PREF_I64: case BL_PREF_U64: case BL_PREF_DOUBLE: return 8;
    default: return 0;
    }
}

/* Reads a key's file: its type and up to size bytes of value. Returns the value's length, or -1. */
static int read_key(PrefsSlot *p, const char *key, int *type, void *buf, size_t size)
{
    char path[80];
    if (!key_path(p, key, path, sizeof(path))) {
        return -1;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return -1;
    }
    int t = f.read();
    int len = (int)f.size() - 1;
    if (t < 0 || len < 0) {
        f.close();
        return -1;
    }
    if (buf && size) {
        f.read((uint8_t *)buf, size < (size_t)len ? size : (size_t)len);
    }
    f.close();
    if (type) {
        *type = t;
    }
    return len;
}

extern "C" int bl_impl_prefs_open(const char *name, int read_only)
{
    if (!name || !*name || strlen(name) > 15 || strchr(name, '/')) {
        return BL_ERR_ARG;
    }
    if (!lfs_mount()) {
        return BL_ERR_FAIL;
    }
    for (int h = 0; h < MAX_PREFS; h++) {
        if (s_prefs[h].used) {
            continue;
        }
        PrefsSlot *p = &s_prefs[h];
        snprintf(p->dir, sizeof(p->dir), PREFS_ROOT "/%s", name);
        if (!LittleFS.exists(p->dir)) {
            if (read_only) {
                return BL_ERR_FAIL;     // like NVS: a read-only namespace must exist
            }
            LittleFS.mkdir(PREFS_ROOT);
            if (!LittleFS.mkdir(p->dir)) {
                return BL_ERR_FAIL;
            }
        }
        p->read_only = read_only != 0;
        p->used = true;
        return h;
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_prefs_close(int h)
{
    PrefsSlot *p = prefs_of(h);
    if (p) {
        p->used = false;
    }
}

extern "C" int bl_impl_prefs_clear(int h)
{
    PrefsSlot *p = prefs_of(h);
    if (!p || p->read_only) {
        return BL_ERR_FAIL;
    }
    char names[32][16];
    int n = 0;
    Dir dir = LittleFS.openDir(p->dir);
    while (n < 32 && dir.next()) {
        strlcpy(names[n++], dir.fileName().c_str(), sizeof(names[0]));
    }
    char path[80];
    for (int i = 0; i < n; i++) {
        if (key_path(p, names[i], path, sizeof(path))) {
            LittleFS.remove(path);
        }
    }
    return n == 32 ? bl_impl_prefs_clear(h) : BL_OK;    // more than 32 keys: go again
}

extern "C" int bl_impl_prefs_remove(int h, const char *key)
{
    PrefsSlot *p = prefs_of(h);
    char path[80];
    if (!p || p->read_only || !key_path(p, key, path, sizeof(path))) {
        return BL_ERR_FAIL;
    }
    return LittleFS.remove(path) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_prefs_put(int h, const char *key, int type, const void *data, size_t len)
{
    PrefsSlot *p = prefs_of(h);
    char path[80];
    if (!p || p->read_only || !data || !key_path(p, key, path, sizeof(path))) {
        return 0;
    }
    if (type == BL_PREF_STRING) {
        len = strlen((const char *)data);
    } else if (type != BL_PREF_BYTES) {
        if (len < fixed_size(type) || !fixed_size(type)) {
            return 0;
        }
        len = fixed_size(type);
    }
    File f = LittleFS.open(path, "w");
    if (!f) {
        return 0;
    }
    uint8_t t = (uint8_t)type;
    bool ok = f.write(&t, 1) == 1 && f.write((const uint8_t *)data, len) == len;
    f.close();
    return ok ? (int)len : 0;
}

extern "C" int bl_impl_prefs_get(int h, const char *key, int type, void *buf, size_t size)
{
    PrefsSlot *p = prefs_of(h);
    if (!p) {
        return -1;
    }
    if (type == BL_PREF_STRING) {
        int len = read_key(p, key, NULL, buf, size ? size - 1 : 0);
        if (len >= 0 && buf && size) {
            ((char *)buf)[(size_t)len < size - 1 ? (size_t)len : size - 1] = '\0';
        }
        return len;
    }
    if (type == BL_PREF_BYTES) {
        return read_key(p, key, NULL, buf, size);
    }
    size_t want = fixed_size(type);
    if (!want) {
        return -1;
    }
    uint8_t tmp[8] = {0};
    if (read_key(p, key, NULL, tmp, sizeof(tmp)) < 0) {
        return -1;
    }
    if (buf) {
        memcpy(buf, tmp, size < want ? size : want);
    }
    return (int)want;
}

extern "C" int bl_impl_prefs_info(int h, const char *key, int what)
{
    PrefsSlot *p = prefs_of(h);
    if (!p) {
        return BL_ERR_ARG;
    }
    int type = 0;
    int len = key ? read_key(p, key, &type, NULL, 0) : -1;
    switch (what) {
    case BL_PREF_INFO_EXISTS:
        return len >= 0 ? 1 : 0;
    case BL_PREF_INFO_TYPE:     // Arduino-ESP32's PreferenceType: floats are blobs, bools bytes
        if (len < 0) return 10;                                         // PT_INVALID
        if (type == BL_PREF_FLOAT || type == BL_PREF_DOUBLE) return 9;  // PT_BLOB
        if (type == BL_PREF_BOOL) return BL_PREF_U8;
        return type;
    case BL_PREF_INFO_LENGTH:
        return len;
    case BL_PREF_INFO_FREE_ENTRIES: {   // no fixed number of entries on LittleFS: report the free space in entries
        FSInfo info;
        return s_lfs_mounted && LittleFS.info(info) ? (int)((info.totalBytes - info.usedBytes) / 32) : 0;
    }
    default:
        return BL_ERR_ARG;
    }
}
