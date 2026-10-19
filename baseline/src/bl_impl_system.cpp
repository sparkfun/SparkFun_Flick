/* API table: system information and random numbers (backs the sketch's ESP object). */
#include <Arduino.h>
#include "esp_random.h"
#include "gen/bl_impl.h"

extern "C" uint32_t bl_impl_system_info(int what)
{
    switch (what) {
    case BL_INFO_CPU_MHZ:        return getCpuFrequencyMhz();
    case BL_INFO_HEAP_SIZE:      return ESP.getHeapSize();
    case BL_INFO_MIN_FREE_HEAP:  return ESP.getMinFreeHeap();
    case BL_INFO_MAX_ALLOC_HEAP: return ESP.getMaxAllocHeap();
    case BL_INFO_PSRAM_SIZE:     return ESP.getPsramSize();
    case BL_INFO_FREE_PSRAM:     return ESP.getFreePsram();
    case BL_INFO_FLASH_SIZE:     return ESP.getFlashChipSize();
    default:                     return 0;
    }
}

extern "C" uint32_t bl_impl_random(void)
{
    return esp_random();
}
