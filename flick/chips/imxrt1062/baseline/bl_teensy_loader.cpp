/*
 * bl_teensy_loader.cpp - the USB console, the @BSK loader protocol, sketch
 * slots in flash, starting a sketch, and crash rollback, for the i.MX RT1062.
 *
 * The wire protocol is the one ESP32 Baselines speak (baseline/src/loader_proto.c),
 * so `flick upload`, `flick info` and `flick safe` work unchanged.
 *
 * Flash: Baseline's linker script (imxrt1062_flick.ld.in) keeps Teensy's core,
 * with the eepromemu_flash_* routines, in ITCM, and they turn interrupts off
 * while they erase or program; .rodata is in DTCM. So nothing reads flash
 * during a flash operation. Functions that reset FlexSPI are FASTRUN too, and
 * noinline: inlined into a caller in flash they would run from flash.
 */
#include <string.h>
#include <EEPROM.h>
#include "bl_teensy.h"

extern "C" {
void eepromemu_flash_write(void *addr, const void *data, uint32_t len);    /* Teensyduino eeprom.c */
void eepromemu_flash_erase_sector(void *addr);
void eepromemu_flash_erase_64K_block(void *addr);
}

#define LINE_MAX        1600
#define DATA_MAX        1024        /* decoded bytes per DATA line */
#define RX_SIZE         2048        /* console bytes buffered for the sketch */
#define TRIAL_MS        10000       /* a new sketch that runs this long is kept */
#define MAX_CRASHES     3           /* crashes in a row before safe mode */
#define FLASH_PAGE      256
#define WDOG_HALF_SECONDS 10        /* watchdog: 5 s */

/* ---- State that survives a reset ----------------------------------------
 * The top 2 KB of OCRAM, above the heap (imxrt1062_flick.ld): the sketch's RTC
 * memory, Baseline's state, then Teensy's CrashReport (0x2027FF80). An MPU
 * region makes it uncached, so nothing is lost in the cache when the watchdog
 * resets the chip. */
#define KEEP_BASE       BL_TEENSY_KEEP_BASE
#define RTC_MEM         ((uint8_t *)KEEP_BASE)
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
static noinit_state &s_ni = *(noinit_state *)(KEEP_BASE + RTC_MEM_SIZE);
static int s_wake_cause;

static void keep_region_uncached(void)
{
    arm_dcache_flush_delete((void *)KEEP_BASE, 2048);
    SCB_MPU_RBAR = KEEP_BASE | SCB_MPU_RBAR_REGION(15) | SCB_MPU_RBAR_VALID;   // 15 wins over Teensy's regions
    SCB_MPU_RASR = SCB_MPU_RASR_TEX(1) | SCB_MPU_RASR_AP(3) | SCB_MPU_RASR_XN |
                   SCB_MPU_RASR_SIZE(10) | SCB_MPU_RASR_ENABLE;                 // normal, not cached, RW, 2 KB
    asm volatile("dsb\n isb" ::: "memory");
}

void *bl_rtc_memory_block(uint32_t *size)
{
    if (size) {
        *size = RTC_MEM_SIZE;
    }
    return RTC_MEM;
}

int bl_wake_cause_code(void)
{
    return s_wake_cause;
}

/* Deep sleep stand-in: the i.MX RT1062 has no deep sleep with a timer wake
 * in Teensyduino yet, so wait, then restart as if woken by the timer. */
void bl_sleep_restart(uint64_t us)
{
    uint64_t end = micros() + us;       // fine for the seconds sleeps are used for
    while ((int64_t)(end - (uint64_t)micros()) > 0) {
        bl_poll();
    }
    s_ni.wake_cause = 4;    // ESP_SLEEP_WAKEUP_TIMER
    bl_restart_clean();
}

static void ni_save(void)
{
    arm_dcache_flush(&s_ni, sizeof(s_ni));
}

/* ---- State kept in Teensy's EEPROM emulation ---------------------------- */
#define PERSIST_MAGIC   0x464C4B53  /* "FLKS" */
#define PERSIST_ADDR    1024        /* top of the 1080-byte EEPROM; sketches get the rest later */
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
    EEPROM.put(PERSIST_ADDR, s_ps);
}

/* ---- Slots and the remap window ----------------------------------------- */
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
static int s_remap_mode;        /* 1: system addresses, 2: FlexSPI offsets, 0: failed */

