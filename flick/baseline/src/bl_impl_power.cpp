/* API table: sleep, RTC memory, timers, time and NTP (ABI 1.6). */
#include <Arduino.h>
#include <sys/time.h>
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "soc/soc_caps.h"
#include "gen/bl_impl.h"
#include "bl_console.h"
#include "bl_state.h"
#include "sketch_loader.h"

static int esp_to_bl(esp_err_t e)
{
    return e == ESP_OK ? BL_OK : e == ESP_ERR_INVALID_ARG ? BL_ERR_ARG :
           e == ESP_ERR_NOT_SUPPORTED ? BL_ERR_UNSUPPORTED : BL_ERR_FAIL;
}

/* ---------------- sleep ---------------- */

static_assert(BL_WAKE_ALL == ESP_SLEEP_WAKEUP_ALL && BL_WAKE_EXT0 == ESP_SLEEP_WAKEUP_EXT0 &&
              BL_WAKE_EXT1 == ESP_SLEEP_WAKEUP_EXT1 && BL_WAKE_TIMER == ESP_SLEEP_WAKEUP_TIMER &&
              BL_WAKE_TOUCH == ESP_SLEEP_WAKEUP_TOUCHPAD && BL_WAKE_GPIO == ESP_SLEEP_WAKEUP_GPIO &&
              BL_WAKE_UART == ESP_SLEEP_WAKEUP_UART, "BL_WAKE_* must match esp_sleep_wakeup_cause_t");

extern "C" int bl_impl_sleep_wake(int source, uint64_t value, int pin, int level)
{
    switch (source) {
    case BL_WAKE_TIMER:
        return esp_to_bl(esp_sleep_enable_timer_wakeup(value));
#if SOC_PM_SUPPORT_EXT0_WAKEUP
    case BL_WAKE_EXT0:
        if (!rtc_gpio_is_valid_gpio((gpio_num_t)pin)) {
            return BL_ERR_ARG;      // only RTC-capable pins can wake from deep sleep
        }
        return esp_to_bl(esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level ? 1 : 0));
#endif
#if SOC_PM_SUPPORT_EXT1_WAKEUP
    case BL_WAKE_EXT1:
        return esp_to_bl(esp_sleep_enable_ext1_wakeup(value, (esp_sleep_ext1_wakeup_mode_t)level));
#endif
#if SOC_PM_SUPPORT_TOUCH_SENSOR_WAKEUP
    case BL_WAKE_TOUCH:
        return esp_to_bl(esp_sleep_enable_touchpad_wakeup());
#endif
    case BL_WAKE_GPIO:
        if (!GPIO_IS_VALID_GPIO(pin)) {
            return BL_ERR_ARG;
        }
        gpio_wakeup_enable((gpio_num_t)pin, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
        return esp_to_bl(esp_sleep_enable_gpio_wakeup());
    case BL_WAKE_UART:
        return esp_to_bl(esp_sleep_enable_uart_wakeup(0));
    default:
        return BL_ERR_UNSUPPORTED;
    }
}

extern "C" int bl_impl_sleep_disable_wake(int source)
{
    return esp_to_bl(esp_sleep_disable_wakeup_source((esp_sleep_source_t)source));
}

extern "C" void bl_impl_sleep_deep(void)
{
    // Going to sleep on purpose is a sign of a healthy sketch: end its trial,
    // so waking up (which reboots) is never mistaken for a crash loop.
    bl_state_sketch_stable();
    BLOG_I("deep sleep");
    bl_console_flush();
    delay(20);
    esp_deep_sleep_start();
}

extern "C" int bl_impl_sleep_light(void)
{
    WdtPause wdt;
    bl_console_flush();
    return esp_to_bl(esp_light_sleep_start());
}

extern "C" int bl_impl_sleep_wake_cause(void)
{
    return (int)esp_sleep_get_wakeup_cause();
}

extern "C" uint64_t bl_impl_sleep_ext1_status(void)
{
#if SOC_PM_SUPPORT_EXT1_WAKEUP
    return esp_sleep_get_ext1_wakeup_status();
#else
    return 0;
#endif
}

#define RTC_MEM_SIZE    1024
#define RTC_MEM_MAGIC   0x52544D31u     // "RTM1"
static RTC_NOINIT_ATTR uint8_t s_rtc_mem[RTC_MEM_SIZE];
static RTC_NOINIT_ATTR uint32_t s_rtc_magic;

extern "C" void *bl_impl_rtc_memory(uint32_t *size)
{
    static bool checked;
    if (!checked) {
        checked = true;
        esp_reset_reason_t r = esp_reset_reason();
        if (s_rtc_magic != RTC_MEM_MAGIC || r == ESP_RST_POWERON || r == ESP_RST_BROWNOUT) {
            memset(s_rtc_mem, 0, sizeof(s_rtc_mem));
            s_rtc_magic = RTC_MEM_MAGIC;
        }
    }
    if (size) {
        *size = RTC_MEM_SIZE;
    }
    return s_rtc_mem;
}

/* ---------------- timers ---------------- */

#define MAX_TIMERS      16
#define MAX_PENDING     100     // a sketch that stops pumping doesn't pile up forever

