/*
 * baseline_storage.h - Preferences (NVS key/value) and LittleFS for sketches,
 * with the same names as Arduino-ESP32. Included by baseline_arduino.h.
 *
 *   Preferences prefs;                    prefs.begin("app"); prefs.putInt("count", 1);
 *   LittleFS.begin(true);                 File f = LittleFS.open("/log.txt", FILE_APPEND);
 *
 * LittleFS lives on the board's "userfs" partition (1.2 MB) and survives sketch
 * uploads. At most 8 files/directories can be open at once. The namespace
 * "baseline" is reserved for Baseline's own settings.
 */
#ifndef BASELINE_STORAGE_H
#define BASELINE_STORAGE_H

#ifdef __cplusplus

#include <time.h>

/* ---------------- Preferences ---------------- */

typedef enum {
    PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID
} PreferenceType;

class Preferences {
public:
    ~Preferences() { end(); }
    bool begin(const char *name, bool readOnly = false, const char *partition_label = 0)
    {
        end();
        h_ = bl_prefs_open(name, readOnly);
        return h_ >= 0;
    }
    void end()
    {
        if (h_ >= 0) {
            bl_prefs_close(h_);
            h_ = -1;
        }
    }
    bool clear() { return h_ >= 0 && bl_prefs_clear(h_) == BL_OK; }
    bool remove(const char *key) { return h_ >= 0 && bl_prefs_remove(h_, key) == BL_OK; }
    bool isKey(const char *key) { return h_ >= 0 && bl_prefs_info(h_, key, BL_PREF_INFO_EXISTS) == 1; }
    PreferenceType getType(const char *key) { return h_ >= 0 ? (PreferenceType)bl_prefs_info(h_, key, BL_PREF_INFO_TYPE) : PT_INVALID; }
    size_t freeEntries() { int n = h_ >= 0 ? bl_prefs_info(h_, 0, BL_PREF_INFO_FREE_ENTRIES) : 0; return n > 0 ? n : 0; }

    size_t putChar(const char *key, int8_t value) { return put(key, BL_PREF_I8, &value, sizeof(value)); }
    size_t putUChar(const char *key, uint8_t value) { return put(key, BL_PREF_U8, &value, sizeof(value)); }
    size_t putShort(const char *key, int16_t value) { return put(key, BL_PREF_I16, &value, sizeof(value)); }
    size_t putUShort(const char *key, uint16_t value) { return put(key, BL_PREF_U16, &value, sizeof(value)); }
    size_t putInt(const char *key, int32_t value) { return put(key, BL_PREF_I32, &value, sizeof(value)); }
    size_t putUInt(const char *key, uint32_t value) { return put(key, BL_PREF_U32, &value, sizeof(value)); }
    size_t putLong(const char *key, int32_t value) { return putInt(key, value); }
    size_t putULong(const char *key, uint32_t value) { return putUInt(key, value); }
    size_t putLong64(const char *key, int64_t value) { return put(key, BL_PREF_I64, &value, sizeof(value)); }
    size_t putULong64(const char *key, uint64_t value) { return put(key, BL_PREF_U64, &value, sizeof(value)); }
    size_t putFloat(const char *key, float value) { return put(key, BL_PREF_FLOAT, &value, sizeof(value)); }
    size_t putDouble(const char *key, double value) { return put(key, BL_PREF_DOUBLE, &value, sizeof(value)); }
    size_t putBool(const char *key, bool value) { uint8_t v = value; return put(key, BL_PREF_BOOL, &v, 1); }
    size_t putString(const char *key, const char *value) { return value ? put(key, BL_PREF_STRING, value, strlen(value) + 1) : 0; }
    size_t putString(const char *key, const String &value) { return putString(key, value.c_str()); }
    size_t putBytes(const char *key, const void *value, size_t len) { return put(key, BL_PREF_BYTES, value, len); }

    int8_t getChar(const char *key, int8_t defaultValue = 0) { return get(key, BL_PREF_I8, defaultValue); }
    uint8_t getUChar(const char *key, uint8_t defaultValue = 0) { return get(key, BL_PREF_U8, defaultValue); }
    int16_t getShort(const char *key, int16_t defaultValue = 0) { return get(key, BL_PREF_I16, defaultValue); }
    uint16_t getUShort(const char *key, uint16_t defaultValue = 0) { return get(key, BL_PREF_U16, defaultValue); }
    int32_t getInt(const char *key, int32_t defaultValue = 0) { return get(key, BL_PREF_I32, defaultValue); }
    uint32_t getUInt(const char *key, uint32_t defaultValue = 0) { return get(key, BL_PREF_U32, defaultValue); }
    int32_t getLong(const char *key, int32_t defaultValue = 0) { return getInt(key, defaultValue); }
    uint32_t getULong(const char *key, uint32_t defaultValue = 0) { return getUInt(key, defaultValue); }
    int64_t getLong64(const char *key, int64_t defaultValue = 0) { return get(key, BL_PREF_I64, defaultValue); }
    uint64_t getULong64(const char *key, uint64_t defaultValue = 0) { return get(key, BL_PREF_U64, defaultValue); }
    float getFloat(const char *key, float defaultValue = 0) { return get(key, BL_PREF_FLOAT, defaultValue); }
    double getDouble(const char *key, double defaultValue = 0) { return get(key, BL_PREF_DOUBLE, defaultValue); }
    bool getBool(const char *key, bool defaultValue = false) { return get(key, BL_PREF_BOOL, (uint8_t)defaultValue) != 0; }
    /* Copies the string with its NUL; returns the bytes copied, or 0 if missing or too long. */
    size_t getString(const char *key, char *value, size_t maxLen);
    String getString(const char *key, const String &defaultValue = String());
    size_t getBytesLength(const char *key) { int n = h_ >= 0 ? bl_prefs_info(h_, key, BL_PREF_INFO_LENGTH) : -1; return n > 0 ? n : 0; }
    /* Returns the blob length, or 0 if missing or larger than maxLen. */
    size_t getBytes(const char *key, void *buf, size_t maxLen);

private:
    size_t put(const char *key, int type, const void *data, size_t len)
    {
        int n = h_ >= 0 ? bl_prefs_put(h_, key, type, data, len) : 0;
        return n > 0 ? (size_t)n : 0;
    }
    template <typename T> T get(const char *key, int type, T defaultValue)
    {
        T v;
        return (h_ >= 0 && bl_prefs_get(h_, key, type, &v, sizeof(v)) >= 0) ? v : defaultValue;
    }
    int h_ = -1;
};

