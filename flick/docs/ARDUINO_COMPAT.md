# Writing sketches: classic Arduino names

Baseline was designed so that you can just write ordinary Arduino-ESP32 code. Every call below is
routed through the Baseline firmware on the board. Those remain the raw API, listed in [API.md](API.md).

`.ino` files include everything automatically. If code you copy includes a
classic header, it still compiles: these headers live in `sdk/include/compat`
and all lead to Baseline.

- **Core and peripherals:** `<Arduino.h>`, `<SPI.h>`, `<Wire.h>`, `<HardwareSerial.h>`, `<FunctionalInterrupt.h>`, `<ESP_I2S.h>`, `<ESP32Servo.h>`, `<Servo.h>`, `<FastLED.h>`, `<esp32-hal-psram.h>`
- **Storage:** `<Preferences.h>`, `<LittleFS.h>`, `<FS.h>`, `<SD.h>`, `<SD_MMC.h>`
- **Time, sleep, timers:** `<time.h>`, `<sys/time.h>`, `<esp_sleep.h>`, `<esp_timer.h>`, `<Ticker.h>`
- **USB device (ESP32-S3):** `<USB.h>`, `<USBHIDKeyboard.h>`, `<USBHIDMouse.h>`, `<USBHIDGamepad.h>`, `<USBHIDConsumerControl.h>`, `<USBMIDI.h>`
- **Battery:** `<SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>`
- **WiFi and networking:** `<WiFi.h>`, `<WiFiClient.h>`, `<WiFiClientSecure.h>`, `<WiFiServer.h>`, `<WiFiUdp.h>`, `<Network*.h>`, `<HTTPClient.h>`, `<WebServer.h>`, `<ESPmDNS.h>`, `<ArduinoOTA.h>`, `<PubSubClient.h>`, `<DNSServer.h>`
- **Bluetooth Classic:** `<BluetoothSerial.h>` (classic ESP32 only)
- **BLE:** `<BLEDevice.h>`, `<BLEServer.h>`, `<BLEUtils.h>`, `<BLE2902.h>`, `<BLE2901.h>`, `<BLEScan.h>`, `<BLEClient.h>`, `<BLEAdvertisedDevice.h>`, `<BLEAdvertising.h>`, `<BLECharacteristic.h>`, `<BLEService.h>`, …
- **ESP-IDF:** `<esp_now.h>`, `<esp_wifi.h>`, `<esp_system.h>`, `<esp_log.h>`, `<esp_random.h>`

## What works

