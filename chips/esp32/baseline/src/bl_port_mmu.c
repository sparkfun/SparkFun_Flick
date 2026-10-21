/*
 * bl_port_mmu.c - map the sketch slot at its fixed address: classic ESP32.
 *
 * Verified against ESP-IDF 5.5.5 (Arduino-ESP32 3.3.11). The ESP32 differs
 * from the S3 in three ways:
 *   - instruction (IRAM0) and data (DROM0) addresses use separate MMU entries,
 *     so the slot is mapped twice;
 *   - each CPU has its own MMU table (MMU_TABLE_CORE0 / CORE1), and the sketch
 *     task runs on core 1 while Baseline reads the header on core 0, so both
 *     tables are written;
 *   - the cache cannot be invalidated by address, so the whole cache is
 *     flushed with IDF's cache_sync(), as esp_mmu_map does for this chip. Do
 *     not call the ROM's Cache_Flush() directly: PSRAM shares this write-back
 *     cache, and Cache_Flush() throws away PSRAM writes still in it. cache_sync()
 *     writes them back first.
 */
#include "esp_attr.h"
#include "hal/mmu_hal.h"
#include "hal/cache_ll.h"
#include "esp_private/cache_utils.h"
#include "esp_private/esp_cache_esp32_private.h"
#include "sketch_abi.h"
#include "bl_port.h"

/* Runs with the cache disabled, so it and everything it calls must be in IRAM.
 * noinline: if inlined into a flash-resident caller it would run from flash. */
static void IRAM_ATTR __attribute__((noinline)) map_window(uint32_t paddr, uint32_t len)
{
    spi_flash_disable_interrupts_caches_and_other_cpu();
    uint32_t mapped = 0;
    for (uint32_t mmu = MMU_TABLE_CORE0; mmu <= MMU_TABLE_CORE1; mmu++) {
        mmu_hal_map_region(mmu, MMU_TARGET_FLASH0, BL_SKETCH_IBUS_BASE, paddr, len, &mapped);
        mmu_hal_map_region(mmu, MMU_TARGET_FLASH0, BL_SKETCH_DBUS_BASE, paddr, len, &mapped);
    }
    cache_bus_mask_t bus = cache_ll_l1_get_bus(0, BL_SKETCH_IBUS_BASE, len) |
                           cache_ll_l1_get_bus(0, BL_SKETCH_DBUS_BASE, len);
    cache_ll_l1_enable_bus(0, bus);
    cache_ll_l1_enable_bus(1, bus);
    cache_sync();
    spi_flash_enable_interrupts_caches_and_other_cpu();
}

void bl_port_map_window(uint32_t paddr, uint32_t len)
{
    map_window(paddr, len);
}

bool bl_port_window_in_use(void)
{
    uint32_t paddr;
    mmu_target_t target;
    return mmu_hal_vaddr_to_paddr(MMU_TABLE_CORE0, BL_SKETCH_IBUS_BASE, &paddr, &target) ||
           mmu_hal_vaddr_to_paddr(MMU_TABLE_CORE0, BL_SKETCH_DBUS_BASE, &paddr, &target);
}
