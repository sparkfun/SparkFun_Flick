/*
 * bl_rp2_loader.cpp - the USB console, the @BSK loader protocol, sketch slots in
 * flash, starting a sketch, and crash rollback, for the RP2350 (arduino-pico).
 *
 * The wire protocol is the one ESP32 Baselines speak (baseline/src/loader_proto.c),
 * so `flick upload`, `flick info` and `flick safe` work unchanged.
 *
 * The sketch window: the QMI translates the last 4 MB of the flash window
 * (ATRANS3, 0x10C00000) to the active slot. Baseline's code and LittleFS live
 * in the first 12 MB, which stay mapped one to one. Flash writes go through
 * pico-sdk's flash_range_* (they run from RAM with XIP off, interrupts off).
 *
 * The sketch's setup() and loop() run on the process stack (PSP), allocated
 * from the heap at the size the sketch's header asks for, with PSPLIM at its
 * bottom: a sketch that overflows its stack faults instead of corrupting the
 * heap. Interrupts keep using the main stack (MSP), whose limit (MSPLIM) is
 * the top of the sketch RAM window.
 */
#include <string.h>
#include <EEPROM.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#if !defined(PICO_RP2040)
#include <hardware/structs/qmi.h>       /* the RP2350's address translation */
#endif
#include <hardware/structs/scb.h>
#include <hardware/structs/systick.h>
#include "bl_rp2.h"

#define LINE_MAX        1600
#define DATA_MAX        1024        /* decoded bytes per DATA line */
#define RX_SIZE         2048        /* console bytes buffered for the sketch */
#define TRIAL_MS        10000       /* a new sketch that runs this long is kept */
#define MAX_CRASHES     3           /* crashes in a row before safe mode */
#define WDOG_MS         5000
#define MIN_STACK       6144        /* Baseline calls (printf, LittleFS, ...) run on the sketch's stack too */

/* pico-sdk's hardware_xip_cache: in arduino-pico's libpico.a, its header not on the include path. */
extern "C" void xip_cache_invalidate_range(uintptr_t start_offset, uintptr_t size_bytes);

#define SCB_CFSR        (*(volatile uint32_t *)0xE000ED28)
#define SCB_HFSR        (*(volatile uint32_t *)0xE000ED2C)
#define SCB_BFAR        (*(volatile uint32_t *)0xE000ED38)

/* ---- State that survives a reset ----------------------------------------
 * The 4 KB at BL_RP2_KEEP_BASE (bl_target.h), above the heap: neither the C
 * runtime nor arduino-pico's OTA boot stub touches it. A watchdog or software
 * reset keeps SRAM; a power cycle doesn't, and the magic tells. */
#define RTC_MEM_SIZE    1024
#define NOINIT_MAGIC    0x464C4B31  /* "FLK1" */
struct noinit_state {
    uint32_t magic;
    uint32_t crashes;           /* in a row */
    uint32_t in_sketch;         /* a sketch was running: a reset now is a crash */
    uint32_t safe_request;      /* @BSK SAFE */
    uint32_t clean_restart;     /* Baseline restarted on purpose */
    uint32_t last_reason;       /* bl_reset_reason codes (esp_reset_reason_t values) */
    uint32_t wake_cause;        /* the timed restart that stands in for deep sleep */
    char crash[256];            /* last crash, one line */
};
static uint8_t *const s_rtc_mem = (uint8_t *)BL_RP2_KEEP_BASE;
static noinit_state &s_ni = *(noinit_state *)(BL_RP2_KEEP_BASE + RTC_MEM_SIZE);
static_assert(RTC_MEM_SIZE + sizeof(noinit_state) <= 0x1000, "Baseline's kept state outgrew its 4 KB");
static int s_wake_cause;

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
    watchdog_reboot(0, 0, 0);
    while (1) {
    }
}

