SparkFun Flick
=====================

Flick is a pre-compiled blob of stuff that gets loaded onto your ESP32 and allows you to *very* quickly (one second) load new code. Let's say you're messing about with WiFi or Bluetooth. You shouldn't have to compile and upload the same stuff over and over again! Life is too short.

The goal of Flick is to reduce the time it takes to compile and upload sketches. Have a look at the comparison table below: Flick (once Baseline is loaded) makes compiling and uploading incredibly fast. In short, Baseline is a precompiled blob of nearly all the functionality of a given processor. Since the user is just adding the if-this-then-that on top, we can leave the vast majority of the code living on the chip and update only the connecting code.

| Sketch | Arduino first compile | Arduino compile | Arduino upload | **Arduino total** | **Baseline total** (compile + upload) | Faster by | Arduino upload size | Baseline upload size |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| blink | 48.1 s | 11.9 s | 8.7 s | **20.6 s** | **0.51 s** | 40x | 274,928 bytes | 1,160 bytes |
| hello_world | 51.1 s | 12.2 s | 7.2 s | **19.4 s** | **0.50 s** | 39x | 275,296 bytes | 1,464 bytes |
| ble_serial | 140.2 s | 13.1 s | 7.8 s | **20.9 s** | **0.51 s** | 41x | 563,648 bytes | 1,520 bytes |
| wifi_http_get | 126.1 s | 15.0 s | 8.6 s | **23.6 s** | **0.59 s** | 40x | 979,008 bytes | 2,660 bytes |

Measured on a SparkFun Thing Plus ESP32-S3 (native USB). On a board with a USB-serial chip, such as the IoT RedBoard - ESP32, Flick's build plus upload takes 0.66–0.71 s for the same four sketches.

