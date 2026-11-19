/*
 * bl_apollo3_loader.cpp - the console, the @BSK loader protocol, sketch slots
 * in flash, starting a sketch, and crash rollback, for the Apollo3 (SparkFun's
 * Apollo3 core, Mbed OS).
 *
 * The wire protocol is the one ESP32 Baselines speak (baseline/src/loader_proto.c),
 * so `flick upload`, `flick info` and `flick safe` work unchanged.
 *
 * The console is UART0, through the board's CH340, at 115200. Baseline takes
 * over its receive interrupt: the core's own buffer holds 256 bytes, less than
 * one upload line.
 *
 * The sketch window is a fixed flash region (bl_target.h): Baseline copies the
 * active slot into it, header last, as on the RP2040. Flash is written through
 * the boot ROM's helpers with interrupts off; code in flash can't run meanwhile.
 *
 * setup() and loop() run on an Mbed thread ("sketch") whose stack has the size
 * the sketch's header asks for.
 */
#include <stddef.h>
#include <string.h>
#include <mbed.h>
#include "am_mcu_apollo.h"
#include "bl_apollo3.h"

/* Mbed wraps the printf family (-Wl,--wrap) with a minimal printf that ignores
 * zero padding (%08lx) and has no %f: Baseline's own text is formatted by newlib's. */
extern "C" int __real_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
extern "C" int __real_snprintf(char *buf, size_t size, const char *fmt, ...);
#define vsnprintf __real_vsnprintf
#define snprintf __real_snprintf
#if __has_include("bl_build_id.h")
#include "bl_build_id.h"                /* written by flick: a hash of the build's inputs */
#else
#define BL_BUILD_ID "-"
#endif
#ifndef BL_CORE_VERSION
#define BL_CORE_VERSION "-"
#endif

#define LINE_MAX        1600
#define DATA_MAX        1024        /* decoded bytes per DATA line */
#define RX_SIZE         4096        /* console bytes received, not yet read */
#define SKETCH_RX_SIZE  2048        /* console bytes buffered for the sketch */
#define TRIAL_MS        10000       /* a new sketch that runs this long is kept */
#define MAX_CRASHES     3           /* crashes in a row before safe mode */
#define WDOG_TICKS      80          /* 5 s at 16 Hz */
#define MIN_STACK       6144        /* Baseline calls (printf, ...) run on the sketch's stack too */
#define PAGE            AM_HAL_FLASH_PAGE_SIZE     /* 8 KB */

/* INFO build=. The "BLBUILD=" prefix lets the download page find the same ID in an image. */
static const char s_build[] = "BLBUILD=" BL_BUILD_ID;

/* ---- State that survives a reset ----------------------------------------
 * SRAM does not: every reset (software, watchdog, the reset pin) comes back
 * with it all zero (measured on the RedBoard Artemis ATP; the chip's secure
 * bootloader or the reset itself clears it). What must survive lives in
 * MCUCTRL SCRATCH1, which no reset clears (SCRATCH0 bit 0 belongs to the
 * secure bootloader's debugger handshake), packed into 32 bits; the last
 * crash's text goes to a flash page of its own when it happens. s_ni is the
 * working copy; kept_save() writes it back. */
#define KEPT_MAGIC      0xB5u
struct kept_state {
    uint32_t crashes;           /* in a row (0-15) */
    uint32_t in_sketch;         /* a sketch was running: a reset now is a crash */
    uint32_t safe_request;      /* @BSK SAFE */
    uint32_t clean_restart;     /* Baseline restarted on purpose */
    uint32_t last_reason;       /* bl_reset_reason codes (esp_reset_reason_t values, < 32) */
    uint32_t wake_cause;        /* the timed restart that stands in for deep sleep (< 16) */
    char crash[256];            /* last crash, one line (RAM; flash copy at BL_APOLLO3_CRASH_PAGE) */
};
static kept_state s_ni;

/* Bits 0-7: a tag of this Baseline build. A new Baseline (flashed through SVL,
 * which ends with a software reset) starts fresh: the old one's "a sketch was
 * running" isn't a crash. */
