# walkman(nw-a30シリーズ)向けfbdoom
多分a-40/50でも動く(未確認)

## 前提
[wlakman one](https://www.mrwalkman.com/p/walkman-one-nw-a30series.html)か[wampy](https://github.com/unknown321/wampy)を導入しておく(adbが生える)  
adbが使える  
linuxを扱える(古いハード向けのクロスコンパイル)  
など

## ほぅー…と…うせ…いt
1. `git clone https://github.com/isanan39s/fbDOOM`
2. [cross-tools/musl-cross](https://github.com/cross-tools/musl-cross)から armv7-unknown-linux-musleabihf.tar.xxを落として展開&パスを通す
2. `make`
2. USBマスストレージモードのwalkmanにできたfbdoomと使うwadをコピー
2. usbマスストレージをオフにしてadb shellで入る
2. `/contents/`から最低限fbdoomを`/contents/`外にコピーする(wadやらスクリプトもやったほうが楽)
2. hagoromo1,2 の順でstop サービス止める `stop hagoromo1`
3. `./fbdoom -iwad doom.wad` enjoy doom!



### 以下gemini4によるreadme
# wmDOOM — fbDOOM for Sony Walkman

A dedicated Sony Walkman port of **fbDOOM**, forked from [mztulip/fbDOOM](https://github.com/mztulip/fbDOOM) (originally [maximevince/fbDOOM](https://github.com/maximevince/fbDOOM)).

Designed to run standalone directly on the Walkman's Linux/Android framebuffer (`/dev/graphics/fb0`, 480×800) and capacitive touchscreen (`/dev/input/event1`, e.g. `himax-hx8526-icx`) with no external keyboard or SDL dependencies.

## Features for Walkman

1. **32 bpp `mmap` rendering & `mtkfb` page-flipping (`fbdoom/i_video_fbdev.c`)**
   - Supports 32 bpp (`RGBA8888` / `BGRA8888`) and 16 bpp `mmap` framebuffers with hardware double buffering (`yres_virtual >= 2 * yres`).
   - Triggers display refresh on MediaTek (`mtkfb`) panels via `FBIOPAN_DISPLAY` + `FBIOPUT_VSCREENINFO` (`FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE`) every frame.
2. **480×800 portrait split-screen layout**
   - **Top (`480×360`)**: DOOM's `320×200` framebuffer scaled cleanly to true 4:3 aspect ratio.
   - **Bottom (`480×440`)**: Crisp on-screen touch controller rendered directly into the framebuffer (redrawn only when button states change, adding zero per-frame overhead).
3. **Multi-touch `evdev` input (`fbdoom/i_touch.c`)**
   - Reads Linux multi-touch events (Type A & Type B) from `/dev/input/event1` and auto-scales touch coordinates via `EVIOCGABS`.
   - Supports simultaneous inputs (e.g. moving/turning on the D-Pad while firing).
   - **Toggleable Sprint (`SHIFT`)**: Tapping `SHIFT` toggles autorun (`RUN:ON` / `RUN:OFF`) and persists across map transitions.
   - **Menu-aware buttons**: `CTRL` and `SPACE` automatically act as `Enter` (or `'y'` on confirmation prompts) while the DOOM menu is active, so the entire game can be navigated by touch alone.

## On-Screen Controls

| Button | In-Game Action | In-Menu Action |
|---|---|---|
| **SHIFT** (`RUN:ON/OFF`) | Toggle sprint (autorun) | — |
| **CTRL** (`FIRE`) | Fire weapon | Confirm / Select (`Enter` / `Y`) |
| **SPACE** | Use (open doors, switches) | Confirm / Select (`Enter` / `Y`) |
| **TAB** | Toggle Automap | — |
| **ESC** | Open Menu | Back / Close Menu |
| **Circular `✕` D-Pad** | Move & Turn (8-way with diagonal forward+turn) | Navigate Menu (4-way) |
| **`1` – `7`** | Switch weapons | — |

## Building

Requires an `armv7-unknown-linux-musleabihf-` cross toolchain (builds a static `armv7-a` hard-float binary by default):

```sh
cd fbdoom
make -j$(nproc)
```

To specify a different toolchain prefix:

```sh
make CROSS_COMPILE=arm-linux-musleabihf- -j$(nproc)
```

## Running on Walkman

1. Copy the built `fbdoom` binary and an IWAD file (e.g. `doom1.wad` or `freedoom1.wad`) to the device.
2. Stop the stock UI process holding `/dev/graphics/fb0` so it does not overwrite the screen.
3. Launch `fbdoom`:

```sh
./fbdoom -iwad freedoom1.wad
```

### Command-Line Options

| Option | Description |
|---|---|
| `-iwad <file>` | Path to IWAD file (`doom1.wad`, `doom.wad`, `freedoom1.wad`, etc.) |
| `-fb <dev>` | Framebuffer device path (default: `/dev/graphics/fb0`, falls back to `/dev/fb0`) |
| `-touch <dev>` | Touchscreen evdev path (default: auto-detects `/dev/input/event1`, etc.) |
| `-singlebuffer` | Disable double buffering and draw into the front buffer |
| `-novsync` | Flip buffers without waiting for `FBIO_WAITFORVSYNC` |
| `-fps` | Print frame rate every 5 seconds |

## License

Released under the **GNU General Public License v2.0** (see [LICENSE](LICENSE) and [README.TXT](README.TXT)).

めっちゃ本家[id-Software/DOOM](https://github.com/id-Software/DOOM)がGPL-v2