static const char *sketch_check(int slot)
{
    const uint8_t *img = (const uint8_t *)slot_addr(slot);
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

FASTRUN __attribute__((noinline)) static void flexspi_purge(void)
{
    FLEXSPI_MCR0 |= FLEXSPI_MCR0_SWRESET;       // drop stale data from FlexSPI's AHB buffers
    while (FLEXSPI_MCR0 & FLEXSPI_MCR0_SWRESET) {
    }
    arm_dcache_delete((void *)BL_SKETCH_IBUS_BASE, BL_SKETCH_MAX_SIZE);
    asm volatile("dsb" ::: "memory");
    SCB_CACHE_ICIALLU = 0;
    asm volatile("dsb\n isb" ::: "memory");
}

/* Point the sketch window at a slot: FlexSPI address remap, ADDR_START <=
 * address < ADDR_END reads address + OFFSET (IOMUXC_GPR30/31/32, bits 31:12;
 * NXP AN12255). The reference manual leaves open whether the fields hold system
 * addresses or FlexSPI offsets, so both are tried, and the window is checked
 * against the slot. */
FASTRUN __attribute__((noinline)) static bool map_window(int slot)
{
    const uint32_t W = BL_SKETCH_IBUS_BASE, phys = slot_addr(slot);
    for (int mode = 1; mode <= 2; mode++) {
        uint32_t base = mode == 1 ? 0 : 0x60000000;
        IOMUXC_GPR_GPR30 = 0;
        IOMUXC_GPR_GPR31 = 0;
        IOMUXC_GPR_GPR32 = phys - W;
        IOMUXC_GPR_GPR31 = W + BL_SKETCH_MAX_SIZE - base;
        IOMUXC_GPR_GPR30 = W - base;
        flexspi_purge();
        if (memcmp((const void *)W, (const void *)phys, BL_SKETCH_HEADER_SIZE) == 0 &&
            memcmp((const void *)(W + 0x1000), (const void *)(phys + 0x1000), 64) == 0) {
            s_remap_mode = mode;
            return true;
        }
    }
    IOMUXC_GPR_GPR30 = 0;
    IOMUXC_GPR_GPR31 = 0;
    IOMUXC_GPR_GPR32 = 0;
    s_remap_mode = 0;
    return false;
}

static void sketch_start(int slot)
{
    const char *why = sketch_check(slot);
    if (why) {
        s_status = why;
        return;
    }
    if (!map_window(slot)) {
        s_status = "flash remap failed";
        return;
    }
    const bl_sketch_header_t *h = (const bl_sketch_header_t *)BL_SKETCH_DBUS_BASE;
    memcpy(s_name, h->name, sizeof(s_name) - 1);
    memcpy((void *)h->data_start, (const void *)h->data_load, h->data_end - h->data_start);
    memset((void *)h->bss_start, 0, h->bss_end - h->bss_start);
    s_ni.in_sketch = 1;
    ni_save();
    memset(&s_cb, 0, sizeof(s_cb));
    ((bl_sketch_entry_t)h->entry)(&bl_api_table, &s_cb);    // sets BL, which constructors use
    for (uint32_t p = h->init_array_start; p < h->init_array_end; p += 4) {
        (*(void (**)(void))p)();
    }
    s_status = "running";
    s_running = true;
    s_started_ms = millis();
    if (s_cb.setup) {
        s_cb.setup();
    }
}

/* ---- Watchdog: WDOG1 resets the chip if Baseline is not polled for 5 s
 * (a loop() that never returns or calls delay()). Once on, it stays on. */
static void wdog_start(void)
{
    CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);
    WDOG1_WMCR = 0;     // no power-down counter
    WDOG1_WCR = WDOG_WCR_WT(WDOG_HALF_SECONDS - 1) | WDOG_WCR_WDA | WDOG_WCR_SRS | WDOG_WCR_WDE;
}

static void wdog_feed(void)
{
    WDOG1_WSR = 0x5555;
    WDOG1_WSR = 0xAAAA;
}

/* ---- Faults: record where, mark a crash, reset at once ------------------ */
extern "C" FASTRUN __attribute__((noinline)) void bl_fault_c(const uint32_t *frame, uint32_t ipsr)
{
    static const char *const names[] = {"?", "?", "NMI", "HardFault", "MemManage", "BusFault", "UsageFault"};
    uint32_t pc = frame[6], lr = frame[5];
    bool in_sketch = pc >= BL_SKETCH_IBUS_BASE && pc < BL_SKETCH_IBUS_BASE + BL_SKETCH_MAX_SIZE;
    snprintf(s_ni.crash, sizeof(s_ni.crash), "%s in %s: pc=0x%08lx lr=0x%08lx cfsr=0x%08lx mmfar=0x%08lx "
             "r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx sp=%08lx",
             ipsr < 7 ? names[ipsr] : "fault", in_sketch ? "sketch" : "Baseline",
             (unsigned long)pc, (unsigned long)lr, (unsigned long)SCB_CFSR, (unsigned long)SCB_MMFAR,
             (unsigned long)frame[0], (unsigned long)frame[1], (unsigned long)frame[2], (unsigned long)frame[3],
             (unsigned long)frame[4], (unsigned long)frame);
    s_ni.in_sketch = 1;         // counted as a crash at the next boot
    s_ni.last_reason = 4;       // ESP_RST_PANIC
    ni_save();
    USB1_USBCMD = USB_USBCMD_RST;           // let the PC see the port go away
    USBPHY1_CTRL_SET = USBPHY_CTRL_SFTRST;
    for (volatile uint32_t i = 0; i < 20000000; i++) {
    }
    SCB_AIRCR = 0x05FA0004;
    while (1) {
    }
}