static uint32_t build_tag(void)
{
    uint32_t h = 2166136261u;
    for (const char *p = BL_BUILD_ID; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h & 0xFF;
}

static bool kept_load(void)
{
    uint32_t v = MCUCTRL->SCRATCH1;
    if ((v >> 24) != KEPT_MAGIC || (v & 0xFF) != build_tag()) {
        return false;
    }
    s_ni.crashes = (v >> 20) & 0xF;
    s_ni.in_sketch = (v >> 19) & 1;
    s_ni.safe_request = (v >> 18) & 1;
    s_ni.clean_restart = (v >> 17) & 1;
    s_ni.last_reason = (v >> 12) & 0x1F;
    s_ni.wake_cause = (v >> 8) & 0xF;
    return true;
}

static void kept_save(void)
{
    uint32_t c = s_ni.crashes > 15 ? 15 : s_ni.crashes;
    MCUCTRL->SCRATCH1 = KEPT_MAGIC << 24 | c << 20 | (s_ni.in_sketch & 1) << 19 | (s_ni.safe_request & 1) << 18 |
                        (s_ni.clean_restart & 1) << 17 | (s_ni.last_reason & 0x1F) << 12 | (s_ni.wake_cause & 0xF) << 8 |
                        build_tag();
}

/* The crash record page: a magic word, then the text. Written only when a crash happens. */
#define CRASH_MAGIC     0x464C4B43  /* "FLKC" */
static void crash_save(void);
static void crash_load(void)
{
    const uint32_t *p = (const uint32_t *)BL_APOLLO3_CRASH_PAGE;
    if (p[0] == CRASH_MAGIC) {
        strlcpy(s_ni.crash, (const char *)(p + 1), sizeof(s_ni.crash));
    }
}

/* RTC memory: plain RAM here, so it holds only until the next reset. */
#define RTC_MEM_SIZE    1024
static uint8_t s_rtc_mem[RTC_MEM_SIZE];
static int s_wake_cause;
static uint32_t s_reset_stat;   /* RSTGEN STAT at boot */

void *bl_rtc_memory_block(uint32_t *size)
{
    if (size) {
        *size = RTC_MEM_SIZE;
    }
    return s_rtc_mem;
}

int bl_wake_cause_code(void)
{
    return s_wake_cause;
}

static void hard_reset(void) __attribute__((noreturn));
static void hard_reset(void)
{
    kept_save();
    am_hal_reset_control(AM_HAL_RESET_CONTROL_SWPOI, 0);
    while (1) {
    }
}

/* ---- Watchdog: resets the chip if Baseline is not polled for 5 s (a loop()
 * that never returns or calls delay()). Clocked by the LFRC at 16 Hz. */
static void wdog_start(void)
{
    am_hal_wdt_config_t cfg = {};
    cfg.ui32Config = AM_HAL_WDT_LFRC_CLK_16HZ | AM_HAL_WDT_ENABLE_RESET;
    cfg.ui16InterruptCount = WDOG_TICKS;
    cfg.ui16ResetCount = WDOG_TICKS;
    am_hal_wdt_init(&cfg);
    am_hal_wdt_start();
}

static inline void wdog_feed(void)
{
    am_hal_wdt_restart();
}

void bl_wdog_long(bool on)
{
    if (on) {
        am_hal_wdt_halt();
    } else {
        wdog_start();
    }
}

/* ---- Deep sleep ------------------------------------------------------------
 * A chip reset would wipe SRAM, RTC memory with it, so deep sleep is a wait
 * (the chip idles between Mbed ticks; uploads and INFO keep working) and then
 * a restart of the sketch alone: the main thread stops the sketch's thread,
 * takes back what the sketch held (heap, files, buses, timers, interrupts) and
 * starts it again, with the wake cause set, as an ESP32 wakes. */
static volatile int s_restart_cause;    /* set by the sketch's thread: restart with this wake cause */

void bl_sleep_restart(uint64_t us, int pin, int level)
{
    if (!us && pin < 0) {
        bl_impl_printf("[flick] deep sleep with no wake source: the sketch stays stopped until a reset or upload\n");
    }
    bl_ble_reset();             // the radio goes quiet, as an ESP32's does in deep sleep
    uint64_t start = bl_impl_micros();
    int cause = 0;
    while (!cause) {
        bl_poll();
        if (us && bl_impl_micros() - start >= us) {
            cause = 4;          // ESP_SLEEP_WAKEUP_TIMER
        } else if (pin >= 0 && digitalRead(pin) == (level ? HIGH : LOW)) {
            cause = 2;          // ESP_SLEEP_WAKEUP_EXT0
        } else {
            rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    s_restart_cause = cause;
    for (;;) {
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(1000));      // the main thread ends this thread
    }
}

/* ---- MPU: flash is read-only, so a write through a null (or any flash)
 * pointer faults, as on the other chips, instead of being ignored. Everything
 * else keeps the default memory map (PRIVDEFENA). Off while Baseline itself
 * writes flash. */
static void mpu_start(void)
{
    MPU->RNR = 0;
    MPU->RBAR = 0x00000000;
    MPU->RASR = (6u << MPU_RASR_AP_Pos)             /* read-only, privileged and not */
              | (1u << MPU_RASR_C_Pos)              /* normal memory, cached */
              | (19u << MPU_RASR_SIZE_Pos)          /* 2^(19+1) = 1 MB */
              | MPU_RASR_ENABLE_Msk;
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
}

/* ---- Flash: the boot ROM's helpers, interrupts and the MPU off (code in
 * flash can't be fetched while they run). Sources must be in SRAM. ---------- */
void bl_flash_erase(uint32_t addr, uint32_t len)
{
    for (uint32_t a = addr & ~(PAGE - 1); a < addr + len; a += PAGE) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        uint32_t mpu = MPU->CTRL;
        MPU->CTRL = 0;
        am_hal_flash_page_erase(AM_HAL_FLASH_PROGRAM_KEY, AM_HAL_FLASH_ADDR2INST(a), AM_HAL_FLASH_ADDR2PAGE(a));
        MPU->CTRL = mpu;
        __DSB();
        __ISB();
        __set_PRIMASK(primask);
        wdog_feed();
    }
}

/* addr and len: multiples of 4. */
void bl_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t mpu = MPU->CTRL;
    MPU->CTRL = 0;
    am_hal_flash_program_main(AM_HAL_FLASH_PROGRAM_KEY, (uint32_t *)data, (uint32_t *)addr, len / 4);
    MPU->CTRL = mpu;
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
}

