#include <string.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "bl_console.h"
#include "bl_state.h"

#define RTC_MAGIC   0xB15E5AFEu
#define NVS_NS      "baseline"

/* Survives software resets and crashes, not power loss. */
typedef struct {
    uint32_t magic;
    uint32_t crash_count;
    uint32_t sketch_running;
    uint32_t force_safe;
} rtc_state_t;

static RTC_NOINIT_ATTR rtc_state_t s_rtc;

static uint8_t s_active;        /* active slot */
static uint8_t s_prev;          /* slot to roll back to */
static uint8_t s_trial;         /* 1 = active slot has not yet proven itself */
static const char *s_safe_reason;

static void save_slots(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        BLOG_E("cannot open NVS to save sketch slot");
        return;
    }
    nvs_set_u8(h, "active", s_active);
    nvs_set_u8(h, "prev", s_prev);
    nvs_set_u8(h, "trial", s_trial);
    nvs_commit(h);
    nvs_close(h);
}

static bool boot_button_held(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BL_SAFE_MODE_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    esp_rom_delay_us(2000);
    bool held = gpio_get_level(BL_SAFE_MODE_PIN) == 0;
    gpio_reset_pin(BL_SAFE_MODE_PIN);
    return held;
}

void bl_state_init(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    if (s_rtc.magic != RTC_MAGIC || reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT) {
        memset(&s_rtc, 0, sizeof(s_rtc));
        s_rtc.magic = RTC_MAGIC;
    }

    bool crashed = s_rtc.sketch_running &&
                   (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
                    reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT);
    s_rtc.sketch_running = 0;
    if (crashed) {
        s_rtc.crash_count++;
        BLOG_W("previous boot: sketch crashed (reset reason %d), crash count %u",
               (int)reason, (unsigned)s_rtc.crash_count);
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "active", &s_active);
        nvs_get_u8(h, "prev", &s_prev);
        nvs_get_u8(h, "trial", &s_trial);
        nvs_close(h);
    }
    s_active &= 1;
    s_prev &= 1;

    if (crashed && s_trial) {
        BLOG_W("new sketch in slot %c crashed on its first run: rolling back to slot %c",
               'a' + s_active, 'a' + s_prev);
        s_active = s_prev;
        s_trial = 0;
        s_rtc.crash_count = 0;
        save_slots();
    }

    if (s_rtc.force_safe) {
        s_rtc.force_safe = 0;
        s_safe_reason = "requested";
    } else if (s_rtc.crash_count >= BL_CRASH_LIMIT) {
        s_safe_reason = "sketch crashed repeatedly";
    } else if (boot_button_held()) {
        s_safe_reason = "BOOT button held";
    }
}

const char *bl_state_safe_mode_reason(void)
{
    return s_safe_reason;
}

void bl_state_request_safe_mode(void)
{
    s_rtc.force_safe = 1;
}

int bl_state_active_slot(void)
{
    return s_active;
}

void bl_state_set_pending(int slot)
{
    if (slot != s_active) {
        s_prev = s_active;
    }
    s_active = slot & 1;
    s_trial = 1;
    s_rtc.crash_count = 0;
    save_slots();
}

void bl_state_sketch_running(void)
{
    s_rtc.sketch_running = 1;
}

void bl_state_sketch_stable(void)
{
    s_rtc.crash_count = 0;
    if (s_trial) {
        s_trial = 0;
        save_slots();
    }
}