/* Deep sleep stand-in: wait, then restart as if woken by the timer. */
void bl_sleep_restart(uint64_t us)
{
    uint64_t end = time_us_64() + us;
    while ((int64_t)(end - time_us_64()) > 0) {
        bl_poll();
    }
    s_ni.wake_cause = 4;    // ESP_SLEEP_WAKEUP_TIMER
    bl_restart_clean();
}

/* ---- State kept in arduino-pico's EEPROM sector (BL_BOARD_EEPROM_START) ---- */
#define PERSIST_MAGIC   0x464C4B53  /* "FLKS" */
#define EEPROM_SIZE     256
struct persist_state {
    uint32_t magic;
    uint8_t active;             /* slot 0 (A) or 1 (B) */
    uint8_t trial;              /* active slot is new: roll back if it crashes */
    uint8_t reserved[2];
};
static persist_state s_ps;

static void ps_save(void)
{
    s_ps.magic = PERSIST_MAGIC;
    EEPROM.put(0, s_ps);
    EEPROM.commit();
}

/* ---- Flash ---------------------------------------------------------------- */
void bl_flash_erase(uint32_t offset, uint32_t len)
{
    uint32_t end = offset + len;
    while (offset < end) {
        uint32_t n = ((offset & 0xFFFF) == 0 && end - offset >= 0x10000) ? 0x10000 : 0x1000;
        noInterrupts();
        rp2040.idleOtherCore();
        flash_range_erase(offset, n);
        rp2040.resumeOtherCore();
        interrupts();
        offset += n;
        watchdog_update();
    }
}

/* offset and len: multiples of FLASH_PAGE_SIZE (256); data in RAM. */
void bl_flash_write(uint32_t offset, const uint8_t *data, uint32_t len)
{
    noInterrupts();
    rp2040.idleOtherCore();
    flash_range_program(offset, data, len);
    rp2040.resumeOtherCore();
    interrupts();
}

/* ---- Slots and the sketch window ------------------------------------------ */
static uint32_t slot_addr(int slot)
{
    return slot ? BL_BOARD_SLOT_B : BL_BOARD_SLOT_A;
}

static const char *s_status = "no sketch";
static char s_name[32];
static bool s_running;
static bool s_safe;
static const char *s_safe_reason;
static uint32_t s_started_ms;
static bl_sketch_callbacks_t s_cb;
static int s_mapped = -1;       /* slot the window shows, -1: none */
static uint32_t s_stack_top, s_stack_bottom;
static void systick_hook(void);
static void trial_check(void);
static void console_release(void);
static bool console_holding(void);

#if defined(BL_TARGET_COPIED_WINDOW)
/* RP2040: no address translation. The window is a flash region of its own, and
 * Baseline copies the active slot into it, the header last, so a window whose
 * header matches the slot's holds all of it (a reset mid-copy leaves a header
 * that doesn't match, and the next boot copies again). Slots are read where
 * they are, one to one. */
static const uint8_t *slot_view(int slot)
{
    return (const uint8_t *)(XIP_BASE + slot_addr(slot));
}

static bool map_window(int slot)
{
    static uint8_t buf[4096];
    const uint32_t win = BL_SKETCH_IBUS_BASE - XIP_BASE;
    const uint8_t *src = slot_view(slot);
    const uint32_t size = ((const bl_sketch_header_t *)src)->image_size;
    if (memcmp(src, (const void *)BL_SKETCH_IBUS_BASE, BL_SKETCH_HEADER_SIZE) == 0) {
        s_mapped = slot;
        return true;
    }
    s_mapped = -1;
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_SKETCH_MAX_SIZE) {
        return false;
    }
    bl_flash_erase(win, (size + 0xFFF) & ~0xFFFu);
    for (uint32_t off = BL_SKETCH_HEADER_SIZE; off < size; off += sizeof(buf)) {
        uint32_t n = size - off < sizeof(buf) ? size - off : sizeof(buf);
        memcpy(buf, src + off, n);      // flash can't be programmed from flash
        memset(buf + n, 0xFF, sizeof(buf) - n);
        bl_flash_write(win + off, buf, (n + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1));
        watchdog_update();
    }
    memcpy(buf, src, BL_SKETCH_HEADER_SIZE);
    bl_flash_write(win, buf, BL_SKETCH_HEADER_SIZE);
    if (memcmp(src, (const void *)BL_SKETCH_IBUS_BASE, size) != 0) {
        return false;
    }
    s_mapped = slot;
    return true;
}
#else
/* Point the sketch window at a slot (ATRANS3: BASE and SIZE in 4 KB units) and
 * drop what the XIP cache holds for the window. Then check the window shows
 * the slot: read the first bytes through the flash's own read command. */
