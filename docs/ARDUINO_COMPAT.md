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
| Pins | `pinMode`, `digitalWrite`, `digitalRead`, `analogRead`, `analogReadMilliVolts`, `analogWrite`, `rgbLedWrite`, `HIGH/LOW/INPUT/OUTPUT/INPUT_PULLUP/...`, `LED_BUILTIN`, `STAT_LED`, `RGB_BUILTIN`, `A0`, `SDA`, `SCL` |
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
| Serial | `Serial.begin/print/println/printf/write/read/available/readString/readStringUntil/setTimeout` |
| Hardware UARTs | `Serial1` (default pins RX 44 / TX 43) and `Serial2` (pass pins): `begin(baud, SERIAL_8N1, rx, tx)/end/print/write/read/available/peek/flush`, `SERIAL_5N1..SERIAL_8O2` |
| Text | `String` (concat, `+`, compare, `indexOf`, `substring`, `replace`, `trim`, `toInt`, `toFloat`, ...), `printf`, `snprintf`, `sprintf`, `atoi`, `atof`, `strtol`, `strtod`, `F()` |
| Math | `map`, `constrain`, `min`, `max`, `random`, `randomSeed`, `esp_random` |
| SPI | `SPI.begin/end/beginTransaction/endTransaction/transfer/transfer16/transfer32/transferBytes/writeBytes/setFrequency/setDataMode/setBitOrder`, `SPISettings`, `MSBFIRST`, `SPI_MODE0..3` |
| I2C | `Wire.begin/beginTransmission/write/endTransmission/requestFrom/available/read` |
| WiFi station | `WiFi.begin/status/isConnected/disconnect/config/setHostname/localIP/gatewayIP/subnetMask/dnsIP/macAddress/SSID/RSSI/channel/hostByName`, `WL_*`, `IPAddress` |
| WiFi AP and scan | `WiFi.mode(WIFI_STA/WIFI_AP/WIFI_AP_STA/WIFI_OFF)`, `WiFi.softAP/softAPIP/softAPdisconnect/softAPgetStationNum`, `WiFi.scanNetworks/SSID(i)/RSSI(i)/channel(i)/encryptionType(i)/BSSIDstr(i)/scanDelete`, `WIFI_AUTH_*` |
| TCP | `WiFiClient` (`connect/print/write/available/read/peek/connected/stop/remoteIP`), `WiFiServer` (`begin/accept/available/hasClient`), `NetworkClient/NetworkServer` |
| TLS | `WiFiClientSecure` / `NetworkClientSecure`: `setInsecure`, `setCACert`, or by default Baseline's built-in CA bundle |
| UDP | `WiFiUDP` / `NetworkUDP`: `begin/beginPacket/write/endPacket/parsePacket/available/read/remoteIP/remotePort` |
| HTTP client | `HTTPClient`: `begin`, `addHeader`, `setAuthorization`, `GET/POST/PUT/PATCH/sendRequest`, `getString`, `getSize`, `errorToString`, `HTTP_CODE_*` |
| Web server | `WebServer`: `on`, `onNotFound`, `begin`, `handleClient`, `send`, `send_P`, `sendHeader`, `arg`, `hasArg`, `args`, `argName`, `uri`, `method`, `header`, `collectHeaders`, `client().remoteIP()`, `HTTP_GET/POST/...` |
| Preferences | `begin/end/clear/remove/isKey/getType/freeEntries`, `put/get` for `Char/UChar/Short/UShort/Int/UInt/Long/ULong/Long64/ULong64/Float/Double/Bool/String/Bytes`, `getBytesLength` |
| microSD | `SD.begin()` (the board's slot) or `SD.begin(csPin)` (SPI), `SD_MMC.begin(mount, mode1bit)`, `end/cardType/cardSize/totalBytes/usedBytes`, `CARD_*`; files as below |
| DNS / captive portal | `DNSServer.start(port, domain, ip)/stop/processNextRequest/setErrorReplyCode`, `DNSReplyCode` |
| LittleFS | `LittleFS.begin(formatOnFail)/end/format/totalBytes/usedBytes/open/exists/remove/rename/mkdir/rmdir`; `File`: `print/write/read/readBytes/peek/available/seek/position/size/name/path/isDirectory/openNextFile/rewindDirectory/getLastWrite/flush/close`; `fs::FS`, `fs::File`, `FILE_READ/WRITE/APPEND` |
| mDNS | `MDNS.begin/end/addService/addServiceTxt/queryHost/queryService/hostname(i)/address(i)/port(i)` |
| MQTT | `PubSubClient`: `setServer/setCallback/setClient/setKeepAlive/setSocketTimeout/setBufferSize`, `connect` (all forms, with will and credentials), `publish/publish_P/beginPublish/endPublish`, `subscribe/unsubscribe`, `loop/connected/state/disconnect`, `MQTT_*` states; TLS with `WiFiClientSecure` |
| WiFi upload | `ArduinoOTA.setHostname/setPassword/setPort/begin/handle/end`; then `bsk upload <sketch> --host NAME` |
| ESP-NOW | `esp_now_init`, `esp_now_register_recv_cb`, `esp_now_register_send_cb` (both the IDF 5 and the older callback forms), `esp_now_add_peer`, `esp_now_del_peer`, `esp_now_is_peer_exist`, `esp_now_send` |
| BLE | `BLEDevice`, `BLEServer`, `BLEService`, `BLECharacteristic` (read/write/notify/indicate, `setValue/getValue/getData`), `BLEDescriptor`, `BLE2902`, `BLE2901`, `BLEAdvertising`, `BLEAdvertisementData`, `BLEScan` (blocking and background), `BLEAdvertisedDevice`, `BLEScanResults`, `BLEClient`, `BLERemoteService`, `BLERemoteCharacteristic` (read/write/`registerForNotify`), `BLEUUID`, `BLEAddress`, and all the callback classes |
| BLE serial | `BLESerial`: a ready-made BLE serial port (Nordic UART) |
| Bluetooth Classic | `BluetoothSerial` (SPP): `begin(name)/end/hasClient/connected(timeout)/setPin/print/write/read/available/peek/flush`. Classic ESP32 only; on other chips `begin()` returns false |
| System | `ESP.getFreeHeap/getHeapSize/getMinFreeHeap/getMaxAllocHeap/getPsramSize/getFreePsram/getFlashChipSize/getCpuFreqMHz/getChipModel/getChipCores/getEfuseMac/restart`, `getCpuFrequencyMhz`, `esp_restart` |
| PSRAM | `psramFound`, `ps_malloc`, `ps_calloc`, `ps_realloc` (free with `free`). Plain `malloc`/`new` over 4 KB also go to PSRAM when the board has it. Boards without PSRAM report 0 and `ps_*` return NULL. |
| Logging | `log_e/w/i/d/v`, `ESP_LOGE/W/I/D/V` |

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

## Differences from plain Arduino

- **BLE `onRead()`** fires after the read has been answered, so set the value ahead of time.
- **BLE descriptor values** are fixed when `addDescriptor()` is called.
- **`BLEClient::getService()`** can't confirm the service exists. `getCharacteristic()` returns `nullptr` if the service or characteristic is missing.
- **A BLE scan** keeps at most 40 devices.
- **`WebServer` handlers** must be plain functions or capture-less lambdas.
- **LittleFS** always uses the `userfs` partition (1.2 MB); at most 8 files or directories can be open at once.
- **Preferences:** the namespace `baseline` is reserved; at most 4 namespaces open at once.
- **PubSubClient** callbacks run inside `mqtt.loop()`, as in the original. The payload buffer has a NUL after the data, so it can be printed as text. `WiFiClientSecure::setInsecure()` is ignored for MQTT (the broker's certificate is always checked).
- **ArduinoOTA** uploads *sketches* (with `bsk upload --host`), not firmware images. The board reboots into the new sketch, so the `onStart/onEnd/onProgress/onError` callbacks are never called. Baseline itself is still updated over USB.
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
- **USB device mode** (S3): the first `USB.begin()` with a new set of devices reboots the board into USB device mode (the sketch starts again). Serial, `bsk upload` and `bsk monitor` keep working through the new USB serial port, whose COM number may change. Crash messages are not shown in this mode (the coredump still records them). Safe mode, installing another sketch, or `bsk baseline flash` turn it off. USB descriptor settings (VID/PID, names) are fixed.
- **I2S** uses one port. `available()` reads ahead up to 128 bytes, which waits for samples.
- **microSD** shares the SPI bus with `SPI` on the Thing Plus C.
- **Not supported:** ESP-NOW encryption, `WiFi.scanNetworks(true)` (async; it scans synchronously), HTTP file uploads to `WebServer`, and BLE extended advertising.

## Not supported yet

- SPIFFS, `Update` (firmware OTA), USB mass storage, camera
- `rtc_gpio_*` pin isolation for deep sleep, ULP
- `<ctype.h>` macros (`isdigit`, ...)
- Arduino libraries from the Library Manager. A library can be added to Baseline and exposed through the API.

If code uses something on this list, the build fails with "not declared".
Nothing fails silently on the board.
