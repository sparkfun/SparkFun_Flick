/*
 * bl_apollo3_storage.cpp - LittleFS and Preferences on the Apollo3.
 *
 * LittleFS is Mbed's LittleFileSystem2 in the flash region bl_target.h gives it
 * (BL_APOLLO3_FS_START..END, 8 KB blocks), on a block device that writes
 * through Baseline's own flash functions (interrupts and the MPU off, as for
 * sketch uploads). One handle table serves files and directories.
 *
 * Preferences: a namespace is a folder in LittleFS (/.prefs/<namespace>) and a
 * key is a file holding a type byte and the value, as on the RP2350 and Teensy.
 * LittleFS is mounted for them on first use.
 */
#include <fcntl.h>
#include <mbed.h>
#include "BlockDevice.h"
#include "LittleFileSystem2.h"
#include "bl_apollo3.h"

using mbed::bd_addr_t;
using mbed::bd_size_t;
using mbed::LittleFileSystem2;

#define MAX_FILES       8
#define MAX_PREFS       4
#define PREFS_ROOT      "/.prefs"

/* ---- the block device: Baseline's flash region for files ---- */
class BlFlashBD : public mbed::BlockDevice {
public:
    int init() override { return 0; }
    int deinit() override { return 0; }
    int read(void *buffer, bd_addr_t addr, bd_size_t size) override
    {
        if (addr + size > span()) {
            return -1;
        }
        memcpy(buffer, (const void *)(BL_APOLLO3_FS_START + (uint32_t)addr), (size_t)size);
        return 0;
    }
    int program(const void *buffer, bd_addr_t addr, bd_size_t size) override
    {
        static uint32_t buf[64];        // word aligned, in SRAM
        if (addr + size > span() || (addr & 3) || (size & 3)) {
            return -1;
        }
        const uint8_t *src = (const uint8_t *)buffer;
        while (size) {
            uint32_t n = size < sizeof(buf) ? (uint32_t)size : sizeof(buf);
            memcpy(buf, src, n);
            bl_flash_write(BL_APOLLO3_FS_START + (uint32_t)addr, (const uint8_t *)buf, n);
            src += n;
            addr += n;
            size -= n;
        }
        return 0;
    }
    int erase(bd_addr_t addr, bd_size_t size) override
    {
        if (addr + size > span() || (addr % AM_PAGE) || (size % AM_PAGE)) {
            return -1;
        }
        bl_flash_erase(BL_APOLLO3_FS_START + (uint32_t)addr, (uint32_t)size);
        return 0;
    }
    bd_size_t get_read_size() const override { return 1; }
    bd_size_t get_program_size() const override { return 4; }
    bd_size_t get_erase_size() const override { return AM_PAGE; }
    bd_size_t get_erase_size(bd_addr_t) const override { return AM_PAGE; }
    int get_erase_value() const override { return 0xFF; }
    bd_size_t size() const override { return span(); }
    const char *get_type() const override { return "FLICK"; }

private:
    static constexpr uint32_t AM_PAGE = 8192;
    static uint32_t span() { return BL_APOLLO3_FS_END - BL_APOLLO3_FS_START; }
};

static BlFlashBD s_bd;
static LittleFileSystem2 s_fs(NULL);
static bool s_lfs_mounted;

