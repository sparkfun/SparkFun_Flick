/* The WiFi/BLE co-processor over ESP-Hosted (bl_hosted.h). */
#include <Arduino.h>
#include "bl_hosted.h"

#if CONFIG_ESP_HOSTED_ENABLED
#include "freertos/event_groups.h"
#include "esp32-hal-hosted.h"
#include "bl_console.h"
#include "sketch_loader.h"

#define HOSTED_TASK_STACK   4096
#define HOSTED_WAIT_MS      20000       /* longer than ESP-Hosted's own card-init retries */
#define DONE_BIT            BIT0

static EventGroupHandle_t s_done;
static volatile bool s_ok;
static char s_info[40] = "starting";

static void hosted_task(void *arg)
{
    // hostedInitWiFi() only brings the link up (WiFi itself starts with the
    // sketch's first WiFi call); BLE then needs only its controller.
    // hostedInitWiFi() can report success with the co-processor silent (seen
    // with it held in reset): only a firmware version read back proves it runs.
    uint32_t major = 0, minor = 0, patch = 0;
    if (hostedInitWiFi()) {
        hostedGetSlaveVersion(&major, &minor, &patch);
    }
    s_ok = major || minor || patch;
    if (s_ok) {
        snprintf(s_info, sizeof(s_info), "%s %lu.%lu.%lu", hostedGetSlaveTargetName(), (unsigned long)major,
                 (unsigned long)minor, (unsigned long)patch);
        BLOG_I("WiFi/BLE co-processor: %s", s_info);
    } else {
        strlcpy(s_info, "not answering", sizeof(s_info));
        BLOG_E("WiFi/BLE co-processor not answering: WiFi and BLE calls will fail");
    }
    xEventGroupSetBits(s_done, DONE_BIT);
    vTaskDelete(NULL);
}

extern "C" void bl_hosted_start(void)
{
    // The pins Arduino's WiFi and BLE pass to ESP-Hosted come from its generic
    // P4 variant (Espressif's dev board); ESP-Hosted already runs with the
    // board's (the target's sdkconfig) and refuses others. Give Arduino those.
    hostedSetPins(CONFIG_ESP_HOSTED_SDIO_PIN_CLK, CONFIG_ESP_HOSTED_SDIO_PIN_CMD, CONFIG_ESP_HOSTED_SDIO_PIN_D0,
                  CONFIG_ESP_HOSTED_SDIO_PIN_D1, CONFIG_ESP_HOSTED_SDIO_PIN_D2, CONFIG_ESP_HOSTED_SDIO_PIN_D3,
                  CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE);
    s_done = xEventGroupCreate();
    if (!s_done || xTaskCreate(hosted_task, "bl_hosted", HOSTED_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        strlcpy(s_info, "not started (no memory)", sizeof(s_info));
        if (s_done) {
            xEventGroupSetBits(s_done, DONE_BIT);
        }
    }
}

extern "C" bool bl_hosted_ready(void)
{
    if (!s_done) {
        return false;
    }
    if (!(xEventGroupGetBits(s_done) & DONE_BIT)) {
        WdtPause wdt;
        xEventGroupWaitBits(s_done, DONE_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(HOSTED_WAIT_MS));
    }
    return s_ok;
}

extern "C" const char *bl_hosted_info(void)
{
    return s_info;
}

#else

extern "C" void bl_hosted_start(void) {}
extern "C" bool bl_hosted_ready(void) { return true; }
extern "C" const char *bl_hosted_info(void) { return NULL; }

#endif