/* ---- State kept in flash (BL_APOLLO3_STATE_PAGE) ------------------------- */
#define PERSIST_MAGIC   0x464C4B53  /* "FLKS" */
struct persist_state {
    uint32_t magic;
    uint8_t active;             /* slot 0 (A) or 1 (B) */
    uint8_t trial;              /* active slot is new: roll back if it crashes */
    uint8_t reserved[2];
};
static persist_state s_ps;

static void ps_save(void)
{
    static persist_state buf;   // in SRAM, word aligned
    s_ps.magic = PERSIST_MAGIC;
    if (memcmp(&s_ps, (const void *)BL_APOLLO3_STATE_PAGE, sizeof(s_ps)) == 0) {
        return;
    }
    buf = s_ps;
    bl_flash_erase(BL_APOLLO3_STATE_PAGE, PAGE);
    bl_flash_write(BL_APOLLO3_STATE_PAGE, (const uint8_t *)&buf, sizeof(buf));
}

/* The crash record: written when a crash happens (rare), erased by CRASHCLEAR.
 * Called from fault handlers too: flash access needs no interrupts. */
static void crash_save(void)
{
    static uint32_t buf[1 + 256 / 4];
    const uint32_t *p = (const uint32_t *)BL_APOLLO3_CRASH_PAGE;
    if (!s_ni.crash[0]) {
        if (p[0] != 0xFFFFFFFFu) {
            bl_flash_erase(BL_APOLLO3_CRASH_PAGE, PAGE);
        }
        return;
    }
    memset(buf, 0, sizeof(buf));
    buf[0] = CRASH_MAGIC;
    strlcpy((char *)(buf + 1), s_ni.crash, 256);
    bl_flash_erase(BL_APOLLO3_CRASH_PAGE, PAGE);
    bl_flash_write(BL_APOLLO3_CRASH_PAGE, (const uint8_t *)buf, sizeof(buf));
}

/* ---- Slots and the sketch window ------------------------------------------ */
static uint32_t slot_addr(int slot)
{
    return slot ? BL_APOLLO3_SLOT_B : BL_APOLLO3_SLOT_A;
}

static const uint8_t *slot_view(int slot)
{
    return (const uint8_t *)slot_addr(slot);
}

static const char *s_status = "no sketch";
static char s_name[32];
static bool s_running;
static bool s_safe;
static const char *s_safe_reason;
static uint32_t s_started_ms;
static bl_sketch_callbacks_t s_cb;
static void trial_check(void);

/* Copies a slot into the window, the header last, so a window whose header
 * matches the slot's holds all of it (a reset mid-copy leaves a header that
 * doesn't match, and the next boot copies again). */
static bool map_window(int slot)
{
    static uint32_t buf[1024];      // 4 KB: flash can't be programmed from flash
    const uint32_t win = BL_SKETCH_IBUS_BASE;
    const uint8_t *src = slot_view(slot);
    const uint32_t size = ((const bl_sketch_header_t *)src)->image_size;
    if (memcmp(src, (const void *)win, BL_SKETCH_HEADER_SIZE) == 0) {
        return true;
    }
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_SKETCH_MAX_SIZE) {
        return false;
    }
    bl_flash_erase(win, size);
    for (uint32_t off = BL_SKETCH_HEADER_SIZE; off < size; off += sizeof(buf)) {
        uint32_t n = size - off < sizeof(buf) ? size - off : sizeof(buf);
        memcpy(buf, src + off, n);
        memset((uint8_t *)buf + n, 0xFF, sizeof(buf) - n);
        bl_flash_write(win + off, (const uint8_t *)buf, (n + 3) & ~3u);
        wdog_feed();
    }
    memcpy(buf, src, BL_SKETCH_HEADER_SIZE);
    bl_flash_write(win, (const uint8_t *)buf, BL_SKETCH_HEADER_SIZE);
    return memcmp(src, (const void *)win, size) == 0;
}

