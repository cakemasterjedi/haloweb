# Digital BMW emblem (ESP32-S3 2.8" round display)

Board: [Waveshare ESP32-S3-LCD-2.8C](https://www.waveshare.com/wiki/ESP32-S3-LCD-2.8C).
The display start-up sequence and timings come from Waveshare's own demo code.

Firmware that turns the ESP32-S3 2.8" 480×480 round display board into a
digital roundel for the car. You control it from your phone's browser, so
there's no app to install.

## What it can show

| Mode | What it looks like | Speed slider |
|---|---|---|
| **Roundel** | Classic roundel with chrome rims. Colours and ring lettering are editable. Presets: Classic, Blackout, Motorsport, Carbon, Gold | – |
| **Spin** | Roundel with the quarters rotating | spin speed / direction |
| **M Stripes** | Tri-colour stripes on a carbon-weave background | 0 = static, otherwise scrolls |
| **Colour** | Solid colour | 0 = steady, otherwise breathes |
| **Picture** | Photos, GIFs or videos from your phone (10 slots). Fill / Fit / Fit circle, zoom (pinch), drag, rotate. Animations play at up to ~15 fps | – |
| **Text** | Your own text, `\|` starts a new line (e.g. `M\|POWER`) | – |

All modes also have **brightness** and **rotation**. Use rotation to level the
emblem if the board ends up mounted at an angle. Settings are saved on the
board and come back after power-up, and the roundel spins up when it boots.

## Using it

1. Power the board. It starts a Wi-Fi network **`BMW-Emblem`**, password **`emblem123`**.
2. Join it from your phone. Most phones then open the control page on their
   own as a "sign in to network" page. If yours doesn't, browse to
   **http://192.168.4.1** (or http://emblem.local).
3. On Android, when it warns that the network has no internet, choose
   **Stay connected**.
4. Add the page to your home screen so it works like an app.

Under **Wi-Fi & system** you can:

- rename the network or change its password;
- have the board also join another network, such as your phone's hotspot, so
  your phone keeps its internet. The page then shows the emblem's address on
  that network;
- install firmware updates over Wi-Fi, so you don't need to reach the USB port
  once it's in the car.

## Building & flashing

This is a [PlatformIO](https://platformio.org/) project. It works from VS Code
with the PlatformIO extension, or from the command line:

```bash
pio run -t upload        # build + flash over USB
pio device monitor       # serial log (shows the IP address)
```

The phone page lives in `web/index.html`. Every build embeds it into the
firmware automatically (`tools/embed_web.py` writes `include/web_ui.h`). After
the first USB flash, later updates can go over Wi-Fi: upload
`.pio/build/esp32-s3-display/firmware.bin` from the page.

### Working on it without the board

```bash
python3 tools/mock_server.py          # phone UI against a fake emblem: http://localhost:8000
g++ -O2 -std=c++17 -Isrc tools/host_preview.cpp src/renderer.cpp -o preview && ./preview .
                                      # renders every mode to .ppm images
```

## Pictures & animations

- **Photos** are stored at full quality (480×480).
- **GIFs / animated WebP** keep their own timing. The phone needs a browser that
  can read them frame by frame (Chrome / Android); other browsers use the first
  frame.
- **Videos** (MP4, WebM, MOV): pick the frame rate (10–25 fps) and length
  (up to 15 s).
- **Size limits:** animations play straight from storage, so the limit is free
  space. Internal flash (~12 MB) allows up to 8 MB per animation; a micro SD
  card allows up to 32 MB. If a clip is too big, the page lowers the quality,
  then drops frames, until it fits. The line under the slots shows which
  storage is in use and how much is free.
- **Micro SD card:** optional. Insert it (FAT32) *before* powering up; the
  emblem then stores pictures in an `/emblem` folder on the card. Without a
  card it uses the internal flash. Pictures on one aren't copied to the other.

## Hardware notes

- **PSRAM:** this board has *quad* (QSPI) PSRAM, so `platformio.ini` uses
  `memory_type = qio_qspi`. With `qio_opi` it doesn't boot at all: no screen,
  no Wi-Fi, no serial output.
- **Hardware check:** `pio run -e diag -t upload` flashes a test that skips
  the display, starts an open `EMBLEM-TEST` network and reports chip, PSRAM
  and I2C details over serial and at http://192.168.4.1.
- **Pins** are in `include/display_config.h`.
- **Backlight / brightness** uses PWM on GPIO6. If your board doesn't have the
  backlight there, add `-DLCD_BL_PIN=-1` to `build_flags` to dim in software.
- **Panel type:** the default is Waveshare's own setup for this board. If the
  screen stays black or the colours look wrong, pick another panel type under
  *Wi-Fi & system* on the phone page.
- **Power in the car:** use a 12 V → 5 V USB buck converter on a switched
  (ignition) feed so it doesn't drain the battery. Fuse it.
- **Mounting:** the display isn't weatherproof. Behind a clear, sealed cover
  in the grille or boot, or inside the cabin (steering wheel, dash), is much
  safer than an exposed badge position. Direct sunlight will wash it out.
- **Road use:** rules on lit or animated badges vary by country and state.
  Check yours before driving with it lit.

## Project layout

```
src/main.cpp          Wi-Fi, web server, storage, display output
src/renderer.*        draws every mode into a frame buffer (portable C++)
src/stroke_font.h     vector font for the ring lettering and text mode
src/settings.h        settings + defaults
web/index.html        phone control page
include/display_config.h  board pins / display init
tools/                web embedding, mock server, PC preview
```
