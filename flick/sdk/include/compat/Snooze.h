/*
 * Snooze.h - Teensy low-power sleep with the Snooze library's names (Teensy 4.x).
 *
 *   SnoozeTimer timer;  SnoozeDigital digital;
 *   SnoozeBlock config(timer, digital);
 *   timer.setTimer(10);                              // seconds
 *   digital.pinMode(2, INPUT_PULLUP, FALLING);       // or wake on a pin (0-32)
 *   int who = Snooze.sleep(config);                  // returns: the pin, or TIMER_WAKE
 *   Snooze.deepSleep(config);                        // restarts the sketch on wake
 *
 * Baseline does the sleeping (Teensyduino's Snooze). One difference from
 * Snooze on a plain Teensy: deepSleep() and hibernate() restart the sketch when
 * the board wakes, as an ESP32's deep sleep does, instead of returning, because
 * the USB port goes away while the chip sleeps. Keep state across it in
 * bl_rtc_memory(). sleep() returns as usual.
 */
#pragma once
#include "baseline.h"
#if !defined(BL_PLATFORM_TEENSY)
#error "This board has no Snooze (Snooze.h is for Teensy 4.x; ESP32 boards use esp_deep_sleep_start)."
#endif

#ifdef __cplusplus

#define COMPARE_WAKE 34
#define ALARM_WAKE   35
#define TIMER_WAKE   36

class SnoozeBlock {
public:
    SnoozeBlock() {}
    template <typename... Drivers> SnoozeBlock(Drivers &...drivers) { (add(drivers), ...); }
    virtual ~SnoozeBlock() {}
    /* Set this driver's wake source in Baseline (drivers override it). */
    virtual void apply() {}
    /* What sleep() returned means for this driver: its pin, or -1. */
    virtual int pinFor(uint64_t mask) { (void)mask; return -1; }
    void applyAll()
    {
        bl_sleep_disable_wake(BL_WAKE_ALL);
        for (int i = 0; i < n_; i++) {
            d_[i]->apply();
        }
    }
    int sourceAfterSleep()
    {
        int cause = bl_sleep_wake_cause();
        if (cause == BL_WAKE_TIMER) {
            return TIMER_WAKE;
        }
        uint64_t mask = cause == BL_WAKE_EXT1 ? bl_sleep_ext1_status() : 0;
        for (int i = 0; i < n_; i++) {
            int pin = d_[i]->pinFor(mask);
            if (pin >= 0) {
                return pin;
            }
        }
        return -1;
    }

private:
    void add(SnoozeBlock &d) { if (n_ < 6) d_[n_++] = &d; }
    SnoozeBlock *d_[6] = {};
    int n_ = 0;
};

class SnoozeTimer : public SnoozeBlock {
public:
    void setTimer(uint32_t seconds) { us_ = (uint64_t)seconds * 1000000ULL; }
    void apply() override { if (us_) bl_sleep_wake(BL_WAKE_TIMER, us_, -1, 0); }

private:
    uint64_t us_ = 0;
};

class SnoozeAlarm : public SnoozeBlock {
public:
    void setRtcTimer(uint8_t hours, uint8_t minutes, uint8_t seconds)
    {
        us_ = ((uint64_t)hours * 3600 + (uint64_t)minutes * 60 + seconds) * 1000000ULL;
    }
    void apply() override { if (us_) bl_sleep_wake(BL_WAKE_TIMER, us_, -1, 0); }

private:
    uint64_t us_ = 0;
};

class SnoozeDigital : public SnoozeBlock {
public:
    /* type: RISING or HIGH wakes going high; FALLING or LOW going low. */
    int pinMode(int pin, int mode, int type)
    {
        (void)mode;
        if (pin < 0 || pin > 32 || n_ >= 8) {
            return -1;
        }
        pins_[n_] = (uint8_t)pin;
        high_ = (type == RISING || type == HIGH);
        n_++;
        return pin;
    }
    void apply() override
    {
        if (n_ == 1) {
            bl_sleep_wake(BL_WAKE_EXT0, 0, pins_[0], high_);
        } else if (n_ > 1) {
            uint64_t mask = 0;
            for (int i = 0; i < n_; i++) {
                mask |= 1ULL << pins_[i];
            }
            bl_sleep_wake(BL_WAKE_EXT1, mask, -1, high_);
        }
    }
    int pinFor(uint64_t mask) override
    {
        for (int i = 0; i < n_; i++) {
            if (n_ == 1 || (mask & (1ULL << pins_[i]))) {
                return pins_[i];
            }
        }
        return -1;
    }

private:
    uint8_t pins_[8] = {};
    int n_ = 0;
    bool high_ = false;
};

class SnoozeUSBSerial : public SnoozeBlock {};     /* waking on USB input isn't supported */

class SnoozeClass {
public:
    int sleep(SnoozeBlock &config)
    {
        config.applyAll();
        if (bl_sleep_light() != BL_OK) {
            return -1;
        }
        return config.sourceAfterSleep();
    }
    int deepSleep(SnoozeBlock &config)
    {
        config.applyAll();
        bl_sleep_deep();            /* restarts the sketch on wake */
        return -1;                  /* only if no wake source was set */
    }
    int hibernate(SnoozeBlock &config) { return deepSleep(config); }
};
inline SnoozeClass Snooze;

#endif /* __cplusplus */
