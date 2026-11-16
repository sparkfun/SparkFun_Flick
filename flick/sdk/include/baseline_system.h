/*
 * baseline_system.h - sleep, time/NTP, timers and tone() for sketches, with
 * the Arduino-ESP32 / ESP-IDF names. Included by baseline_arduino.h.
 *
 * Timer callbacks (Ticker, timerAttachInterrupt, esp_timer) run on the sketch
 * task, like attachInterrupt handlers: inside delay()/yield() or between
 * loop() calls, so they may use Serial and your variables freely. Timing is
 * accurate to about a millisecond.
 *
 * Deep sleep restarts the board on wake: Baseline boots and runs setup()
 * again. Keep data across deep sleep in bl_rtc_memory() (1 KB):
 *     struct Saved { int boots; };
 *     Saved &saved = *(Saved *)bl_rtc_memory(nullptr);
 * (RTC_DATA_ATTR variables are not supported: sketch variables live in normal RAM.)
 */
#ifndef BASELINE_SYSTEM_H
#define BASELINE_SYSTEM_H

#ifdef __cplusplus

#include <time.h>
#include <sys/time.h>

#if defined(BL_PLATFORM_ESP32)     /* ESP-IDF names, up to the timers */
#ifndef ESP_OK
typedef int esp_err_t;
#define ESP_OK                  0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL                (-1)
#endif
#ifndef ESP_ERR_INVALID_ARG
#define ESP_ERR_INVALID_ARG     0x102
#endif
#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE   0x103
#endif

static inline esp_err_t bl_to_esp(int r) { return r == BL_OK ? ESP_OK : r == BL_ERR_ARG ? ESP_ERR_INVALID_ARG : ESP_FAIL; }

/* ---------------- GPIO numbers (ESP-IDF gpio_num_t) ---------------- */

typedef enum {
    GPIO_NUM_NC = -1,
    GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3, GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7,
    GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15,
    GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_20, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23,
    GPIO_NUM_24, GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27, GPIO_NUM_28, GPIO_NUM_29, GPIO_NUM_30, GPIO_NUM_31,
    GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_36, GPIO_NUM_37, GPIO_NUM_38, GPIO_NUM_39,
    GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_43, GPIO_NUM_44, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47,
    GPIO_NUM_48,
} gpio_num_t;

/* ---------------- sleep (esp_sleep.h) ---------------- */

typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED = 0,
    ESP_SLEEP_WAKEUP_ALL = BL_WAKE_ALL,
    ESP_SLEEP_WAKEUP_EXT0 = BL_WAKE_EXT0,
    ESP_SLEEP_WAKEUP_EXT1 = BL_WAKE_EXT1,
    ESP_SLEEP_WAKEUP_TIMER = BL_WAKE_TIMER,
    ESP_SLEEP_WAKEUP_TOUCHPAD = BL_WAKE_TOUCH,
    ESP_SLEEP_WAKEUP_ULP = 6,
    ESP_SLEEP_WAKEUP_GPIO = BL_WAKE_GPIO,
    ESP_SLEEP_WAKEUP_UART = BL_WAKE_UART,
} esp_sleep_wakeup_cause_t;
typedef esp_sleep_wakeup_cause_t esp_sleep_source_t;

typedef enum {
    ESP_EXT1_WAKEUP_ALL_LOW = 0,    /* classic ESP32 */
    ESP_EXT1_WAKEUP_ANY_LOW = 0,    /* ESP32-S3 */
    ESP_EXT1_WAKEUP_ANY_HIGH = 1,
} esp_sleep_ext1_wakeup_mode_t;

typedef enum { GPIO_INTR_LOW_LEVEL = 4, GPIO_INTR_HIGH_LEVEL = 5 } gpio_int_type_t;

#define BUTTON_PIN_BITMASK(GPIO) (1ULL << (GPIO))

static inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us) { return bl_to_esp(bl_sleep_wake(BL_WAKE_TIMER, time_in_us, 0, 0)); }
static inline esp_err_t esp_sleep_enable_ext0_wakeup(gpio_num_t gpio, int level) { return bl_to_esp(bl_sleep_wake(BL_WAKE_EXT0, 0, gpio, level)); }
static inline esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) { return bl_to_esp(bl_sleep_wake(BL_WAKE_EXT1, mask, 0, mode)); }
static inline esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) { return esp_sleep_enable_ext1_wakeup(mask, mode); }
static inline esp_err_t esp_sleep_enable_touchpad_wakeup(void) { return bl_to_esp(bl_sleep_wake(BL_WAKE_TOUCH, 0, 0, 0)); }
static inline esp_err_t esp_sleep_enable_uart_wakeup(int uart_num) { return bl_to_esp(bl_sleep_wake(BL_WAKE_UART, 0, 0, 0)); }
/* Light sleep GPIO wake: gpio_wakeup_enable() picks the pin and level. */
static inline esp_err_t gpio_wakeup_enable(gpio_num_t gpio, gpio_int_type_t intr) { return bl_to_esp(bl_sleep_wake(BL_WAKE_GPIO, 0, gpio, intr == GPIO_INTR_HIGH_LEVEL)); }
static inline esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }
static inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source) { return bl_to_esp(bl_sleep_disable_wake(source)); }
static inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return (esp_sleep_wakeup_cause_t)bl_sleep_wake_cause(); }
static inline uint64_t esp_sleep_get_ext1_wakeup_status(void) { return bl_sleep_ext1_status(); }
static inline void esp_deep_sleep_start(void) { bl_sleep_deep(); for (;;) {} }
static inline void esp_deep_sleep(uint64_t time_in_us) { esp_sleep_enable_timer_wakeup(time_in_us); esp_deep_sleep_start(); }
static inline esp_err_t esp_light_sleep_start(void) { return bl_to_esp(bl_sleep_light()); }

