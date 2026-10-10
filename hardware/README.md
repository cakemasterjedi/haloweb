# G30 trunk emblem build (housing, wiring, install)

A slim, perfectly round 88 mm housing that sits over the 84 mm trunk badge holder on a G30 5 Series (made for a 2019 M550i) and holds a 2.8" round ESP32-S3 touchscreen. A centred 3" × 1.5 mm acrylic lens sits flush with the rim.

The display goes in **from the back**: the board drops into a wide pocket and the glass stops against a lip at the front. The top of the housing covers the screen's cable tab and ribbon, so there's no separate tab cap and from the front you see a clean circle. To fit the cable tab inside 88 mm, the screen sits 2.65 mm above the housing's centre under the centred lens.

A thin printed **back plate** (0.6 mm) drops into the base, rests on the board's brass studs and sits flush with the base. Its two short solid pegs locate in the factory badge holes, a shallow dome on its paint side fills the 0.86 mm dip in the badge seat, and the wires leave through its centre hole and **one 16 mm hole drilled at the badge centre**. One continuous 0.64 mm VHB ring inside the badge holder seals the housing base and plate edge to the paint. It stands **10.3 mm** off the paint, tape included.

**Start with the build guide:** [`../docs/G30_Emblem_Display_Build_Guide.pdf`](../docs/G30_Emblem_Display_Build_Guide.pdf). It covers parts, printing, the firmware install, wiring, fitting it to the car and the optional heat spreader.

## Files
| File | What it is |
|---|---|
| `print/Halo_88_HOUSING.3mf` | Housing, ready to print (back down) |
| `print/Halo_88_BACK_PLATE.3mf` | Back plate with centre hole and locating pegs (flat side down, pegs up) |
| `print/Halo_88_BACK_PLATE_SPREADER.3mf` | Same plate with a window and recess for the aluminium heat-spreader disc |
| `print/trunk_electronics_tray.3mf` | Clip-in tray for the 12 V → 5 V converter and INA219 (no drilling) |
| `stl/` | The same parts as STL files |
| `source/halo_housing.py` | Generator for the housing and both plates: every dimension is a value at the top |
| `source/trunk_electronics_tray.scad` | Editable OpenSCAD source for the tray |
| `VHB_tape_template.svg` | 1:1 cutting template for the tape ring (print at 100 %) |
| `Halo_Spreader_Disc_Template.svg` | 1:1 cutting template for the 50 mm aluminium disc |
| [`../docs/`](../docs) | Build guide (PDF), wiring diagram, short print and install notes, preview images |

## Key dimensions (defaults)
- Outside diameter **88 mm**, **10.3 mm** off the paint including the 0.64 mm VHB tape; 45° bevel on the bottom edge
- Display: Waveshare ESP32-S3-Touch-LCD-2.8C, 73.0 mm glass, 70.13 mm image, cable tab with ribbon loop at 6 o'clock; glass to brass studs 7.54 mm
- Clearances: 0.6 mm around the glass, 0.5 mm at the cable tab, board pocket 1.5 mm wider than the glass
- Lens: 3" (76.2 mm) × 1/16" (1.5 mm) clear acrylic disc (Enoin 3" × 0.060"), flush in a 1.5 mm pocket
- Back plate: 0.6 mm, 14 mm centre hole, pegs 4 mm × 6 mm, **56 mm** apart (same on every G30)
- Tape ring: 68 to 83.6 mm, inside the 84 mm badge holder

## Changing the design
```
pip install numpy scikit-image
python3 source/halo_housing.py
```
Edit the values in `P` at the top first: display size and clearances (`disp_clear`, `chin_hw`, `tab_clear`, `board_grow`), module depth, lens size and thickness (`lens_t`), outside size (`R`), peg length, the seat dip, and the spreader window and recess. The screen offset is worked out from the tab and ribbon size.

