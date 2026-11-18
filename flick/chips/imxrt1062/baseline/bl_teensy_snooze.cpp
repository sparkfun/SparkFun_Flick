/*
 * bl_teensy_snooze.cpp - the low-power sleep under Baseline's sleep entries
 * (bl_teensy_impl.cpp): Teensyduino's Snooze library, WAIT mode with the
 * clocks slowed and most of them gated.
 *
 * Snooze gates the flash controller's clock while the chip sleeps, and its wake
 * interrupt runs before that clock is back on. So:
 *  - this file's code, with the inline functions of Snooze's headers it
 *    compiles (SnoozeBlock::clear_flags runs in the wake interrupt), goes in
 *    ITCM, as Snooze's own does (imxrt1062_flick.ld.in matches the file name);
 *  - only Snooze's wake interrupts stay enabled for the length of a sleep (it
 *    enables those itself): the others, whose handlers are in flash (most of
 *    Baseline's, and the sketch's), are off until it's over, then run.
 * WDOG1 stops counting while the chip sleeps (WDZST/WDW, bl_teensy_loader.cpp).
 */
#include "bl_teensy.h"
#include <Snooze.h>

static SnoozeTimer s_timer;
static SnoozeDigital s_digital;
static SnoozeBlock s_block(s_timer, s_digital);

#define NVIC_REGS 5             /* 160 interrupts */

extern "C" volatile uint32_t systick_millis_count;

/* The SNVS low-power RTC's 32768 Hz counter (it keeps counting while the chip
 * sleeps): read twice until both reads agree, as the two halves tick apart. */
static uint64_t srtc_ticks(void)
{
    for (;;) {
        uint32_t hi = SNVS_LPSRTCMR, lo = SNVS_LPSRTCLR;
        if (hi == SNVS_LPSRTCMR && lo == SNVS_LPSRTCLR) {
            return ((uint64_t)(hi & 0x7FFF) << 32) | lo;
        }
    }
}

int bl_snooze_sleep(uint32_t timer_s)
{
    if (timer_s) {
        s_timer.setTimer(timer_s);
    } else {
        s_timer.isUsed = false;     /* pins only; Snooze keeps a set timer armed otherwise */
    }
    volatile uint32_t *iser = (volatile uint32_t *)0xE000E100;
    volatile uint32_t *icer = (volatile uint32_t *)0xE000E180;
    uint32_t enabled[NVIC_REGS];
    __disable_irq();
    for (int i = 0; i < NVIC_REGS; i++) {
        enabled[i] = iser[i];
        icer[i] = 0xFFFFFFFF;
    }
    __enable_irq();
    /* millis() stops while the chip sleeps (no SysTick) and Snooze's guess at
     * the time slept is off: set it from the RTC, whatever woke the chip. */
    uint32_t ms0 = systick_millis_count;
    uint64_t t0 = srtc_ticks();
    int who = Snooze.sleep(s_block);
    systick_millis_count = ms0 + (uint32_t)(((srtc_ticks() - t0) * 1000) >> 15);   /* 32768 Hz */
    for (int i = 0; i < NVIC_REGS; i++) {
        iser[i] = enabled[i];
    }
    return who;
}

void bl_snooze_pin(int pin, bool high)
{
    s_digital.pinMode(pin, high ? INPUT_PULLDOWN : INPUT_PULLUP, high ? RISING : FALLING);
}

void bl_snooze_timer_off(void)
{
    s_timer.isUsed = false;
}
