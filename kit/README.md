# Flick

Write Arduino sketches for SparkFun ESP32 boards. They build in about a second and upload in under a second, over USB or WiFi. The board runs **Baseline**, precompiled firmware with WiFi, BLE, storage and the rest of Arduino-ESP32 already inside, so your sketch holds only your own code.

## Get started

1. **Put Baseline on the board (once).** Open the Flick web page in Chrome or Edge, pick your board and press **Flash**. Then try the examples right on that page.
2. **Install [Python](https://www.python.org/downloads/) and [VS Code](https://code.visualstudio.com/)**, if you don't have them yet. On Windows, tick "Add python.exe to PATH" in the Python installer.
3. **Open this folder in VS Code** (File > Open Folder).
4. **Install the compiler (once).** With the board plugged in, run Terminal > Run Task > **Setup: install the compiler (once)**. It downloads the compiler for your board's chip (about 170–670 MB) into `~/.flick`.
5. **Open `examples/01_basics/hello_flick/hello_flick.ino` and press Ctrl+Shift+B.** The LED blinks, and `Hello flick!` scrolls past in the terminal.

Now change the message, press **Ctrl+Shift+B** again, and the new sketch runs a second later.

## Your own sketches

Run Terminal > Run Task > **New sketch**, give it a name, and open the `.ino` file it creates in `my_sketches/`. Or copy an example into `my_sketches/` and rename the folder and its `.ino` to match.

| Folder | What's in it |
|---|---|
| `examples/` | Working sketches, from `01_basics` to `07_updates`. See `examples/README.md`. |
| `my_sketches/` | Your sketches. |
| `libraries/` | Arduino libraries you install (made the first time). |
| `flick/` | The tool, the SDK, the board files and the docs. You don't need to open it. |

## Libraries

Arduino libraries that use plain Arduino APIs work: most I2C and SPI sensor and display drivers, ArduinoJson, and so on.

```
python flick.py lib search qwiic oled
python flick.py lib install "SparkFun Qwiic OLED Arduino Library"
```

Libraries you installed with the Arduino IDE's Library Manager work too: Flick also looks in its libraries folder.

## With an AI assistant

Open this folder in Claude Code, VS Code with Copilot, Cursor or similar. `AGENTS.md` (read by Claude Code through `CLAUDE.md`) tells the assistant how sketches are built, what Baseline supports, and its limits. Ask for what you want, for example:

- "Make a sketch that reads the BMI270 IMU on Qwiic and shows the acceleration on the 1.3 inch Qwiic OLED. Install the libraries you need, upload it and check the serial output."
- "Log the fuel gauge's battery voltage to the microSD card every minute, with the time from NTP."
- "Turn examples/07_updates/wifi_ota into a web page that shows the board's temperature."

## From the command line

```
python flick.py setup                                     install the compiler (once)
python flick.py upload examples/01_basics/hello_flick -m  build, upload, show the serial output (Ctrl+C quits)
python flick.py new plant_monitor                         start my_sketches/plant_monitor
python flick.py info                                      what the board is running
python flick.py --help                                    everything else
```

<details><summary>Flash Baseline without the browser</summary>

Download `baseline-<your board>.bin` from the release, then run:

```
pip install esptool
python flick.py baseline flash --file baseline-<your board>.bin
```

`python flick.py targets` lists the boards.
</details>

## More

| | |
|---|---|
| `flick/docs/ARDUINO_COMPAT.md` | Which Arduino APIs work, and their differences |
| `flick/docs/API.md` | The Baseline API itself |
| `flick/docs/OTA.md` | Updating sketches and Baseline over WiFi |
| `secrets.cfg.example` | WiFi credentials for sketches, kept out of your code |
