# Releasing Flick

## What users get

| Piece | What it is | Where |
|---|---|---|
| `baseline-<target>.bin` | The full image (bootloader, partition table, Baseline), flashed at 0x0 | Release assets; the **Flash** button on the download page |
| `baseline-<target>.bsu` | The same Baseline as an over-the-air update (docs/OTA.md) | Release assets; the download page |
| `flick-kit-<version>.zip` | Everything for writing sketches (layout below) | Release assets; the download page |
| Download page | 1. one row per board, each with a **Flash** button (ESP Web Tools, Chrome/Edge) and the downloads; 2. the example runner: every example, prebuilt for every board, uploaded over Web Serial (`tools/site/runner.js`); 3. the kit | GitHub Pages |

**The kit's layout.** Its top level holds only what users work with; `flick package` puts everything else in `flick/` and writes `flick/kit.json`, which tells `flick.py` it runs in the kit (`IN_KIT`: secrets, libraries and `my_sketches/` are then one level up).

```
README.md  flick.py  AGENTS.md  CLAUDE.md  .vscode/  secrets.cfg.example  LICENSE  requirements.txt
examples/       01_basics/hello_flick ... 07_updates/  (examples/README.md lists them)
my_sketches/    the user's sketches (flick new NAME puts them here)
flick/          tools/flick.py, tools/tests/, sdk/, chips/, targets/, docs/, THIRD_PARTY_NOTICES.md, kit.json
```

`flick.py` at the top only starts `flick/tools/flick.py`. The files at the top come from `kit/`; the rest from `KIT_FILES` in `tools/flick.py`.

**The example runner** has no WiFi credentials: the release builds the examples without `secrets.cfg` (`build_examples.py --out` always does), and the page lists the ones that need the user's network (`WIFI_SSID` in the source) as "build it from the kit".

**Why both the firmware and a kit:**
- **Baseline alone** only runs sketches. Sketches are compiled against the kit's headers, which are the API. Kit 1.x works with any Baseline 1.x of the same or a newer minor version.
- **The kit alone** cannot replace Baseline: users flash it once (the page, or `flick baseline flash --file`), then update it over the air.

The kit contains no Baseline source and needs no ESP-IDF.
- `flick setup` downloads the sketch compiler (Espressif's GCC, the same release Baseline is built with).
- `flick lib install` installs Arduino libraries from the Library Manager.

## Cutting a release

1. **Set the version:** `BASELINE_VERSION` in `baseline/src/bl_main.h`.
   - Bump the minor or major version for user-visible changes.
   - The ABI version is separate: `api/baseline_api.txt`, locked with `python tools/gen_api.py --lock`.
2. **Commit, tag and push:**

   ```
   git tag v0.2.0
   git push origin main v0.2.0
   ```

3. **`.github/workflows/release.yml` does the rest:**
   - builds Baseline for every target (`baseline.yml`: ESP-IDF 5.5.5 Docker image + Arduino-ESP32 3.3.11);
   - makes the kit (`flick package`);
   - builds every example for every board for the example runner (`tools/ci/build_examples.py --out dist/examples`);
   - creates the GitHub release (a pre-release while the version is 0.x);
   - publishes the download page (`tools/make_site.py`).

The tag must match `BASELINE_VERSION`; the workflow checks. Running the workflow by hand (Actions > Release > Run workflow) rebuilds the page without a release.

## One-time setup on GitHub

- **Pages:** Settings > Pages > Source: **GitHub Actions**.
- **Visibility:** the page and the release files must be reachable without signing in, so the repository must be **public**. The alternative is private Pages on a GitHub Enterprise plan, but the board's `ota_pull_*` cannot download from a private repository's releases either.
- **Board list:** the page lists every target; `"listed": false` in a `target.json` hides one. A board's name links to its `product_url` when it has one.
- **While the repository is private:** a release still builds and publishes the GitHub release (visible to people with access). The page is built as the workflow artifact `download-page` but not deployed.

## Workflows

| Workflow | When | What |
|---|---|---|
| `ci.yml` | Every push and pull request | ABI lock check, generated files current, every example built for one target per chip (`tools/ci/build_examples.py`), kit packaged |
| `baseline.yml` | Changes to the firmware's sources; called by `release.yml` | Baseline for every target (`tools/ci/build_baseline.sh`), one artifact per target |
| `release.yml` | A `v*` tag, or by hand | Everything above, plus the release and the page |

To run a CI build locally (Docker):

```
docker run --rm -v "$PWD:/work" -w /work espressif/idf:v5.5.5 bash tools/ci/build_baseline.sh thing_plus_esp32c6
```

A Linux-built image was flashed to a Thing Plus C6 and passed `flick test` (2026-10-23).

## Before 1.0

A checklist for calling the ABI and the flash layout final: `TESTING.md` ("Not tested on hardware yet" and the issues list).