static bool map_window(int slot)
{
    const uint32_t phys = slot_addr(slot);
    qmi_hw->atrans[3] = ((uint32_t)(BL_SKETCH_MAX_SIZE >> 12) << QMI_ATRANS0_SIZE_LSB) | (phys >> 12);
    __asm volatile("dsb\n isb" ::: "memory");
    xip_cache_invalidate_range(BL_SKETCH_IBUS_BASE - XIP_BASE, BL_SKETCH_MAX_SIZE);
    __asm volatile("dsb\n isb" ::: "memory");
    uint8_t direct[16];
    uint8_t cmd[4 + 16] = {0x03, (uint8_t)(phys >> 16), (uint8_t)(phys >> 8), (uint8_t)phys};
    uint8_t rx[sizeof(cmd)];
    noInterrupts();
    rp2040.idleOtherCore();
    flash_do_cmd(cmd, rx, sizeof(cmd));
    rp2040.resumeOtherCore();
    interrupts();
    memcpy(direct, rx + 4, sizeof(direct));
    s_mapped = memcmp((const void *)BL_SKETCH_IBUS_BASE, direct, sizeof(direct)) == 0 ? slot : -1;
    return s_mapped == slot;
}

/* RP2350: a slot is read through the window (the last 4 MB are not mapped one to one). */
static const uint8_t *slot_view(int slot)
{
    return map_window(slot) ? (const uint8_t *)BL_SKETCH_DBUS_BASE : NULL;
}
#endif

/* Checks a slot's image where slot_view() shows it. Its addresses are the
 * window's, wherever the slot is. */
