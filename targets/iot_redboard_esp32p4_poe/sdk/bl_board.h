/*
 * bl_board.h - SparkFun IoT RedBoard - ESP32-P4, PoE version (Arduino R3 form
 * factor). ESP32-P4 rev 3, 16 MB flash, PSRAM in the package, native USB
 * (USB-Serial-JTAG), Ethernet (TI DP83825 PHY, RMII), microSD (4-bit SDIO),
 * Qwiic, MAX17048, WS2812B. No radio: the WiFi/BLE version adds an ESP32-C5.
 * Pins: the board's KiCad schematic (SparkFun_IoT_RedBoard-ESP32-P4/Hardware/PoE).
 *
 * Board facts for a product target, included at the end of the chip's
 * bl_target.h: seen by Baseline (default pins) and by sketches. #defines only.
 */
#pragma once

#define BL_TARGET_NAME          "iot_redboard_esp32p4_poe"
#define BL_BOARD_NAME           "SparkFun IoT RedBoard - ESP32-P4 (PoE)"

/* ---- Pins seen by sketches ---- */
#define BL_BOARD_LED_BUILTIN    12          /* the WS2812B is the only user LED */
#define BL_BOARD_STAT_LED       12
#define BL_BOARD_RGB_LED        12          /* WS2812B; its DOUT is on the header's NC pin */
#define BL_BOARD_SDA            26          /* Qwiic, D18/SDA */
#define BL_BOARD_SCL            27          /* Qwiic, D19/SCL */
#define BL_BOARD_A0             54
#define BL_BOARD_A1             53
#define BL_BOARD_A2             52
#define BL_BOARD_A3             51
#define BL_BOARD_A4             50
#define BL_BOARD_A5             49
#define BL_BOARD_SCK            9           /* D13 */
#define BL_BOARD_MISO           10          /* D12, POCI */
#define BL_BOARD_MOSI           8           /* D11, PICO */
#define BL_BOARD_SS             7           /* D10 */

#define BL_BOARD_FUEL_GAUGE_ADDR  0x36      /* MAX17048 LiPo fuel gauge on the Qwiic bus */
#define BL_BOARD_FUEL_GAUGE_ALERT 46        /* MAX17048 ALRT, active low */
#define BL_BOARD_POWER_STATUS     48        /* LM66200 ST: which supply is in use */
#define BL_BOARD_SD_DETECT        47        /* microSD card detect, low = card in */

/* Ethernet: DP83825 PHY on RMII (not yet used by Baseline). */
#define BL_BOARD_ETH_PHY_RESET  20
#define BL_BOARD_ETH_MDIO       21
#define BL_BOARD_ETH_MDC        22
#define BL_BOARD_ETH_REF_CLK    32          /* RMII 50 MHz clock (also tied to GPIO 23) */

/* ---- Baseline defaults ---- */
#define BL_SERIAL1_RX           38          /* D0/RX: UART0's pins, free because the console is USB */
#define BL_SERIAL1_TX           37          /* D1/TX */
#define BL_SERIAL2_RX           (-1)
#define BL_SERIAL2_TX           (-1)
/* microSD: 4-bit SDIO on SDMMC slot 0's fixed pins (CLK 43, CMD 44, D0 39,
 * D1 40, D2 41, D3 42), powered from the on-chip LDO channel 4 and switched by
 * GPIO 45 (low = on); Arduino's esp32p4 variant handles both. */
#define BL_SD_DEFAULT_BUS       1           /* BL_SD_MMC */
#define BL_SD_CS                (-1)
