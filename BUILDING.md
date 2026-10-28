# Building Felucca-OPX

The build is upstream Felucca's; the fork adds the OPX engine (`FELUCCA_OPX`, below) and the release process at the
end. Building needs a Linux x86-64 machine or Docker, but nobody has to build locally: every push to `opx-engine`
is built and tested by GitHub Actions (`.github/workflows/build.yml`), and the package is kept as the run's
`firmware` artifact.

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.2.0` makes a release build: the version string `v0.2.0` (the identity is `FM-1_9001`, as every
build's); the package is `build/felucca-opx-0.2.0.fwsc`, and
`build/release-0.2.0/` holds what a release ships: the package, the app
(`felucca-opx-0.2.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).
`FELUCCA_OPX_ONLY=1 ./build.sh --release 0.2.0`, run after it, adds Felucca-OPX Solo's (`felucca-opx-solo-0.2.0.fwsc`,
identity `FM-1_9002`, version `v0.2.0-opx-solo`) to the same folder; CI builds both for a release.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/src/usb.c`) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 1 | the SLICE engine |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |
| `FELUCCA_CPU` | (unset) | OPX fork: the core to compile for. Unset: the compiler's generic one, as upstream. `r3`: the AC79's own (as JieLi's SDK), with its hardware floating point (`FELUCCA_HWFLOAT`, a float row in the benchmark). An experiment: no gain measured (docs/OPX_PLAN.md, Cost) |
| `FELUCCA_OPX` | 1 | the OPX engine (engine 15), its pages and its flash region (`firmware/src/opx_map.h`); `0` builds upstream Felucca's engine set and flash map |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build/emu 8790     # open http://localhost:8790/
```

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates), TRS MIDI and the user sample slots. A build of it is on the site:
<https://thomasgregorymarsden-code.github.io/Felucca-opx/webapp/try/>.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://thomasgregorymarsden-code.github.io/Felucca-opx/>. It installs the latest published release; the latest
build of `opx-engine` is at <https://thomasgregorymarsden-code.github.io/Felucca-opx/dev/>.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Versions and releases

Felucca-OPX has its own version numbers, starting at **`0.0.1`** (the first release, on Felucca 1.5.1); 0.x while the
OPX engine is incomplete. Which Felucca release a version is based on is in [CHANGELOG.md](CHANGELOG.md).
Development builds say the next release + `-dev`, e.g. `v0.0.2-dev` (`firmware/src/felucca.c`); raise it after each
release. The package identity is one per firmware, the same for every build: Felucca-OPX `FM-1_9001`, Solo
`FM-1_9002` (numeric: the FM-1 tools read `FM-1_` and digits); releases up to 0.1.0 had Felucca's `FM-1_9XY`. The installers use it to confirm, after an install, that the FM-1 runs the package just written; the
device answers the identity request with it.

To make a release, from the GitHub web page (no local build is needed):

1. Make sure the last run of **Build and publish** on `opx-engine` is green, and that [CHANGELOG.md](CHANGELOG.md)
   has the release's entry.
2. **Releases > Draft a new release**. Tag: `v` + the version, e.g. `v0.0.2`, created on `opx-engine`. Title:
   `Felucca-OPX 0.0.2`. Paste the release's CHANGELOG entry as the description.
3. **Publish release.** The workflow builds `--release` with that version, runs the tests, attaches the package, the
   app, `SHA256SUMS` and the licence files to the release, then runs on `opx-engine` again so that the site's
   installer offers the new release. If the tests fail, nothing is attached: delete the release and its tag, fix,
   and publish again.

A tag that is not `v` + a version `tools/build.py --release` accepts (`X.Y`, `X.Y.Z` or `X.Y.Z.W`, one digit each,
an optional `-suffix` of letters and digits) fails the build.
