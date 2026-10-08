# G30 trunk emblem build (housing, wiring, install)

A slim, perfectly round housing that puts a 2.8" round ESP32-S3 touchscreen where the 74 mm trunk roundel sits on a G30 5 Series (made for a 2019 M550i). A 3" acrylic lens covers the whole screen flush with the rim. A curved cap hides the display's cable tab, so from the front you see a clean circle.

A thin printed **back plate** (0.6 mm) closes in the back and sits on the paint under the board. Three lugs on its edge reach out under the rim through gaps in the VHB ring and are glued to the rim, so the plate and housing are one unit. The plate's two hollow 4 mm pegs go into the factory emblem grommets, so the display locates on the factory holes and the wires run down through the pegs. **No drilling.** The rim is held on with a 0.64 mm VHB ring. It stands **10.8 mm** off the paint, tape included.

**Start with the build guide:** [`../docs/G30_Emblem_Display_Build_Guide.pdf`](../docs/G30_Emblem_Display_Build_Guide.pdf). It covers parts, printing, the firmware install, wiring and fitting it to the car.

**Fully parametric.** Open [`source/G30_emblem_display_housing.scad`](source/G30_emblem_display_housing.scad) in OpenSCAD, MakerWorld's Parametric Model Maker or the Thingiverse Customizer to change any measurement and export your own STL.

## Files
| File | What it is |
|---|---|
| `print/G30_emblem_display_SLIM_HOUSING.3mf` | Housing, ready to print (back down) |
| `print/G30_emblem_display_SLIM_TAB_CAP.3mf` | Tab cap, ready to print (face up) |
| `print/G30_emblem_display_SLIM_BACK_PLATE.3mf` | Back plate with pegs (flat side down, pegs up) |
| `print/G30_emblem_display_SLIM_BACK_PLATE_CENTER_HOLE.3mf` | Same plate with a center hole instead of pegs, for a drilled 16 mm hole |
| `print/trunk_electronics_tray.3mf` | Clip-in tray for the 12 V → 5 V converter and INA219 (no drilling) |
| `stl/` | The same parts as STL files |
| `source/*.scad` | Editable OpenSCAD source, every dimension a Customizer parameter |
| `VHB_tape_template.svg` | 1:1 cutting template for the tape ring (print at 100 %) |
| [`../docs/`](../docs) | Build guide (PDF), wiring diagram, short print and install notes, preview images |

## Key dimensions (defaults)
- Outside diameter **89.1 mm**, **10.8 mm** off the paint including the 0.64 mm VHB tape
- Display: Waveshare ESP32-S3-Touch-LCD-2.8C, 73.0 mm glass, 70.13 mm image, 33.5 × 3.65 mm cable tab, 8.23 mm deep
- Lens: 3" (76.2 mm) × 1/16" (1.5 mm) clear acrylic disc (Enoin 3" × 0.060")
- Back plate: 0.6 mm thick, 3 lugs (3, 9 and 12 o'clock), pegs 4 mm OD × 10 mm with a 2.7 mm wire hole, **56 mm** apart (same on every G30)

## Customizer parameters
- **Display panel:** glass diameter, cable tab width and reach, module depth, back plate thickness
- **Lens:** diameter, thickness, clearance, adhesive thickness
- **Housing:** open back on/off, VHB thickness, corner wall, edge radius, dome, chamfers
- **Tab cap:** coverage, seam clearance, ribbon pocket
- **Back plate:** pegs or center hole, lug width, reach and positions, peg diameter, wire-hole size, peg length, peg spacing

The outside diameter is calculated automatically from the display and tab size, so a different round display mostly just needs its measurements entered.

## Print
ASA (UV- and heat-resistant), 0.2 mm layers, 4 walls, ironing on top surfaces. **No brim or supports needed:** every part sits flat with full bed contact (the housing's bottom chamfer is only 0.4 mm). Use a clean textured PEI or glue-stick bed at 100–110 °C, enclosure closed, no part cooling for the first 3 layers. Back plate: 3 walls and 100 % infill so the pegs are solid around the wire hole; let it cool on the bed so it stays flat. Do a test fit in PLA first. For a factory finish: sand, prime, gloss black, then 2K clear, with the housing and tab cap painted together.

## Hardware
- 3" × 1/16" (1.5 mm) acrylic disc and clear optical adhesive (LOCA). Don't use B-7000 or other solvent glues on acrylic; they craze it
- 0.64 mm 3M VHB tape (5925), cut into a ring for the rim with [`VHB_tape_template.svg`](VHB_tape_template.svg). Adhesion promoter recommended
- Black neutral-cure silicone (lens edge, tab cap seam, plate lugs to the rim, tape gaps, wires inside the pegs)
- 28 AWG silicone wire, red + black (5 V + GND, right peg) and 30 AWG silicone wire × 4 (3.3 V + GND + SDA + SCL, left peg). Single wires, not sheathed cable, so they fit the 2.7 mm holes
- Small silica gel pack
- 12 V → 5 V converter, INA219, **fuse tap kit with Mini and ATO taps** and 3 A fuses (the G30 box has both sizes; use the one that matches the fuse you tap) on an always-on circuit, ring terminal for chassis ground
- Optional: 12 V kill switch rated 5 A or more (inline rocker on a pigtail is easiest)

## Wiring
Always-on fuse in the trunk fuse box → Mini or ATO fuse tap (to match the fuse) with a 3 A fuse → optional kill switch → INA219 sense terminals → converter. Converter ground goes to a chassis ground in the trunk lid. The converter's 5 V output and ground go up the right peg to the board's 12-pin header: pin 2 is 5 V, pin 1 is GND. The INA219's 3.3 V, GND, SDA and SCL go up the left peg to the board's I2C port. Left and right are as seen from behind the car. See [`../docs/wiring_diagram.html`](../docs/wiring_diagram.html) (download it and open it in a browser).

## Notes
- **Firmware:** install it from the **[web installer](https://cakemasterjedi.github.io/haloweb/)** (Chrome or Edge, USB-C, one click). The first time it powers up it shows its own Wi-Fi name (`EMBLEM-xxxx`) and a temporary password on the screen; join it on your phone and choose your own password when the page asks.
- **Running with the car off:** the display runs from an always-on fuse. On the control page's Power tab, set the voltage source to Car battery (INA219) and turn on battery protection (cutoff about 12.2 V).
- **Auto-off: leave it on Never** (Power tab → Normal → Turn off after) unless you fitted a kill switch. Once auto-off turns the emblem off, it stays off until the BOOT button on the board is pressed, which you can't reach once it's sealed in, or until a kill switch cuts and restores its power. Battery protection is different: after a low-voltage shutdown it checks every 10 minutes and comes back on by itself once the battery recovers. Use the kill switch if the car sits for weeks.
- Make sure the firmware and the phone control page work before sealing it up. The USB port isn't reachable once it's assembled; later updates go over Wi-Fi from the control page (Settings → Firmware).
- If you'd rather not use the pegs, the same housing can go on tape alone with the wires through a single 16 mm drilled hole (see Appendix A of the guide; print `print/G30_emblem_display_SLIM_BACK_PLATE_CENTER_HOLE.3mf`).
- Rules on lit or animated displays facing backward vary by state and country. Check yours.
- Unofficial fan project, not affiliated with or endorsed by BMW.
