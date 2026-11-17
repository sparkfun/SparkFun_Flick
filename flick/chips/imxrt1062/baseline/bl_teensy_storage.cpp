/*
 * bl_teensy_storage.cpp - LittleFS, microSD and Preferences on the i.MX RT1062.
 *
 * LittleFS is Teensy's LittleFS_Program: the end of program flash, below the
 * EEPROM emulation (BL_BOARD_FS_SIZE; the targets' bl_board.h shows where).
 * microSD is Teensy's SD library (SdFat): the 4.1's SDIO slot, or SPI. Both are
 * FS objects with the same File class, so one handle table serves both, as on
 * the ESP32 family.
 *
 * Preferences have no Teensy counterpart; here a namespace is a folder in
 * LittleFS (/.prefs/<namespace>) and a key is a file holding a type byte and
 * the value. LittleFS is mounted for them on first use.
 */
#include <LittleFS.h>
#include <SD.h>
#include "bl_teensy.h"

#define MAX_FILES       8
#define MAX_PREFS       4
#define PREFS_ROOT      "/.prefs"

static LittleFS_Program s_lfs;
static bool s_lfs_mounted;
static int s_sd_bus = -1;       /* BL_SD_SPI / BL_SD_MMC while mounted */

static bool lfs_mount(void)
{
    if (!s_lfs_mounted) {
        BlWdogLong wdog;        // formatting a blank region takes seconds
        s_lfs_mounted = s_lfs.begin(BL_BOARD_FS_SIZE);      // formats a blank region
    }
    return s_lfs_mounted;
}

extern "C" FS *bl_usbh_fs(void);

static FS *fs_by_id(int fs)
{
    if (fs == BL_FS_LITTLEFS) {
        return s_lfs_mounted ? (FS *)&s_lfs : NULL;
    }
    if (fs == BL_FS_SD) {
        return s_sd_bus >= 0 ? (FS *)&SD : NULL;
    }
    if (fs == BL_FS_USB) {
        return bl_usbh_fs();        // a USB drive on the host port (bl_teensy_usbhost.cpp)
    }
    return NULL;
}

/* ---------------- files ---------------- */

struct FileSlot {
    File *file;
    char *path;     /* full path (Teensy's File::name() is the last part only) */
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

/* Arduino-ESP32 modes ("r", "w", "a", "r+", "w+", "a+") on Teensy's FILE_* modes. */
static int open_in(FS *fs, const char *path, const char *mode)
{
    if (!path) {
        return BL_ERR_ARG;
    }
    char m = mode && *mode ? mode[0] : 'r';
    bool plus = mode && strchr(mode, '+');
    File f;
    if (m == 'w') {
        f = fs->open(path, FILE_WRITE_BEGIN);
        if (f) {
            f.truncate(0);
        }
    } else if (m == 'a') {
        f = fs->open(path, FILE_WRITE);
    } else {
        f = fs->open(path, plus ? FILE_WRITE_BEGIN : FILE_READ);
    }
    return file_slot(f, path);
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
    // Teensy's LittleFS has no end(); the files are closed by their handles.
}

extern "C" int bl_impl_fs_format(void)
{
    if (!lfs_mount()) {
        return BL_ERR_FAIL;
    }
    BlWdogLong wdog;
    return s_lfs.quickFormat() ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_fs_info(int what)
{
    if (!s_lfs_mounted) {
        return 0;
    }
    return what == BL_FS_TOTAL_BYTES ? (uint32_t)s_lfs.totalSize()
         : what == BL_FS_USED_BYTES  ? (uint32_t)s_lfs.usedSize() : 0;
}

extern "C" int bl_impl_fs_open(const char *path, const char *mode, int create)
{
    return s_lfs_mounted ? open_in(&s_lfs, path, mode) : BL_ERR_STATE;
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
    return (f && mode >= 0 && mode <= 2 && f->seek(pos, mode)) ? 1 : 0;
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
    case BL_FS_FILE_POSITION: return (uint32_t)f->position();
    case BL_FS_FILE_SIZE:     return (uint32_t)f->size();
    case BL_FS_FILE_IS_DIR:   return f->isDirectory() ? 1 : 0;
    case BL_FS_FILE_LAST_WRITE: {
        DateTimeFields tm;
        return f->getModifyTime(tm) ? (uint32_t)makeTime(tm) : 0;
    }
    default:
        return 0;
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
    return path_op(s_lfs_mounted ? &s_lfs : NULL, op, path, path2);
}

/* ---------------- microSD ---------------- */

extern "C" void bl_impl_sd_end(void)
{
    s_sd_bus = -1;      // Teensy's SD library has no end()
}

extern "C" int bl_impl_sd_begin(int bus, int cs, uint32_t freq_hz, int format_if_empty)
{
#ifdef BL_BOARD_SD_BUILTIN
    if (bus < 0) {
        bus = BL_SD_MMC;
    }
#else
    if (bus < 0) {
        bus = BL_SD_SPI;
    }
#endif
    BlWdogLong wdog;        // with no card, SdFat retries for seconds before it gives up
    bool ok;
    if (bus == BL_SD_MMC) {
#ifdef BL_BOARD_SD_BUILTIN
        ok = SD.begin(BL_BOARD_SD_BUILTIN);     // the 4.1's SDIO slot
#else
        return BL_ERR_UNSUPPORTED;
#endif
    } else if (bus == BL_SD_SPI) {
        if (cs < 0) {
            cs = BL_BOARD_SS;
        }
        ok = SD.begin((uint8_t)cs);
    } else {
        return BL_ERR_ARG;
    }
    s_sd_bus = ok ? bus : -1;
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint64_t bl_impl_sd_info(int what)
{
    if (s_sd_bus < 0) {
        return 0;       // CARD_NONE
    }
    switch (what) {
    case BL_SD_CARD_TYPE:   return 3;       // CARD_SDHC: SdFat doesn't tell the classes apart here
    case BL_SD_CARD_SIZE:   return SD.totalSize();
    case BL_SD_TOTAL_BYTES: return SD.totalSize();
    case BL_SD_USED_BYTES:  return SD.usedSize();
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
    File f = s_lfs.open(path, FILE_READ);
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
        f.read(buf, size < (size_t)len ? size : (size_t)len);
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
        if (!s_lfs.exists(p->dir)) {
            if (read_only) {
                return BL_ERR_FAIL;     // like NVS: a read-only namespace must exist
            }
            s_lfs.mkdir(PREFS_ROOT);
            if (!s_lfs.mkdir(p->dir)) {
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
    File dir = s_lfs.open(p->dir);
    if (!dir) {
        return BL_ERR_FAIL;
    }
    char names[32][16];
    int n = 0;
    for (File f = dir.openNextFile(); f && n < 32; f = dir.openNextFile()) {
        strlcpy(names[n++], f.name(), sizeof(names[0]));
        f.close();
    }
    dir.close();
    char path[80];
    for (int i = 0; i < n; i++) {
        if (key_path(p, names[i], path, sizeof(path))) {
            s_lfs.remove(path);
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
    return s_lfs.remove(path) ? BL_OK : BL_ERR_FAIL;
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
    File f = s_lfs.open(path, FILE_WRITE_BEGIN);
    if (!f) {
        return 0;
    }
    f.truncate(0);
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
    case BL_PREF_INFO_FREE_ENTRIES:     // no fixed number of entries on LittleFS: report the free space in entries
        return s_lfs_mounted ? (int)((s_lfs.totalSize() - s_lfs.usedSize()) / 32) : 0;
    default:
        return BL_ERR_ARG;
    }
}
