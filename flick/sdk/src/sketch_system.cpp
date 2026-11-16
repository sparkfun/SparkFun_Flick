/* sketch_system.cpp - timers, time and tone() (see baseline_system.h). */
#include "baseline.h"

/* ---- timer dispatch: Baseline reports firings by handle; run them here ---- */

#define MAX_TIMERS 16   /* == Baseline's timer table */

struct TimerHandler {
    void (*fn)(void *);
    void *arg;
};
static TimerHandler s_handlers[MAX_TIMERS];

static void timer_pump()
{
    int h;
    while ((h = bl_timer_take()) >= 0) {
        if (h < MAX_TIMERS && s_handlers[h].fn) {
            s_handlers[h].fn(s_handlers[h].arg);
        }
    }
}

int bl_sdk_timer_new(void (*fn)(void *), void *arg)
{
    int h = bl_timer_new();
    if (h >= 0 && h < MAX_TIMERS) {
        s_handlers[h] = TimerHandler{ fn, arg };
        bl_sketch_add_pump(timer_pump);
    }
    return h;
}

void bl_sdk_timer_free(int h)
{
    if (h >= 0 && h < MAX_TIMERS) {
        bl_timer_free(h);
        s_handlers[h] = TimerHandler{};
    }
}

/* ---- Ticker ---- */

void Ticker::plain(void *self)
{
    Ticker *t = (Ticker *)self;
    ((callback_function_t)t->cb_)();
}

void Ticker::withArg(void *self)
{
    Ticker *t = (Ticker *)self;
    ((void (*)(void *))t->cb_)(t->arg_);
}

void Ticker::start(uint64_t us, bool periodic, void (*dispatch)(void *), void *cb)
{
    detach();
    cb_ = cb;
    h_ = bl_sdk_timer_new(dispatch, this);
    if (h_ >= 0) {
        bl_timer_start(h_, us, periodic);
    }
}

void Ticker::detach()
{
    if (h_ >= 0) {
        bl_sdk_timer_free(h_);
        h_ = -1;
    }
}

#if defined(BL_PLATFORM_ESP32)
/* ---- hardware timer API on Baseline timers ---- */

struct hw_timer_s {
    int h;
    uint32_t freq;
    bool running;
    uint64_t base_us;       // when the counter was (virtually) zero
    uint64_t held_count;    // counter value while stopped
    uint64_t alarm;
    bool alarm_set;
    bool autoreload;
    uint64_t reload_count;
    uint64_t fired;
    void (*cb)(void);
    void (*cb_arg)(void *);
    void *arg;
};

static uint64_t ticks_to_us(hw_timer_t *t, uint64_t ticks) { return t->freq ? ticks * 1000000ULL / t->freq : 0; }

uint64_t timerRead(hw_timer_t *t)
{
    if (!t) {
        return 0;
    }
    if (!t->running) {
        return t->held_count;
    }
    return (micros() - t->base_us) * t->freq / 1000000ULL;
}

static void arm(hw_timer_t *t)
{
    if (!t->alarm_set || !t->running) {
        bl_timer_stop(t->h);
        return;
    }
    uint64_t now = timerRead(t);
    uint64_t first = t->alarm > now ? t->alarm - now : 1;
    // A periodic Baseline timer models autoreload (the count restarts at 0 each alarm).
    if (t->autoreload && now == 0) {
        bl_timer_start(t->h, ticks_to_us(t, t->alarm), 1);
    } else {
        bl_timer_start(t->h, ticks_to_us(t, first), t->autoreload);
    }
}

static void hw_fired(void *p)
{
    hw_timer_t *t = (hw_timer_t *)p;
    t->fired++;
    if (t->autoreload) {
        t->base_us = micros();       // counter restarts from 0
        if (t->reload_count && t->fired >= t->reload_count) {
            bl_timer_stop(t->h);
        }
    }
    if (t->cb) {
        t->cb();
    } else if (t->cb_arg) {
        t->cb_arg(t->arg);
    }
}

hw_timer_t *timerBegin(uint32_t frequency)
{
    if (!frequency) {
        return 0;
    }
    hw_timer_t *t = (hw_timer_t *)calloc(1, sizeof(hw_timer_t));
    if (!t) {
        return 0;
    }
    t->h = bl_sdk_timer_new(hw_fired, t);
    if (t->h < 0) {
        free(t);
        return 0;
    }
    t->freq = frequency;
    t->running = true;
    t->base_us = micros();
    return t;
}

void timerEnd(hw_timer_t *t)
{
    if (t) {
        bl_sdk_timer_free(t->h);
        free(t);
    }
}

void timerStart(hw_timer_t *t)
{
    if (t && !t->running) {
        t->base_us = micros() - ticks_to_us(t, t->held_count);
        t->running = true;
        arm(t);
    }
}

void timerStop(hw_timer_t *t)
{
    if (t && t->running) {
        t->held_count = timerRead(t);
        t->running = false;
        bl_timer_stop(t->h);
    }
}