extern "C" FASTRUN __attribute__((naked)) void bl_fault(void)
{
    asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mrs r1, ipsr\n"
        "b bl_fault_c\n");
}

void bl_restart_clean(void)
{
    s_ni.in_sketch = 0;
    s_ni.clean_restart = 1;
    ni_save();
    Serial.flush();
    delay(20);
    __disable_irq();
    USB1_USBCMD = USB_USBCMD_RST;
    USBPHY1_CTRL_SET = USBPHY_CTRL_SFTRST;
    delayMicroseconds(100000);      // the PC notices the port is gone
    SCB_AIRCR = 0x05FA0004;
    while (1) {
    }
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

void bl_console_out(const void *data, size_t len)
{
    Serial.write((const uint8_t *)data, len);
}

/* ---- Loader protocol ------------------------------------------------------ */
static void reply(const char *status, const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("@BSK %s %s\n", status, buf);
    Serial.flush();
}

#define OK(...)  reply("OK", __VA_ARGS__)
#define ERR(...) reply("ERR", __VA_ARGS__)

static struct {
    bool active;
    int slot;
    uint32_t size;
    uint32_t next;
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

static void flash_erase(uint32_t addr, uint32_t len)
{
    uint32_t end = addr + len;
    while (addr < end) {
        if ((addr & 0xFFFF) == 0 && end - addr >= 0x10000) {
            eepromemu_flash_erase_64K_block((void *)addr);
            addr += 0x10000;
        } else {
            eepromemu_flash_erase_sector((void *)addr);
            addr += 0x1000;
        }
        wdog_feed();
    }
}

static void flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    while (len) {
        uint32_t n = FLASH_PAGE - (addr % FLASH_PAGE);     // a page program must not cross a page
        if (n > len) {
            n = len;
        }
        eepromemu_flash_write((void *)addr, data, n);
        addr += n;
        data += n;
        len -= n;
    }
}

static void cmd_info(void)
{
    char crash[256];
    if (bl_last_crash_text(crash, sizeof(crash)) <= 0) {
        strcpy(crash, "-");
    }
    OK("baseline=%s abi=%d.%d chip=%s target=%s slot=%c sketch=%s status=\"%s\" safe=%s usb=0 reset=%d update=none "
       "bpart=- remap=%d crash=\"%s\"",
       BASELINE_VERSION, BL_ABI_MAJOR, BL_ABI_MINOR, BL_TARGET_CHIP, BL_TARGET_NAME, 'a' + s_ps.active,
       s_name[0] ? s_name : "-", s_status, s_safe ? "yes" : "no", bl_reset_reason_code(), s_remap_mode, crash);
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
    flash_erase(slot_addr(slot), (size + 0xFFF) & ~0xFFFu);
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
    flash_write(slot_addr(s_up.slot) + s_up.next, buf, n);
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
    uint8_t digest[32];
    bl_sha256_final(&s_up.ctx, digest);
    int slot = s_up.slot;
    s_up.active = false;
    if (memcmp(digest, s_up.sha, 32) != 0) {
        ERR("transfer corrupted (sha256 mismatch)");
        return;
    }
    const char *why = sketch_check(slot);       // reads back what is in flash
    if (why) {
        ERR("rejected: %s", why);
        return;
    }
    char name[32];
    memcpy(name, ((const bl_sketch_header_t *)slot_addr(slot))->name, sizeof(name));
    name[sizeof(name) - 1] = '\0';
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
        ni_save();
        OK("crash cleared");
    } else if (!strcmp(cmd, "BAUD")) {
        ERR("BAUD is for a UART console only");
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
    int avail;
    while ((avail = Serial.available()) > 0) {
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
void bl_teensy_start(void)
{
    keep_region_uncached();
    bool fresh = s_ni.magic != NOINIT_MAGIC;
    if (fresh) {
        memset(RTC_MEM, 0, RTC_MEM_SIZE);   // RTC memory: cleared at power-on, kept through resets
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
    ni_save();

    for (int i = 3; i <= 6; i++) {
        _VectorsRam[i] = bl_fault;      // HardFault, MemManage, BusFault, UsageFault
    }
    wdog_start();
    analogReadResolution(12);           // the API's 0-4095
    Serial.begin(115200);

    EEPROM.get(PERSIST_ADDR, s_ps);
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
        ni_save();
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

void bl_teensy_loop(void)
{
    bl_poll();
    if (!s_running) {
        return;
    }
    if (s_cb.loop) {
        s_cb.loop();
    }
    if (s_started_ms && millis() - s_started_ms >= TRIAL_MS) {
        s_started_ms = 0;
        s_ni.crashes = 0;
        ni_save();
        if (s_ps.trial) {
            s_ps.trial = 0;
            ps_save();
        }
    }
}
