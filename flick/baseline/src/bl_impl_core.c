/* API table: time, console, memory, system. */
#include <stdio.h>
#include <stdlib.h>
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_main.h"
#include "loader_proto.h"
#include "sketch_loader.h"

/* ---- time ---- */

uint32_t bl_impl_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

uint64_t bl_impl_micros(void)
{
    return (uint64_t)esp_timer_get_time();
}

void bl_impl_delay_ms(uint32_t ms)
{
    // Sleep in slices so a long delay() doesn't look like a hung sketch to the watchdog.
    bool feed = sketch_is_current_task();
    while (ms) {
        uint32_t step = ms > 1000 ? 1000 : ms;
        TickType_t ticks = pdMS_TO_TICKS(step);
        vTaskDelay(ticks ? ticks : 1);
        ms -= step;
        if (feed) {
            esp_task_wdt_reset();
        }
    }
}

void bl_impl_delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

void bl_impl_yield(void)
{
    taskYIELD();
}

/* ---- console / logging ---- */

int bl_impl_vprintf(const char *fmt, va_list ap)
{
    return bl_console_vprintf(fmt, ap);
}

int bl_impl_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = bl_console_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int bl_impl_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    return vsnprintf(buf, size, fmt, ap);
}

int bl_impl_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int bl_impl_console_write(const uint8_t *data, size_t len)
{
    bl_console_write((const char *)data, len);
    return (int)len;
}

int bl_impl_console_available(void)
{
    return loader_sketch_available();
}

int bl_impl_console_read(void)
{
    return loader_sketch_read();
}

void bl_impl_log(int level, const char *tag, const char *fmt, ...)
{
    static const char letters[] = "?EWIDV";
    char letter = (level >= BL_LOG_ERROR && level <= BL_LOG_VERBOSE) ? letters[level] : '?';
    va_list ap;
    va_start(ap, fmt);
    bl_console_vlog(letter, tag ? tag : "sketch", fmt, ap);
    va_end(ap);
}

/* ---- memory ---- */

void *bl_impl_malloc(size_t size)
{
    return malloc(size);
}

void *bl_impl_calloc(size_t n, size_t size)
{
    return calloc(n, size);
}

void *bl_impl_realloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

void bl_impl_free(void *ptr)
{
    free(ptr);
}

uint32_t bl_impl_free_heap(void)
{
    return esp_get_free_heap_size();
}

/* ---- system ---- */

void bl_impl_restart(void)
{
    esp_restart();
}

const char *bl_impl_baseline_version(void)
{
    return BASELINE_VERSION;
}