void timerWrite(hw_timer_t *t, uint64_t val)
{
    if (!t) {
        return;
    }
    if (t->running) {
        t->base_us = micros() - ticks_to_us(t, val);
    } else {
        t->held_count = val;
    }
    arm(t);
}

void timerRestart(hw_timer_t *t) { timerWrite(t, 0); }
uint64_t timerReadMicros(hw_timer_t *t) { return t ? ticks_to_us(t, timerRead(t)) : 0; }
uint64_t timerReadMillis(hw_timer_t *t) { return timerReadMicros(t) / 1000; }
double timerReadSeconds(hw_timer_t *t) { return timerReadMicros(t) / 1000000.0; }
uint32_t timerGetFrequency(hw_timer_t *t) { return t ? t->freq : 0; }

void timerAttachInterrupt(hw_timer_t *t, void (*userFunc)(void))
{
    if (t) {
        t->cb = userFunc;
        t->cb_arg = 0;
    }
}

void timerAttachInterruptArg(hw_timer_t *t, void (*userFunc)(void *), void *arg)
{
    if (t) {
        t->cb = 0;
        t->cb_arg = userFunc;
        t->arg = arg;
    }
}

void timerDetachInterrupt(hw_timer_t *t)
{
    if (t) {
        t->cb = 0;
        t->cb_arg = 0;
    }
}

void timerAlarm(hw_timer_t *t, uint64_t alarm_value, bool autoreload, uint64_t reload_count)
{
    if (!t) {
        return;
    }
    t->alarm = alarm_value;
    t->alarm_set = alarm_value > 0;
    t->autoreload = autoreload;
    t->reload_count = reload_count;
    t->fired = 0;
    arm(t);
}

/* ---- esp_timer ---- */

struct EspTimer {
    int h;
    esp_timer_cb_t cb;
    void *arg;
};
#define TO_HANDLE(p) ((esp_timer_handle_t)(p))
#define FROM_HANDLE(h) ((EspTimer *)(h))

static void esp_fired(void *p)
{
    EspTimer *t = (EspTimer *)p;
    if (t->cb) {
        t->cb(t->arg);
    }
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!args || !out || !args->callback) {
        return ESP_ERR_INVALID_ARG;
    }
    EspTimer *t = (EspTimer *)calloc(1, sizeof(EspTimer));
    if (!t) {
        return ESP_FAIL;
    }
    t->cb = args->callback;
    t->arg = args->arg;
    t->h = bl_sdk_timer_new(esp_fired, t);
    if (t->h < 0) {
        free(t);
        return ESP_FAIL;
    }
    *out = TO_HANDLE(t);
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t h, uint64_t timeout_us)
{
    return h ? bl_to_esp(bl_timer_start(FROM_HANDLE(h)->h, timeout_us, 0)) : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t period)
{
    return h ? bl_to_esp(bl_timer_start(FROM_HANDLE(h)->h, period, 1)) : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_timer_stop(esp_timer_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    bl_timer_stop(FROM_HANDLE(h)->h);
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t h)
{
    if (!h) {
        return ESP_ERR_INVALID_ARG;
    }
    bl_sdk_timer_free(FROM_HANDLE(h)->h);
    free(FROM_HANDLE(h));
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t h)
{
    return h && bl_timer_active(FROM_HANDLE(h)->h);
}

/* ---- time ---- */

bool getLocalTime(struct tm *info, uint32_t ms)
{
    uint32_t start = millis();
    while (!bl_time_synced()) {
        if (millis() - start >= ms) {
            return false;
        }
        delay(10);
    }
    time_t now = time(0);
    localtime_r(&now, info);
    return true;
}

#endif /* BL_PLATFORM_ESP32 */
/* ---- tone ---- */

static uint64_t s_tone_pins;    // pins tone() attached to LEDC
static int s_tone_timer = -1;
static uint8_t s_tone_pin;

static void tone_done(void *)
{
    noTone(s_tone_pin);
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
    if (pin >= 64) {
        return;
    }
    if (!(s_tone_pins & (1ULL << pin))) {
        if (!bl_ledc_attach(pin, frequency ? frequency : 1000, 10)) {
            return;
        }
        s_tone_pins |= 1ULL << pin;
    }
    bl_ledc_write_tone(pin, frequency);
    if (duration) {
        if (s_tone_timer < 0) {
            s_tone_timer = bl_sdk_timer_new(tone_done, 0);
        }
        s_tone_pin = pin;
        if (s_tone_timer >= 0) {
            bl_timer_start(s_tone_timer, (uint64_t)duration * 1000, 0);
        }
    }
}

void noTone(uint8_t pin)
{
    if (pin < 64 && (s_tone_pins & (1ULL << pin))) {
        bl_ledc_write_tone(pin, 0);
        bl_ledc_detach(pin);
        s_tone_pins &= ~(1ULL << pin);
    }
}
