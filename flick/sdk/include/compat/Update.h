/*
 * Update.h - Arduino-ESP32's Update class, on Baseline's update_* entries
 * (ABI 1.10). What is written decides where it goes:
 *   - a sketch (.flick) goes to the free sketch slot and starts at the next
 *     restart (it rolls back if it crashes, like any new sketch);
 *   - an ESP-IDF app image (a Baseline, or any Arduino firmware .bin) goes to
 *     the free app slot, on boards with two Baseline slots only.
 * Call ESP.restart() after end() to start it.
 *
 * Not supported: U_SPIFFS (filesystem images), setMD5 (returns false), rollBack.
 */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_ESP32)
#error "This board has no Update library (it is an ESP32 API)."
#endif

#include <functional>

#define UPDATE_ERROR_OK           0
#define UPDATE_ERROR_WRITE        1
#define UPDATE_ERROR_ERASE        2
#define UPDATE_ERROR_READ         3
#define UPDATE_ERROR_SPACE        4
#define UPDATE_ERROR_SIZE         5
#define UPDATE_ERROR_STREAM       6
#define UPDATE_ERROR_MD5          7
#define UPDATE_ERROR_MAGIC_BYTE   8
#define UPDATE_ERROR_ACTIVATE     9
#define UPDATE_ERROR_NO_PARTITION 10
#define UPDATE_ERROR_BAD_ARGUMENT 11
#define UPDATE_ERROR_ABORT        12

#define UPDATE_SIZE_UNKNOWN 0xFFFFFFFF

#define U_FLASH  0
#define U_SPIFFS 100
#define U_AUTH   200

class UpdateClass {
public:
    typedef std::function<void(size_t, size_t)> THandlerFunction_Progress;

    bool begin(size_t size = UPDATE_SIZE_UNKNOWN, int command = U_FLASH, int ledPin = -1, uint8_t ledOn = 0,
               const char *label = NULL)
    {
        error_ = UPDATE_ERROR_OK;
        if (command != U_FLASH) {
            return fail(UPDATE_ERROR_BAD_ARGUMENT, "only U_FLASH updates are supported");
        }
        if (size == 0) {
            return fail(UPDATE_ERROR_SIZE, "size 0");
        }
        size_ = size;
        progress_ = 0;
        if (bl_update_begin(size == UPDATE_SIZE_UNKNOWN ? 0 : (uint32_t)size) != BL_OK) {
            return fail(UPDATE_ERROR_NO_PARTITION, NULL);
        }
        running_ = true;
        if (progress_cb_) {
            progress_cb_(0, size_);
        }
        return true;
    }

    size_t write(uint8_t *data, size_t len)
    {
        if (!running_ || error_) {
            return 0;
        }
        if (size_ != UPDATE_SIZE_UNKNOWN && len > remaining()) {
            fail(UPDATE_ERROR_SPACE, "more data than the size given to begin");
            return 0;
        }
        if (bl_update_write(data, len) != (int)len) {
            fail(UPDATE_ERROR_WRITE, NULL);
            return 0;
        }
        progress_ += len;
        if (progress_cb_) {
            progress_cb_(progress_, size_);
        }
        return len;
    }

    size_t writeStream(Stream &data)
    {
        uint8_t buf[512];
        size_t written = 0;
        while (running_ && !error_ && (size_ == UPDATE_SIZE_UNKNOWN || remaining() > 0)) {
            size_t want = sizeof(buf);
            if (size_ != UPDATE_SIZE_UNKNOWN && remaining() < want) {
                want = remaining();
            }
            size_t got = data.readBytes(buf, want);
            if (got == 0) {
                if (size_ != UPDATE_SIZE_UNKNOWN) {
                    fail(UPDATE_ERROR_STREAM, "stream ended early");
                }
                break;
            }
            if (write(buf, got) != got) {
                break;
            }
            written += got;
        }
        return written;
    }

    bool end(bool evenIfRemaining = false)
    {
        if (!running_ || error_) {
            return false;
        }
        if (!isFinished() && !evenIfRemaining) {
            abort();
            return fail(UPDATE_ERROR_ABORT, "not all of the image was written");
        }
        running_ = false;
        if (bl_update_end(1) != BL_OK) {
            return fail(UPDATE_ERROR_ACTIVATE, NULL);
        }
        return true;
    }

    void abort()
    {
        if (running_) {
            bl_update_end(0);
            running_ = false;
        }
        error_ = UPDATE_ERROR_ABORT;
    }

    UpdateClass &onProgress(THandlerFunction_Progress fn)
    {
        progress_cb_ = fn;
        return *this;
    }

    bool setMD5(const char *expected_md5) { return false; }
    bool canRollBack() { return false; }
    bool rollBack() { return false; }

    void printError(Print &out) { out.printf("Update error %u: %s\n", error_, errorString()); }
    const char *errorString()
    {
        if (!message_[0] && error_) {
            bl_update_error(message_, sizeof(message_));
        }
        return error_ ? (message_[0] ? message_ : "update failed") : "No Error";
    }
    uint8_t getError() { return error_; }
    void clearError() { error_ = UPDATE_ERROR_OK; message_[0] = '\0'; }
    bool hasError() { return error_ != UPDATE_ERROR_OK; }
    bool isRunning() { return running_; }
    bool isFinished() { return size_ != UPDATE_SIZE_UNKNOWN && progress_ == size_; }
    size_t size() { return size_; }
    size_t progress() { return progress_; }
    size_t remaining() { return size_ == UPDATE_SIZE_UNKNOWN ? 0 : size_ - progress_; }

private:
    bool fail(uint8_t error, const char *message)
    {
        error_ = error;
        message_[0] = '\0';
        if (message) {
            strlcpy(message_, message, sizeof(message_));
        } else {
            bl_update_error(message_, sizeof(message_));
        }
        if (running_) {
            bl_update_end(0);
            running_ = false;
        }
        return false;
    }

    size_t size_ = 0, progress_ = 0;
    bool running_ = false;
    uint8_t error_ = UPDATE_ERROR_OK;
    char message_[96] = "";
    THandlerFunction_Progress progress_cb_;
};

inline UpdateClass Update;
