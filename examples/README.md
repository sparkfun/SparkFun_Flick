# Examples

Start with **`01_basics/hello_flick`**: it blinks the board's LED and prints `Hello flick!`. Open its `.ino` file and press **Ctrl+Shift+B** (or run `python flick.py upload examples/01_basics/hello_flick -m`). Then change something and upload again.

To make one of these your own, copy its folder into `my_sketches/` and rename the folder and its `.ino` to match.

Sketches marked **WiFi** read your network name and password from `secrets.cfg`: copy `secrets.cfg.example` to `secrets.cfg` and fill it in.

## 01_basics

| Sketch | What it does |
|---|---|
| `hello_flick` | Blinks the LED and prints `Hello flick!`. Start here. |
| `fade` | Fades the LED in and out with `analogWrite`. |
| `button_interrupt` | Counts presses of the BOOT button with `attachInterrupt`. |
| `analog_read` | Reads pin A0 and prints the value and the voltage. |
| `rgb_led` | Cycles the board's RGB LED through colors. |
| `timers` | `Ticker`, a hardware timer and `esp_timer` side by side. |

## 02_hardware

| Sketch | What it does | You need |
|---|---|---|
| `i2c_scanner` | Lists every device on the Qwiic bus. | A Qwiic board, or none |
| `fuel_gauge` | Reads the LiPo battery's voltage and charge. | A LiPo battery, a board with a MAX17048 |
| `uart_loopback` | Sends text out Serial1 and reads it back. | A jumper wire |
| `spi_loopback` | Sends bytes over SPI and checks the echo. | A jumper wire |
| `touch_button` | A bare wire as a touch button. | A wire (ESP32, S3) |
| `led_strip` | FastLED rainbow on the RGB LED or a strip. | |
| `servo_sweep` | Sweeps a hobby servo. | A servo |
| `i2s_tone` | Plays a 440 Hz tone. | An I2S amplifier |

## 03_wifi

| Sketch | What it does |
|---|---|
| `wifi_scan` | Lists nearby WiFi networks. |
| `wifi_http_get` | **WiFi.** Fetches a web page over HTTPS. |
| `ntp_clock` | **WiFi.** Gets the time from the internet and prints a clock. |
| `wifi_ap_webserver` | The board makes its own network and serves a page that switches the LED. |
| `captive_portal` | An access point whose page opens by itself on a phone. |
| `mqtt_basic` | **WiFi.** Publishes and subscribes on a public MQTT broker. |
| `tcp_client` | **WiFi.** Plain HTTP over a raw TCP connection. |
| `udp_ntp` | **WiFi.** The time from an NTP server with raw UDP packets. |
| `espnow_broadcast` | Boards talk to each other without a router (load it on two). |

## 04_bluetooth

| Sketch | What it does |
|---|---|
| `ble_serial` | A BLE serial port to a phone app, bridged to USB. |
| `ble_scan` | Lists nearby BLE devices. |
| `ble_server` | A BLE characteristic that counts up and accepts writes. |
| `spp_serial` | A Bluetooth Classic serial port (classic ESP32 only). |

## 05_storage

| Sketch | What it does |
|---|---|
| `littlefs_files` | Writes, reads and lists files in flash. |
| `preferences_counter` | Counts restarts in settings that survive power cycles. |
| `sd_card` | Writes and reads a file on the microSD card. |

## 06_power_and_usb

| Sketch | What it does |
|---|---|
| `deep_sleep` | Sleeps 10 s, wakes, and counts the wake-ups. |
| `usb_keyboard` | The board types text as a USB keyboard (ESP32-S3). |

## 07_updates

| Sketch | What it does |
|---|---|
| `wifi_ota` | **WiFi.** Upload sketches, and Baseline, over WiFi from your PC. |
| `ota_pull_sketch` | **WiFi.** The board downloads a newer version of itself from a web server. |
| `ota_pull_baseline` | **WiFi.** The same for Baseline. |