/* Checks a slot's image where it is. Its addresses are the window's. */
static const char *sketch_check(int slot)
{
    const uint8_t *img = slot_view(slot);
    const bl_sketch_header_t *h = (const bl_sketch_header_t *)img;
    if (h->magic != BL_SKETCH_MAGIC) {
        return "no sketch";
    }
    if (h->chip_id != BL_TARGET_CHIP_ID) {
        return "built for another chip";
    }
    if (h->abi_major != BL_ABI_MAJOR) {
        return "built for another ABI major version";
    }
    if (h->abi_minor > BL_ABI_MINOR) {
        return "needs a newer Baseline";
    }
    uint32_t size = h->image_size;
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_APOLLO3_SLOT_SIZE || size > BL_SKETCH_MAX_SIZE) {
        return "bad image size";
    }
    const uint32_t W = BL_SKETCH_IBUS_BASE;
    if (h->entry < W + BL_SKETCH_HEADER_SIZE || h->entry >= W + size ||
        h->data_start < BL_SKETCH_RAM_BASE || h->bss_end > BL_SKETCH_RAM_BASE + BL_SKETCH_RAM_SIZE ||
        h->data_start > h->data_end || h->data_end > h->bss_start || h->bss_start > h->bss_end ||
        h->data_load < W || h->data_load + (h->data_end - h->data_start) > W + size ||
        h->init_array_start > h->init_array_end || h->init_array_end > W + size) {
        return "addresses outside the sketch windows";
    }
    bl_sketch_header_t hdr;
    memcpy(&hdr, h, sizeof(hdr));
    memset(hdr.sha256, 0, sizeof(hdr.sha256));
    bl_sha256_t c;
    uint8_t digest[32];
    bl_sha256_init(&c);
    bl_sha256_update(&c, &hdr, sizeof(hdr));
    bl_sha256_update(&c, img + sizeof(hdr), size - sizeof(hdr));
    bl_sha256_final(&c, digest);
    if (memcmp(digest, h->sha256, 32) != 0) {
        return "sha256 mismatch";
    }
    return NULL;
}

/* The sketch's thread: constructors, setup(), then loop() forever. */
static rtos::Thread *s_thread;
static uint8_t *s_stack_mem;
static uint32_t s_stack_size;
static volatile uint32_t s_halt_pc;      /* where a while(1) stopped the sketch */

static void sketch_main(void)
{
    const bl_sketch_header_t *h = (const bl_sketch_header_t *)BL_SKETCH_DBUS_BASE;
    ((bl_sketch_entry_t)h->entry)(&bl_api_table, &s_cb);    // sets BL, which constructors use
    for (uint32_t p = h->init_array_start; p < h->init_array_end; p += 4) {
        (*(void (**)(void))p)();
    }
    s_status = "running";
    s_started_ms = millis();
    if (s_cb.setup) {
        s_cb.setup();
    }
    for (;;) {
        if (s_cb.loop) {
            s_cb.loop();
        } else {
            rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
        }
        bl_poll();
        trial_check();
    }
}

static void sketch_start(int slot)
{
    const char *why = sketch_check(slot);
    if (why) {
        s_status = why;
        return;
    }
    if (!map_window(slot)) {
        s_status = "copying the sketch into its window failed";
        return;
    }
    const bl_sketch_header_t *h = (const bl_sketch_header_t *)BL_SKETCH_DBUS_BASE;
    memcpy(s_name, h->name, sizeof(s_name) - 1);
    uint32_t stack = h->stack_size < MIN_STACK ? MIN_STACK : (h->stack_size + 7) & ~7u;
    uint8_t *mem = (uint8_t *)malloc(stack);
    if (!mem) {
        s_status = "no memory for the sketch's stack";
        return;
    }
    memcpy((void *)h->data_start, (const void *)h->data_load, h->data_end - h->data_start);
    memset((void *)h->bss_start, 0, h->bss_end - h->bss_start);
    memset(&s_cb, 0, sizeof(s_cb));
    s_ni.in_sketch = 1;
    kept_save();
    s_running = true;
    s_status = "starting";
    s_stack_mem = mem;
    s_stack_size = stack;
    s_halt_pc = 0;
    s_thread = new rtos::Thread(osPriorityNormal, stack, mem, "sketch");
    if (s_thread->start(mbed::callback(sketch_main)) != osOK) {
        s_running = false;
        s_ni.in_sketch = 0;
        kept_save();
        s_status = "could not start the sketch's thread";
    }
}

/* ---- while(1); stops the sketch ------------------------------------------
 * The classic Arduino way to stop, `while (1);` or `for (;;);`, compiles to one
 * instruction that branches to itself. Left alone it would spin until the
 * watchdog resets the board and rolls the sketch back. Instead, a Ticker
 * (interrupt context, every 100 ms) looks at the thread it interrupted: every
 * thread runs on PSP, so the frame there is the interrupted thread's, and only
 * the sketch's thread runs code in the sketch window. On a branch to itself,
 * the thread resumes in bl_parked(), which keeps uploads, INFO and the
 * watchdog going and never returns to the sketch. A loop that does anything
 * else is still a hang for the watchdog. */
static mbed::Ticker s_stop_ticker;
static char s_status_buf[64];

extern "C" __attribute__((noreturn, used)) void bl_parked_c(void)
{
    snprintf(s_status_buf, sizeof(s_status_buf), "stopped: while(1) at 0x%08lx", (unsigned long)s_halt_pc);
    s_status = s_status_buf;
    bl_impl_printf("\n[flick] the sketch stopped in while(1) (0x%08lx); uploads still work\n", (unsigned long)s_halt_pc);
    for (;;) {
        trial_check();
        bl_poll();
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
    }
}

/* Entered by exception return on the sketch's stack: align it for the AAPCS
 * (doubles, varargs) before any C runs. */
