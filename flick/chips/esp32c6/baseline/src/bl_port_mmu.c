/*
 * bl_port_mmu.c - map the sketch slot at its fixed address: ESP32-C6.
 *
 * Written for ESP-IDF 5.5.5 (Arduino-ESP32 3.3.11); NOT YET RUN ON HARDWARE. Uses the low-level
 * MMU HAL, following esp_mm's own s_do_mapping(); recheck when moving to a new
 * IDF release.
 */
#include "esp_attr.h"
#include "hal/mmu_hal.h"
#include "hal/cache_hal.h"
#include "hal/cache_ll.h"
#include "esp_private/cache_utils.h"
#include "sketch_abi.h"
#include "bl_port.h"

/* Runs with the cache disabled, so it and everything it calls must be in IRAM.
 * The I-bus and D-bus share one MMU table (on the C6/C5 they are even the same
 * addresses), so one mapping serves both windows. Single core. noinline: if inlined into a flash-resident caller it would run
 * from flash. */
static void IRAM_ATTR __attribute__((noinline)) map_window(uint32_t paddr, uint32_t len)
{
    spi_flash_disable_interrupts_caches_and_other_cpu();
    uint32_t mapped = 0;
    mmu_hal_map_region(0, MMU_TARGET_FLASH0, BL_SKETCH_IBUS_BASE, paddr, len, &mapped);
    cache_bus_mask_t bus = cache_ll_l1_get_bus(0, BL_SKETCH_IBUS_BASE, len) |
                           cache_ll_l1_get_bus(0, BL_SKETCH_DBUS_BASE, len);
    cache_ll_l1_enable_bus(0, bus);
    cache_hal_invalidate_addr(BL_SKETCH_IBUS_BASE, len);
#if BL_SKETCH_DBUS_BASE != BL_SKETCH_IBUS_BASE
    cache_hal_invalidate_addr(BL_SKETCH_DBUS_BASE, len);
#endif
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
    return mmu_hal_vaddr_to_paddr(0, BL_SKETCH_IBUS_BASE, &paddr, &target);
}
