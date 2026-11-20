/*
 * ext_demo.cpp - a Baseline extension for tools/tests/ext_test. It is Baseline
 * code: it may call ESP-IDF (or the Teensy / arduino-pico core) directly.
 *
 *   python flick.py baseline update -t <target> --ext flick/tools/tests/ext_demo -p COMx
 */
#include "baseline_extension.h"
#include "ext_demo_api.h"
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#endif

static int demo_add(int a, int b)
{
    return a + b;
}

static const char *demo_platform(void)
{
#if defined(ESP_PLATFORM)
    return esp_get_idf_version();
#else
    return "not ESP-IDF";
#endif
}

static uint32_t demo_internal_free(void)
{
#if defined(ESP_PLATFORM)
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
#else
    return 0;
#endif
}

static const ext_demo_api_t s_api = {1, demo_add, demo_platform, demo_internal_free};
BL_EXTENSION(EXT_DEMO_NAME, &s_api);