extern "C" __attribute__((naked, noreturn)) void bl_parked(void)
{
    __asm volatile(
        "mov r0, sp\n"
        "bic r0, r0, #7\n"
        "mov sp, r0\n"
        "ldr r2, =bl_parked_c\n"
        "bx r2\n");
}

static void stop_check(void)
{
    if (!s_running || s_halt_pc || !s_stack_mem) {
        return;
    }
    uint32_t *frame = (uint32_t *)__get_PSP();
    uint32_t lo = (uint32_t)s_stack_mem, hi = lo + s_stack_size;
    if ((uint32_t)frame < lo || (uint32_t)frame + 32 > hi) {
        return;                 // another thread's frame
    }
    uint32_t pc = frame[6];
    if (pc < BL_SKETCH_IBUS_BASE + BL_SKETCH_HEADER_SIZE || pc >= BL_SKETCH_IBUS_BASE + BL_SKETCH_MAX_SIZE - 4) {
        return;
    }
    const uint16_t *ins = (const uint16_t *)pc;
    if (ins[0] == 0xE7FE || (ins[0] == 0xF7FF && ins[1] == 0xBFFE)) {     // b.n . / b.w .
        s_halt_pc = pc;
        frame[6] = (uint32_t)bl_parked & ~1u;   // the thread resumes in bl_parked()
        frame[7] &= ~0x0600FC00u;               // no IT block state
    }
}

/* ---- Faults: record where, mark a crash, reset at once ------------------ */
extern "C" __attribute__((used)) void bl_fault_c(const uint32_t *frame, uint32_t exc_return)
{
    uint32_t pc = frame[6], lr = frame[5];
    bool in_sketch = pc >= BL_SKETCH_IBUS_BASE && pc < BL_SKETCH_IBUS_BASE + BL_SKETCH_MAX_SIZE;
    uint32_t cfsr = SCB->CFSR, hfsr = SCB->HFSR, bfar = SCB->BFAR;
    snprintf(s_ni.crash, sizeof(s_ni.crash), "fault in %s: pc=0x%08lx lr=0x%08lx cfsr=0x%08lx hfsr=0x%08lx "
             "bfar=0x%08lx r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx sp=%08lx",
             in_sketch ? "sketch" : "Baseline",
             (unsigned long)pc, (unsigned long)lr, (unsigned long)cfsr, (unsigned long)hfsr,
             (unsigned long)bfar, (unsigned long)frame[0], (unsigned long)frame[1],
             (unsigned long)frame[2], (unsigned long)frame[3], (unsigned long)frame[4], (unsigned long)frame);
    s_ni.in_sketch = 1;         // counted as a crash at the next boot
    s_ni.last_reason = 4;       // ESP_RST_PANIC
    crash_save();
    hard_reset();
}

extern "C" __attribute__((naked)) void bl_fault(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "ldr r2, =bl_fault_c\n"
        "bx r2\n");
}

/* Mbed's own fatal errors (an RTX stack overflow, a failed assert, ...) end
 * here instead of halting: recorded as a crash like a fault. */
extern "C" void mbed_error_hook(const mbed_error_ctx *ctx)
{
    snprintf(s_ni.crash, sizeof(s_ni.crash), "Mbed error 0x%08lx at 0x%08lx (thread %s)",
             (unsigned long)ctx->error_status, (unsigned long)ctx->error_address,
             ctx->thread_id && s_thread && ctx->thread_id == (uint32_t)s_thread->get_id() ? "sketch" : "Baseline");
    s_ni.in_sketch = 1;
    s_ni.last_reason = 4;
    crash_save();
    hard_reset();
}

void bl_restart_clean(void)
{
    s_ni.in_sketch = 0;
    s_ni.clean_restart = 1;
    wait_us(20000);         // the last reply leaves the UART (blocking writes) and the PC reads it
    hard_reset();
}

int bl_last_crash_text(char *buf, size_t size)
{
    if (!s_ni.crash[0] || size == 0) {
        return 0;
    }
    strlcpy(buf, s_ni.crash, size);
    return (int)strlen(buf);
}

int bl_reset_reason_code(void)
{
    return (int)s_ni.last_reason;
}

/* ---- Console -------------------------------------------------------------- */
static uint8_t s_in[RX_SIZE];                   /* filled by the UART interrupt */
static volatile uint32_t s_in_head, s_in_tail;

/* UART0's receive interrupt is Baseline's: the FIFO on, an interrupt at 4
 * bytes or when the line goes quiet (receive timeout), drained straight from
 * the data register. Mbed's driver dispatches only the FIFO-level interrupt,
 * which is why the core runs with the FIFO off: one interrupt through Mbed's
 * serial layer per byte, too slow above 115200. Sending stays Mbed's. */
extern "C" void bl_uart0_isr(void)
{
    UART0->IEC = UART0->MIS;
    while (!UART0->FR_b.RXFE) {
        uint8_t c = (uint8_t)UART0->DR;
        uint32_t next = (s_in_head + 1) % RX_SIZE;
        if (next != s_in_tail) {
            s_in[s_in_head] = c;
            s_in_head = next;
        }
    }
}