/* ---------------- LittleFS ---------------- */

#define FILE_READ   "r"
#define FILE_WRITE  "w"
#define FILE_APPEND "a"

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

/* Copies share one open file (reference counted), as in Arduino-ESP32. */
class File : public Stream {
public:
    File() {}
    explicit File(int handle) : h_(handle) {}       // takes ownership of a handle
    File(const File &o) : Stream(o), h_(o.h_) { retain(); }
    File &operator=(const File &o)
    {
        if (this != &o) {
            close();
            h_ = o.h_;
            retain();
        }
        return *this;
    }
    virtual ~File() { close(); }

    using Print::write;
    size_t write(const uint8_t *buf, size_t size) override { return h_ >= 0 ? (size_t)bl_fs_write(h_, buf, size) : 0; }
    int available() override { return h_ >= 0 ? (int)(size() - position()) : 0; }
    int read() override;
    size_t read(uint8_t *buf, size_t size) { int n = h_ >= 0 ? bl_fs_read(h_, buf, size) : 0; return n > 0 ? n : 0; }
    size_t readBytes(char *buf, size_t size) { return read((uint8_t *)buf, size); }
    int peek() { return h_ >= 0 ? bl_fs_peek(h_) : -1; }
    void flush() { if (h_ >= 0) bl_fs_flush(h_); }
    bool seek(uint32_t pos, SeekMode mode = SeekSet) { return h_ >= 0 && bl_fs_seek(h_, pos, mode); }
    size_t position() const { return info(BL_FS_FILE_POSITION); }
    size_t size() const { return info(BL_FS_FILE_SIZE); }
    bool isDirectory() const { return info(BL_FS_FILE_IS_DIR) != 0; }
    time_t getLastWrite() { return (time_t)info(BL_FS_FILE_LAST_WRITE); }
    const char *name() const;   // "log.txt"
    const char *path() const;   // "/logs/log.txt"
    void close()
    {
        if (h_ >= 0) {
            bl_fs_release(h_);
            h_ = -1;
        }
    }
    operator bool() const { return h_ >= 0; }

    /* directories */
    File openNextFile(const char *mode = FILE_READ) { int n = h_ >= 0 ? bl_fs_next(h_) : -1; return n >= 0 ? File(n) : File(); }
    void rewindDirectory();

private:
    uint32_t info(int what) const { return h_ >= 0 ? bl_fs_file_info(h_, what) : 0; }
    void retain() { if (h_ >= 0) bl_fs_retain(h_); }
    int h_ = -1;
    mutable char name_[72] = {};
    mutable char path_[72] = {};
};

class FS {
public:
    File open(const char *path, const char *mode = FILE_READ, bool create = false)
    {
        int h = bl_fs_open(path, mode, create);
        return h >= 0 ? File(h) : File();
    }
    File open(const String &path, const char *mode = FILE_READ, bool create = false) { return open(path.c_str(), mode, create); }
    bool exists(const char *path) { return bl_fs_path_op(BL_FS_EXISTS, path, 0) == 1; }
    bool exists(const String &path) { return exists(path.c_str()); }
    bool remove(const char *path) { return bl_fs_path_op(BL_FS_REMOVE, path, 0) == 1; }
    bool remove(const String &path) { return remove(path.c_str()); }
    bool rename(const char *from, const char *to) { return bl_fs_path_op(BL_FS_RENAME, from, to) == 1; }
    bool rename(const String &from, const String &to) { return rename(from.c_str(), to.c_str()); }
    bool mkdir(const char *path) { return bl_fs_path_op(BL_FS_MKDIR, path, 0) == 1; }
    bool mkdir(const String &path) { return mkdir(path.c_str()); }
    bool rmdir(const char *path) { return bl_fs_path_op(BL_FS_RMDIR, path, 0) == 1; }
    bool rmdir(const String &path) { return rmdir(path.c_str()); }
};

/* Arguments other than formatOnFail are accepted for compatibility: the file
 * system is always the "userfs" partition. */
class LittleFSFS : public FS {
public:
    bool begin(bool formatOnFail = false, const char *basePath = "/littlefs", uint8_t maxOpenFiles = 8,
               const char *partitionLabel = "userfs")
    {
        return bl_fs_begin(formatOnFail) == BL_OK;
    }
    void end() { bl_fs_end(); }
    bool format() { return bl_fs_format() == BL_OK; }
    size_t totalBytes() { return bl_fs_info(BL_FS_TOTAL_BYTES); }
    size_t usedBytes() { return bl_fs_info(BL_FS_USED_BYTES); }
};

extern LittleFSFS LittleFS;

/* Arduino code often writes fs::FS and fs::File */
namespace fs {
using ::File;
using ::FS;
using ::LittleFSFS;
using ::SeekMode;
}

#endif /* __cplusplus */
#endif /* BASELINE_STORAGE_H */