static bool lfs_mount(void)
{
    if (!s_lfs_mounted) {
        BlWdogLong wdog;        // formatting a blank region takes a while
        int err = s_fs.mount(&s_bd);
        if (err) {
            err = s_fs.reformat(&s_bd);     // a blank or damaged region: format it, as the ESP32's begin(true)
        }
        s_lfs_mounted = err == 0;
    }
    return s_lfs_mounted;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return s_fs.stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool exists(const char *path)
{
    struct stat st;
    return s_fs.stat(path, &st) == 0;
}

/* ---------------- files ---------------- */

struct FileSlot {
    mbed::File *file;   /* one of file and dir */
    mbed::Dir *dir;
    char *path;         /* full path, as the sketch opened it */
    int refs;
};
static FileSlot s_files[MAX_FILES];

static FileSlot *slot_of(int h)
{
    return (h >= 0 && h < MAX_FILES && (s_files[h].file || s_files[h].dir)) ? &s_files[h] : NULL;
}

static mbed::File *file_of(int h)
{
    return (h >= 0 && h < MAX_FILES) ? s_files[h].file : NULL;
}

static int open_flags(const char *mode)
{
    if (!mode || !*mode || !strcmp(mode, "r")) return O_RDONLY;
    if (!strcmp(mode, "w"))  return O_WRONLY | O_CREAT | O_TRUNC;
    if (!strcmp(mode, "a"))  return O_WRONLY | O_CREAT | O_APPEND;
    if (!strcmp(mode, "r+")) return O_RDWR;
    if (!strcmp(mode, "w+")) return O_RDWR | O_CREAT | O_TRUNC;
    if (!strcmp(mode, "a+")) return O_RDWR | O_CREAT | O_APPEND;
    return -1;
}

static int free_slot(void)
{
    for (int h = 0; h < MAX_FILES; h++) {
        if (!s_files[h].file && !s_files[h].dir) {
            return h;
        }
    }
    return BL_ERR_NO_MEM;   // too many open files
}

static int open_path(const char *path, const char *mode)
{
    if (!path) {
        return BL_ERR_ARG;
    }
    int h = free_slot();
    if (h < 0) {
        return h;
    }
    if (is_dir(path)) {
        mbed::Dir *d = new mbed::Dir;
        if (d->open(&s_fs, path) != 0) {
            delete d;
            return BL_ERR_FAIL;
        }
        s_files[h] = {NULL, d, strdup(path), 1};
        return h;
    }
    int flags = open_flags(mode);
    if (flags < 0) {
        return BL_ERR_ARG;
    }
    mbed::File *f = new mbed::File;
    if (f->open(&s_fs, path, flags) != 0) {
        delete f;
        return BL_ERR_FAIL;
    }
    s_files[h] = {f, NULL, strdup(path), 1};
    return h;
}

static int path_op(int op, const char *path, const char *path2)
{
    if (!s_lfs_mounted || !path) {
        return 0;
    }
    switch (op) {
    case BL_FS_EXISTS: return exists(path);
    case BL_FS_REMOVE: return s_fs.remove(path) == 0;
    case BL_FS_RENAME: return path2 ? s_fs.rename(path, path2) == 0 : 0;
    case BL_FS_MKDIR:  return s_fs.mkdir(path, 0777) == 0 || is_dir(path);
    case BL_FS_RMDIR:  return is_dir(path) && s_fs.remove(path) == 0;
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
        s_fs.unmount();
        s_lfs_mounted = false;
    }
}

extern "C" int bl_impl_fs_format(void)
{
    BlWdogLong wdog;
    bl_impl_fs_end();
    bool ok = s_fs.reformat(&s_bd) == 0;
    s_lfs_mounted = ok;
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_fs_info(int what)
{
    struct statvfs st;
    if (!s_lfs_mounted || s_fs.statvfs("/", &st) != 0) {
        return 0;
    }
    return what == BL_FS_TOTAL_BYTES ? (uint32_t)(st.f_blocks * st.f_bsize)
         : what == BL_FS_USED_BYTES  ? (uint32_t)((st.f_blocks - st.f_bfree) * st.f_bsize) : 0;
}

extern "C" int bl_impl_fs_open(const char *path, const char *mode, int create)
{
    return s_lfs_mounted ? open_path(path, mode) : BL_ERR_STATE;
}

extern "C" void bl_impl_fs_retain(int h)
{
    if (slot_of(h)) {
        s_files[h].refs++;
    }
}

extern "C" void bl_impl_fs_release(int h)
{
    FileSlot *s = slot_of(h);
    if (s && --s->refs <= 0) {
        if (s->file) {
            s->file->close();
            delete s->file;
        }
        if (s->dir) {
            s->dir->close();
            delete s->dir;
        }
        free(s->path);
        *s = {};
    }
}

extern "C" int bl_impl_fs_read(int h, uint8_t *buf, size_t size)
{
    mbed::File *f = file_of(h);
    if (!f || !buf) {
        return -1;
    }
    ssize_t n = f->read(buf, size);
    return n < 0 ? -1 : (int)n;
}

extern "C" int bl_impl_fs_write(int h, const uint8_t *data, size_t len)
{
    mbed::File *f = file_of(h);
    if (!f || !data) {
        return 0;
    }
    ssize_t n = f->write(data, len);
    return n < 0 ? 0 : (int)n;
}

extern "C" int bl_impl_fs_peek(int h)
{
    mbed::File *f = file_of(h);
    uint8_t c;
    if (!f || f->read(&c, 1) != 1) {
        return -1;
    }
    f->seek(-1, SEEK_CUR);
    return c;
}

extern "C" int bl_impl_fs_seek(int h, uint32_t pos, int mode)
{
    static const int whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};     // Arduino's SeekSet/SeekCur/SeekEnd
    mbed::File *f = file_of(h);
    return (f && mode >= 0 && mode <= 2 && f->seek((off_t)pos, whence[mode]) >= 0) ? 1 : 0;
}

extern "C" void bl_impl_fs_flush(int h)
{
    mbed::File *f = file_of(h);
    if (f) {
        f->sync();
    }
}

extern "C" uint32_t bl_impl_fs_file_info(int h, int what)
{
    FileSlot *s = slot_of(h);
    if (!s) {
        return 0;
    }
    switch (what) {
    case BL_FS_FILE_POSITION:   return s->file ? (uint32_t)s->file->tell() : 0;
    case BL_FS_FILE_SIZE:       return s->file ? (uint32_t)s->file->size() : 0;
    case BL_FS_FILE_IS_DIR:     return s->dir ? 1 : 0;
    default:                    return 0;       // no modification times in this LittleFS
    }
}

extern "C" int bl_impl_fs_file_name(int h, int full_path, char *buf, size_t size)
{
    FileSlot *s = slot_of(h);
    if (!s) {
        return -1;
    }
    const char *p = s->path;
    if (!full_path) {
        const char *slash = strrchr(p, '/');
        p = slash ? slash + 1 : p;
    }
    if (buf && size) {
        strlcpy(buf, p, size);
    }
    return (int)strlen(p);
}

extern "C" int bl_impl_fs_next(int h)
{
    FileSlot *s = slot_of(h);
    if (!s || !s->dir) {
        return -1;
    }
    struct dirent ent;
    while (s->dir->read(&ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) {
            continue;
        }
        char path[256];
        const char *dir = s->path;
        size_t n = strlen(dir);
        snprintf(path, sizeof(path), "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", ent.d_name);
        int next = open_path(path, "r");
        return next >= 0 ? next : -1;
    }
    return -1;
}

extern "C" int bl_impl_fs_path_op(int op, const char *path, const char *path2)
{
    return path_op(op, path, path2);
}

extern "C" int bl_impl_fsx_open(int fs, const char *path, const char *mode, int create)
{
    if (fs != BL_FS_LITTLEFS) {
        return BL_ERR_STATE;        // no microSD on this chip's Baseline yet
    }
    return bl_impl_fs_open(path, mode, create);
}

extern "C" int bl_impl_fsx_path_op(int fs, int op, const char *path, const char *path2)
{
    return fs == BL_FS_LITTLEFS ? path_op(op, path, path2) : 0;
}

/* ---------------- Preferences ---------------- */

struct PrefsSlot {
    char dir[48];       /* /.prefs/<namespace> */
    bool read_only;
    bool used;
};
static PrefsSlot s_prefs[MAX_PREFS];

/* Every file and namespace closed (a sketch restart). LittleFS stays mounted. */
void bl_storage_reset(void)
{
    for (int h = 0; h < MAX_FILES; h++) {
        if (slot_of(h)) {
            s_files[h].refs = 1;
            bl_impl_fs_release(h);
        }
    }
    memset(s_prefs, 0, sizeof(s_prefs));
}

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
    mbed::File f;
    if (f.open(&s_fs, path, O_RDONLY) != 0) {
        return -1;
    }
    uint8_t t;
    int len = (int)f.size() - 1;
    if (f.read(&t, 1) != 1 || len < 0) {
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
        if (!is_dir(p->dir)) {
            if (read_only) {
                return BL_ERR_FAIL;     // like NVS: a read-only namespace must exist
            }
            s_fs.mkdir(PREFS_ROOT, 0777);
            if (s_fs.mkdir(p->dir, 0777) != 0) {
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
    mbed::Dir d;
    if (d.open(&s_fs, p->dir) == 0) {
        struct dirent ent;
        while (n < 32 && d.read(&ent) > 0) {
            if (strcmp(ent.d_name, ".") && strcmp(ent.d_name, "..")) {
                strlcpy(names[n++], ent.d_name, sizeof(names[0]));
            }
        }
        d.close();
    }
    char path[80];
    for (int i = 0; i < n; i++) {
        if (key_path(p, names[i], path, sizeof(path))) {
            s_fs.remove(path);
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
    return s_fs.remove(path) == 0 ? BL_OK : BL_ERR_FAIL;
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
    mbed::File f;
    if (f.open(&s_fs, path, O_WRONLY | O_CREAT | O_TRUNC) != 0) {
        return 0;
    }
    uint8_t t = (uint8_t)type;
    bool ok = f.write(&t, 1) == 1 && f.write(data, len) == (ssize_t)len;
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
        struct statvfs st;
        return s_lfs_mounted && s_fs.statvfs("/", &st) == 0 ? (int)(st.f_bfree * st.f_bsize / 32) : 0;
    }
    default:
        return BL_ERR_ARG;
    }
}