/* After Serial.begin() and after every baud change (Mbed rewrites the UART's setup). */
static void console_rx_setup(void)
{
    NVIC_DisableIRQ(UART0_IRQn);
    UART0->LCRH_b.FEN = 1;
    UART0->IFLS_b.RXIFLSEL = 0;         // 1/8 of the 32-byte FIFO
    UART0->IEC = 0xFFFFFFFFu;
    UART0->IER = UART0_IER_RXIM_Msk | UART0_IER_RTIM_Msk;
    NVIC_SetVector(UART0_IRQn, (uint32_t)bl_uart0_isr);
    NVIC_EnableIRQ(UART0_IRQn);
}

static int in_read(void)
{
    if (s_in_head == s_in_tail) {
        return -1;
    }
    int c = s_in[s_in_tail];
    s_in_tail = (s_in_tail + 1) % RX_SIZE;
    return c;
}

static uint8_t s_rx[SKETCH_RX_SIZE];
static uint32_t s_rx_head, s_rx_tail;

static void to_sketch(const char *p, size_t n)
{
    while (n--) {
        uint32_t next = (s_rx_head + 1) % SKETCH_RX_SIZE;
        if (next == s_rx_tail) {
            return;             // the sketch isn't reading: drop
        }
        s_rx[s_rx_head] = (uint8_t)*p++;
        s_rx_head = next;
    }
}

int bl_sketch_rx_available(void)
{
    bl_poll();
    return (int)((s_rx_head + SKETCH_RX_SIZE - s_rx_tail) % SKETCH_RX_SIZE);
}

int bl_sketch_rx_read(void)
{
    bl_poll();
    if (s_rx_head == s_rx_tail) {
        return -1;
    }
    int c = s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % SKETCH_RX_SIZE;
    return c;
}

void bl_console_out(const void *data, size_t len)
{
    Serial.write((const uint8_t *)data, len);
}

/* ---- Loader protocol ------------------------------------------------------ */
static void reply(const char *status, const char *fmt, ...)
{
    char buf[512];                      /* INFO with a crash line, as on the ESP Baselines */
    int n = snprintf(buf, sizeof(buf), "@BSK %s ", status);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    bl_console_out(buf, n);
}

#define OK(...)  reply("OK", __VA_ARGS__)
#define ERR(...) reply("ERR", __VA_ARGS__)

static struct {
    bool active;
    int slot;
    uint32_t size;
    uint32_t next;
    uint32_t written;           /* bytes in flash: whole words */
    uint32_t fill;
    uint32_t buf[64];           /* 256 bytes, word aligned */
    uint8_t sha[32];
    bl_sha256_t ctx;
} s_up;

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

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Returns the decoded length, or -1. */
static int b64_decode(const char *in, uint8_t *out, size_t size)
{
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *in && *in != '='; in++) {
        int v = b64val(*in);
        if (v < 0) {
            return -1;
        }
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= size) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return (int)n;
}

static void up_flush(void)
{
    if (!s_up.fill) {
        return;
    }
    uint8_t *b = (uint8_t *)s_up.buf;
    memset(b + s_up.fill, 0xFF, sizeof(s_up.buf) - s_up.fill);
    uint32_t n = (s_up.fill + 3) & ~3u;
    bl_flash_write(slot_addr(s_up.slot) + s_up.written, b, n);
    s_up.written += n;
    s_up.fill = 0;
}

static void up_write(const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t n = sizeof(s_up.buf) - s_up.fill;
        if (n > len) {
            n = len;
        }
        memcpy((uint8_t *)s_up.buf + s_up.fill, data, n);
        s_up.fill += n;
        data += n;
        len -= n;
        if (s_up.fill == sizeof(s_up.buf)) {
            up_flush();
        }
    }
}

static void cmd_info(void)
{
    char crash[256];
    if (bl_last_crash_text(crash, sizeof(crash)) <= 0) {
        strcpy(crash, "-");
    }
    OK("baseline=%s build=%s abi=%d.%d chip=%s target=%s slot=%c sketch=%s status=\"%s\" safe=%s usb=0 reset=%d "
       "update=none bpart=- core=%s crash=\"%s\"",
       BASELINE_VERSION, s_build + 8, BL_ABI_MAJOR, BL_ABI_MINOR, BL_TARGET_CHIP, BL_TARGET_NAME, 'a' + s_ps.active,
       s_name[0] ? s_name : "-", s_status, s_safe ? "yes" : "no", bl_reset_reason_code(), BL_CORE_VERSION, crash);
}

static const char *slot_accept(int slot, char *name);

