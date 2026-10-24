/* sketch_storage.cpp - out-of-line parts of baseline_storage.h. */
#include "baseline.h"

LittleFSFS LittleFS;
SDFS SD;
SDMMCFS SD_MMC;

/* ---- Preferences ---- */

size_t Preferences::getString(const char *key, char *value, size_t maxLen)
{
    if (h_ < 0 || !value || !maxLen) {
        return 0;
    }
    int n = bl_prefs_get(h_, key, BL_PREF_STRING, value, maxLen);
    if (n < 0 || (size_t)n >= maxLen) {
        value[0] = '\0';
        return 0;
    }
    return (size_t)n + 1;
}

String Preferences::getString(const char *key, const String &defaultValue)
{
    int n = h_ >= 0 ? bl_prefs_info(h_, key, BL_PREF_INFO_LENGTH) : -1;
    if (n < 0) {
        return defaultValue;
    }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        return defaultValue;
    }
    bl_prefs_get(h_, key, BL_PREF_STRING, buf, (size_t)n + 1);
    String s(buf);
    free(buf);
    return s;
}

size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen)
{
    int n = h_ >= 0 ? bl_prefs_get(h_, key, BL_PREF_BYTES, buf, maxLen) : -1;
    return (n > 0 && (size_t)n <= maxLen) ? (size_t)n : 0;
}

/* ---- File ---- */

int File::read()
{
    uint8_t b;
    return (h_ >= 0 && bl_fs_read(h_, &b, 1) == 1) ? b : -1;
}

const char *File::name() const
{
    name_[0] = '\0';
    if (h_ >= 0) {
        bl_fs_file_name(h_, 0, name_, sizeof(name_));
    }
    return name_;
}

const char *File::path() const
{
    path_[0] = '\0';
    if (h_ >= 0) {
        bl_fs_file_name(h_, 1, path_, sizeof(path_));
    }
    return path_;
}

void File::rewindDirectory()
{
    if (h_ < 0 || !isDirectory()) {
        return;
    }
    char dir[sizeof(path_)];
    snprintf(dir, sizeof(dir), "%s", path());
    close();
    int h = bl_fsx_open(fs_, dir, FILE_READ, 0);
    if (h >= 0) {
        h_ = h;
    }
}