static const char *sketch_check(int slot)
{
    const uint8_t *img = slot_view(slot);
    if (!img) {
        return "flash remap failed";
    }
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
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_BOARD_SLOT_SIZE || size > BL_SKETCH_MAX_SIZE) {
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

/* Calls fn on the process stack: top is its (8-byte aligned) top, limit its
 * bottom (PSPLIM). Interrupts taken meanwhile still use the main stack. Written
 * for Armv6-M too (the RP2040), so: no IT blocks, no 32-bit-only forms. */
extern "C" __attribute__((naked, noinline)) void bl_call_on_psp(void (*fn)(void), uint32_t top, uint32_t limit)
{
    __asm volatile(
        "push {r4, lr}\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "msr psplim, r2\n"
#endif
        "msr psp, r1\n"
        "mrs r4, control\n"
        "movs r2, #2\n"
        "orr r2, r4\n"          // (Armv6-M's divided syntax: the flag-setting 16-bit form)
        "msr control, r2\n"     // SPSEL = 1: thread mode uses PSP from here
        "isb\n"
        "blx r0\n"
        "msr control, r4\n"     // back on the main stack
        "isb\n"
        "pop {r4, pc}\n");
}

/* Calls fn(arg) on another stack, from thread mode: top is its 8-byte aligned
 * top, limit its bottom. The limit register is cleared while SP moves, so
 * neither stack trips it. */
extern "C" __attribute__((naked, noinline)) void bl_call_on_stack(void (*fn)(void *), void *arg, uint32_t top,
                                                                  uint32_t limit)
{
    __asm volatile(
        "push {r4, r5, r6, lr}\n"
        "mov r4, sp\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "mrs r5, psplim\n"
        "movs r6, #0\n"
        "msr psplim, r6\n"
#endif
        "mov sp, r2\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "msr psplim, r3\n"
#endif
        "mov r6, r0\n"
        "mov r0, r1\n"
        "blx r6\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "movs r6, #0\n"
        "msr psplim, r6\n"
#endif
        "mov sp, r4\n"
#if defined(__ARM_ARCH_8M_MAIN__)
        "msr psplim, r5\n"
#endif
        "pop {r4, r5, r6, pc}\n");
}

/* TLS (BearSSL's RSA and elliptic-curve math) needs more stack than a sketch
 * usually has: such calls run on a stack of Baseline's own, made on first use. */
#define BIG_STACK       16384
void bl_on_big_stack(void (*fn)(void *), void *arg)
{
    static uint8_t *stack;
    static bool on_it;
    uint32_t control;
    __asm volatile("mrs %0, control" : "=r"(control));
    if (on_it || !(control & 2)) {     // already on it, or on the main stack (not a sketch call)
        fn(arg);
        return;
    }
    if (!stack) {
        stack = (uint8_t *)malloc(BIG_STACK);
        if (!stack) {
            fn(arg);
            return;
        }
    }
    on_it = true;
    bl_call_on_stack(fn, arg, ((uint32_t)stack + BIG_STACK) & ~7u, (uint32_t)stack);
    on_it = false;
}

static void run_sketch(void (*fn)(void))
{
    bl_call_on_psp(fn, s_stack_top, s_stack_bottom);
}

static void sketch_start(int slot)
{
    const char *why = sketch_check(slot);
    if (why) {
        s_status = why;
        return;
    }
#if defined(BL_TARGET_COPIED_WINDOW)
    if (!map_window(slot)) {
        s_status = "copying the sketch into its window failed";
        return;
    }
#endif
    const bl_sketch_header_t *h = (const bl_sketch_header_t *)BL_SKETCH_DBUS_BASE;
    memcpy(s_name, h->name, sizeof(s_name) - 1);
    uint32_t stack = h->stack_size < MIN_STACK ? MIN_STACK : (h->stack_size + 7) & ~7u;
    uint8_t *mem = (uint8_t *)malloc(stack + 8);
    if (!mem) {
        s_status = "no memory for the sketch's stack";
        return;
    }
    s_stack_top = ((uint32_t)mem + stack + 8) & ~7u;
    s_stack_bottom = s_stack_top - stack;
    memcpy((void *)h->data_start, (const void *)h->data_load, h->data_end - h->data_start);
    memset((void *)h->bss_start, 0, h->bss_end - h->bss_start);
    s_ni.in_sketch = 1;
    memset(&s_cb, 0, sizeof(s_cb));
    ((bl_sketch_entry_t)h->entry)(&bl_api_table, &s_cb);    // sets BL, which constructors use
    for (uint32_t p = h->init_array_start; p < h->init_array_end; p += 4) {
        run_sketch(*(void (**)(void))p);
    }
    s_status = "running";
    s_running = true;
    s_started_ms = millis();
    systick_hook();
    if (s_cb.setup) {
        run_sketch(s_cb.setup);
    }
}

/* ---- Watchdog: resets the chip if Baseline is not polled for 5 s (a loop()
 * that never returns or calls delay()). */
void bl_wdog_long(bool on)
{
    if (on) {
        hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    } else {
        watchdog_enable(WDOG_MS, true);
    }
}

/* ---- while(1); stops the sketch ------------------------------------------
 * The classic Arduino way to stop, `while (1);` or `for (;;);`, compiles to one
 * instruction that branches to itself. Left alone it would spin until the
 * watchdog resets the board and rolls the sketch back. Instead, arduino-pico's
 * SysTick interrupt (it counts cycles; wrapped here, about every 110 ms) looks
 * at where the sketch's thread was interrupted: on a branch to itself, it sends
 * that thread into bl_parked(), which keeps uploads, INFO and the watchdog
 * going and never returns to the sketch. A loop that does anything else is
 * still a hang for the watchdog. */
static void (*volatile s_systick_next)(void);
static volatile uint32_t s_halt_pc;
static char s_status_buf[64];

extern "C" void bl_systick(void);

extern "C" __attribute__((noreturn, used)) void bl_parked_c(void)
{
    snprintf(s_status_buf, sizeof(s_status_buf), "stopped: while(1) at 0x%08lx", (unsigned long)s_halt_pc);
    s_status = s_status_buf;
    bl_impl_printf("\n[flick] the sketch stopped in while(1) (0x%08lx); uploads still work\n", (unsigned long)s_halt_pc);
    for (;;) {
        trial_check();
        bl_poll();
        tight_loop_contents();
    }
}

/* Entered by exception return on the sketch's stack: align it for the AAPCS
 * (doubles, varargs) before any C runs. */
extern "C" __attribute__((naked, noreturn)) void bl_parked(void)
{
    __asm volatile(
        "mov r0, sp\n"
        "movs r1, #7\n"
        "bic r0, r1\n"
        "mov sp, r0\n"
        "ldr r2, 1f\n"
        "bx r2\n"
        ".align 2\n"
        "1: .word bl_parked_c\n");
}

extern "C" __attribute__((used)) void __not_in_flash_func(bl_systick_c)(uint32_t *frame)
{
    if (frame && s_running && !s_halt_pc && (frame[7] & 0x1FF) == 0) {     // the thread was interrupted, not an ISR
        uint32_t pc = frame[6];
        if (pc >= BL_SKETCH_IBUS_BASE && pc < BL_SKETCH_IBUS_BASE + BL_SKETCH_MAX_SIZE) {
            const uint16_t *ins = (const uint16_t *)pc;
            if (ins[0] == 0xE7FE || (ins[0] == 0xF7FF && ins[1] == 0xBFFE)) {     // b.n . / b.w .
                s_halt_pc = pc;
                frame[6] = (uint32_t)bl_parked & ~1u;   // the thread resumes in bl_parked()
            }
        }
    }
    s_systick_next();
}

/* The sketch's thread runs on PSP: only a frame there can be the sketch's. */
extern "C" __attribute__((naked)) void __not_in_flash_func(bl_systick)(void)
{
    __asm volatile(
        "movs r0, #4\n"
        "mov r1, lr\n"
        "tst r0, r1\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1: movs r0, #0\n"
        "2: ldr r2, 3f\n"
        "bx r2\n"
        ".align 2\n"
        "3: .word bl_systick_c\n");
}

static void (**vtable(void))(void)
{
    return (void (**)(void))scb_hw->vtor;
}

/* Install the wrapper, and again if the core replaced the SysTick vector. */
static void systick_hook(void)
{
    void (*cur)(void) = vtable()[15];
    if (cur != bl_systick) {
        s_systick_next = cur;
        __asm volatile("dsb" ::: "memory");
        vtable()[15] = bl_systick;
    }
}

/* ---- Faults: record where, mark a crash, reset at once ------------------ */
extern "C" __attribute__((used)) void bl_fault_c(const uint32_t *frame, uint32_t exc_return)
{
    uint32_t pc = frame[6], lr = frame[5];
    bool in_sketch = pc >= BL_SKETCH_IBUS_BASE && pc < BL_SKETCH_IBUS_BASE + BL_SKETCH_MAX_SIZE;
#if defined(__ARM_ARCH_8M_MAIN__)
    uint32_t cfsr = SCB_CFSR, hfsr = SCB_HFSR, bfar = SCB_BFAR;
#else
    uint32_t cfsr = 0, hfsr = 0, bfar = 0;     // Armv6-M has no fault status registers
#endif
    const char *what = (cfsr & (1u << 20)) ? "stack overflow" : "HardFault";     // UFSR.STKOF
    snprintf(s_ni.crash, sizeof(s_ni.crash), "%s in %s: pc=0x%08lx lr=0x%08lx cfsr=0x%08lx hfsr=0x%08lx "
             "bfar=0x%08lx r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx sp=%08lx",
             what, in_sketch ? "sketch" : "Baseline",
             (unsigned long)pc, (unsigned long)lr, (unsigned long)cfsr, (unsigned long)hfsr,
             (unsigned long)bfar, (unsigned long)frame[0], (unsigned long)frame[1],
             (unsigned long)frame[2], (unsigned long)frame[3], (unsigned long)frame[4], (unsigned long)frame);
    s_ni.in_sketch = 1;         // counted as a crash at the next boot
    s_ni.last_reason = 4;       // ESP_RST_PANIC
    hard_reset();
}

extern "C" __attribute__((naked)) void bl_fault(void)
{
    __asm volatile(
        "movs r0, #4\n"
        "mov r1, lr\n"
        "tst r0, r1\n"
        "beq 1f\n"
        "mrs r0, psp\n"
        "b 2f\n"
        "1: mrs r0, msp\n"
        "2: mov r1, lr\n"
        "ldr r2, 3f\n"
        "bx r2\n"
        ".align 2\n"
        "3: .word bl_fault_c\n");
}

void bl_restart_clean(void)
{
    s_ni.in_sketch = 0;
    s_ni.clean_restart = 1;
    uint32_t t = millis();
    while (console_holding() && Serial && millis() - t < 300) {     // a reply still held for a port just opened
        console_release();
        watchdog_update();
    }
    Serial.flush();
    delay(50);          // the PC reads the last reply before the port goes away
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
static uint8_t s_rx[RX_SIZE];
static volatile uint32_t s_rx_head, s_rx_tail;

static void to_sketch(const char *p, size_t n)
{
    while (n--) {
        uint32_t next = (s_rx_head + 1) % RX_SIZE;
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
    return (int)((s_rx_head + RX_SIZE - s_rx_tail) % RX_SIZE);
}

int bl_sketch_rx_read(void)
{
    bl_poll();
    if (s_rx_head == s_rx_tail) {
        return -1;
    }
    int c = s_rx[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1) % RX_SIZE;
    return c;
}

/* arduino-pico's USB serial drops output while no program on the PC has the
 * port open (DTR low): after a reset, what setup() prints would be gone before
 * the monitor reconnects. Until then it waits here, the first 4 KB of it (the
 * ESP32 consoles keep theirs in the USB driver's buffer, the same idea). */
#define TX_HOLD_SIZE 4096
static uint8_t s_tx_hold[TX_HOLD_SIZE];
static uint32_t s_tx_held;
static bool s_tx_dropped;

/* True once a program on the PC has had the port open for 100 ms: Windows
 * serial programs (pyserial among them) clear their receive buffer just after
 * opening the port, which raises DTR, so output sent at once would be lost. */
static bool console_listening(void)
{
    static uint32_t since;
    if (!Serial) {
        since = 0;
        return false;
    }
    if (!since) {
        since = millis() | 1;
    }
    return millis() - since >= 100;
}

static bool console_holding(void)
{
    return s_tx_held != 0;
}

static void console_release(void)
{
    if (s_tx_held && console_listening()) {
        Serial.write(s_tx_hold, s_tx_held);
        if (s_tx_dropped) {
            Serial.print("\n[flick] console output was dropped while no program had the port open\n");
        }
        s_tx_held = 0;
        s_tx_dropped = false;
    }
}

void bl_console_out(const void *data, size_t len)
{
    if (!console_listening()) {
        uint32_t n = len < TX_HOLD_SIZE - s_tx_held ? (uint32_t)len : TX_HOLD_SIZE - s_tx_held;
        memcpy(s_tx_hold + s_tx_held, data, n);
        s_tx_held += n;
        s_tx_dropped |= n < len;
        return;
    }
    console_release();
    Serial.write((const uint8_t *)data, len);
}

/* ---- Loader protocol ------------------------------------------------------ */
/* Where replies go: the USB console, or the TCP connection a WiFi upload came
 * in on (bl_loader_line()). */
static bl_reply_sink_t s_sink;
static void *s_sink_ctx;

static void reply(const char *status, const char *fmt, ...)
{
    char buf[420];
    int n = snprintf(buf, sizeof(buf), "@BSK %s ", status);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2) {
        n = sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    if (s_sink) {
        s_sink(buf, n, s_sink_ctx);
        return;
    }
    // Through the hold buffer: a command can arrive before the host's DTR does.
    bl_console_out(buf, n);
    Serial.flush();
}

#define OK(...)  reply("OK", __VA_ARGS__)
#define ERR(...) reply("ERR", __VA_ARGS__)

static struct {
    bool active;
    int slot;
    uint32_t size;
    uint32_t next;
    uint32_t written;           /* bytes in flash: whole pages */
    uint32_t page_fill;
    uint8_t page[FLASH_PAGE_SIZE];
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

/* Upload data goes to flash a page (256 bytes) at a time. */
static void up_page_flush(void)
{
    if (!s_up.page_fill) {
        return;
    }
    memset(s_up.page + s_up.page_fill, 0xFF, sizeof(s_up.page) - s_up.page_fill);
    bl_flash_write(slot_addr(s_up.slot) + s_up.written, s_up.page, sizeof(s_up.page));
    s_up.written += sizeof(s_up.page);
    s_up.page_fill = 0;
}

static void up_write(const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t n = sizeof(s_up.page) - s_up.page_fill;
        if (n > len) {
            n = len;
        }
        memcpy(s_up.page + s_up.page_fill, data, n);
        s_up.page_fill += n;
        data += n;
        len -= n;
        if (s_up.page_fill == sizeof(s_up.page)) {
            up_page_flush();
        }
    }
}

static void cmd_info(void)
{
    char crash[256];
    if (bl_last_crash_text(crash, sizeof(crash)) <= 0) {
        strcpy(crash, "-");
    }
    OK("baseline=%s abi=%d.%d chip=%s target=%s slot=%c sketch=%s status=\"%s\" safe=%s usb=0 reset=%d update=none "
       "bpart=- core=%s crash=\"%s\"",
       BASELINE_VERSION, BL_ABI_MAJOR, BL_ABI_MINOR, BL_TARGET_CHIP, BL_TARGET_NAME, 'a' + s_ps.active,
       s_name[0] ? s_name : "-", s_status, s_safe ? "yes" : "no", bl_reset_reason_code(), ARDUINO_PICO_VERSION_STR,
       crash);
}

static void cmd_begin(char *args)
{
    memset(&s_up, 0, sizeof(s_up));
    char *size_s = strtok(args, " ");
    char *sha_s = strtok(NULL, " ");
    uint32_t size = size_s ? strtoul(size_s, NULL, 10) : 0;
    if (size < BL_SKETCH_HEADER_SIZE || size > BL_BOARD_SLOT_SIZE || size > BL_SKETCH_MAX_SIZE) {
        ERR("bad size %u (slot holds %u)", (unsigned)size, (unsigned)BL_BOARD_SLOT_SIZE);
        return;
    }
    if (!parse_sha(sha_s, s_up.sha)) {
        ERR("bad sha256");
        return;
    }
    int slot = s_ps.active ^ 1;     // never overwrite the running sketch
    bl_flash_erase(slot_addr(slot), (size + 0xFFF) & ~0xFFFu);
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
    up_page_flush();
    uint8_t digest[32];
    bl_sha256_final(&s_up.ctx, digest);
    int slot = s_up.slot;
    s_up.active = false;
    if (memcmp(digest, s_up.sha, 32) != 0) {
        ERR("transfer corrupted (sha256 mismatch)");
        return;
    }
    // Reads back what is in flash. On the RP2350 that is through the window: the
    // running sketch's code is on this call's stack, so the window goes back to
    // its slot after.
    int was = s_mapped;
    const char *why = sketch_check(slot);
    char name[32];
    const uint8_t *img = slot_view(slot);
    if (img) {
        memcpy(name, ((const bl_sketch_header_t *)img)->name, sizeof(name));
        name[sizeof(name) - 1] = '\0';
    } else {
        strcpy(name, "?");
    }
#if !defined(BL_TARGET_COPIED_WINDOW)
    if (was >= 0) {
        map_window(was);
    }
#else
    (void)was;
#endif
    if (why) {
        ERR("rejected: %s", why);
        return;
    }
    s_ps.active = (uint8_t)slot;
    s_ps.trial = 1;
    ps_save();
    s_ni.crashes = 0;
    OK("slot=%c name=%s rebooting", 'a' + slot, name);
    bl_restart_clean();
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
        OK("crash cleared");
    } else if (!strcmp(cmd, "BAUD")) {
        ERR("BAUD is for a UART console only");
    } else {
        ERR("unknown command '%s'", cmd);
    }
}

void bl_loader_line(char *line, bl_reply_sink_t sink, void *ctx)
{
    s_sink = sink;
    s_sink_ctx = ctx;
    handle_line(line);
    s_sink = NULL;
    s_sink_ctx = NULL;
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
    watchdog_update();
    if (busy) {
        return;         // a loader command is running (it may call delay())
    }
    busy = true;
    bl_timers_poll();
    bl_net_poll();
    console_release();
    if (s_running) {
        systick_hook();
    }
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (buffering) {
            if (c == '\n' || c == '\r') {
                line[len] = '\0';
                buffering = false;
                line_start = true;
                if (strncmp(line, "@BSK ", 5) == 0) {
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
void bl_rp2_start(void)
{
#if defined(__ARM_ARCH_8M_MAIN__)
    uint32_t msplim = BL_SKETCH_RAM_BASE + BL_SKETCH_RAM_SIZE;
    __asm volatile("msr msplim, %0" :: "r"(msplim));
#endif
    bool fresh = s_ni.magic != NOINIT_MAGIC;
    if (fresh) {
        memset(s_rtc_mem, 0, RTC_MEM_SIZE);     // RTC memory: cleared at power-on, kept through resets
        memset(&s_ni, 0, sizeof(s_ni));
        s_ni.magic = NOINIT_MAGIC;
        s_ni.last_reason = 1;           // ESP_RST_POWERON
    } else if (s_ni.in_sketch) {
        s_ni.crashes++;
        if (s_ni.last_reason != 4) {    // not a fault: the watchdog (or a reset we didn't make)
            s_ni.last_reason = 6;       // ESP_RST_TASK_WDT
            strlcpy(s_ni.crash, "watchdog: the sketch ran 5 s without returning from loop() or calling delay()",
                    sizeof(s_ni.crash));
        }
    } else if (s_ni.clean_restart) {
        s_ni.last_reason = 3;           // ESP_RST_SW
    } else {
        s_ni.last_reason = 1;
    }
    bool crashed = !fresh && s_ni.in_sketch;
    s_wake_cause = (int)s_ni.wake_cause;
    s_ni.wake_cause = 0;
    s_ni.in_sketch = 0;
    s_ni.clean_restart = 0;

#ifdef BL_BOARD_EN_3V3_SW
    // The board's switched 3.3 V rail (Qwiic, microSD, ...). Sketches may turn
    // it off with digitalWrite().
    pinMode(BL_BOARD_EN_3V3_SW, OUTPUT);
    digitalWrite(BL_BOARD_EN_3V3_SW, HIGH);
#endif
    vtable()[3] = bl_fault;             // HardFault (the M33's other faults escalate to it)
    watchdog_enable(WDOG_MS, true);
    analogReadResolution(12);           // the API's 0-4095

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, s_ps);
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
        if (s_ps.trial) {
            s_ps.trial = 0;
            ps_save();
        }
    }
}

void bl_rp2_loop(void)
{
    bl_poll();
    if (!s_running) {
        return;
    }
    if (s_cb.loop) {
        run_sketch(s_cb.loop);
    }
    trial_check();
}