static void cmd_begin(char *args)
{
    memset(&s_up, 0, sizeof(s_up));
    char *size_s = strtok(args, " ");
    char *sha_s = strtok(NULL, " ");
    uint32_t size = size_s ? strtoul(size_s, NULL, 10) : 0;
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_APOLLO3_SLOT_SIZE || size > BL_SKETCH_MAX_SIZE) {
        ERR("bad size %u (slot holds %u)", (unsigned)size, (unsigned)BL_APOLLO3_SLOT_SIZE);
        return;
    }
    if (!parse_sha(sha_s, s_up.sha)) {
        ERR("bad sha256");
        return;
    }
    int slot = s_ps.active ^ 1;     // never overwrite the running sketch
    bl_flash_erase(slot_addr(slot), size);
    s_up.active = true;
    s_up.slot = slot;
    s_up.size = size;
    bl_sha256_init(&s_up.ctx);
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
    int n = b64_decode(b64, buf, sizeof(buf));
    if (n < 0) {
        ERR("bad base64");
        return;
    }
    if (s_up.next + n > s_up.size) {
        ERR("data past declared size");
        return;
    }
    up_write(buf, n);
    bl_sha256_update(&s_up.ctx, buf, n);
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
    up_flush();
    uint8_t digest[32];
    bl_sha256_final(&s_up.ctx, digest);
    int slot = s_up.slot;
    s_up.active = false;
    if (memcmp(digest, s_up.sha, 32) != 0) {
        ERR("transfer corrupted (sha256 mismatch)");
        return;
    }
    char name[32];
    const char *why = slot_accept(slot, name);
    if (why) {
        ERR("rejected: %s", why);
        return;
    }
    OK("slot=%c name=%s rebooting", 'a' + slot, name);
    bl_restart_clean();
}

/* Checks a slot just written and makes it the active one (on trial).
 * NULL when accepted, else why not. name: the sketch's (32 bytes). */
static const char *slot_accept(int slot, char *name)
{
    const char *why = sketch_check(slot);
    memcpy(name, ((const bl_sketch_header_t *)slot_view(slot))->name, 32);
    name[31] = '\0';
    if (why) {
        return why;
    }
    s_ps.active = (uint8_t)slot;
    s_ps.trial = 1;
    ps_save();
    s_ni.crashes = 0;
    return NULL;
}

/* BAUD <rate>: the console switches to <rate> right after this reply, to make
 * an upload faster. It goes back to 115200 when no loader line has arrived for
 * BAUD_IDLE_MS, and at every restart, as on the ESP32 boards. One line in
 * flight (window=1): a line arriving while flash is written (interrupts off)
 * would lose bytes. */
#define CONSOLE_BAUD    115200
#define BAUD_IDLE_MS    3000
static uint32_t s_baud = CONSOLE_BAUD;
static uint32_t s_last_line_ms;

static void console_set_baud(uint32_t baud)
{
    uint32_t t = millis();
    while ((!UART0->FR_b.TXFE || UART0->FR_b.BUSY) && millis() - t < 50) {
        // the reply leaves the transmit FIFO and the shift register at the old rate
    }
    static_cast<mbed::UnbufferedSerial &>(Serial).baud((int)baud);
    console_rx_setup();
    s_baud = baud;
}

static void cmd_baud(char *args)
{
    unsigned long baud = strtoul(args, NULL, 10);
    if (baud != 115200 && baud != 230400 && baud != 460800 && baud != 921600) {
        ERR("unsupported baud rate %lu", baud);
        return;
    }
    OK("baud=%lu window=1", baud);
    console_set_baud(baud);
}

static void handle_line(char *line)
{
    if (strncmp(line, "@BSK ", 5) != 0) {
        return;
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
        s_up.active = false;
        OK("aborted");
    } else if (!strcmp(cmd, "SAFE")) {
        s_ni.safe_request = 1;
        OK("rebooting into safe mode");
        bl_restart_clean();
    } else if (!strcmp(cmd, "RESET")) {
        OK("rebooting");
        bl_restart_clean();
    } else if (!strcmp(cmd, "CRASHCLEAR")) {
        s_ni.crash[0] = '\0';
        crash_save();
        OK("crash cleared");
    } else if (!strcmp(cmd, "BAUD")) {
        cmd_baud(args);
    } else {
        ERR("unknown command '%s'", cmd);
    }
}

/* Console input is shared with the sketch. A line that starts with '@' is
 * buffered and, if it is "@BSK ...", handled here; every other byte goes to
 * the sketch's input buffer as soon as it arrives. */
void bl_poll(void)
{
    static char line[LINE_MAX];
    static size_t len;
    static bool line_start = true, buffering;
    static bool busy;
    wdog_feed();
    if (busy) {
        return;         // a loader command is running (it may call delay())
    }
    busy = true;
    bl_timers_poll();
    bl_ble_poll();
    if (s_baud != CONSOLE_BAUD && millis() - s_last_line_ms > BAUD_IDLE_MS) {
        console_set_baud(CONSOLE_BAUD);     // the upload ended or was abandoned
    }
    int ch;
    while ((ch = in_read()) >= 0) {
        char c = (char)ch;
        if (buffering) {
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                buffering = false;
                line_start = true;
                if (strncmp(line, "@BSK ", 5) == 0) {
                    s_last_line_ms = millis();
                    handle_line(line);
                } else {
                    to_sketch(line, len);
                    to_sketch(&c, 1);
                }
                len = 0;
            } else if (len < LINE_MAX - 1) {
                line[len++] = c;
            }
            continue;
        }
        if (line_start && c == '@') {
            buffering = true;
            len = 0;
            line[len++] = c;
            continue;
        }
        to_sketch(&c, 1);
        line_start = (c == '\n' || c == '\r');
    }
    busy = false;
}