/* ---------------- time and NTP ---------------- */

/* Start NTP (WiFi must be connected) and set the time zone by offsets, or by a
 * POSIX TZ string such as "EST5EDT,M3.2.0,M11.1.0". */
#if defined(BL_HAS_WIFI)
static inline void configTime(long gmtOffset_sec, int daylightOffset_sec, const char *server1,
                              const char *server2 = 0, const char *server3 = 0)
{
    bl_sdk_tz_set_offsets(gmtOffset_sec, daylightOffset_sec);
    bl_time_ntp_start(server1, server2, server3);
}
static inline void configTzTime(const char *tz, const char *server1, const char *server2 = 0, const char *server3 = 0)
{
    bl_sdk_tz_set(tz);
    bl_time_ntp_start(server1, server2, server3);
}
#endif /* BL_HAS_WIFI */
/* Wait up to ms for the clock to be set, then fill info with the local time. */
bool getLocalTime(struct tm *info, uint32_t ms = 5000);
#endif /* BL_PLATFORM_ESP32 */

/* ---------------- timers ---------------- */

/* SDK-internal: a Baseline timer whose firings call fn(arg) on the sketch task. */
int bl_sdk_timer_new(void (*fn)(void *), void *arg);
void bl_sdk_timer_free(int h);

/* Ticker (Arduino-ESP32's Ticker library). Callbacks are plain functions or
 * capture-less lambdas. */
class Ticker {
public:
    typedef void (*callback_function_t)(void);
    Ticker() {}
    ~Ticker() { detach(); }
    void attach(float seconds, callback_function_t cb) { start((uint64_t)(seconds * 1000000.0f), true, plain, (void *)cb); }
    void attach_ms(uint32_t ms, callback_function_t cb) { start((uint64_t)ms * 1000, true, plain, (void *)cb); }
    void attach_us(uint32_t us, callback_function_t cb) { start(us, true, plain, (void *)cb); }
    void once(float seconds, callback_function_t cb) { start((uint64_t)(seconds * 1000000.0f), false, plain, (void *)cb); }
    void once_ms(uint32_t ms, callback_function_t cb) { start((uint64_t)ms * 1000, false, plain, (void *)cb); }
    void once_us(uint32_t us, callback_function_t cb) { start(us, false, plain, (void *)cb); }
    template <typename TArg> void attach(float seconds, void (*cb)(TArg), TArg arg) { startArg((uint64_t)(seconds * 1000000.0f), true, cb, arg); }
    template <typename TArg> void attach_ms(uint32_t ms, void (*cb)(TArg), TArg arg) { startArg((uint64_t)ms * 1000, true, cb, arg); }
    template <typename TArg> void once(float seconds, void (*cb)(TArg), TArg arg) { startArg((uint64_t)(seconds * 1000000.0f), false, cb, arg); }
    template <typename TArg> void once_ms(uint32_t ms, void (*cb)(TArg), TArg arg) { startArg((uint64_t)ms * 1000, false, cb, arg); }
    void detach();
    bool active() { return h_ >= 0 && bl_timer_active(h_); }

private:
    static void plain(void *self);
    template <typename TArg> void startArg(uint64_t us, bool periodic, void (*cb)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "Ticker argument must fit in a pointer");
        arg_ = (void *)(uintptr_t)arg;
        start(us, periodic, withArg, (void *)cb);
    }
    static void withArg(void *self);
    void start(uint64_t us, bool periodic, void (*dispatch)(void *), void *cb);
    int h_ = -1;
    void *cb_ = 0;
    void *arg_ = 0;
};

#if defined(BL_PLATFORM_ESP32)
/* Hardware timers (Arduino-ESP32 3.x API), backed by Baseline timers. */
struct hw_timer_s;
typedef struct hw_timer_s hw_timer_t;
hw_timer_t *timerBegin(uint32_t frequency);
void timerEnd(hw_timer_t *timer);
void timerStart(hw_timer_t *timer);
void timerStop(hw_timer_t *timer);
void timerRestart(hw_timer_t *timer);
void timerWrite(hw_timer_t *timer, uint64_t val);
uint64_t timerRead(hw_timer_t *timer);
uint64_t timerReadMicros(hw_timer_t *timer);
uint64_t timerReadMillis(hw_timer_t *timer);
double timerReadSeconds(hw_timer_t *timer);
uint32_t timerGetFrequency(hw_timer_t *timer);
void timerAttachInterrupt(hw_timer_t *timer, void (*userFunc)(void));
void timerAttachInterruptArg(hw_timer_t *timer, void (*userFunc)(void *), void *arg);
void timerDetachInterrupt(hw_timer_t *timer);
void timerAlarm(hw_timer_t *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count);

/* esp_timer (ESP-IDF high resolution timer API). */
typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);
typedef enum { ESP_TIMER_TASK, ESP_TIMER_ISR } esp_timer_dispatch_t;
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args, esp_timer_handle_t *out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
bool esp_timer_is_active(esp_timer_handle_t timer);
static inline int64_t esp_timer_get_time(void) { return (int64_t)micros(); }

#endif /* BL_PLATFORM_ESP32 */
/* ---------------- tone (LEDC) ---------------- */
void tone(uint8_t pin, unsigned int frequency, unsigned long duration = 0);
void noTone(uint8_t pin);

#endif /* __cplusplus */
#endif /* BASELINE_SYSTEM_H */
