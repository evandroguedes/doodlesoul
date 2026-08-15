# doodlesoul

**Try it now, nothing to install:**
[evandroguedes.github.io/doodlesoul](https://evandroguedes.github.io/doodlesoul/)
— meet your chip's soul and flash the firmware straight from Chrome.

Every chip ships with a soul. Flash this, and it wakes.

**doodlesoul** wakes the one unique hand-drawn character that was always
latent in your M5StickC Plus — a little doodle person with a name, a
face, traits, a rarity tier, and moods, derived from the chip's
factory-burned MAC address. No button to skip to the next one. This one
is yours, and it is the *device's*: reflash, update, fix bugs — the same
soul greets you every time.

![four possible souls](docs/souls.png)

*Four of the 4,294,967,296 possible souls: Matone, Lugo, Posime, and
Tagugu. One of them — or more likely, someone nobody has ever seen —
will be born in your stick.*

![a living soul](docs/soul.gif)

Faces are 100% procedural (no image assets), drawn by
[doodleink](https://github.com/evandroguedes/doodleink): seeded trait
casting with rarities, a rough 3D head for pose, and a wobbly ink renderer
on paper texture. Redrawn ~12× a second with re-wobbled strokes — proper
boiling-line animation, like a sketchbook come alive.

## How the soul works

- Every ESP32 leaves the factory with a unique MAC burned into eFuse.
  doodlesoul mixes all 48 bits through splitmix64 and uses the result as
  the character seed: the soul *is* the device.
- Flashing doesn't create it and can't destroy it. The first boot just
  meets it (with a little birth announcement); every boot after that is a
  reunion. Firmware updates are safe.
- Two people flashing the same downloaded binary get different souls,
  guaranteed by the hardware.
- There is no command to change it. No reroll, no reincarnation, no
  escape hatch. If you want a different character, you need a different
  chip. That is the point.

Souls have pronounceable names (Pomu, Tavik, Belora...), derived from the
seed, and a rarity tier from common to legendary based on how improbable
their trait roll was.

## Living with it

- **tilt** — it turns to follow gravity, like a level
- **BtnA** (front) — pet it: it beams and does a little hop
- **shake** — it gets dizzy (careful)
- **BtnB click** — its soul card: name, traits, rarity, id, boot count
- **BtnB hold** — freeze on a fine-art still
- **left alone** — it daydreams: glancing around, blinking, changing mood
- **untouched 2 min** — screen dims; **6 min** — it falls asleep ("z z z",
  display off). Move it or press a button to wake it. On USB power it
  never dims.

## The web page (no install, no display needed)

**[evandroguedes.github.io/doodlesoul](https://evandroguedes.github.io/doodlesoul/)** —
open it in Chrome or Edge:

- **connect stick & meet its soul** reads your chip's MAC over Web Serial
  and renders the exact character that lives (or would live) in it — drawn
  in the browser by the same engine, compiled to WebAssembly from the same
  source. Works on display-less boards too.
- **install doodlesoul firmware** flashes the stick right from the page
  (esptool-js), no toolchain needed. Every part is MD5-verified in flash
  before the page claims success, and it refuses to write to the wrong
  chip type. If anything ever goes sideways, one PlatformIO flash heals
  it — the soul itself is never at risk.
- Or type any MAC address and meet its soul.

Because the browser and the firmware share `soul.h` verbatim, the page
doubles as a soft provenance check: connect any device and compare the
face on its screen with what its silicon says it should be. (It proves
the face belongs to the MAC — not that the firmware is unmodified; that
would need secure boot.)

## Build & flash

```sh
pio run -e m5stick-c-plus -t upload
pio device monitor     # the soul card prints at boot
```

Serial console: `s` dumps a pixel-perfect screenshot (decode with
doodleink's `extras/tools/screenshot.py`), `i` prints accelerometer
samples.

If tilt feels inverted on your unit, flip `YAW_SIGN` / `PITCH_SIGN` at
the top of `src/main.cpp`.

## Board compatibility

The identity math works on every Espressif chip, so the web page can meet
the soul of anything esptool talks to. The firmware in this repo is built
for the plain ESP32; the page refuses to flash it onto other chip
families (an S3 accepting it was how we learned to check).

| board | chip | screen | status |
|---|---|---|---|
| M5StickC Plus | ESP32-PICO-D4 | 135×240 ST7789 | tested, this is the dev board |
| M5StickC Plus2 | ESP32-PICO-V3-02 | 135×240 ST7789 | should work with the same binary, untested. M5Unified detects the board at runtime and the code reads the panel size dynamically |
| M5StickC (original) | ESP32-PICO-D4 | 80×160 ST7735 | likely works untested, the face just renders smaller |
| M5Stack Core2 | ESP32 + 8MB PSRAM | 320×240 | needs a small change: the framebuffers outgrow internal RAM at 320×240, so they have to move to PSRAM |
| M5Cardputer | ESP32-S3 | 240×135 ST7789 | needs an S3 build target and a button remap (it has a keyboard). Same panel size as the Stick, so the art is ready |
| M5Stack CoreS3, AtomS3, Dial | ESP32-S3 | various | same S3 build work as the Cardputer |
| TTGO T-Display and other bare ESP32 + ST7789 boards | ESP32 | 135×240 | the engine runs fine, but these need a display shim to replace M5Unified (the canvas interface is two methods, see doodleink) |
| M5Paper and other e-paper | ESP32 | e-paper | planned. The ink-on-paper look was made for it |
| every other Espressif chip (S2, C3, C6...) | any | any or none | the web page reads its MAC and shows its soul today. Firmware needs a port |

Souls are portable by definition: the same chip produces the same
character no matter which board or firmware build carries it.

The whole app is one file, and the engine's canvas interface is two
methods, so ports are mostly board bring-up. If you do one, open a PR.

## Credits

Built on [doodleink](https://github.com/evandroguedes/doodleink).
Inspired by [Mannay](https://x.com/mannay)'s crowd doodles and
[cyber-crowd](https://github.com/kengocodes/cyber-crowd) (MIT). Power
management patterns hardened on a much-loved kids' toy project on the
same board.

MIT.
