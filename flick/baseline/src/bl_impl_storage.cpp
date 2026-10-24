/* API table: Preferences (NVS key/value), LittleFS (ABI 1.4), microSD (1.6). */
#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <SD.h>
#include "soc/soc_caps.h"
#if SOC_SDMMC_HOST_SUPPORTED
#include <SD_MMC.h>
#endif
#include "gen/bl_impl.h"
#include "bl_str.h"
#include "bl_port.h"
#include "sketch_loader.h"

#define MAX_PREFS       4
#define MAX_FILES       8
#define FS_PARTITION    "userfs"
#define RESERVED_NS     "baseline"      // Baseline's own settings (bl_state.c)

static_assert(BL_PREF_I8 == PT_I8 && BL_PREF_U8 == PT_U8 && BL_PREF_I16 == PT_I16 && BL_PREF_U16 == PT_U16 &&
              BL_PREF_I32 == PT_I32 && BL_PREF_U32 == PT_U32 && BL_PREF_I64 == PT_I64 && BL_PREF_U64 == PT_U64 &&
              BL_PREF_STRING == PT_STR && BL_PREF_BYTES == PT_BLOB, "BL_PREF_* must match PreferenceType");

/* ---------------- Preferences ---------------- */

static Preferences *s_prefs[MAX_PREFS];

static Preferences *prefs_of(int h)
{
    return (h >= 0 && h < MAX_PREFS) ? s_prefs[h] : NULL;
}

