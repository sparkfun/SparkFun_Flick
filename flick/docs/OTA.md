# Over-the-air updates

A board running Baseline can be updated without a USB cable. You can update the sketch, and Baseline itself (the precompiled firmware under every sketch).

There are two ways to deliver an update:

- **Push:** your PC sends the update over WiFi with `flick`. The board must be running a sketch that calls `ArduinoOTA.begin()`.
- **Pull:** the board downloads the update from a web server, such as a GitHub release, your own server, or `python -m http.server` on your PC. The sketch decides when, with `BaselineUpdate`.

| | Push from the PC | Pull by the board |
|---|---|---|
| Sketch | `flick upload my_sketch --host baseline` | `BaselineUpdate.sketchFromUrl(url)` |
| Baseline | `flick baseline update --host baseline` | `BaselineUpdate.baselineFromUrl(url, sketchUrl)` |
| Example | `examples/07_updates/01_wifi_ota` | `examples/07_updates/02_ota_pull_sketch`, `examples/07_updates/03_ota_pull_baseline` |

The same `flick` commands also work over USB: use `-p COMx` instead of `--host`. `flick baseline update` updates Baseline over USB without esptool.

## What gets downloaded

- **Sketches** are the `.flick` files `flick build` writes into Flick's cache folder; `flick build <sketch> -o <folder>/` copies one where you want it.
- **Baseline** updates are `.bsu` files. `flick baseline build -t <target>` writes `dist/<target>/baseline-<target>.bsu` next to the USB image. A `.bsu` holds the Baseline app compressed (about 60% of its size), with the chip, the board (target) and the version in its header.
  - A board refuses a `.bsu` made for another board.
  - Every board takes the same kind of file.

## Two kinds of boards

`BaselineUpdate.mode()` (and `update=` in `flick info`) tells which kind a board is.

**`BL_UPDATE_DUAL`: 8 and 16 MB of flash.**
- **Boards:** IoT RedBoard, DataLogger IoT, Thing Plus C, Thing Plus C6, Thing Plus C5, RTK Postcard and the ESP32-P4 board.
- **How it works:** Baseline has two slots, `baseline_0` and `baseline_1`. The new Baseline is written into the slot that is not running while the sketch keeps running, then the board restarts into it.
- **Rollback:** the new Baseline confirms itself after 10 s. If it resets before then (a crash, a hang, a power cut), the bootloader goes back to the previous Baseline.
- **Kept:** the sketch, its settings and its files.

**`BL_UPDATE_STAGED`: 4 MB of flash.**
- **Boards:** Pro Micro C3, Qwiic Pocket C6 and Thing Plus S3.
- **Why it differs:** there is room for only one Baseline. The update is stored over the sketch slots and LittleFS, and the bootloader installs it on the next start.
- **Safety:** the copy starts again if the power is cut partway through, and the result is checked against the update's SHA-256.
- **Each update erases the sketch and the LittleFS files.** Settings saved with `Preferences` are kept.
  - Baseline first restarts without the sketch ("update mode"), so nothing runs from the area being overwritten.
  - With a pull, give `baselineFromUrl` a sketch URL as well. Baseline then reconnects to WiFi and installs that sketch after the update.
  - With a push, upload a sketch over USB afterwards, since the board is no longer on WiFi.

Both kinds need one USB flash with a Baseline that has these features: `python flick.py baseline flash -t <target>`. On 8/16 MB boards this flash changes the partition table, which erases LittleFS once.

## A pull, step by step

`examples/07_updates/03_ota_pull_baseline` checks a small text file on the server every minute:

```
baseline-version.txt          0.2.0
baseline-<target>.bsu         one per kind of board (from flick/dist/<target>/)
03_ota_pull_baseline.flick    the sketch, for 4 MB boards to reinstall
```

When the server's version is newer than `BaselineUpdate.baselineVersion()`, the sketch calls:

```cpp
BaselineUpdate.baselineFromUrl(UPDATE_URL "/baseline-" BL_TARGET_NAME ".bsu",
                               UPDATE_URL "/03_ota_pull_baseline.flick");
```

On success the board restarts, and the call returns only on failure. `BL_TARGET_NAME` is the board's target name, so one sketch serves every kind of board.

- **GitHub releases work as the server:** downloads follow GitHub's redirect to its file storage over HTTPS.
- **Private repositories don't:** their release files need a token, and the board does not send one.

`examples/07_updates/02_ota_pull_sketch` does the same for the sketch, with a version number in `sketch-version.txt`. A new sketch that crashes on its first run is rolled back to the previous one, as with USB uploads.

## Under the hood (for maintainers)

- **`baseline/src/bl_update.cpp`:** the engine. It handles both modes, update mode, the pulls and the 10 s confirmation. Arduino's `verifyRollbackLater()` is overridden so Baseline confirms itself.
- **`baseline/src/bsu/`:**
  - `bsu_format.h`: the `.bsu` header and the staging record.
  - `bsu_inflate.c`: a raw-deflate decoder that reads its 32 KB window back from flash, so it needs about 6 KB of RAM. The bootloader uses it too.
- **`idf/bootloader_components/bsu_stage`:** the bootloader hook. `flick` builds it only into 4 MB targets, and it adds about 3.3 KB to the bootloader.
- **Loader commands:** `UBEGIN <app|bsu> <size> <sha256>`, `UDATA <offset> <base64>`, `UEND`.
  - `flick` sends the raw app image to two-slot boards and the `.bsu` to staged ones.
  - A staged board first answers that it is restarting into update mode, and `flick` sends again.
  - Over WiFi, `flick` keeps 16 lines in flight.
- **API (ABI 1.7):** `baseline_update_mode`, `ota_pull_sketch`, `ota_pull_baseline`. The SDK wraps them in `BaselineUpdate` (`sdk/include/baseline_net.h`).

Measured on 2026-10-23:

| Board | Transport | Package | Result |
|---|---|---|---|
| IoT RedBoard (dual) | USB at 921600 | 2.1 MB raw image | 35 s (Baseline 0.3.0: 4 lines in flight; 120 s one at a time) |
| IoT RedBoard (dual) | WiFi push | 2.1 MB raw image | 45 s |
| Pro Micro C3 (staged) | native USB push | 1.26 MB `.bsu` | 14 s, then about 30 s for the bootloader's install |
| Pro Micro C3 (staged) | WiFi push | 1.26 MB `.bsu` | 23 s, then about 30 s for the bootloader's install |
| Pro Micro C3 (staged) | GitHub pull | 1.26 MB `.bsu` | 10 s download, then the install and the sketch reinstall |
| Thing Plus C6 (dual) | native USB push | 2.1 MB raw image | 23 s |
| Thing Plus C6 (dual) | GitHub pull | 1.28 MB `.bsu` | 17 s download and inflate into the free slot, sketch running |
