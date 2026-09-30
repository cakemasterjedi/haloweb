# Digital BMW emblem (ESP32-S3 2.8" round display)

Board: [Waveshare ESP32-S3-LCD-2.8C](https://www.waveshare.com/wiki/ESP32-S3-LCD-2.8C).
The display is driven the same way as Waveshare's own demo: same ST7701 start-up
commands over hardware SPI, and ESP-IDF's RGB panel driver with the demo's pins,
timings and bounce buffers (Arduino core 3.x via pioarduino).

Firmware that turns the ESP32-S3 2.8" 480×480 round display board into a
digital roundel for the car. You control it from your phone's browser, so
there's no app to install.

## What it can show

| Mode | What it looks like | Speed slider |
|---|---|---|
| **Roundel** | The classic badge: thin silver rims, glossy black ring with bold upright lettering, blue and white quarters. Colours and lettering are editable; presets Classic, Blackout, Motorsport, Carbon, Gold | – |
| **Spin** | Roundel with the quarters rotating | spin speed / direction |
| **Stripes** | M tri-colour stripes on carbon-fibre twill | 0 = static, otherwise scrolls |
| **Picture** | Photos, GIFs or videos from your phone (10 slots). Fill / Fit / Fit circle, zoom (pinch), drag, rotate | – |
| **Text** | Your own text, `\|` starts a new line (e.g. `M\|POWER`) | – |

All modes also have **brightness** and **rotation** (to level the emblem if it's
mounted at an angle). Settings are saved on the board.

At power-on it plays a **start-up animation**: the ring sweeps in, the
quarters spin into place, a glint crosses the badge, then your selected mode
takes over (can be switched off under *Settings*).

## Power (the Power tab)

- **Normal mode:** optionally turns itself off after 5 min – 4 h, counted from
  power-on or your last change on the phone.
- **Car show mode:** stays on (for 1–24 h, or until the battery is low) with its
  own brightness.
- **Battery protection:** live voltage readout; turns off if the voltage stays
  below your cutoff for 30 s (so cranking dips don't trigger it). After a
  low-voltage shutdown it wakes every 10 minutes and comes back on once the
  battery is above the cutoff + 0.3 V.
- **Voltage source:**
  - *Board LiPo* – a battery on the board's battery connector (GPIO4).
  - *Car battery* – an **INA219** module on the board's I2C connector
    (SDA 15, SCL 7, 3.3 V, GND; VIN+ to the car battery +, VIN- can be left
    unconnected, and the module's GND to the car's ground). Every analog pin that
    works alongside Wi-Fi is taken by the display, so a 12 V battery can't be read
    directly.
  - Calibrate against a multimeter reading from the page.
- **"Off" is deep sleep:** screen, backlight and Wi-Fi off. Wake it with the
  **BOOT** button or by cutting and restoring power. The board still draws a
  little current asleep (regulator, USB chip, LEDs), so for zero drain use an
  ignition-switched supply.

## Using it

1. Power the board. It starts a Wi-Fi network **`BMW-Emblem`**, password **`emblem123`**.
2. Join it from your phone. Most phones then open the control page on their
   own as a "sign in to network" page. If yours doesn't, browse to
   **http://192.168.4.1** (http://emblem.local also works on iPhones and computers, not Android).
3. On Android, when it warns that the network has no internet, choose
   **Stay connected**.
4. Add the page to your home screen so it works like an app.

Under **Settings** you can:

- rename the network or change its password;
- have the board also join another network, such as your phone's hotspot, so
  your phone keeps its internet. The page shows the address it gets there
  (it's also listed as "emblem" among the hotspot's devices). Use that
  address: `emblem.local` doesn't work in Android browsers;
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
- **GIFs** keep their own timing and work in any browser (Firefox, Chrome,
  Safari). Animated WebP needs a browser with ImageDecoder (Chrome, newer
  Firefox); otherwise its first frame is used.
- **Videos** (MP4, WebM, MOV): pick the frame rate (10–25 fps) and length
  (up to 60 s). The browser has to be able to play the video: many phone
  cameras record HEVC (H.265), which Firefox usually can't. Turn off "High
  efficiency video" (Samsung) or pick "Most compatible" (iPhone) in the camera
  settings, or convert the clip to a normal H.264 MP4.
- **Size limits:** animations play straight from storage, so the limit is free
  space. Internal flash (~12 MB) allows up to 8 MB per animation; a micro SD
  card allows up to 32 MB. If a clip is too big, the page lowers the quality,
  then drops frames, until it fits. The line under the slots shows which
  storage is in use and how much is free.
- **Micro SD card:** optional. Insert it (FAT32) *before* powering up; the
  emblem then stores pictures in an `/emblem` folder on the card. Without a
  card it uses the internal flash. Pictures on one aren't copied to the other.

## Hardware notes

- **PSRAM:** the board has octal PSRAM (`memory_type = qio_opi`, as in
  Waveshare's demo). If the serial log ever shows `PSRAM ID read error`, that
  setting is wrong and the display can't start.
- **Serial log:** the USB-C port goes through a CH343 USB-serial chip, so the
  firmware logs to UART0 (`ARDUINO_USB_CDC_ON_BOOT=0`).
- **Hardware check:** `pio run -e diag -t upload` flashes a test that skips
  the display, starts an open `EMBLEM-TEST` network and reports chip, PSRAM
  and I2C details over serial and at http://192.168.4.1.
- **Pins** are in `include/display_config.h`.
- **Backlight / brightness** uses PWM on GPIO6. If your board doesn't have the
  backlight there, add `-DLCD_BL_PIN=-1` to `build_flags` to dim in software.
- **Panel timing** (*Settings* tab): 18 MHz pixel clock by default (Waveshare's
  ESP-IDF demo), plus 30 MHz and 12 MHz options. *Test colours* fills the screen
  red, green, blue, white.
- **Smooth output:** double-buffered (no tearing), and the RGB stream is
  re-synced after flash writes and once a second so the picture can't stay
  shifted if the S3 falls behind (a known ESP32-S3 RGB "drift").
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
src/label_font.h      bold letters for the ring (made by tools/gen_label_font.py)
src/stroke_font.h     vector font for text mode
src/settings.h        settings + defaults
web/index.html        phone control page
include/display_config.h  board pins / display init
tools/                web embedding, mock server, PC preview
```
