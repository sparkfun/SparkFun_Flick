#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_main.h"
#include "bl_state.h"
#include "loader_proto.h"
#include "sketch_loader.h"

#define BSK_LINE_MAX        1600
#define DATA_MAX        1024    /* decoded bytes per DATA line */
#define LOADER_STACK    6144
#define LOADER_CORE     0
#define SKETCH_RX_SIZE  1024    /* console bytes buffered for the sketch */

static char s_line[BSK_LINE_MAX];
static size_t s_len;
static bool s_overflow;
static StreamBufferHandle_t s_rx;

static struct {
    bool active;
    int slot;
    const esp_partition_t *part;
    uint32_t size;
    uint32_t next;
    uint8_t sha[32];
    mbedtls_sha256_context ctx;
} s_up;

/* Replies go to whichever transport sent the command: the USB console, or a
 * WiFi (OTA) connection. s_lock serializes commands from the two. */
static SemaphoreHandle_t s_lock;
static loader_sink_t s_sink;
static void *s_sink_ctx;

static void console_sink(const char *text, void *ctx)
{
    bl_console_write(text, strlen(text));
}

static void reply(const char *status, const char *fmt, ...)
{
    char buf[200], line[220];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    snprintf(line, sizeof(line), "@BSK %s %s\n", status, buf);
    (s_sink ? s_sink : console_sink)(line, s_sink_ctx);
}

#define OK(...)  reply("OK", __VA_ARGS__)
#define ERR(...) reply("ERR", __VA_ARGS__)

