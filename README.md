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
| **M 50** | The "50 Years of BMW M" badge (the 1973 BMW Motorsport emblem): a small classic roundel on white enamel with light blue, dark blue and red half circles around it | – |
| **M 50 Spin** | M 50 with the quarters rotating | spin speed / direction |

All modes also have **brightness** and **rotation** (to level the emblem if it's
mounted at an angle). Settings are saved on the board.

- **Colours:** tapping any colour opens a colour wheel (works the same in
  Firefox and Chrome) with brightness and saturation sliders, hex and RGB
  entry, BMW paint colours and your recent colours. The emblem follows as you
  pick; Cancel puts the old colour back.

- **Auto-cycle** (Emblem tab): rotates through the designs you pick (modes and
  picture slots) every 5 s – 10 min. An animation finishes its loop before the
  next design comes on.
- **Smooth transitions:** designs crossfade into each other (Settings).
- **Animation speed:** 25–300 % for uploaded animations (Emblem tab, shown when
  an animation is selected).
- **Special dates** (Emblem tab): a design for certain days every year, e.g.
  1–26 Dec → your Christmas animation (up to 8 dates, presets for common
  holidays). On those days it replaces the normal design and auto-cycle.
- **Start-up** (Settings): the built-in animation (the ring sweeps in, the
  quarters spin into place, a glint crosses the badge), any picture or
  animation slot, or off. Then your selected design takes over.

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
- **Night dimming:** its own brightness between two times (default 19:00 –
  07:00), fading over. The board's clock chip keeps the time while it has
  power; it's set whenever you open the page, and from the internet when it's
  on your hotspot.
- **"Off" is deep sleep:** screen, backlight and Wi-Fi off. Wake it with the
  **BOOT** button or by cutting and restoring power. The board still draws a
  little current asleep (regulator, USB chip, LEDs), so for zero drain use an
  ignition-switched supply.

## Motion sensor (Settings tab)

The board has a motion sensor (QMI8658), used for:

- **Motion effects:** Spin, Stripes and animations speed up (up to 3×) when you
  accelerate or brake. Animations keep playing while driving either way.
- **Double-tap to change design:** tap the badge twice for the next design
  (the next auto-cycle design, or the next mode / picture). Ignored while
  driving. The page shows the last jolt so you can pick the sensitivity.
- **Welcome animation:** when the car has been still for 5 minutes, the next
  jolt (a door or the boot shutting) plays a welcome clip: the built-in
  animation or any picture/animation slot. Pick how big a jolt it needs.
- **Screen off when parked:** after the car has been still for a set time the
  screen goes dark; a jolt wakes it (with the welcome, if on), as does opening
  the page. The board stays on, so use the auto-off timer or battery
  protection for longer stops.
- **Level:** a one-time set-up on the bench (hold it upright → Step 1, turn it
  a quarter turn clockwise → Step 2) teaches it which way is up. Once fitted,
  park on level ground and tap *Level now*; it sets the rotation for you.
  Rotation now turns pictures and text as well as the roundel and stripes.

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

## Android Auto (the car's screen)

`android/` is a small Android app that puts the emblem on the car's screen
through Android Auto: a grid of the designs (with thumbnails of your own
pictures and animations); tap one to show it. **Next** steps to the next
design, the sun button sets the brightness. It talks to the emblem over Wi-Fi
with the same requests as the web page, and finds it by itself on the phone's
hotspot (or at 192.168.4.1 when the phone is on the emblem's Wi-Fi).

It's an Android Auto "IoT" app installed outside the Play Store, so Android
Auto has to be told to show it, once:

1. Install `emblem-auto.apk` on the phone (allow installing from your browser
   or file manager when asked) and open it once; it should say "Emblem found".
2. Android Auto's settings (Settings → Connected devices → Connection
   preferences → Android Auto): scroll down, tap **Version** 10 times and
   allow developer settings.
3. ⋮ menu → **Developer settings** → turn on **Unknown sources**.
4. **Customise launcher**: make sure Emblem is ticked.

Build it with `cd android && ./gradlew assembleRelease` (needs the Android SDK;
the APK lands in `app/build/outputs/apk/release/`). It's signed with
`android/emblem.keystore`, so newer builds install over older ones. The design
icons in `res/drawable-nodpi` are drawn by the emblem's own renderer.

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
  (up to 60 s). Phone videos work straight from the camera in any browser,
  Firefox included: when the browser can't play HEVC (H.265) itself, the page
  decodes it with a WebAssembly decoder that the emblem serves (about 110 KB,
  `web/vendor/hevc`). HDR videos are tone-mapped to normal colours, and
  portrait videos come out upright. That decoding runs on the phone, so a long
  4K clip takes a while to prepare.
- **Size limits:** animations play straight from storage, so the limit is free
  space. Internal flash (~12 MB) allows up to 8 MB per animation; a micro SD
  card allows up to 32 MB. If a clip is too big, the page lowers the quality,
  then drops frames, until it fits. The line under the slots shows which
  storage is in use and how much is free.
- **Micro SD card:** optional. Insert it (FAT32) *before* powering up; the
  emblem then stores pictures in an `/emblem` folder on the card. Without a
  card it uses the internal flash. Pictures on one aren't copied to the other.
- **Screen goes dark while saving:** writing to the internal flash briefly
  stops the chip feeding the screen, which would show as torn blocks, so the
  backlight is off while a picture (or a firmware update) is being saved and
  fades back in with the new picture. With a micro SD card this isn't needed.

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
src/motion.*          motion sensor (acceleration, double-tap, level)
src/rtc_clock.*       clock chip (night dimming)
web/index.html        phone control page
web/vendor/hevc/      HEVC video decoder for the page (hevc.js, MIT, with a tiles fix)
include/display_config.h  board pins / display init
tools/                web embedding, mock server, PC preview
```
