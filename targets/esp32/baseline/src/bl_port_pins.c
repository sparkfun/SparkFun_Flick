/*
 * bl_port_pins.c - which GPIOs the classic ESP32's flash and PSRAM use,
 * besides the usual flash pins 6-11 (bl_port.h blocks those always).
 *
 *   - Some packages remap the flash by eFuse. ESP32-PICO-V3-02, for example:
 *     CLK 6, Q 17, D 23, CS 11, HD 16.
 *   - Quad PSRAM shares the flash data lines, including HD and WP, and adds
 *     its own CLK and CS: GPIO17/16 on ESP32-D0WD (WROVER), GPIO16 on
 *     ESP32-D0WDR2-V3. D2WD and the PICO packages keep them within 6-11
 *     (IDF esp_psram_impl_quad.c).
 *
 * Read once, after Arduino has probed for PSRAM (that happens before setup()).
 */
#include "esp_rom_efuse.h"
#include "esp32/rom/efuse.h"
#include "esp_efuse.h"
#include "soc/efuse_defs.h"
#include "bootloader_flash_config.h"
#include "esp32-hal-psram.h"
#include "bl_port.h"

#define PIN_BIT(n) (1ULL << (n))

static uint64_t compute(void)
{
    uint64_t pins = 0;
    uint32_t cfg = esp_rom_efuse_get_flash_gpio_info();
    if (cfg == EFUSE_SPICONFIG_HSPI_DEFAULTS) {
        // HSPI pads: CLK 14, Q 12, D 13, CS 15, HD 4, WP 2
        pins |= PIN_BIT(2) | PIN_BIT(4) | PIN_BIT(12) | PIN_BIT(13) | PIN_BIT(14) | PIN_BIT(15);
    } else if (cfg != EFUSE_SPICONFIG_SPI_DEFAULTS) {
        pins |= PIN_BIT(EFUSE_SPICONFIG_RET_SPICLK(cfg)) | PIN_BIT(EFUSE_SPICONFIG_RET_SPIQ(cfg)) |
                PIN_BIT(EFUSE_SPICONFIG_RET_SPID(cfg)) | PIN_BIT(EFUSE_SPICONFIG_RET_SPICS0(cfg)) |
                PIN_BIT(EFUSE_SPICONFIG_RET_SPIHD(cfg));
    }
    if (psramFound()) {
        int wp = bootloader_flash_get_wp_pin();
        if (wp >= 0 && wp < 40) {
            pins |= PIN_BIT(wp);
        }
        uint32_t pkg = esp_efuse_get_pkg_ver();
        if (pkg == EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6 || pkg == EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5) {
            pins |= PIN_BIT(16) | PIN_BIT(17);
        } else if (pkg == EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3) {
            pins |= PIN_BIT(16);
        }
    }
    return pins;
}

uint64_t bl_port_memory_pins(void)
{
    static uint64_t s_pins;
    static bool s_done;
    if (!s_done) {
        s_pins = compute();
        s_done = true;
    }
    return s_pins;
}