## Print
ASA (UV- and heat-resistant), 0.2 mm layers, 5 walls, 6 top / 5 bottom layers, flow +2–5 % so the walls are watertight. Outer wall about 40 mm/s, part cooling off for the first 3 layers and low after that, and dry filament: the 0.8 mm wall behind the cable ribbon at 6 o'clock shows fraying first. **No brim or supports needed.** Back plate: 3 walls and 100 % infill; let it cool on the bed so it stays flat. Do a test fit in PLA first.

## Heat spreader (optional)
Print `Halo_88_BACK_PLATE_SPREADER.3mf`, cut a 50 mm disc with a 16 mm hole from 0.016″ (0.4 mm) aluminium with the template, insulate its board side with Kapton tape, and put soft insulating thermal pads (3–6 W/mK, about 30 % thicker than the gap) on the hot parts on the back of the board. The disc sits flush in the plate's recess, the pads press on it through the window, and a 1.5 mm pad ring on its paint side bridges the dip in the badge seat, so the board's heat goes into the trunk lid. Everything stays inside the tape ring, so the seal is unchanged. See Appendix A of the guide.

## Hardware
- 3" × 1/16" (1.5 mm) acrylic disc and clear optical adhesive (LOCA). Don't use B-7000 or other solvent glues on acrylic; they craze it
- 0.64 mm 3M VHB tape (5925), cut into a ring with [`VHB_tape_template.svg`](VHB_tape_template.svg). Adhesion promoter recommended
- Black neutral-cure silicone (lens edge, glass edge, plate edge, wire hole, grommet)
- 28 AWG silicone wire, red + black (5 V + GND) and 30 AWG silicone wire × 4 (3.3 V + GND + SDA + SCL)
- 16 mm (5/8") step drill, 5/8" rubber grommet, touch-up paint
- Small silica gel pack
- 12 V → 5 V converter (YRDZXG 12/24 V → 5 V 5 A 25 W), INA219, **fuse tap kit with Mini and ATO taps** and 3 A fuses on an always-on circuit, ring terminal for chassis ground
- Optional: 12 V kill switch rated 5 A or more; aluminium sheet, thermal pads and Kapton tape for the heat spreader

## Wiring
Always-on fuse in the trunk fuse box → Mini or ATO fuse tap (to match the fuse) with a 3 A fuse → optional kill switch → INA219 sense terminals → converter. Converter ground goes to a chassis ground in the trunk lid. The converter's 5 V output and ground go to the board's 12-pin header: pin 2 is 5 V, pin 1 is GND. The INA219's 3.3 V, GND, SDA and SCL go to the board's I2C port. All six display wires pass through the 16 mm centre hole. See [`../docs/wiring_diagram.html`](../docs/wiring_diagram.html) (download it and open it in a browser).

## Notes
- **Firmware:** install it from the **[web installer](https://cakemasterjedi.github.io/haloweb/)** (Chrome or Edge, USB-C, one click). The first time it powers up it shows its own Wi-Fi name (`EMBLEM-xxxx`) and a temporary password on the screen; join it on your phone and choose your own password when the page asks.
- **Running with the car off:** the display runs from an always-on fuse. On the control page's Power tab, set the voltage source to Car battery (INA219) and turn on battery protection (cutoff about 12.2 V).
- **Auto-off: leave it on Never** (Power tab → Normal → Turn off after) unless you fitted a kill switch. Once auto-off turns the emblem off, it stays off until the BOOT button on the board is pressed, which you can't reach once it's sealed in, or until a kill switch cuts and restores its power. Battery protection is different: after a low-voltage shutdown it checks every 10 minutes and comes back on by itself once the battery recovers.
- **Heat guard:** the firmware dims the screen from about 70 °C on the chip and turns it off above 85 °C until it cools to 75 °C. The Power tab shows the temperature and the hottest it has been.
- Make sure the firmware and the phone control page work before sealing it up. The USB port isn't reachable once it's assembled; later updates go over Wi-Fi from the control page (Settings → Firmware).
- Rules on lit or animated displays facing backward vary by state and country. Check yours.
- Unofficial fan project, not affiliated with or endorsed by BMW.
