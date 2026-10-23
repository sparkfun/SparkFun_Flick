# Flick kit

Write Arduino sketches for SparkFun ESP32 boards. They build in about a second and upload in under a second, over USB or WiFi. The board runs **Baseline**: precompiled firmware with WiFi, BLE, storage and the rest of Arduino-ESP32 already inside. Your sketch is only your own code.

## 1. Put Baseline on the board (once)

- **From the browser (easiest):** open the Baseline flasher page, pick your board, and press **Flash** (Chrome or Edge).
- **From this folder:** download `baseline-<your board>.bin` from the release, then run:

  ```
  pip install -r requirements.txt
  python tools/flick.py baseline flash --file baseline-<your board>.bin
  ```

`python tools/flick.py targets` lists the boards.

## 2. Install the compiler (once)

```
pip install -r requirements.txt
python tools/flick.py setup -t <your board>
```

This downloads Espressif's GCC for your board's chip (170–670 MB) into `~/.flick`. If you already have Arduino-ESP32 installed, `flick` uses that compiler and you can skip this step.

## 3. Build and upload

```
python tools/flick.py upload examples/blink -m
```

`-m` shows the serial output; press Ctrl+C to quit. In VS Code, Ctrl+Shift+B does the same for the sketch open in the editor.

## With an AI assistant

Open this folder in Claude Code, VS Code with Copilot, Cursor or similar. `AGENTS.md` (read by Claude Code through `CLAUDE.md`) tells the assistant how sketches are built, what Baseline supports, and its limits. Ask for what you want, for example:

- "Make a sketch that reads the BMI270 IMU on Qwiic and shows the acceleration on the 1.3 inch Qwiic OLED. Install the libraries you need, upload it and check the serial output."
- "Log the fuel gauge's battery voltage to the microSD card every minute, with the time from NTP."
- "Turn examples/wifi_ota into a web page that shows the board's temperature."

The assistant finds and installs Arduino libraries with `python tools/flick.py lib install "<name>"`. You can do the same yourself:

```
python tools/flick.py lib search qwiic oled
python tools/flick.py lib install "SparkFun Qwiic OLED Arduino Library"
```

## More

| | |
|---|---|
| `docs/ARDUINO_COMPAT.md` | Which Arduino APIs work, and their differences |
| `docs/API.md` | The Baseline API itself |
| `docs/OTA.md` | Updating sketches and Baseline over WiFi |
| `secrets.cfg.example` | WiFi credentials for sketches, kept out of your code |