| Area | Classic names |
|---|---|
| Pins | `pinMode`, `digitalWrite`, `digitalRead`, `analogRead`, `analogReadMilliVolts`, `analogWrite`, `rgbLedWrite`, `HIGH/LOW/INPUT/OUTPUT/INPUT_PULLUP/...`, `LED_BUILTIN`, `STAT_LED`, `RGB_BUILTIN`, `A0`, `SDA`, `SCL`, `analogReadResolution`, `analogSetAttenuation`, `analogSetPinAttenuation` (`ADC_0db`...`ADC_11db`), `dacWrite` (classic ESP32), `pulseIn`, `pulseInLong` |
| Interrupts | `attachInterrupt`, `attachInterruptArg`, `detachInterrupt`, `digitalPinToInterrupt`, `RISING/FALLING/CHANGE/ONLOW/ONHIGH`, `IRAM_ATTR` (accepted), `noInterrupts/interrupts` (no-ops, see below) |
| Time | `millis`, `micros`, `delay`, `delayMicroseconds`, `yield` |
| Clock and NTP | `configTime`, `configTzTime`, `getLocalTime`, `time`, `gettimeofday`, `settimeofday`, `localtime(_r)`, `gmtime(_r)`, `mktime`, `strftime`, `difftime`, `struct tm` |
| Timers | `Ticker` (`attach/attach_ms/attach_us/once/once_ms/detach/active`, with or without an argument), `timerBegin/timerEnd/timerStart/timerStop/timerRestart/timerWrite/timerRead(Micros/Millis/Seconds)/timerAttachInterrupt(Arg)/timerDetachInterrupt/timerAlarm`, `esp_timer_create/start_once/start_periodic/stop/delete/is_active/get_time` |
| Sleep | `esp_sleep_enable_timer/ext0/ext1/ext1_io/touchpad/uart/gpio_wakeup`, `gpio_wakeup_enable`, `esp_sleep_disable_wakeup_source`, `esp_deep_sleep_start`, `esp_deep_sleep`, `esp_light_sleep_start`, `esp_sleep_get_wakeup_cause`, `esp_sleep_get_ext1_wakeup_status`, `ESP.deepSleep`, `GPIO_NUM_*`, `BUTTON_PIN_BITMASK`; data that survives deep sleep: `bl_rtc_memory()` (1 KB) |
| Touch | `touchRead`, `touchAttachInterrupt(Arg)`, `touchDetachInterrupt`, `touchSleepWakeUpEnable`, `T0..T9` (ESP32) / `T1..T14` (S3) |
| PWM, tones, servos | `ledcAttach/ledcWrite/ledcDetach/ledcWriteTone/ledcChangeFrequency`, `tone/noTone`, `Servo` (ESP32Servo: `attach/write/writeMicroseconds/read/readMicroseconds/attached/detach/setPeriodHertz`) |
| LED strips | FastLED: `CRGB` (+ HTML color names), `CHSV`, `FastLED.addLeds<WS2812B/WS2812/WS2813/WS2811/SK6812/NEOPIXEL/APA106/..., PIN, ORDER>`, `show/setBrightness/clear/showColor/delay/setMaxPowerInVoltsAndMilliamps`, `fill_solid/fill_rainbow/fill_gradient_RGB/fadeToBlackBy/nscale8/blur1d`, `scale8/qadd8/qsub8/sin8/cos8/sin16/beat8/beatsin8/beatsin16/random8/random16/map8/lerp8by8/blend`, `EVERY_N_MILLISECONDS/SECONDS` |
| I2S audio | `I2SClass` (ESP_I2S): `setPins/setPinsPdmTx/setPinsPdmRx/begin/end/write/readBytes/available/read`, `I2S_MODE_STD/PDM_TX/PDM_RX`, `I2S_DATA_BIT_WIDTH_*`, `I2S_SLOT_MODE_MONO/STEREO` |
| USB device (S3) | `USB.begin`, `USBHIDKeyboard` (`press/release/releaseAll/write/print`, `KEY_*`), `USBHIDMouse` (`move/click/press/release/isPressed`, `MOUSE_*`), `USBHIDGamepad` (`leftStick/rightStick/triggers/hat/pressButton/send`, `BUTTON_*`, `HAT_*`), `USBHIDConsumerControl` (`press/release`, `CONSUMER_CONTROL_*`), `USBMIDI` (`noteOn/noteOff/controlChange/programChange/pitchBend/channelPressure/readPacket/writePacket`) |
| Battery | `SFE_MAX1704X` (SparkFun MAX1704x library): `begin/isConnected/getVoltage/getSOC/getChangeRate/quickStart/setThreshold/getAlert/clearAlert/sleep/wake/...` |
| Serial | `Serial.begin/print/println/printf/write/read/available/readString/readStringUntil/setTimeout`, `readBytes`, `find`, `findUntil`, `parseInt`, `parseFloat`, `availableForWrite`. `readBytes` is virtual, as in Arduino-ESP32, so stream classes in libraries can override it |
| Hardware UARTs | `Serial1` (default pins RX 44 / TX 43) and `Serial2` (pass pins): `begin(baud, SERIAL_8N1, rx, tx)/end/print/write/read/available/peek/flush`, `SERIAL_5N1..SERIAL_8O2`; `setRxBufferSize` (before `begin`), `availableForWrite`, `readBytes` (waits up to `setTimeout`, as in Arduino-ESP32), `updateBaudRate` (on the RP2040/RP2350 it restarts the port, dropping unread bytes) |
| Text | `String` (concat, `+`, compare, `indexOf`, `substring`, `replace`, `trim`, `toInt`, `toFloat`, ...), `printf`, `snprintf`, `sprintf`, `atoi`, `atof`, `strtol`, `strtod`, `F()` |
| Math | `map`, `constrain`, `min`, `max`, `random`, `randomSeed`, `esp_random`, `<math.h>` (`sqrt`, `pow`, `log`, `sin`, ... ; libm is linked), `PI`, `radians`, `degrees`, `sq`, `lowByte`, `highByte`, `bitRead/bitSet/bitClear/bitWrite`, `bit`, `makeWord` |
| SPI | `SPI.begin/end/beginTransaction/endTransaction/transfer/transfer16/transfer32/transferBytes/writeBytes/setFrequency/setDataMode/setBitOrder`, `SPISettings`, `MSBFIRST`, `SPI_MODE0..3` |
| I2C | `Wire.begin/end/setClock/getClock/beginTransmission/write/endTransmission/requestFrom` (all the Arduino overloads)`/available/read/peek`, `I2C_BUFFER_LENGTH`; `Wire1` on chips with a second I2C controller (classic ESP32, S3; the low-power I2C on the C6/C5): pass its pins to `begin` |
| WiFi station | `WiFi.begin/status/isConnected/disconnect/config/setHostname/localIP/gatewayIP/subnetMask/dnsIP/macAddress/SSID/RSSI/channel/hostByName`, `WL_*`, `IPAddress`; `WiFi.onEvent(cb[, event])` / `removeEvent` (`ARDUINO_EVENT_*`; `cb(event)`, `cb(event, info)` or `cb(arduino_event_t *)`, on the sketch task; `info` holds the details of connected, disconnected, got IP, station joined/left and scan done); `WiFi.setLongRange(on[, WIFI_IF_STA/AP])` (ESP32 long range mode); `WiFiMulti` (`addAP`, `run`) |
| Network interfaces | `WiFi.STA` and `WiFi.AP` as Arduino-ESP32 3.x's `NetworkInterface`: `started/connected/hasIP/linkUp/isDefault/setDefault/localIP/subnetMask/gatewayIP/dnsIP(n)/broadcastIP/macAddress/getHostname/setHostname/desc`, printable; `WiFi.STA.begin/end/connect(ssid, pass, channel)/disconnect/config/status/SSID/RSSI`, `WiFi.AP.begin/end/create/clear/config/stationCount/SSID`; `Network.onEvent/removeEvent/eventName/hostByName/setDefaultInterface/getDefaultInterface`. IPv4 only (`IPAddress::type()` is `IPv4`); no `ETH` or `PPP` interface yet |
| WiFi AP and scan | `WiFi.mode(WIFI_STA/WIFI_AP/WIFI_AP_STA/WIFI_OFF)`, `WiFi.softAP/softAPIP/softAPdisconnect/softAPgetStationNum`, `WiFi.scanNetworks` (all of Arduino-ESP32's arguments: hidden, passive, time per channel, one channel, SSID or BSSID)`/SSID(i)/RSSI(i)/channel(i)/encryptionType(i)/BSSIDstr(i)/scanDelete`, `WIFI_AUTH_*` |
| TCP | `Client` (the abstract class libraries take), `WiFiClient` (`connect/print/write/available/read/peek/connected/stop/remoteIP/remotePort/localIP/localPort/setConnectionTimeout`), `WiFiServer` (`begin/accept/available/hasClient`), `NetworkClient/NetworkServer` |
| TLS | `WiFiClientSecure` / `NetworkClientSecure`: `setInsecure`, `setCACert`, or by default Baseline's built-in CA bundle; client certificates (`setCertificate`, `setPrivateKey`), `setHandshakeTimeout`, `lastError` |
| UDP | `WiFiUDP` / `NetworkUDP`: `begin/beginPacket/write/endPacket/parsePacket/available/read/remoteIP/remotePort` |
| HTTP client | `HTTPClient`: `begin`, `addHeader`, `setAuthorization`, `GET/POST/PUT/PATCH/sendRequest`, `getString`, `getSize`, `errorToString`, `HTTP_CODE_*` (`t_http_codes`). No streams yet (`getStream`, `getStreamPtr`, `writeToStream`) |
| Web server | `WebServer`: `on`, `onNotFound`, `begin`, `handleClient`, `send`, `send_P`, `sendHeader`, `arg`, `hasArg`, `args`, `argName`, `uri`, `method`, `header`, `collectHeaders`, `client().remoteIP()`, `HTTP_GET/POST/...` |
| Preferences | `begin/end/clear/remove/isKey/getType/freeEntries`, `put/get` for `Char/UChar/Short/UShort/Int/UInt/Long/ULong/Long64/ULong64/Float/Double/Bool/String/Bytes`, `getBytesLength` |
| microSD | `SD.begin()` (the board's slot) or `SD.begin(csPin)` (SPI), `SD_MMC.begin(mount, mode1bit)`, `end/cardType/cardSize/totalBytes/usedBytes`, `CARD_*`; files as below |
| DNS / captive portal | `DNSServer.start(port, domain, ip)/stop/processNextRequest/setErrorReplyCode`, `DNSReplyCode` |
| LittleFS | `LittleFS.begin(formatOnFail)/end/format/totalBytes/usedBytes/open/exists/remove/rename/mkdir/rmdir`; `File`: `print/write/read/readBytes/peek/available/seek/position/size/name/path/isDirectory/openNextFile/rewindDirectory/getLastWrite/flush/close`; `fs::FS`, `fs::File`, `FILE_READ/WRITE/APPEND` |
| mDNS | `MDNS.begin/end/addService/addServiceTxt/queryHost/queryService/hostname(i)/address(i)/port(i)` |
| MQTT | `PubSubClient`: `setServer/setCallback/setClient/setKeepAlive/setSocketTimeout/setBufferSize`, `connect` (all forms, with will and credentials), `publish/publish_P/beginPublish/endPublish`, `subscribe/unsubscribe`, `loop/connected/state/disconnect`, `MQTT_*` states; TLS with `WiFiClientSecure` |
| WiFi upload | `ArduinoOTA.setHostname/setPassword/setPort/begin/handle/end`; then `flick upload <sketch> --host NAME` |
| ESP-NOW | `esp_now_init`, `esp_now_register_recv_cb`, `esp_now_register_send_cb` (both the IDF 5 and the older callback forms), `esp_now_add_peer`, `esp_now_del_peer`, `esp_now_is_peer_exist`, `esp_now_fetch_peer`, `esp_now_get_peer_num`, `esp_now_send`; the receive info has the destination address and `rx_ctrl->rssi` / `rx_ctrl->channel`; `esp_wifi_set_channel`, `esp_wifi_get_channel` |
| BLE | `BLEDevice`, `BLEServer`, `BLEService`, `BLECharacteristic` (read/write/notify/indicate, `setValue/getValue/getData`, `setNotifyProperty` and the other property setters before the service starts), `BLEService::executeDelete`, `BLEServer::getPeerMTU`, `esp_ble_gap_set_device_name`, `ESP_GATT_MAX_ATTR_LEN`, `BLEDescriptor`, `BLE2902`, `BLE2901`, `BLEAdvertising`, `BLEAdvertisementData`, `BLEScan` (blocking and background), `BLEAdvertisedDevice`, `BLEScanResults`, `BLEClient`, `BLERemoteService`, `BLERemoteCharacteristic` (read/write/`registerForNotify`), `BLEUUID`, `BLEAddress`, and all the callback classes |
| BLE serial | `BLESerial`: a ready-made BLE serial port (Nordic UART) |
| Bluetooth Classic | `BluetoothSerial` (SPP): `begin(name)/end/hasClient/connected(timeout)/setPin/print/write/read/available/peek/flush/disconnect`, `enableSSP()`/`enableSSP(input, output)`/`confirmReply`, `deleteAllBondedDevices`; `setRxBufferSize(bytes)` (default 512) and `setTxBufferSize(bytes)` (default none) before the first `begin()`, in PSRAM when there is some. Classic ESP32 only; on other chips `begin()` returns false |
| System | `ESP.getFreeHeap/getHeapSize/getMinFreeHeap/getMaxAllocHeap/getPsramSize/getFreePsram/getFlashChipSize/getCpuFreqMHz/getChipModel/getChipCores/getEfuseMac/restart`, `getCpuFrequencyMhz`, `esp_restart`, `temperatureRead` (NaN without a sensor), `esp_reset_reason` (`ESP_RST_*`) |
| PSRAM | `psramFound`, `ps_malloc`, `ps_calloc`, `ps_realloc` (free with `free`). Plain `malloc`/`new` over 4 KB also go to PSRAM when the board has it. Boards without PSRAM report 0 and `ps_*` return NULL. |
| Logging | `log_e/w/i/d/v`, `ESP_LOGE/W/I/D/V` |
| Text in flash | `F("...")`, `__FlashStringHelper`, `PROGMEM`, `PSTR`, `pgm_read_byte/word/dword/float/ptr` (+ `_near`/`_far`), `memcpy_P`, `strcpy_P`, `sprintf_P`, ... , `<pgmspace.h>`, `<avr/pgmspace.h>` (all plain memory reads on ESP32) |
| C++ library | `std::string`, `std::map`, `std::vector`, `<algorithm>`, ... (libstdc++ is linked). No exceptions: an error a container would throw logs a message and crashes the sketch, so Baseline rolls back. |

## Arduino libraries

Sketches can use ordinary Arduino libraries. `flick build` follows the sketch's `#include` lines, as the Arduino IDE does: a header that is not in the sketch or the SDK selects the library that provides it, the library's sources are compiled into the sketch, and its own includes are followed in turn. Libraries are looked up in:
1. the sketch's own `libraries/` folder,
2. the folders in `$FLICK_LIBRARIES` (separated by `;` on Windows, `:` elsewhere),
3. the Arduino IDE's libraries folder (from its `arduino-cli.yaml`, else `Documents/Arduino/libraries`).

Install libraries with the Arduino IDE's Library Manager or `arduino-cli lib install`, as usual. The SDK's own headers win, so `Wire.h`, `SPI.h`, `WiFi.h` and the other classic headers keep pointing at Baseline. Library warnings are not shown; errors are.

**Checked** (October 2026): the first example of each of these compiles unchanged for the S3, the C6 and the classic ESP32: ArduinoJson; SparkFun BME280, BMP581, BMI270, ICM-20948, ISM330DHCX, MMC5983MA, TMP102, TMP117, SHTC3, AHT20, SCD30, SCD4x, VEML7700, VCNL4040, VL53L1X, NAU7802, Qwiic Button, Qwiic Relay, Qwiic Joystick, Qwiic Twist, Qwiic Ultrasonic, Qwiic Alphanumeric, Qwiic OLED, I2C Mux, External EEPROM, u-blox GNSS v3 and LG290P GNSS. (TMP102's example uses `A3`, which not every board has.)

Like the Arduino IDE, `flick` declares the functions a `.ino` defines, so a sketch may call a function before its definition.

**Secrets.** Put `KEY = value` lines in `~/.flick/secrets.cfg` (`python flick.py wifi NAME PASSWORD` sets the WiFi ones, `python flick.py secrets --set KEY=value` any other) and every sketch is built with `-DKEY="value"`: `#ifdef WIFI_SSID` ... `WiFi.begin(WIFI_SSID, WIFI_PASSWORD)` keeps passwords out of the sketch.

## How callbacks work

Interrupt handlers, ESP-NOW callbacks and BLE callbacks (`onConnect`,
`onWrite`, `onResult`, notify callbacks) do **not** run on the WiFi or BLE task
or in an interrupt, as they do in plain Arduino. Baseline queues the events,
and your callback runs on the sketch's own task: inside `delay()`, `yield()`,
or between `loop()` calls. Web server handlers run inside
`server.handleClient()`.

As a result, callbacks can use `Serial`, `delay` and your variables without
locks or `volatile`, and `noInterrupts()` is unnecessary. The trade-off is
latency: an interrupt handler runs within a few milliseconds, not
microseconds. A `loop()` that never calls `delay()` or returns holds callbacks
back until it does.

**Teensy 4.x is different:** `attachInterrupt()` and `IntervalTimer` handlers
run in interrupt context, as on a plain Teensy (microsecond latency; keep them
short, make shared variables `volatile`, guard multi-variable reads with
`noInterrupts()`/`interrupts()`). `Ticker` callbacks still run on the sketch's
own context.

## Tasks, mutexes and queues (ESP32 family)

FreeRTOS's own names work without an include (`<freertos/FreeRTOS.h>`,
`task.h`, `semphr.h` and `queue.h` may be included too): `xTaskCreate`,
`xTaskCreatePinnedToCore`, `vTaskDelete`, `vTaskDelay`, `vTaskDelayUntil`,
`xTaskGetTickCount`, `xTaskGetCurrentTaskHandle`, `pcTaskGetName`,
`uxTaskGetStackHighWaterMark`, `uxTaskPriorityGet`/`vTaskPrioritySet`,
`vTaskSuspend`/`vTaskResume`, `uxTaskGetNumberOfTasks`, `xPortGetCoreID`,
`xPortGetFreeHeapSize`, `xPortGetMinimumEverFreeHeapSize`; mutexes, recursive
mutexes, binary and counting semaphores (`xSemaphoreCreateMutex`, `Take`,
`Give`, ...); queues (`xQueueCreate`, `xQueueSend`, `SendToFront`,
`Overwrite`, `Receive`, `Peek`, `uxQueueMessagesWaiting`, `xQueueReset`, ...).

- A sketch's tasks run its code at the same time as `loop()`: guard shared data with a mutex.
- Callbacks (Ticker, ESP-NOW, BLE, WiFi events) still arrive only on the sketch's own task; `delay()` in another task just waits.
- One tick is 1 ms. Priorities are capped at 10 (Baseline's network, timer and IPC tasks run above), and stacks are at least 3072 bytes.
- Sketch code never runs in an interrupt, so the `...FromISR` calls are the ordinary ones.
- `vTaskDelete(NULL)` at the end of `setup()` ends `loop()` and keeps the sketch's other tasks running (`flick info` says `running: loop() ended by vTaskDelete`).
- A crash in any of the sketch's tasks rolls the sketch back like a crash in `loop()`. A task that spins on core 0 without waiting trips the idle task's watchdog after 5 s (a crash too); on core 1 it only slows the sketch.
- Not there: software timers (`xTimerCreate`), event groups, task notifications, stream and message buffers, `portENTER_CRITICAL`, `vTaskSuspendAll`, task snapshots. Use `Ticker` for timers.

## ESP-IDF calls (ESP32 family)

These work with their ESP-IDF names, without an include (`<esp_mac.h>`,
`<esp_heap_caps.h>`, `<esp_partition.h>`, `<esp_task_wdt.h>`, `<esp_err.h>`,
`<esp_attr.h>` and `<esp_ota_ops.h>` may be included too). Found missing by
compiling SparkFun RTK Everywhere; `flick/tools/tests/idf_test` checks them.

- `heap_caps_malloc`/`calloc`/`realloc`/`free`, `heap_caps_get_free_size`, `get_largest_free_block`, `get_minimum_free_size`, `get_total_size`, `heap_caps_check_integrity(_all)`, `heap_caps_malloc_extmem_enable` (for the whole board), `MALLOC_CAP_*`, `psramInit()`.
- `esp_read_mac` (`ESP_MAC_WIFI_STA`, `ESP_MAC_BT`, ...), `esp_err_to_name`, `ESP_ERR_*`, `ESP_ERROR_CHECK`.
- `esp_partition_find`/`find_first`/`get`/`next`/`iterator_release`: the partition table only (no read, write or erase). LittleFS is the `userfs` partition, not `spiffs`.
- `esp_task_wdt_add`/`delete`/`status`/`reset` for the sketch's own tasks. The sketch task itself is Baseline's: add and delete leave it as it is, reset feeds it.
- **`disableLoopWDT()` / `enableLoopWDT()` / `feedLoopWDT()`.** Flick watches `loop()` from the start (so a new sketch that hangs rolls back), while Arduino-ESP32 doesn't by default. A sketch whose `loop()` can block for over 5 s (a library busy-waiting for a module to restart, for example) calls `disableLoopWDT()` in `setup()`. `while (1);` is still detected.
- `RTC_NOINIT_ATTR` (and `NOINIT_ATTR`): kept across a restart or crash, not a power cycle or deep sleep, in the sketch's RAM. Check a magic value before trusting it.
- `Serial1/2.setRxFIFOFull()`.
- `<Update.h>` (`Update.begin/write/writeStream/end/abort`, `errorString`): a sketch image (`.flick`) goes to the free sketch slot; an ESP-IDF app image (a Baseline or other Arduino firmware) to the free Baseline slot, on 8 and 16 MB boards only. It starts at the next restart. No `U_SPIFFS`, `setMD5` or `rollBack`.
- `<mbedtls/x509_crt.h>`, `<pk.h>`, `<ctr_drbg.h>`, `<entropy.h>`, `<ssl.h>`: only checking that a certificate (`mbedtls_x509_crt_parse`) or a private key (`mbedtls_pk_parse_key`, no password) parses, with Baseline's mbedTLS. The parsed fields can't be read; TLS itself is `WiFiClientSecure`/`HTTPClient`.
- `<base64.h>` (`base64::encode`), `sscanf`/`vsscanf` (also on RP2; not Teensy), `strtok_r`, `strcasestr`, `strnstr`.

## What each board has

Each chip's `bl_target.h` says what it has, and the SDK declares only those
APIs, so a sketch that uses something its board lacks fails to compile, with
a line from flick saying why. Compat headers for a missing feature stop with
`#error "This board has no ..."`.

| Macro | Boards | APIs |
|---|---|---|
| `BL_PLATFORM_ESP32` | every ESP32 chip | Arduino-ESP32 / ESP-IDF names: `esp_*` (sleep, timer, random, restart, reset reason, ESP-NOW), `ESP`, `ESP_LOGx`/`log_x`, `ledc*`, `hw_timer`, `ps_malloc`, `psramFound`, `temperatureRead`, `analogReadMilliVolts`, attenuation, I2S, `SD_MMC`, `rgbLedWrite`, FastLED |
| `BL_PLATFORM_TEENSY` | Teensy 4.0, 4.1 | Teensyduino names (below) |
| `BL_PLATFORM_RP2` | RP2040 and RP2350 boards | `rgbLedWrite`, FastLED (below) |
| `BL_PLATFORM_APOLLO3` | Artemis boards (Apollo3 Blue) | the classic Arduino names only (below) |
| `BL_HAS_WIFI` | ESP32, S3, C3, C5, C6; the IoT RedBoard - ESP32-P4 (WiFi BLE); the Thing Plus - RP2350; the Pico 2 W | WiFi, HTTP, web server, DNS, mDNS, MQTT, WiFi OTA, NTP |
| `BL_HAS_ESPNOW` | ESP32, S3, C3, C5, C6 (not through ESP-Hosted) | ESP-NOW |
| `BL_HAS_BLE` | ESP32, S3, C3, C5, C6; the Thing Plus - RP2350; the Pico 2 W | `BLESerial`, the BLE library |
| `BL_HAS_BT_CLASSIC` | classic ESP32 | `BluetoothSerial` |
| `BL_HAS_TOUCH` | ESP32, S3 | `touchRead`, `T0`... |
| `BL_HAS_DAC` | classic ESP32 | `dacWrite` |
| `BL_HAS_USB_DEVICE` | ESP32-S3 | USB keyboard, mouse, gamepad, MIDI |
| `BL_HAS_AUDIO` | Teensy 4.0, 4.1 | the Teensy Audio library |
| `BL_HAS_RTOS` | every ESP32 chip | FreeRTOS tasks, semaphores, queues (above) |

Ticker, Servo, `tone()`, Preferences, LittleFS, SD, Wire, SPI and the UARTs
work on every board.

## Teensy 4.x (`teensy40`, `teensy41`)

- **Audio library:** `#include <Audio.h>`. The objects live in Baseline and the
  sketch's classes forward to them, with the Audio library's names, so code
  from the Audio System Design Tool compiles unchanged. 45 objects: I2S, MQS,
  USB audio, ADC input; waveform, sine, DC, noise, drum, Karplus-Strong, tone
  sweep; envelope, delay, freeverb, bitcrusher, fade, multiply, chorus, flange,
  granular; mixer, amplifier; biquad, state-variable, ladder and FIR filters;
  peak, RMS, FFT256/1024, tone detect, note frequency; SD WAV/RAW, memory and
  queue players, record queue; the SGTL5000 audio shield. Not yet: your own
  `AudioStream` subclasses, wavetable synth, SerialFlash players.
- **USB:** Baseline's USB type is Serial + MIDI + Audio (Teensy's "Serial + MIDI
  + Audio"); keyboard/mouse need a different Baseline.
- **Timing:** `IntervalTimer` (4 PIT channels, shared with `tone()`),
  `elapsedMillis`/`elapsedMicros`. `digitalWriteFast()`, `digitalReadFast()` and
  `digitalToggleFast()` write the pin's GPIO register directly, as on a plain
  Teensy (2 cycles with a constant pin; call `pinMode()` first).
- **Registers:** `#include <imxrt.h>` works; `attachInterruptVector()`,
  `NVIC_*`, `ARM_DWT_CYCCNT` and direct register access too. Baseline's vector
  table is at a fixed address for that.
- **Ports:** `Serial1`-`Serial7` (`Serial8` on the 4.1), `Wire`/`Wire1`/`Wire2`,
  `SPI`. LittleFS is 576 KB on the 4.0 and 3.75 MB on the 4.1; Preferences are
  files in it. microSD: the 4.1's built-in slot (`SD.begin(BUILTIN_SDCARD)`), or SPI.
- **Memory:** sketch globals up to 128 KB (in DTCM; eDMA reaches it), code up to
  384 KB (4.0) or 1 MB (4.1); `malloc` gives the 512 KB OCRAM heap,
  `extmem_malloc` the 4.1's PSRAM (RAM when there is none, as in Teensyduino).
  `FASTRUN`, `DMAMEM`, `FLASHMEM`, `EXTMEM` are accepted and ignored.
- **Teensy names, not ESP32 ones:** `analogWriteFrequency()`/`analogWriteResolution()`
  (not `ledc*`), `extmem_malloc()` (not `ps_malloc`), `tempmonGetTemp()` (not
  `temperatureRead`), `SD.begin(BUILTIN_SDCARD)` on the 4.1 (not `SD_MMC`).
  `esp_*` and `ESP.` don't exist here. Examples: `examples/08_teensy`.
- **Sleep** (`<Snooze.h>`: `Snooze.sleep()`, `deepSleep()`, `hibernate()`) puts the chip in Snooze's low-power WAIT mode until the timer or a pin wakes it;
  deep sleep then restarts the sketch, and RTC memory (1 KB) survives it. USB
  doesn't answer while the chip sleeps; `flick upload` waits for it to wake.
- **No WiFi, BLE or ESP-NOW:** those calls return errors.

## RP2040 and RP2350 (`pro_micro_rp2040`, `thing_plus_rp2040`, `pro_micro_rp2350`, `thing_plus_rp2350`, `rp2350_generic_pico2`, `rp2350_generic_pico2w`)

Baseline for these boards is built on arduino-pico (Earle Philhower's core);
sketches use the same classic Arduino names as on the ESP32.

- **Putting Baseline on:** the first time, hold BOOTSEL while plugging the
  board in (it shows a drive named RP2350 or RPI-RP2), then use the web page's
  Flash button or copy the `.uf2` onto the drive. After that, the Flash button
  and `python flick.py baseline flash` restart the board into that mode
  themselves.
- **Memory:** sketch globals up to 64 KB (RP2350) or 32 KB (RP2040), code up to
  512 KB. On the boards with PSRAM (both RP2350 boards, 8 MB), `malloc()` of
  more than 4 KB is served from PSRAM, as on an ESP32 with PSRAM.
- **Ports:** `Serial1`, `Serial2`, `Wire`/`Wire1` and `SPI` take any pins the
  chip allows for them: Baseline picks the I2C, SPI or UART controller from
  the pins you pass. `analogWrite()`, `tone()` and `Servo` each keep their own
  frequency per pin (the `ledc*` names are Arduino-ESP32's and don't exist
  here). LittleFS is 8 MB; Preferences are files in it. microSD is on SPI.
- **LEDs:** `rgbLedWrite()` and FastLED drive WS2812s through a PIO state
  machine on any pin.
- **WiFi** (Thing Plus - RP2350, on its RM2 radio): `WiFi`, `WiFiClient`,
  `WiFiClientSecure` (checked against Mozilla's CA list, the same as on the
  ESP32), `HTTPClient`, `WebServer`, `WiFiUDP`, `DNSServer`, `PubSubClient`,
  `MDNS`, `ArduinoOTA` and `configTime()`. Differences: `MDNS.queryHost()`
  finds nothing; `configTime()` uses the first two servers; `WiFi.onEvent()`
  reports start, connect, got IP, disconnect, scan done and the access point's
  stations, not every Arduino-ESP32 event. WiFi uploads (`flick upload --host`)
  are served while the sketch calls `loop()` or `delay()`.
- **BLE** (Thing Plus - RP2350): `BLESerial` and the BLE library (server,
  advertising, scan, client), on arduino-pico's BLE library and BTstack.
  Differences: one central at a time, and the board advertises again after
  any disconnect; all services together hold about eight 128-bit
  characteristics; descriptors other than 2901 (user description) and 2902
  aren't supported; one client connection; a remote read returns one packet
  (MTU - 1 bytes, at least 244 once the MTU is negotiated); after
  `BLEDevice::deinit()` the radio stays on.
- **Not here:** ESP-NOW, Bluetooth Classic (`BluetoothSerial`), touch, the DAC, USB device mode,
  and the Arduino-ESP32 names (`esp_*`, `ESP.`, `ps_malloc`,
  `temperatureRead`, `SD_MMC`).
- **Deep sleep** is a timed restart for now; RTC memory (1 KB) survives it.

## Artemis (`redboard_artemis_atp`)

Baseline for these boards is built on SparkFun's Apollo3 Arduino core (Mbed OS
underneath); sketches use the classic Arduino names.

- **Putting Baseline on:** the web page's Flash button or `python flick.py
  baseline flash -t redboard_artemis_atp` write it through the SparkFun
  Variable Loader (SVL) every Artemis board ships with: no button to press.
  SVL stays on the board.
- **Pins** are the pad numbers printed on the board. `A0`-`A5` are the pins
  printed in the Uno's A0-A5 positions (A29 A11 A34 A33 A16 A31: pads 29, 11,
  34, 33, 16, 31), `A6`/`A7` pads 32/35 (`analogRead()` takes any of them by pad
  number too; 12 bits, 0-2.0 V).
- **Memory:** sketch globals up to 64 KB, code up to 128 KB; LittleFS has
  112 KB (Preferences are files in it).
- **Ports:** `Serial1` (UART1; the pins printed TX1 / RX1, pads 24 / 25, by
  default), `Wire` (SCL / SDA, pads 39 / 40, also the Qwiic connector)/`Wire1`
  and `SPI` (SCK / MISO / MOSI, pads 5 / 6 / 7)
  take any pads the chip allows; each of the six IOM blocks is either an I2C
  or an SPI bus, not both. There is no `Serial2`: UART0 is the console.
  `analogWrite()`, `tone()` and `Servo` keep their own frequency per pin (down
  to 3 Hz). `analogWrite()` on a pad without a timer output switches it on at
  128 and above, off below, as a classic Arduino does.
- **The web page** restarts the sketch when it connects: opening a serial port
  in the browser raises DTR, which resets an Artemis (flick doesn't: it opens
  the port with DTR low). Its uploads run at 115200 for the same reason.
- **Deep sleep** waits for the timer or a pin (EXT0), then restarts the sketch
  without resetting the chip: Baseline frees the sketch's heap, closes its
  files and stops its buses, timers and interrupts. RTC memory (1 KB) and
  Preferences survive.
- **Resets:** this chip clears all of RAM at every reset, so RTC memory does
  not survive a restart (an upload, `bl_restart()`) or the reset button.
- **Bluetooth LE** (`BL_HAS_BLE`): `BLESerial`, the `BLEDevice` server,
  scanning and the client, on ArduinoBLE inside Baseline. Up to three
  connections, one of them as a client; centrals share one subscription
  (CCCD) per characteristic; values up to 512 bytes; a service's
  characteristics and descriptors are fixed once it is started; a remote read
  or write is one packet (MTU 242 here, so up to 241 / 239 bytes). Starting
  BLE takes about 3 s (the radio's firmware loads). RSSI values read about
  40 dB higher than a PC's adapter reports for the same devices.
- **Not here (yet):** microSD, WiFi (none on the chip), USB device mode
  (the USB port is a CH340), and the Arduino-ESP32 names (`esp_*`, `ESP.`,
  `ledc*`, `ps_malloc`, `temperatureRead`).

## Differences from plain Arduino

- **BLE `onRead()`** fires after the read has been answered, so set the value ahead of time.
- **BLE descriptor values** are fixed when `addDescriptor()` is called.
- **`BLEClient::getService()`** can't confirm the service exists. `getCharacteristic()` returns `nullptr` if the service or characteristic is missing.
- **A BLE scan** keeps at most 40 devices.
- **`WebServer` handlers** must be plain functions or capture-less lambdas.
- **LittleFS** always uses the `userfs` partition (1.2 MB); at most 8 files or directories can be open at once.
- **Preferences:** the namespace `baseline` is reserved; at most 4 namespaces open at once.
- **PubSubClient** callbacks run inside `mqtt.loop()`, as in the original. The payload buffer has a NUL after the data, so it can be printed as text. `WiFiClientSecure::setInsecure()` is ignored for MQTT (the broker's certificate is always checked).
- **ArduinoOTA** opens Baseline's update port: `flick upload --host` sends sketches and `flick baseline update --host` sends Baseline. The board reboots into the update, so the `onStart/onEnd/onProgress/onError` callbacks are never called. To have the board fetch its own updates, use `BaselineUpdate` (docs/OTA.md).
- **Board pins** (`LED_BUILTIN`, `STAT_LED`, `RGB_BUILTIN`, `SDA`, `SCL`, `A0`, and the `Serial1` defaults) come from the target:
  - ESP32-S3 Thing Plus: LED 0, RGB 46, Qwiic 8/9, `Serial1` on RX 44 / TX 43.
  - Thing Plus C: LED 13, RGB 2, Qwiic 21/22, `Serial1` on RX 16 / TX 17.
- **Serial2** has no default pins on either board: pass `rx` and `tx` to `begin()`.
- **Uploads on the classic ESP32** go through its USB-serial chip at 115200 baud: about 0.3 s for a 2 KB sketch, versus about 0.03 s over the S3's native USB.
- **Timer callbacks** (`Ticker`, `timerAttachInterrupt`, `esp_timer`) run on the sketch task with about 1 ms accuracy, like interrupt handlers. `timerAlarm` works for periods down to 50 µs, but the sketch sees at most one callback per millisecond or so. Ticker callbacks must be plain functions or capture-less lambdas.
- **Deep sleep** reboots on wake: `setup()` runs again. `RTC_DATA_ATTR` variables are not supported; keep data in `bl_rtc_memory()` (1 KB, cleared at power-on). Going to sleep counts as a healthy sketch, so waking never triggers rollback.
- **Touch** values go down when touched on the classic ESP32 and up on the S3; pick the interrupt threshold accordingly.
- **FastLED** runs on Baseline's RMT driver at any output pin (bit-banged on the classic ESP32, with interrupts off on the sketch's core while a frame goes out: about 30 µs per LED). Color palettes, SPI chipsets (APA102, WS2801) and RGBW are not supported; color correction, temperature and dithering are accepted but ignored. `setMaxPowerInVoltsAndMilliamps` uses a simple estimate (20 mA per full channel).
- **Servo** uses 50 Hz, 14-bit LEDC; `write()` takes degrees below 500 and microseconds above.
- **USB device mode** (S3): the first `USB.begin()` with a new set of devices reboots the board into USB device mode (the sketch starts again). Serial, `flick upload` and `flick monitor` keep working through the new USB serial port, whose COM number may change. Crash messages are not shown in this mode (the coredump still records them). Safe mode, installing another sketch, or `flick baseline flash` turn it off. USB descriptor settings (VID/PID, names) are fixed.
- **I2S** uses one port. `available()` reads ahead up to 128 bytes, which waits for samples.
- **microSD** shares the SPI bus with `SPI` on the Thing Plus C.
- **Not supported:** ESP-NOW encryption, `WiFi.scanNetworks(true)` (async; it scans synchronously), HTTP file uploads to `WebServer`, and BLE extended advertising.

- **Characters:** `<ctype.h>` / `<cctype>` (`isdigit`, `isalpha`, `toupper`, ...) and Arduino's `isDigit`, `isAlpha`, `isWhitespace`, `toUpperCase`, ... work on ASCII, as in the "C" locale.

## Not supported yet

- SPIFFS, Arduino's `HTTPUpdate` class (use `BaselineUpdate`, docs/OTA.md), `HTTPClient` streams, USB mass storage, camera
- IPv6, and the `ETH` / `PPP` network interfaces of Arduino-ESP32 3.x
- `rtc_gpio_*` pin isolation for deep sleep, ULP
- Arduino libraries from the Library Manager. A library can be added to Baseline and exposed through the API.

If code uses something on this list, the build fails with "not declared".
Nothing fails silently on the board.