/* ---- Boot ----------------------------------------------------------------- */
void bl_apollo3_start(void)
{
    // Why the chip reset: the HAL keeps RSTGEN STAT from startup, else read it now.
    s_reset_stat = gAmHalResetStatus ? gAmHalResetStatus : RSTGEN->STAT;
    am_hal_reset_control(AM_HAL_RESET_CONTROL_STATUSCLEAR, 0);
    bool power_on = s_reset_stat & (AM_HAL_RESET_STATUS_POR | AM_HAL_RESET_STATUS_BOD);
    bool external = s_reset_stat & AM_HAL_RESET_STATUS_EXTERNAL;   // the reset button, or SVL's DTR pulse
    bool wdt = s_reset_stat & AM_HAL_RESET_STATUS_WDT;

    crash_load();
    bool fresh = !kept_load() || power_on;
    if (fresh) {
        memset(&s_ni, 0, offsetof(kept_state, crash));
        s_ni.last_reason = 1;           // ESP_RST_POWERON
    } else if (s_ni.in_sketch && !external) {
        s_ni.crashes++;
        if (s_ni.last_reason != 4) {    // not a fault: the watchdog (or a reset we didn't make)
            s_ni.last_reason = 6;       // ESP_RST_TASK_WDT
            if (wdt) {
                strlcpy(s_ni.crash, "watchdog: the sketch ran 5 s without returning from loop() or calling delay()",
                        sizeof(s_ni.crash));
            } else {
                snprintf(s_ni.crash, sizeof(s_ni.crash), "reset while the sketch ran (RSTGEN STAT 0x%02lx)",
                         (unsigned long)s_reset_stat);
            }
            crash_save();
        }
    } else if (external) {
        s_ni.last_reason = 2;           // ESP_RST_EXT
    } else if (s_ni.clean_restart) {
        s_ni.last_reason = 3;           // ESP_RST_SW
    } else {
        s_ni.last_reason = 1;
    }
    bool crashed = !fresh && s_ni.in_sketch && !external;
    s_wake_cause = (int)s_ni.wake_cause;
    s_ni.wake_cause = 0;
    s_ni.in_sketch = 0;
    s_ni.clean_restart = 0;
    kept_save();

    Serial.begin(115200);
    console_rx_setup();

    // Faults go to bl_fault (Mbed copied the vector table to RAM: VTOR).
    void (**vt)(void) = (void (**)(void))SCB->VTOR;
    for (int i = 3; i <= 6; i++) {
        vt[i] = bl_fault;               // HardFault, MemManage, BusFault, UsageFault
    }
    SCB->SHCSR |= SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
    mpu_start();
    wdog_start();
    s_stop_ticker.attach(stop_check, std::chrono::milliseconds(100));     // while(1) parking

    memcpy(&s_ps, (const void *)BL_APOLLO3_STATE_PAGE, sizeof(s_ps));
    if (s_ps.magic != PERSIST_MAGIC || s_ps.active > 1) {
        memset(&s_ps, 0, sizeof(s_ps));
        ps_save();
    }
    if (crashed && s_ps.trial) {
        s_ps.active ^= 1;               // the new sketch crashed on trial: back to the old one
        s_ps.trial = 0;
        ps_save();
    }
    if (s_ni.safe_request) {
        s_safe_reason = "requested";
        s_ni.safe_request = 0;
    } else if (s_ni.crashes >= MAX_CRASHES) {
        s_safe_reason = "crashed repeatedly";
    }
    if (s_safe_reason) {
        s_safe = true;
        s_status = "safe mode";
        return;
    }
    sketch_start(s_ps.active);
}

/* A sketch that has run TRIAL_MS without crashing is kept. */
static void trial_check(void)
{
    if (s_started_ms && millis() - s_started_ms >= TRIAL_MS) {
        s_started_ms = 0;
        s_ni.crashes = 0;
        kept_save();
        if (s_ps.trial) {
            s_ps.trial = 0;
            ps_save();
        }
    }
}

/* Arduino's loop(), on the main thread: the loader's when no sketch runs.
 * With a sketch, everything happens on the sketch's thread. */
/* The sketch's thread asked for a restart (deep sleep ended): see bl_sleep_restart(). */
static void sketch_restart(void)
{
    int cause = s_restart_cause;
    s_restart_cause = 0;
    s_thread->terminate();      // it is waiting in sleep_for(), holding no lock
    delete s_thread;
    s_thread = NULL;
    free(s_stack_mem);
    s_stack_mem = NULL;
    s_running = false;
    bl_gpio_reset();
    bl_hw_reset();
    bl_storage_reset();
    bl_ble_reset();
    bl_sketch_heap_release();
    s_wake_cause = cause;
    s_ni.last_reason = 8;       // ESP_RST_DEEPSLEEP
    sketch_start(s_ps.active);
}

void bl_apollo3_loop(void)
{
    if (s_running) {
        if (s_restart_cause) {
            sketch_restart();
        }
        rtos::ThisThread::sleep_for(std::chrono::milliseconds(20));
        return;
    }
    bl_poll();
    rtos::ThisThread::sleep_for(std::chrono::milliseconds(2));
}