static void upload_reset(void)
{
    if (s_up.active) {
        mbedtls_sha256_free(&s_up.ctx);
    }
    memset(&s_up, 0, sizeof(s_up));
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_sha(const char *hex, uint8_t out[32])
{
    if (!hex || strlen(hex) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static void restart_soon(void)
{
    vTaskDelay(pdMS_TO_TICKS(200));     // let the reply reach the host
    esp_restart();
}

static void cmd_info(void)
{
    const char *safe = bl_state_safe_mode_reason();
    OK("baseline=%s abi=%d.%d chip=%s slot=%c sketch=%s status=\"%s\" safe=%s usb=%u",
       BASELINE_VERSION, BL_ABI_MAJOR, BL_ABI_MINOR, BL_TARGET_NAME, 'a' + bl_state_active_slot(),
       sketch_name()[0] ? sketch_name() : "-", sketch_status(), safe ? "yes" : "no",
       (unsigned)bl_impl_usb_devices());
}

static void cmd_begin(char *args)
{
    upload_reset();
    char *size_s = strtok(args, " ");
    char *sha_s = strtok(NULL, " ");
    uint32_t size = size_s ? strtoul(size_s, NULL, 10) : 0;
    int slot = bl_state_active_slot() ^ 1;      // never overwrite the running sketch
    const esp_partition_t *p = sketch_slot_partition(slot);
    if (!p) {
        ERR("sketch partition missing");
        return;
    }
    if (size < BL_SKETCH_HEADER_SIZE || size > p->size || size > BL_SKETCH_MAX_SIZE) {
        ERR("bad size %u (slot holds %u)", (unsigned)size, (unsigned)p->size);
        return;
    }
    if (!parse_sha(sha_s, s_up.sha)) {
        ERR("bad sha256");
        return;
    }
    uint32_t erase = (size + p->erase_size - 1) / p->erase_size * p->erase_size;
    if (esp_partition_erase_range(p, 0, erase) != ESP_OK) {
        ERR("erase failed");
        return;
    }
    s_up.active = true;
    s_up.slot = slot;
    s_up.part = p;
    s_up.size = size;
    mbedtls_sha256_init(&s_up.ctx);
    mbedtls_sha256_starts(&s_up.ctx, 0);
    OK("slot=%c", 'a' + slot);
}

static void cmd_data(char *args)
{
    static uint8_t buf[DATA_MAX];
    if (!s_up.active) {
        ERR("no upload in progress");
        return;
    }
    char *off_s = strtok(args, " ");
    char *b64 = strtok(NULL, " ");
    if (!off_s || !b64 || strtoul(off_s, NULL, 10) != s_up.next) {
        ERR("expected offset %u", (unsigned)s_up.next);
        return;
    }
    size_t n = 0;
    if (mbedtls_base64_decode(buf, sizeof(buf), &n, (const unsigned char *)b64, strlen(b64)) != 0) {
        ERR("bad base64");
        return;
    }
    if (s_up.next + n > s_up.size) {
        ERR("data past declared size");
        return;
    }
    if (esp_partition_write(s_up.part, s_up.next, buf, n) != ESP_OK) {
        ERR("flash write failed");
        upload_reset();
        return;
    }
    mbedtls_sha256_update(&s_up.ctx, buf, n);
    s_up.next += n;
    OK("%u", (unsigned)s_up.next);
}

static void cmd_end(void)
{
    if (!s_up.active) {
        ERR("no upload in progress");
        return;
    }
    if (s_up.next != s_up.size) {
        ERR("incomplete: got %u of %u bytes", (unsigned)s_up.next, (unsigned)s_up.size);
        return;
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&s_up.ctx, digest);
    if (memcmp(digest, s_up.sha, 32) != 0) {
        ERR("transfer corrupted (sha256 mismatch)");
        upload_reset();
        return;
    }
    bl_sketch_header_t hdr;
    const char *why = sketch_check(s_up.part, &hdr);
    if (why) {
        ERR("rejected: %s", why);
        upload_reset();
        return;
    }
    hdr.name[sizeof(hdr.name) - 1] = '\0';
    int slot = s_up.slot;
    upload_reset();
    bl_state_set_pending(slot);
    bl_usb_forget();    // USB devices belong to the old sketch; the new one asks again
    OK("slot=%c name=%s rebooting", 'a' + slot, hdr.name);
    restart_soon();
}

static void handle_line(char *line)
{
    if (strncmp(line, "@BSK ", 5) != 0) {
        return;     // not for us
    }
    char *cmd = line + 5;
    char *args = strchr(cmd, ' ');
    if (args) {
        *args++ = '\0';
    } else {
        args = cmd + strlen(cmd);
    }

    if (!strcmp(cmd, "INFO")) {
        cmd_info();
    } else if (!strcmp(cmd, "BEGIN")) {
        cmd_begin(args);
    } else if (!strcmp(cmd, "DATA")) {
        cmd_data(args);
    } else if (!strcmp(cmd, "END")) {
        cmd_end();
    } else if (!strcmp(cmd, "ABORT")) {
        upload_reset();
        OK("aborted");
    } else if (!strcmp(cmd, "SAFE")) {
        bl_state_request_safe_mode();
        OK("rebooting into safe mode");
        restart_soon();
    } else if (!strcmp(cmd, "RESET")) {
        OK("rebooting");
        restart_soon();
    } else if (!strcmp(cmd, "USBOFF")) {
        bl_usb_forget();    // back to the plain USB serial port (needed before flashing Baseline)
        OK("rebooting without USB devices");
        restart_soon();
    } else {
        ERR("unknown command '%s'", cmd);
    }
}

void loader_handle_line(const char *line, loader_sink_t sink, void *ctx)
{
    static char copy[BSK_LINE_MAX];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(copy, line, sizeof(copy));
    s_sink = sink;
    s_sink_ctx = ctx;
    handle_line(copy);
    s_sink = NULL;
    s_sink_ctx = NULL;
    xSemaphoreGive(s_lock);
}

/* Console input is shared with the sketch. A line that starts with '@' is
 * buffered and, if it is "@BSK ...", handled here. Every other byte goes to
 * the sketch's input buffer (Serial.read()) as soon as it arrives. */
static void to_sketch(const char *data, size_t len)
{
    if (s_rx && len) {
        xStreamBufferSend(s_rx, data, len, 0);  // drops input if the sketch doesn't read
    }
}

static void loader_task(void *arg)
{
    bool line_start = true;
    bool buffering = false;
    for (;;) {
        int c;
        while ((c = bl_console_read()) >= 0) {
            char ch = (char)c;
            if (line_start) {
                buffering = (ch == '@');
                line_start = false;
            }
            if (!buffering) {
                to_sketch(&ch, 1);
                line_start = (ch == '\n');
                continue;
            }
            if (ch == '\n') {
                size_t raw_len = s_len;
                if (s_len && s_line[s_len - 1] == '\r') {
                    s_len--;
                }
                s_line[s_len] = '\0';
                if (s_overflow) {
                    ERR("line too long");
                } else if (strncmp(s_line, "@BSK ", 5) == 0) {
                    loader_handle_line(s_line, console_sink, NULL);
                } else {
                    to_sketch(s_line, raw_len);
                    to_sketch("\n", 1);
                }
                s_len = 0;
                s_overflow = false;
                line_start = true;
            } else if (s_len < BSK_LINE_MAX - 1) {
                s_line[s_len++] = ch;
            } else {
                s_overflow = true;
            }
        }
        vTaskDelay(1);
    }
}

int loader_sketch_available(void)
{
    return s_rx ? (int)xStreamBufferBytesAvailable(s_rx) : 0;
}

int loader_sketch_read(void)
{
    uint8_t c;
    return (s_rx && xStreamBufferReceive(s_rx, &c, 1, 0) == 1) ? c : -1;
}

void loader_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_rx = xStreamBufferCreate(SKETCH_RX_SIZE, 1);
    xTaskCreatePinnedToCore(loader_task, "bsk_loader", LOADER_STACK, NULL, 2, NULL, LOADER_CORE);
}