How the boards compare (upload speeds, flash left, and what each one can't do): [flick/docs/BOARD_COMPARISON.md](flick/docs/BOARD_COMPARISON.md).

Baseline is not an interpreted language; it's fast machine code all the way down. You just tell the chatbot what you want, it writes the code, compiles it (specifically for Baseline) and loads only the bits that need changing.

Baseline is intended for folks who are just past the beginner's phase of programming a microcontroller. There are a few limitations and quirks to Baseline that users should know about. Generally speaking, everything works transparently but there are a few exceptions listed in [docs/ARDUINO_COMPAT.md](flick/docs/ARDUINO_COMPAT.md).

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
* **Over-the-air updates:** sketches *and Baseline itself*, pushed from the PC (`ArduinoOTA`, `flick ... --host`) or pulled by the board from a web server such as a GitHub release (`BaselineUpdate`). See [docs/OTA.md](flick/docs/OTA.md).
* **Releases:** each release has a download page with a **Flash** button per board, the over-the-air updates, and a sketch kit (with `AGENTS.md` for AI assistants). See [docs/RELEASING.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/docs/RELEASING.md).
* **Extras:** `String`, `IPAddress`, `printf`/`snprintf`, `random`, `map`, `constrain`, `log_i`/`ESP_LOGI` logging, and `ESP` chip and memory info

Not in the blob yet: USB mass storage and camera. Libraries from the Arduino Library Manager aren't in the blob either; they're compiled into your sketch instead (see [Add a library](#6-add-a-library)).

## How to use Flick

This walks you from a blank board to your own sketch running, using VS Code. You'll press one key to build and upload, and you won't need to type commands.

This folder is all you need, whether you downloaded this repository or the smaller **sketch kit** zip from [the Flick web page](https://sparkfun.github.io/SparkFun_Flick/): both look the same. You'll work in `examples/` and `my_sketches/`; `flick/` holds the tool, the SDK and the board files, and you don't need to open it.

### 1. Put Baseline on the board, and try it (once)

Open **[the Flick web page](https://sparkfun.github.io/SparkFun_Flick/)** in Chrome or Edge, plug in the board, pick it from the list and press **Flash**. You'll only do this again when you want a newer Baseline. On an RP2040 or RP2350 board, hold the **BOOTSEL** button while you plug it in the first time; later updates don't need it. The same page runs the examples on your board with nothing installed: start with **01_hello_flick**.

### 2. Open this folder in VS Code

Install **[Python 3](https://www.python.org/downloads/)** (on Windows, tick "Add python.exe to PATH") and **[VS Code](https://code.visualstudio.com/)**. Unzip the kit (or this repository), and use **File → Open Folder...** to open it. The VS Code tasks are already set up.

| Folder | |
|---|---|
| `examples/` | Working sketches, from `01_basics` to `07_updates` ([the list](examples/README.md)) |
| `my_sketches/` | Your sketches |
| `libraries/` | Arduino libraries you install with `flick.py lib install` (made the first time) |
| `flick/` | The tool, the SDK and the board files. You don't need to open it. |

### 3. Install the compiler (once)

With the board plugged in, press **Ctrl+Shift+P**, choose **Tasks: Run Task**, then **Setup: install the compiler (once)**. It downloads the compiler for your board's chip into `~/.flick`: Espressif's for the ESP32 boards (170 to 670 MB), arduino-pico's for the RP2040 and RP2350 boards (130 MB), and Teensyduino's compiler, core and libraries for the Teensy 4.x boards (400 MB). If you already have the Arduino IDE with your board's core installed (ESP32, Raspberry Pi Pico/RP2040/RP2350 or Teensy), Flick can use that compiler instead and you can skip this step.

### 4. Try an example

Open `examples/01_basics/01_hello_flick/01_hello_flick.ino` and press **Ctrl+Shift+B**. The LED blinks, and `Hello flick!` scrolls past.

The task builds the sketch for whichever board is plugged in, uploads it, and opens the serial output in VS Code's terminal panel. It takes about a second. Change the message and press **Ctrl+Shift+B** again: it stops the monitor, re-uploads and starts watching again.

### 5. Make your own sketch

Run the task **New sketch** and give it a name, for example `plant_monitor`. It creates `my_sketches/plant_monitor/plant_monitor.ino`:

```cpp
void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
```

Open it and press **Ctrl+Shift+B**. This is ordinary Arduino code, and you don't need `#include <Arduino.h>`. Use the board's pin names (`LED_BUILTIN`, `RGB_BUILTIN`, `SDA`, `SCL`, `A0`, ...) rather than numbers, so the sketch works on any board. Extra `.cpp`, `.c` and `.h` files in the folder are built too.

### 6. Add a library

Flick uses the libraries in your Arduino sketchbook. To add one, install it the way you always have: in the Arduino IDE, go to **Tools → Manage Libraries...**, search (for example "SparkFun Qwiic OLED") and press **Install**. Then `#include` it and press **Ctrl+Shift+B**. Without the Arduino IDE:

```
python flick.py lib search qwiic oled
python flick.py lib install "SparkFun Qwiic OLED Arduino Library"
```

Most I2C and SPI sensor and display libraries, ArduinoJson and the like work. A library that calls ESP-IDF directly or uses C++ exceptions won't build. When a build fails with `undefined reference`, the call isn't in Baseline; [docs/ARDUINO_COMPAT.md](flick/docs/ARDUINO_COMPAT.md) lists what is.

### 7. Or ask an AI assistant to do it

Open this folder in Claude Code, Copilot, Cursor or similar. `AGENTS.md` tells the assistant how Flick works, so you can ask for the whole job:

* *"Make a sketch that shows the temperature from the Qwiic TMP117 on the 1.3 inch Qwiic OLED. Install the libraries you need, upload it and check the serial output."*
* *"Add the SparkFun Qwiic OLED library and change plant_monitor to print the uptime on the display."*
* *"Log the fuel gauge's battery voltage to the microSD card every minute, with the time from NTP."*
* *"Turn examples/07_updates/01_wifi_ota into a web page that shows the board's temperature."*

The assistant finds, installs, builds, uploads and reads the serial output by itself.

### The other VS Code tasks

Press **Ctrl+Shift+P**, choose **Tasks: Run Task**, then pick one:

| Task | What it does |
|---|---|
| Sketch: build + upload + monitor | The same as Ctrl+Shift+B |
| Sketch: build only | Checks that the open sketch compiles, without a board |
| New sketch | Creates a sketch in `my_sketches/` |
| Set WiFi network | Saves the network name and password the WiFi sketches use |
| Serial monitor | Shows the board's serial output without uploading |
| Setup: install the compiler (once) | Step 3 |
| Baseline: reboot into safe mode | Restarts the board without running the sketch, if a sketch misbehaves |

### WiFi passwords

Run the task **Set WiFi network** (or `python flick.py wifi "network" "password"`) once. It saves them in `secrets.cfg` in the `.flick` folder in your home folder, outside your sketches. Each `WIFI_SSID = MyNetwork` line becomes `#define WIFI_SSID "MyNetwork"` in every sketch, so your password never ends up in your code or in git. Other keys: `python flick.py secrets --set API_KEY=...`.

### When things go wrong

* **Don't upload with the Arduino IDE, esptool or PlatformIO.** They erase Baseline, and you'd have to flash it again (step 1).
* **A new sketch crashes?** The board puts the previous sketch back. Fix the code and press Ctrl+Shift+B.
* **The board restarts every few seconds?** `loop()` must return, and nothing may wait for more than 5 seconds without a `delay()`, or the watchdog resets the board.
* **The upload can't find the board?** Check the USB cable, then flash Baseline again (step 1).

### From the command line

Everything the tasks do, you can run yourself from this folder:

```
python flick.py setup                                     install the compiler (once)
python flick.py upload examples/01_basics/01_hello_flick -m  build, upload, then show the serial output (Ctrl+C quits)
python flick.py new plant_monitor                         create my_sketches/plant_monitor
python flick.py build my_sketches/plant_monitor -t T      build only, for board T
python flick.py monitor                                   serial console
python flick.py info                                      Baseline version, board, running sketch, last crash
python flick.py baseline flash --file baseline-<board>.bin   flash Baseline without the browser (pip install esptool first)
python flick.py upload my_sketches/plant_monitor --host baseline   upload over WiFi (see flick/docs/OTA.md)
```

`python flick.py --help` lists the rest.

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

Flick's own code is in the public domain ([the Unlicense](LICENSE)). The Baseline firmware images also contain ESP-IDF, Arduino-ESP32, arduino-pico and other projects under their own licenses: see [THIRD_PARTY_NOTICES.md](flick/THIRD_PARTY_NOTICES.md).

## Working on Flick itself

Baseline's source, the ESP-IDF project, the API table and the build scripts are in `flick/` in [the repository](https://github.com/sparkfun/SparkFun_Flick) (the kit leaves them out). Start with [flick/targets/README.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/targets/README.md) (chips and boards), [flick/docs/API.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/docs/API.md) (the API table) and [flick/TESTING.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/TESTING.md); releases are described in [flick/docs/RELEASING.md](https://github.com/sparkfun/SparkFun_Flick/blob/main/flick/docs/RELEASING.md).
