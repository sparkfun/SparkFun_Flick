/*
 * bl_teensy_isr.cpp - interrupt handlers in the sketch (ABI 1.8): pin
 * interrupts, IntervalTimer and DMA channels, for the i.MX RT1062.
 *
 * Sketch functions run in interrupt context here, unlike on the ESP32 family.
 * That is safe because the sketch window is always readable: flash is only
 * unreachable while it is erased or programmed, and Teensy's flash routines
 * turn every interrupt off meanwhile. Teensy's attachInterrupt() and
 * IntervalTimer take plain void(void) functions, so each pin and timer gets a
 * small trampoline that calls the sketch's fn(arg).
 */
#include <DMAChannel.h>
#include "bl_teensy.h"

/* ---- pin interrupts ---- */
#define MAX_PINS 64     /* >= CORE_NUM_DIGITAL (40 on the 4.0, 55 on the 4.1) */

static volatile bl_callback_t s_pin_fn[MAX_PINS];
static void *volatile s_pin_arg[MAX_PINS];

template <int P> static void pin_trampoline()
{
    bl_callback_t fn = s_pin_fn[P];
    if (fn) {
        fn(s_pin_arg[P]);
    }
}

#define T8(n) pin_trampoline<n>, pin_trampoline<n + 1>, pin_trampoline<n + 2>, pin_trampoline<n + 3>, \
              pin_trampoline<n + 4>, pin_trampoline<n + 5>, pin_trampoline<n + 6>, pin_trampoline<n + 7>
static void (*const s_pin_trampolines[MAX_PINS])(void) = {
    T8(0), T8(8), T8(16), T8(24), T8(32), T8(40), T8(48), T8(56),
};

extern "C" int bl_impl_isr_attach_pin(int pin, int mode, bl_callback_t fn, void *arg)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL || !fn) {
        return BL_ERR_ARG;
    }
    int tmode;
    switch (mode) {
    case BL_RISING:  tmode = RISING; break;
    case BL_FALLING: tmode = FALLING; break;
    case BL_CHANGE:  tmode = CHANGE; break;
    case BL_ONLOW:   tmode = LOW; break;
    case BL_ONHIGH:  tmode = HIGH; break;
    default:         return BL_ERR_ARG;
    }
    __disable_irq();
    s_pin_fn[pin] = fn;
    s_pin_arg[pin] = arg;
    __enable_irq();
    attachInterrupt(pin, s_pin_trampolines[pin], tmode);
    return BL_OK;
}

extern "C" int bl_impl_isr_detach_pin(int pin)
{
    if (pin < 0 || pin >= CORE_NUM_DIGITAL) {
        return BL_ERR_ARG;
    }
    detachInterrupt(pin);
    s_pin_fn[pin] = NULL;
    return BL_OK;
}

/* ---- IntervalTimer: the PIT has four channels, shared with Baseline (tone()) ---- */
#define MAX_TIMERS 4

static IntervalTimer s_timers[MAX_TIMERS];
static volatile bl_callback_t s_timer_fn[MAX_TIMERS];
static void *volatile s_timer_arg[MAX_TIMERS];
static bool s_timer_used[MAX_TIMERS];

template <int T> static void timer_trampoline()
{
    bl_callback_t fn = s_timer_fn[T];
    if (fn) {
        fn(s_timer_arg[T]);
    }
}

static void (*const s_timer_trampolines[MAX_TIMERS])(void) = {
    timer_trampoline<0>, timer_trampoline<1>, timer_trampoline<2>, timer_trampoline<3>,
};

static bool timer_ok(int h)
{
    return h >= 0 && h < MAX_TIMERS && s_timer_used[h];
}

extern "C" int bl_impl_isr_timer_begin(bl_callback_t fn, void *arg, float period_us)
{
    if (!fn || !(period_us > 0)) {
        return BL_ERR_ARG;
    }
    for (int h = 0; h < MAX_TIMERS; h++) {
        if (s_timer_used[h]) {
            continue;
        }
        s_timer_fn[h] = fn;
        s_timer_arg[h] = arg;
        if (!s_timers[h].begin(s_timer_trampolines[h], period_us)) {
            s_timer_fn[h] = NULL;
            return BL_ERR_NO_MEM;     // every PIT channel is busy
        }
        s_timer_used[h] = true;
        return h;
    }
    return BL_ERR_NO_MEM;
}

extern "C" int bl_impl_isr_timer_update(int h, float period_us)
{
    if (!timer_ok(h) || !(period_us > 0)) {
        return BL_ERR_ARG;
    }
    s_timers[h].update(period_us);
    return BL_OK;
}

extern "C" void bl_impl_isr_timer_end(int h)
{
    if (!timer_ok(h)) {
        return;
    }
    s_timers[h].end();
    s_timer_fn[h] = NULL;
    s_timer_used[h] = false;
}

extern "C" int bl_impl_isr_timer_priority(int h, int priority)
{
    if (!timer_ok(h) || priority < 0 || priority > 255) {
        return BL_ERR_ARG;
    }
    s_timers[h].priority((uint8_t)priority);
    return BL_OK;
}

/* ---- DMA channels, from the same pool as Baseline's (DMAChannel) ---- */
static DMAChannel *s_dma[DMA_NUM_CHANNELS];

extern "C" int bl_impl_dma_channel_alloc(void)
{
    DMAChannel *ch = new DMAChannel();      // reserves a channel and turns the DMA engine on
    if (ch->channel >= DMA_NUM_CHANNELS) {
        delete ch;
        return BL_ERR_NO_MEM;
    }
    s_dma[ch->channel] = ch;
    return ch->channel;
}

extern "C" void bl_impl_dma_channel_free(int channel)
{
    if (channel < 0 || channel >= DMA_NUM_CHANNELS || !s_dma[channel]) {
        return;
    }
    delete s_dma[channel];      // releases the channel
    s_dma[channel] = NULL;
}
