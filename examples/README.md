# Examples

Start with **`01_basics/01_hello_flick`**: it blinks the board's LED and prints `Hello flick!`. Open its `.ino` file and press **Ctrl+Shift+B** (or run `python flick.py upload examples/01_basics/01_hello_flick -m`). Then change something and upload again.

To make one of these your own, copy its folder into `my_sketches/` and rename the folder and its `.ino` to match.

Sketches marked **WiFi** need your network name and password. Set them once with the VS Code task **Set WiFi network**, or `python flick.py wifi "network" "password"`; they're kept in `~/.flick/secrets.cfg`, not in the sketch.

## 01_basics

| Sketch | What it does |
|---|---|
| `01_hello_flick` | Blinks the LED and prints `Hello flick!`. Start here. |
| `02_fade` | Fades the LED in and out with `analogWrite`. |
| `03_button_interrupt` | Counts presses of the BOOT button with `attachInterrupt`. |
| `04_analog_read` | Reads pin A0 (A31 on the RedBoard Artemis ATP) and prints the value and the voltage. |
| `05_rgb_led` | Cycles the board's RGB LED through colors. |
| `06_timers` | `Ticker`, a hardware timer and `esp_timer` side by side. |

## 02_hardware

| Sketch | What it does | You need |
|---|---|---|
| `01_i2c_scanner` | Lists every device on the Qwiic bus. | A Qwiic board, or none |
| `02_fuel_gauge` | Reads the LiPo battery's voltage and charge. | A LiPo battery, a board with a MAX17048 |
| `03_uart_loopback` | Sends text out Serial1 and reads it back. | A jumper wire |
| `04_spi_loopback` | Sends bytes over SPI and checks the echo. | A jumper wire |
| `05_touch_button` | A bare wire as a touch button. | A wire (ESP32, S3) |
| `06_led_strip` | FastLED rainbow on the RGB LED or a strip. | |
| `07_servo_sweep` | Sweeps a hobby servo. | A servo |
| `08_i2s_tone` | Plays a 440 Hz tone. | An I2S amplifier |

## 03_wifi

| Sketch | What it does |
|---|---|
| `01_wifi_scan` | Lists nearby WiFi networks. |
| `02_wifi_http_get` | **WiFi.** Fetches a web page over HTTPS. |
| `03_ntp_clock` | **WiFi.** Gets the time from the internet and prints a clock. |
| `04_wifi_ap_webserver` | The board makes its own network and serves a page that switches the LED. |
| `05_captive_portal` | An access point whose page opens by itself on a phone. |
| `06_mqtt_basic` | **WiFi.** Publishes and subscribes on a public MQTT broker. |
| `07_tcp_client` | **WiFi.** Plain HTTP over a raw TCP connection. |
| `08_udp_ntp` | **WiFi.** The time from an NTP server with raw UDP packets. |
| `09_espnow_broadcast` | Boards talk to each other without a router (load it on two). |

## 04_bluetooth

| Sketch | What it does |
|---|---|
| `01_ble_serial` | A BLE serial port to a phone app, bridged to USB. |
| `02_ble_scan` | Lists nearby BLE devices. |
| `03_ble_server` | A BLE characteristic that counts up and accepts writes. |
| `04_spp_serial` | A Bluetooth Classic serial port (classic ESP32 only). |

## 05_storage

| Sketch | What it does |
|---|---|
| `01_littlefs_files` | Writes, reads and lists files in flash. |
| `02_preferences_counter` | Counts restarts in settings that survive power cycles. |
| `03_sd_card` | Writes and reads a file on the microSD card. |

## 06_power_and_usb

| Sketch | What it does |
|---|---|
| `01_deep_sleep` | Sleeps 10 s, wakes, and counts the wake-ups. |
| `02_usb_keyboard` | The board types text as a USB keyboard (ESP32-S3). |

## 07_updates

| Sketch | What it does |
|---|---|
| `01_wifi_ota` | **WiFi.** Upload sketches, and Baseline, over WiFi from your PC. |
| `02_ota_pull_sketch` | **WiFi.** The board downloads a newer version of itself from a web server. |
| `03_ota_pull_baseline` | **WiFi.** The same for Baseline. |

## 08_teensy

Teensy 4.x only: Teensy's own APIs. On other boards these stop with "This board has no ...".

| Sketch | What it does |
|---|---|
| `01_interval_timer` | IntervalTimer: a function called every 100 us from a timer interrupt. |
| `02_usb_audio_tone` | The Teensy Audio library plays notes to the computer over USB audio (and to an audio shield). |
| `03_usb_audio_spectrum` | Music from the computer over USB audio, shown as an FFT spectrum in the serial monitor. |
