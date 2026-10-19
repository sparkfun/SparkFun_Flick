# Writing sketches: classic Arduino names

Baseline Sketcher sketches use ordinary Arduino-ESP32 code. Every call below is
routed through the Baseline firmware on the board, so you never need the `bl_*`
names. Those remain the raw API, listed in [API.md](API.md).

`.ino` files include everything automatically. If code you copy includes a
classic header, it still compiles: these headers live in `sdk/include/compat`
and all lead to Baseline.

- **Core and peripherals:** `<Arduino.h>`, `<SPI.h>`, `<Wire.h>`, `<HardwareSerial.h>`, `<FunctionalInterrupt.h>`
- **Storage:** `<Preferences.h>`, `<LittleFS.h>`, `<FS.h>`
- **WiFi and networking:** `<WiFi.h>`, `<WiFiClient.h>`, `<WiFiClientSecure.h>`, `<WiFiServer.h>`, `<WiFiUdp.h>`, `<Network*.h>`, `<HTTPClient.h>`, `<WebServer.h>`, `<ESPmDNS.h>`, `<ArduinoOTA.h>`, `<PubSubClient.h>`
- **BLE:** `<BLEDevice.h>`, `<BLEServer.h>`, `<BLEUtils.h>`, `<BLE2902.h>`, `<BLE2901.h>`, `<BLEScan.h>`, `<BLEClient.h>`, `<BLEAdvertisedDevice.h>`, `<BLEAdvertising.h>`, `<BLECharacteristic.h>`, `<BLEService.h>`, …
- **ESP-IDF:** `<esp_now.h>`, `<esp_wifi.h>`, `<esp_system.h>`, `<esp_log.h>`, `<esp_random.h>`

## What works

| Area | Classic names |
|---|---|
| Pins | `pinMode`, `digitalWrite`, `digitalRead`, `analogRead`, `analogReadMilliVolts`, `analogWrite`, `rgbLedWrite`, `HIGH/LOW/INPUT/OUTPUT/INPUT_PULLUP/...`, `LED_BUILTIN`, `STAT_LED`, `RGB_BUILTIN`, `A0`, `SDA`, `SCL` |
| Interrupts | `attachInterrupt`, `attachInterruptArg`, `detachInterrupt`, `digitalPinToInterrupt`, `RISING/FALLING/CHANGE/ONLOW/ONHIGH`, `IRAM_ATTR` (accepted), `noInterrupts/interrupts` (no-ops, see below) |
| Time | `millis`, `micros`, `delay`, `delayMicroseconds`, `yield` |
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
| LittleFS | `LittleFS.begin(formatOnFail)/end/format/totalBytes/usedBytes/open/exists/remove/rename/mkdir/rmdir`; `File`: `print/write/read/readBytes/peek/available/seek/position/size/name/path/isDirectory/openNextFile/rewindDirectory/getLastWrite/flush/close`; `fs::FS`, `fs::File`, `FILE_READ/WRITE/APPEND` |
| mDNS | `MDNS.begin/end/addService/addServiceTxt/queryHost/queryService/hostname(i)/address(i)/port(i)` |
| MQTT | `PubSubClient`: `setServer/setCallback/setClient/setKeepAlive/setSocketTimeout/setBufferSize`, `connect` (all forms, with will and credentials), `publish/publish_P/beginPublish/endPublish`, `subscribe/unsubscribe`, `loop/connected/state/disconnect`, `MQTT_*` states; TLS with `WiFiClientSecure` |
| WiFi upload | `ArduinoOTA.setHostname/setPassword/setPort/begin/handle/end`; then `bsk upload <sketch> --host NAME` |
| ESP-NOW | `esp_now_init`, `esp_now_register_recv_cb`, `esp_now_register_send_cb` (both the IDF 5 and the older callback forms), `esp_now_add_peer`, `esp_now_del_peer`, `esp_now_is_peer_exist`, `esp_now_send` |
| BLE | `BLEDevice`, `BLEServer`, `BLEService`, `BLECharacteristic` (read/write/notify/indicate, `setValue/getValue/getData`), `BLEDescriptor`, `BLE2902`, `BLE2901`, `BLEAdvertising`, `BLEAdvertisementData`, `BLEScan` (blocking and background), `BLEAdvertisedDevice`, `BLEScanResults`, `BLEClient`, `BLERemoteService`, `BLERemoteCharacteristic` (read/write/`registerForNotify`), `BLEUUID`, `BLEAddress`, and all the callback classes |
| BLE serial | `BLESerial`: a ready-made BLE serial port (Nordic UART) |
| System | `ESP.getFreeHeap/getHeapSize/getMinFreeHeap/getMaxAllocHeap/getPsramSize/getFreePsram/getFlashChipSize/getCpuFreqMHz/getChipModel/getChipCores/getEfuseMac/restart`, `getCpuFrequencyMhz`, `esp_restart` |
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
- **Serial2** has no default pins on this board: pass `rx` and `tx` to `begin()`.
- **Not supported:** ESP-NOW encryption, `WiFi.scanNetworks(true)` (async; it scans synchronously), HTTP file uploads to `WebServer`, and BLE extended advertising.

## Not supported yet

- SPIFFS, SD cards, `Update` (firmware OTA)
- I2S, touch, deep sleep, `esp_timer`
- DNSServer (captive portal)
- `<ctype.h>` macros (`isdigit`, ...)
- Arduino libraries from the Library Manager. A library can be added to Baseline and exposed through the API.

If code uses something on this list, the build fails with "not declared".
Nothing fails silently on the board.
