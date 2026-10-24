/*
 * sketch_crt.c - Linked into every sketch. Provides the image header,
 * sketch_entry() (which Baseline calls after it has mapped the image and
 * initialized .data/.bss), delay()/yield(), and the callback pump.
 *
 * flick build passes -DBL_SKETCH_NAME="..." and -DBL_SKETCH_STACK=...
 */
#include "baseline.h"
#include "sketch_abi.h"

#ifndef BL_SKETCH_NAME
#define BL_SKETCH_NAME "sketch"
#endif
#ifndef BL_SKETCH_STACK
#define BL_SKETCH_STACK BL_SKETCH_DEFAULT_STACK
#endif

#define MAX_PUMPS   12
#define PUMP_SLICE  5       /* ms: how often delay() checks for callbacks */

const bl_api_t *BL;
#ifdef BL_SDK_RGB_ONLY_LED
uint8_t bl_sdk_led_level;      /* digitalRead(LED_BUILTIN) on a WS2812-only board (baseline.h) */
#endif

/* Defined in sketch.ld */
extern const uint8_t _sketch_data_load[], _sketch_data_start[], _sketch_data_end[];
extern uint8_t _sketch_bss_start[], _sketch_bss_end[];
extern const uint8_t _sketch_init_array_start[], _sketch_init_array_end[];

void teardown(void) __attribute__((weak));

void sketch_entry(const bl_api_t *api, bl_sketch_callbacks_t *out);

__attribute__((section(".sketch_header"), used))
const bl_sketch_header_t bl_sketch_header = {
    .magic = BL_SKETCH_MAGIC,
    .header_version = BL_SKETCH_HEADER_VERSION,
    .chip_id = BL_TARGET_CHIP_ID,
    .abi_major = BL_ABI_MAJOR,
    .abi_minor = BL_ABI_MINOR,
    .entry = (uint32_t)&sketch_entry,
    .data_load = (uint32_t)_sketch_data_load,
    .data_start = (uint32_t)_sketch_data_start,
    .data_end = (uint32_t)_sketch_data_end,
    .bss_start = (uint32_t)_sketch_bss_start,
    .bss_end = (uint32_t)_sketch_bss_end,
    .init_array_start = (uint32_t)_sketch_init_array_start,
    .init_array_end = (uint32_t)_sketch_init_array_end,
    .stack_size = BL_SKETCH_STACK,
    .name = BL_SKETCH_NAME,
};

/* ---- Callback pump ----
 * Baseline queues events (ESP-NOW packets, delivery reports, ...). Pumps
 * registered by the SDK drain those queues and call the sketch's callbacks,
 * here on the sketch task, so callbacks never race the sketch's own code. */

static void (*s_pumps[MAX_PUMPS])(void);
static int s_pump_count;
static bool s_pumping;

void bl_sketch_add_pump(void (*pump)(void))
{
    for (int i = 0; i < s_pump_count; i++) {
        if (s_pumps[i] == pump) {
            return;
        }
    }
    if (s_pump_count < MAX_PUMPS) {
        s_pumps[s_pump_count++] = pump;
    }
}

static void run_pumps(void)
{
    if (s_pumping) {
        return;     // a callback called delay(): don't re-enter
    }
    s_pumping = true;
    for (int i = 0; i < s_pump_count; i++) {
        s_pumps[i]();
    }
    s_pumping = false;
}

void delay(uint32_t ms)
{
    if (!s_pump_count) {
        BL->delay_ms(ms);
        return;
    }
    run_pumps();
    while (ms) {
        uint32_t step = ms < PUMP_SLICE ? ms : PUMP_SLICE;
        BL->delay_ms(step);
        ms -= step;
        run_pumps();
    }
}

void yield(void)
{
    BL->yield();
    run_pumps();
}

static void loop_and_pump(void)
{
    loop();
    run_pumps();
}

void sketch_entry(const bl_api_t *api, bl_sketch_callbacks_t *out)
{
    BL = api;
    out->setup = setup;
    out->loop = loop_and_pump;
    out->teardown = teardown;   /* NULL when the sketch doesn't define one */
}
