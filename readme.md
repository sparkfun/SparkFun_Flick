You're messing about with WiFi or Bluetooth. You shouldn't have to compile and upload the same stuff over and over again! Life is too short.

The goal of Baseline is to reduce the time it takes to compile and upload sketches. Have a look at the comparison table below: Baseline makes compiling and uploading incredibly fast. In short, Baseline is a precompiled blob of nearly all the functionality of a given processor. Since the user is just adding the if-this-then-that on top, we can leave the vast majority of the code living on the chip and update only the connecting code.

| Sketch | Arduino first compile | Arduino compile | Arduino upload | **Arduino total** | **Baseline total** (compile + upload) | Faster by | Arduino upload size | Baseline upload size |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| blink | 48.1 s | 11.9 s | 8.7 s | **20.6 s** | **0.51 s** | 40x | 274,928 bytes | 1,160 bytes |
| hello_world | 51.1 s | 12.2 s | 7.2 s | **19.4 s** | **0.50 s** | 39x | 275,296 bytes | 1,464 bytes |
| ble_serial | 140.2 s | 13.1 s | 7.8 s | **20.9 s** | **0.51 s** | 41x | 563,648 bytes | 1,520 bytes |
| wifi_http_get | 126.1 s | 15.0 s | 8.6 s | **23.6 s** | **0.59 s** | 40x | 979,008 bytes | 2,660 bytes |

Measured on a SparkFun ESP32-S3 Thing Plus over USB, Arduino-ESP32 3.3.11, Windows 11. *Arduino first compile* is a clean build with nothing cached. *Arduino compile* is the everyday case where only your sketch changed. *Arduino total* is that compile plus the upload. On the Baseline side, the upload itself takes about 0.03 s; nearly all of the half second is the compiler. Full build numbers for every example are in [docs/BUILD_RESULTS.md](docs/BUILD_RESULTS.md).

Baseline is not an interpreted language; it's fast machine code all the way down. You just tell the chatbot what you want, it writes the code, compiles it (specifically for Baseline) and loads only the bits that need changing.

Baseline is intended for folks who are just past the beginner's phase of programming a microcontroller. There are a few limitations and quirks to Baseline that users should know about. Generally speaking, everything works transparently but there are a few exceptions listed in [docs/ARDUINO_COMPAT.md](docs/ARDUINO_COMPAT.md).

The Baseline 'blob' includes the following libraries and features. If your code needs something more, ask the chatbot to update Baseline with the new bits and re-flash the target. Your future compilations and uploads will then skip any redundant compilation or uploading of bytes.

* **Pins:** `pinMode`, `digitalWrite`, `digitalRead`, `analogRead` (ADC), `analogReadMilliVolts`, `analogWrite` (PWM), `rgbLedWrite` (the WS2812 RGB LED)
* **Interrupts:** `attachInterrupt`, `attachInterruptArg`, `detachInterrupt`
* **Touch:** `touchRead`, `touchAttachInterrupt` (touch pins as buttons)
* **PWM, tones and servos:** `ledcAttach`/`ledcWrite`, `tone`/`noTone`, `Servo`
* **LED strips:** FastLED (`CRGB`, `CHSV`, `FastLED.addLeds<WS2812B, PIN, GRB>()`, `fill_rainbow`, ...) on any pin
* **Audio:** I2S (`I2SClass`) for amplifiers and microphones
* **Time:** `millis`, `micros`, `delay`, `delayMicroseconds`, plus the real date and time from the internet (`configTime`, `getLocalTime`, `strftime`)
* **Timers:** `Ticker`, hardware timers (`timerBegin`, `timerAlarm`), `esp_timer`
* **Sleep:** deep and light sleep with timer, button and touch wake-ups
* **Serial:** `Serial` (the USB port), plus `Serial1` and `Serial2` (hardware UARTs)
* **I2C:** `Wire` (the Qwiic connector)
* **SPI:** `SPI`, `SPISettings`
* **WiFi:** connect to a network, be an access point, scan, static IP, hostname
* **Networking:** `WiFiClient`, `WiFiServer`, `WiFiUDP`, `WiFiClientSecure` (TLS with a built-in certificate bundle), `HTTPClient`, `WebServer`, `MDNS`, `DNSServer` (captive portals)
* **MQTT:** `PubSubClient`
* **ESP-NOW:** `esp_now_init`, `esp_now_send`, `esp_now_add_peer`, send and receive callbacks
* **BLE:** the Arduino BLE library (`BLEDevice`, `BLEServer`, `BLECharacteristic`, `BLEScan`, `BLEClient`, advertising and all the callbacks), plus `BLESerial`, a ready-made BLE serial port
* **Bluetooth Classic** (classic ESP32): `BluetoothSerial`, a serial port your phone or PC pairs with
* **USB device** (ESP32-S3): the board becomes a USB keyboard, mouse, gamepad, media remote or MIDI instrument
* **Storage:** `Preferences` (settings that survive power cycles), `LittleFS` (on-board files) and microSD cards (`SD`, `SD_MMC`)
* **Battery:** the LiPo fuel gauge (`SFE_MAX1704X`): voltage, percent charged, charge rate
* **Over-the-air updates:** sketches *and Baseline itself*, pushed from the PC (`ArduinoOTA`, `flick ... --host`) or pulled by the board from a web server such as a GitHub release (`BaselineUpdate`). See [docs/OTA.md](docs/OTA.md).
* **Releases:** each release has a download page with a **Flash** button per board, the over-the-air updates, and a sketch kit (with `AGENTS.md` for AI assistants). See [docs/RELEASING.md](docs/RELEASING.md).
* **Extras:** `String`, `IPAddress`, `printf`/`snprintf`, `random`, `map`, `constrain`, `log_i`/`ESP_LOGI` logging, and `ESP` chip and memory info

Not in the blob yet: USB mass storage, camera, and libraries from the Arduino Library Manager.

## What's an ABI?

An ABI (Application Binary Interface) is like an API, but for code that's already been compiled.

An API is the promise you code against: "there's a function called `digitalWrite`, and it takes a pin and a level." An ABI is the same promise once everything is machine code: exactly where to find that function, and how to hand it its arguments.

That matters here because your sketch and Baseline are compiled separately. Your sketch never knows where `digitalWrite` actually lives inside Baseline. Instead, when your sketch starts, Baseline hands it a table of function pointers, and your sketch is compiled to say "call the 19th entry in the table." That table is the ABI.

The rule that keeps it all working is simple: **the table only ever grows.** New functions get added to the end, and existing entries never move or change. That way a sketch you compiled months ago still runs on a newer Baseline.

The version number tells you where things stand. In ABI **1.6**:

* **1** only changes if an existing entry ever has to change. Every sketch would need recompiling, so we try hard never to do that.
* **6** goes up each time a batch of new functions is added. Version 1.6 added microSD, sleep, timers, NTP time, touch, I2S, LED strips, servos, captive portals and USB devices.

Every sketch records the ABI it was built for. If you load a sketch onto a Baseline that's too old for it, Baseline politely refuses to run it instead of crashing. Re-flash Baseline and you're back in business.

## License

Flick's own code is in the public domain ([the Unlicense](LICENSE)). The Baseline firmware images also contain ESP-IDF, Arduino-ESP32 and other projects under their own licenses: see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