struct TimerSlot {
    bool used;
    esp_timer_handle_t timer;
    uint32_t pending;           // firings not yet taken by the sketch
};
static TimerSlot s_timers[MAX_TIMERS];
static portMUX_TYPE s_timer_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_next_take;

static void timer_fired(void *arg)
{
    int h = (int)(intptr_t)arg;
    portENTER_CRITICAL(&s_timer_lock);
    if (s_timers[h].pending < MAX_PENDING) {
        s_timers[h].pending++;
    }
    portEXIT_CRITICAL(&s_timer_lock);
}

static TimerSlot *timer_of(int h)
{
    return (h >= 0 && h < MAX_TIMERS && s_timers[h].used) ? &s_timers[h] : NULL;
}

extern "C" int bl_impl_timer_new(void)
{
    for (int h = 0; h < MAX_TIMERS; h++) {
        if (!s_timers[h].used) {
            esp_timer_create_args_t args = {};
            args.callback = timer_fired;
            args.arg = (void *)(intptr_t)h;
            args.dispatch_method = ESP_TIMER_TASK;
            args.name = "bl_timer";
            if (esp_timer_create(&args, &s_timers[h].timer) != ESP_OK) {
                return BL_ERR_NO_MEM;
            }
            s_timers[h].used = true;
            s_timers[h].pending = 0;
            return h;
        }
    }
    return BL_ERR_NO_MEM;
}

extern "C" void bl_impl_timer_stop(int h)
{
    TimerSlot *t = timer_of(h);
    if (t) {
        esp_timer_stop(t->timer);
        portENTER_CRITICAL(&s_timer_lock);
        t->pending = 0;
        portEXIT_CRITICAL(&s_timer_lock);
    }
}

extern "C" void bl_impl_timer_free(int h)
{
    TimerSlot *t = timer_of(h);
    if (t) {
        esp_timer_stop(t->timer);
        esp_timer_delete(t->timer);
        *t = TimerSlot();
    }
}

extern "C" int bl_impl_timer_start(int h, uint64_t period_us, int periodic)
{
    TimerSlot *t = timer_of(h);
    if (!t) {
        return BL_ERR_ARG;
    }
    esp_timer_stop(t->timer);
    if (periodic) {
        return esp_to_bl(esp_timer_start_periodic(t->timer, period_us < 50 ? 50 : period_us));
    }
    return esp_to_bl(esp_timer_start_once(t->timer, period_us));
}

extern "C" int bl_impl_timer_active(int h)
{
    TimerSlot *t = timer_of(h);
    return t && esp_timer_is_active(t->timer) ? 1 : 0;
}

extern "C" int bl_impl_timer_take(void)
{
    int found = -1;
    portENTER_CRITICAL(&s_timer_lock);
    for (int i = 0; i < MAX_TIMERS; i++) {
        int h = (s_next_take + i) % MAX_TIMERS;
        if (s_timers[h].used && s_timers[h].pending) {
            s_timers[h].pending--;
            found = h;
            s_next_take = (h + 1) % MAX_TIMERS;     // round-robin between timers
            break;
        }
    }
    portEXIT_CRITICAL(&s_timer_lock);
    return found;
}

/* ---------------- time and NTP ----------------
 * Only the UTC clock and NTP live here. Arduino's configTime()/configTzTime()
 * and newlib's localtime/strftime/mktime/tzset are deliberately not used: on
 * the classic ESP32 (PSRAM cache workaround) newlib places them in IRAM,
 * which has no room left. The SDK does time zones and formatting instead. */

static String s_ntp[3];     // SNTP keeps the server name pointers: own the strings
static bool s_ntp_started;

static const char *keep(int i, const char *s)
{
    s_ntp[i] = s ? s : "";
    return s_ntp[i].length() ? s_ntp[i].c_str() : nullptr;
}

extern "C" void bl_impl_time_ntp_start(const char *server1, const char *server2, const char *server3)
{
    const char *servers[3] = {
        keep(0, server1 && *server1 ? server1 : "pool.ntp.org"), keep(1, server2), keep(2, server3),
    };
    if (s_ntp_started) {
        esp_netif_sntp_deinit();
    }
    esp_netif_init();       // harmless if WiFi already did it
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(servers[0]);
    cfg.num_of_servers = 1;
    for (int i = 1; i < 3 && i < CONFIG_LWIP_SNTP_MAX_SERVERS; i++) {
        if (servers[i]) {
            cfg.servers[cfg.num_of_servers++] = servers[i];
        }
    }
    s_ntp_started = esp_netif_sntp_init(&cfg) == ESP_OK;
}

extern "C" int bl_impl_time_get(int64_t *sec, int32_t *usec)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (sec) *sec = tv.tv_sec;
    if (usec) *usec = (int32_t)tv.tv_usec;
    return BL_OK;
}

extern "C" int bl_impl_time_set(int64_t sec, int32_t usec)
{
    struct timeval tv = { (time_t)sec, (suseconds_t)usec };
    return settimeofday(&tv, NULL) == 0 ? BL_OK : BL_ERR_FAIL;
}

extern "C" int bl_impl_time_synced(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (s_ntp_started && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) ||
           tv.tv_sec > 1609459200;      // after 2021-01-01: the clock has been set somehow
}