extern "C" int bl_impl_prefs_open(const char *name, int read_only)
{
    if (!name || !*name || strcmp(name, RESERVED_NS) == 0) {
        return BL_ERR_ARG;
    }
    for (int h = 0; h < MAX_PREFS; h++) {
        if (!s_prefs[h]) {
            Preferences *p = new Preferences();
            if (!p->begin(name, read_only != 0)) {
                delete p;
                return BL_ERR_FAIL;
            }
            s_prefs[h] = p;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_prefs_close(int h)
{
    Preferences *p = prefs_of(h);
    if (p) {
        p->end();
        delete p;
        s_prefs[h] = NULL;
    }
}

extern "C" int bl_impl_prefs_clear(int h)
{
    Preferences *p = prefs_of(h);
    return (p && p->clear()) ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_prefs_remove(int h, const char *key)
{
    Preferences *p = prefs_of(h);
    return (p && key && p->remove(key)) ? BL_OK : BL_ERR_FAIL;
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

extern "C" int bl_impl_prefs_put(int h, const char *key, int type, const void *data, size_t len)
{
    Preferences *p = prefs_of(h);
    if (!p || !key || !data || len < fixed_size(type)) {
        return 0;
    }
    switch (type) {
    case BL_PREF_I8:     return p->putChar(key, *(const int8_t *)data);
    case BL_PREF_U8:     return p->putUChar(key, *(const uint8_t *)data);
    case BL_PREF_I16:    return p->putShort(key, *(const int16_t *)data);
    case BL_PREF_U16:    return p->putUShort(key, *(const uint16_t *)data);
    case BL_PREF_I32:    return p->putInt(key, *(const int32_t *)data);
    case BL_PREF_U32:    return p->putUInt(key, *(const uint32_t *)data);
    case BL_PREF_I64:    return p->putLong64(key, *(const int64_t *)data);
    case BL_PREF_U64:    return p->putULong64(key, *(const uint64_t *)data);
    case BL_PREF_FLOAT:  return p->putFloat(key, *(const float *)data);
    case BL_PREF_DOUBLE: return p->putDouble(key, *(const double *)data);
    case BL_PREF_BOOL:   return p->putBool(key, *(const uint8_t *)data != 0);
    case BL_PREF_STRING: return p->putString(key, (const char *)data);
    case BL_PREF_BYTES:  return p->putBytes(key, data, len);
    default:             return 0;
    }
}

template <typename T> static int copy_value(T v, void *buf, size_t size)
{
    if (buf) {
        memcpy(buf, &v, size < sizeof(v) ? size : sizeof(v));
    }
    return sizeof(v);
}

extern "C" int bl_impl_prefs_get(int h, const char *key, int type, void *buf, size_t size)
{
    Preferences *p = prefs_of(h);
    if (!p || !key || !p->isKey(key)) {
        return -1;
    }
    switch (type) {
    case BL_PREF_I8:     return copy_value(p->getChar(key), buf, size);
    case BL_PREF_U8:     return copy_value(p->getUChar(key), buf, size);
    case BL_PREF_I16:    return copy_value(p->getShort(key), buf, size);
    case BL_PREF_U16:    return copy_value(p->getUShort(key), buf, size);
    case BL_PREF_I32:    return copy_value(p->getInt(key), buf, size);
    case BL_PREF_U32:    return copy_value(p->getUInt(key), buf, size);
    case BL_PREF_I64:    return copy_value(p->getLong64(key), buf, size);
    case BL_PREF_U64:    return copy_value(p->getULong64(key), buf, size);
    case BL_PREF_FLOAT:  return copy_value(p->getFloat(key), buf, size);
    case BL_PREF_DOUBLE: return copy_value(p->getDouble(key), buf, size);
    case BL_PREF_BOOL:   return copy_value((uint8_t)p->getBool(key), buf, size);
    case BL_PREF_STRING: {
        String s = p->getString(key);
        if (buf && size) {
            bl_copy_str((char *)buf, size, s);
        }
        return (int)s.length();
    }
    case BL_PREF_BYTES: {
        size_t n = p->getBytesLength(key);
        if (buf && size) {
            p->getBytes(key, buf, size);
        }
        return (int)n;
    }
    default:
        return -1;
    }
}

extern "C" int bl_impl_prefs_info(int h, const char *key, int what)
{
    Preferences *p = prefs_of(h);
    if (!p) {
        return BL_ERR_ARG;
    }
    switch (what) {
    case BL_PREF_INFO_EXISTS:
        return (key && p->isKey(key)) ? 1 : 0;
    case BL_PREF_INFO_TYPE:
        return key ? (int)p->getType(key) : (int)PT_INVALID;
    case BL_PREF_INFO_LENGTH: {
        if (!key || !p->isKey(key)) {
            return -1;
        }
        PreferenceType t = p->getType(key);
        if (t == PT_STR) {
            return (int)p->getString(key).length();
        }
        if (t == PT_BLOB) {
            return (int)p->getBytesLength(key);
        }
        return (int)fixed_size((int)t);
    }
    case BL_PREF_INFO_FREE_ENTRIES:
        return (int)p->freeEntries();
    default:
        return BL_ERR_ARG;
    }
}

/* ---------------- LittleFS ---------------- */

struct FileSlot {
    File *file;
    int refs;
};
static FileSlot s_files[MAX_FILES];
static bool s_mounted;

static File *file_of(int h)
{
    return (h >= 0 && h < MAX_FILES) ? s_files[h].file : NULL;
}

static int file_slot(File f)
{
    if (!f) {
        return BL_ERR_FAIL;
    }
    for (int h = 0; h < MAX_FILES; h++) {
        if (!s_files[h].file) {
            s_files[h] = { new File(f), 1 };
            return h;
        }
    }
    return BL_ERR_NO_MEM;   // too many open files
}

extern "C" int bl_impl_fs_begin(int format_if_failed)
{
    if (!s_mounted) {
        WdtPause wdt;   // formatting a blank partition takes a few seconds
        s_mounted = LittleFS.begin(format_if_failed != 0, "/littlefs", MAX_FILES, FS_PARTITION);
    }
    return s_mounted ? BL_OK : BL_ERR_FAIL;
}

extern "C" void bl_impl_fs_end(void)
{
    if (s_mounted) {
        LittleFS.end();
        s_mounted = false;
    }
}

extern "C" int bl_impl_fs_format(void)
{
    WdtPause wdt;
    return LittleFS.format() ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint32_t bl_impl_fs_info(int what)
{
    if (!s_mounted) {
        return 0;
    }
    return what == BL_FS_TOTAL_BYTES ? LittleFS.totalBytes() : what == BL_FS_USED_BYTES ? LittleFS.usedBytes() : 0;
}

extern "C" int bl_impl_fs_open(const char *path, const char *mode, int create)
{
    if (!s_mounted) {
        return BL_ERR_STATE;
    }
    if (!path) {
        return BL_ERR_ARG;
    }
    return file_slot(LittleFS.open(path, mode ? mode : FILE_READ, create != 0));
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
    case BL_FS_FILE_POSITION:   return f->position();
    case BL_FS_FILE_SIZE:       return f->size();
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
    const char *s = full_path ? f->path() : f->name();
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
    return next ? file_slot(next) : -1;
}

extern "C" int bl_impl_fs_path_op(int op, const char *path, const char *path2)
{
    if (!s_mounted || !path) {
        return 0;
    }
    switch (op) {
    case BL_FS_EXISTS: return LittleFS.exists(path);
    case BL_FS_REMOVE: return LittleFS.remove(path);
    case BL_FS_RENAME: return path2 ? LittleFS.rename(path, path2) : 0;
    case BL_FS_MKDIR:  return LittleFS.mkdir(path);
    case BL_FS_RMDIR:  return LittleFS.rmdir(path);
    default:           return 0;
    }
}

/* ---------------- microSD + any file system (ABI 1.6) ---------------- */

static int s_sd_bus = -1;       // BL_SD_SPI / BL_SD_MMC while mounted

static fs::FS *fs_by_id(int fs)
{
    if (fs == BL_FS_LITTLEFS) {
        return s_mounted ? &LittleFS : NULL;
    }
    if (fs == BL_FS_SD) {
#if SOC_SDMMC_HOST_SUPPORTED
        if (s_sd_bus == BL_SD_MMC) {
            return &SD_MMC;
        }
#endif
        return s_sd_bus == BL_SD_SPI ? &SD : NULL;
    }
    return NULL;
}

extern "C" void bl_impl_sd_end(void)
{
#if SOC_SDMMC_HOST_SUPPORTED
    if (s_sd_bus == BL_SD_MMC) {
        SD_MMC.end();
    }
#endif
    if (s_sd_bus == BL_SD_SPI) {
        SD.end();
    }
    s_sd_bus = -1;
}

extern "C" int bl_impl_sd_begin(int bus, int cs, uint32_t freq_hz, int format_if_empty)
{
    bl_impl_sd_end();
    if (bus < 0) {
        bus = BL_SD_DEFAULT_BUS;
    }
    WdtPause wdt;   // card init and formatting can take seconds
    bool ok = false;
    if (bus == BL_SD_MMC) {
#if SOC_SDMMC_HOST_SUPPORTED
#ifdef BL_SDMMC_CLK
        SD_MMC.setPins(BL_SDMMC_CLK, BL_SDMMC_CMD, BL_SDMMC_D0, BL_SDMMC_D1, BL_SDMMC_D2, BL_SDMMC_D3);
#endif
        ok = SD_MMC.begin("/sdcard", cs == 1, format_if_empty != 0,
                          freq_hz ? (int)(freq_hz / 1000) : SDMMC_FREQ_DEFAULT, MAX_FILES);
#else
        return BL_ERR_UNSUPPORTED;
#endif
    } else if (bus == BL_SD_SPI) {
        if (cs < 0) {
            cs = BL_SD_CS;
        }
        if (cs < 0 || !bl_pin_usable(cs)) {
            return BL_ERR_ARG;
        }
        // SD.begin() starts SPI on the Arduino variant's pins unless it is
        // already running; start it on the board's (a no-op if the sketch did).
        SPI.begin(BL_BOARD_SCK, BL_BOARD_MISO, BL_BOARD_MOSI, -1);
        ok = SD.begin((uint8_t)cs, SPI, freq_hz ? freq_hz : 4000000, "/sd", MAX_FILES, format_if_empty != 0);
    } else {
        return BL_ERR_ARG;
    }
    s_sd_bus = ok ? bus : -1;
    return ok ? BL_OK : BL_ERR_FAIL;
}

extern "C" uint64_t bl_impl_sd_info(int what)
{
#if SOC_SDMMC_HOST_SUPPORTED
    if (s_sd_bus == BL_SD_MMC) {
        switch (what) {
        case BL_SD_CARD_TYPE:   return SD_MMC.cardType();
        case BL_SD_CARD_SIZE:   return SD_MMC.cardSize();
        case BL_SD_TOTAL_BYTES: return SD_MMC.totalBytes();
        case BL_SD_USED_BYTES:  return SD_MMC.usedBytes();
        default:                return 0;
        }
    }
#endif
    if (s_sd_bus == BL_SD_SPI) {
        switch (what) {
        case BL_SD_CARD_TYPE:   return SD.cardType();
        case BL_SD_CARD_SIZE:   return SD.cardSize();
        case BL_SD_TOTAL_BYTES: return SD.totalBytes();
        case BL_SD_USED_BYTES:  return SD.usedBytes();
        default:                return 0;
        }
    }
    return 0;   // CARD_NONE
}

extern "C" int bl_impl_fsx_open(int fs, const char *path, const char *mode, int create)
{
    fs::FS *f = fs_by_id(fs);
    if (!f) {
        return BL_ERR_STATE;
    }
    if (!path) {
        return BL_ERR_ARG;
    }
    return file_slot(f->open(path, mode ? mode : FILE_READ, create != 0));
}

extern "C" int bl_impl_fsx_path_op(int fs, int op, const char *path, const char *path2)
{
    fs::FS *f = fs_by_id(fs);
    if (!f || !path) {
        return 0;
    }
    switch (op) {
    case BL_FS_EXISTS: return f->exists(path);
    case BL_FS_REMOVE: return f->remove(path);
    case BL_FS_RENAME: return path2 ? f->rename(path, path2) : 0;
    case BL_FS_MKDIR:  return f->mkdir(path);
    case BL_FS_RMDIR:  return f->rmdir(path);
    default:           return 0;
    }
}