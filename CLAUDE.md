# LIGHTPEN — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer**
(RP2040), built on the header-only **ComputerCard** library. Sibling project
to `../WorkshopNibbleDrum`, `../Workshop2D2`, `../WorkshopBio` and the other
`Workshop*` card folders in `../` — reuse their conventions and structure
where they fit.

**LIGHTPEN drives an "RGB sensor wand"**: a whiteboard-marker body with three
LDRs in the tip under red, green and blue gels. It reads room light or scans
printed colour (gradient maps, barcode stripes, rainbow strips) and turns it
into CV and audio across eight modes.

## Current status: v0.3.0, all eight modes written, NOT yet on hardware

Builds clean under `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion` to
`build/lightpen.uf2`: **4.9% flash, 77% RAM** (the RAM is almost entirely
`gTape`, the 168KB take shared by Modes 4 and 7).
`python tools/lpsim.py` passes all checks. Nothing has been
flashed or played yet: the bench unknowns below come first.

`info.yaml` is `draft: true`, `Status: In development`. Own repo:
`uglifruit/WorkshopLightPen`. Design plan of record:
`~/.claude/plans/glistening-popping-avalanche.md`.

## Fixed wiring (every mode)

| Jack | Job |
|---|---|
| CV Out 1 | LDR supply, `CVOut1Precise(262143)` once. Latched by the library. Only `main.cpp` writes it; engines cannot. |
| CV In 1 / CV In 2 / Audio In 2 | Red / Green / Blue LDR returns |
| X knob | global gain, 0.25x..4x (exp, unity at noon) |
| Y knob | global slew, shift 5..14 at 48kHz (~240Hz .. ~340ms) |
| Switch Up | next mode, on arrival, after a 20ms debounce |
| Switch Down | the mode's action (press / release / 1s hold events) |
| Down held at power-on | white/black calibration, saved to flash |

LEDs are three rows of two (`0 1 / 2 3 / 4 5`). Left column (0, 2, 4) = the
mode as a 3-bit number counting from zero (mode 1 = all dark, mode 8 = all
lit); it blinks for 1s on a mode change, so a change is visible even when it
lands on a dark pattern. Right column (1, 3, 5) = live R, G, B. A mode's own
option change (chord, rotation, scale) blinks its option number as the first
N LEDs for 1s. All six blinking at boot = no calibration saved yet.

## Calibration

Two-point, per channel: black reads 0 and white full scale, so the three
gels read on the same scale however much light each passes. The sign of
white minus black also sets the polarity, so a wand with the LDRs on either
side of the divider just works.

Flow (`calibration.h`, driven from `main.cpp`): hold Down at power-on and
release. LED 0 blinks: point at white, tap Down (averages ~0.34s while the
LEDs fill). LEDs 0 and 2 blink: point at black, tap Down. The right column
shows raw R/G/B while waiting. All six steady = good, then it saves and
reboots. A channel whose white and black differ by less than 64 LSB
(`kMinCalibSpan`, ~0.2V) fails: its right-column LED flashes fast for 2s and
the flow restarts at white. Switch Up at any point cancels and keeps the
previous calibration.

Saving (`calibstore.h`): the calibration finishes inside `ProcessSample()`,
which calls `Abort()`. That makes `Run()` return in `main()` with both of
the library's interrupt handlers removed, so `main()` writes the one flash
sector (512KB in; magic `LPC1`, version, checksum) with nothing that could
fetch from flash mid-erase, then `watchdog_reboot`s. The next boot loads it.
A record is ignored if its magic, version or checksum is wrong, if it fails
the span check, or if the firmware image ever grows past 512KB. Bump
`kCalibVersion` if `CalibData`'s layout changes.

## Modes

| # | File | Main | Down | Outputs |
|---|---|---|---|---|
| 1 | `modes/mirror` | gate threshold | hold = freeze CV | CV2/A1/A2 = R/G/B 0-5V; P1/P2 = R/B over threshold |
| 2 | `modes/triad` | sine→tri→saw→square | tap = Maj/Min/Sus2/Sus4 | A1+A2 = drone; R/G/B = root/3rd/5th VCA; A In 1 = 1V/oct (0V = A2) |
| 3 | `modes/barcode` | speed 1/8x..8x (1x at noon, dead zone) | hold = record, release = loop | luminance, cut into elements; P1 = every element, P2 = wide ones; CV2 = brightness, A1 = element width, A2 = black/white gate |
| 4 | `modes/tapescrub` | LP→BP→HP walk | hold = record a take | A1+A2; A In 1 = source; G = head, R = cutoff, B = resonance |
| 5 | `modes/synesthesia` | sine→saw→square | tap = rotate R/G/B roles | A1+A2 voice; CV2 = envelope; A In 1 = 1V/oct (0V = C3); P In 1 = gate |
| 6 | `modes/hueorgan` | scale (4 zones) | tap = transpose +1 (wraps), hold 1s = C | CV2 = note (calibrated); P1 = new note; P2 = colour seen; A1+A2 organ |
| 7 | `modes/jog` | jog depth (nudge→shuttle) | hold = record a take | A1+A2; G = speed, B = platter weight, R = cutoff; CV2 = position; P1 = loop start; P2 = reversing |
| 8 | `modes/prism` | sine→tri→square | tap = rotate R/G/B roles | A1+A2 voice; CV2 = envelope; R = FM, G = crush, B = cutoff; sustains on the gate |

Design decisions worth knowing before changing things:

- **X is gain only**, applied after calibration: 0.25x..4x of the calibrated
  white-to-black range.
- **Mode 2 has no inversions**: four flavours, root position. The spec said
  "flavours / inversions", and the plan traded inversions away. Easy to add
  to `kChord`.
- **Mode 2 takes 1V/oct on Audio In 1**, which the plan said it would not have.
  The jack is free in that mode, and a drone you cannot tune is limiting.
- **Mode 3 reads luminance, not colour** (`(ur+ug+ub)/3`): a printed barcode
  varies lightness, and summing gives three channels of signal with their
  noise averaged down. Coloured stripes still read, by their lightness.
- **Mode 3 stores elements, not a waveform.** After release, an incremental
  pass (64 samples per control tick, ~43ms for a full take) runs a Schmitt
  trigger over the recorded bytes and records each run of black or white as
  {start, width, dark}. Playback walks those, so triggers land on bar edges
  at any speed and the wide/narrow distinction (against the take's mean
  width) is what drives Pulse Out 2 and the width CV. One pass inside a
  single ProcessSample would overrun the 20.8us budget many times over.
  Recording is 1 sample per control tick (1.5kHz, 4096 max = 2.73s): width
  resolution is the whole point of this mode, so it is not decimated further.
  Takes go into a spare buffer and swap in only once analysed and found to
  hold at least two elements, so a stray tap keeps the playing loop.
- **Mode 4's filter is a continuous crossfade** (0-20% LP, 40-60% BP, 80-100%
  HP), not hard thirds. The user confirmed this.
- **Modes 4 and 7 share one take** (`tape.h`, `gTape`, 168KB at file scope).
  Recording is a gesture in both: hold Down and the take is as long as the
  hold, capped at `Tape::kMaxLen` (1.75s — the most that fits with ~54KB to
  spare). A take under 50ms is discarded and the previous one kept, though
  its first few ms have been overwritten by then. Mode 4 places the head
  absolutely from Green; Mode 7 lets the loop run and Green sets its speed.
- **Mode 8 is Mode 5's architecture on different destinations** (FM, crush,
  cutoff) with a SUSTAINING envelope rather than a percussive one, so the
  two voices do not just sound like knob swaps of each other.
- **Mode 5's fold is closed-form** (`Fold()` in `synesthesia.cpp`), not
  NibbleDrum's 3-pass reflect loop: at 8x drive a full-scale input needs more
  than three reflections.
- **Mode 6 rotates the hue wheel 30°** so the wrap is at magenta. At the
  default origin, red would dither between the lowest and highest note.
  Degrees are evenly spaced in hue (two octaves + top root), with a
  quarter-band hysteresis and a 30ms dwell per note. The organ voice on the
  audio outs was not in the plan; it was added because those jacks were free.
- **Per-sample virtual dispatch** of `AudioTick` is deliberate; see
  `engine.h`.

## Analogue front end (from Tom's Rev 1 docs + schematic)

`../Workshop_Computer/documentation/` has `Computer_ Rev 1 Documentation.pdf`
and `computer_Rev_1_0_0_Schematic.pdf` (no text layer; render it with
pymupdf). Sheets 2/5 and 3/5 settle how the wand behaves:

- **Every signal input is 120k into a virtual earth**: CV In 1 (R17), CV In 2
  (R25), Audio In 1 (R27) and Audio In 2 (R42) each feed an MCP6004
  inverting stage (33k feedback, -5V_REF offset) that maps ±6V to 0-3.3V.
  So all four inputs present the **same** 120k to 0V, and an LDR from CV Out 1
  to an input tip is a divider against that 120k. The wand needs no pull-down
  and no ground wire of its own.
- **CV and audio outputs already have a 1k series resistor** (R55, R3, R48,
  R51) from a TL074. With 120k always dominating, each LDR channel draws
  about 50µA, so all three together drop ~0.15V across that 1k: cross-talk
  between channels is ~2%, and no current limiting is needed.
- **All ins and outs are bipolar DC-coupled, ±6V** — including the CV/Audio
  pair, stated explicitly in the documentation. Blue on Audio In 2 is sound,
  and the audio outs work as CV.
- Reading in code units: `2048 * 120 / (121 + R_LDR_in_k)`. 10k reads ~1876,
  120k ~1019, 1M ~219. Sensitivity peaks where the gelled LDR's resistance
  is near 120k.
- **The audio inputs are normalled to each other**, and Audio In 2 always
  holds the Blue LDR, so `main()` calls `EnableNormalisationProbe()`: the
  library then forces an unpatched Audio In 1 to exactly 0 instead of
  whatever the normalling gives it.

## Bench unknowns (check these before trusting any mode)

1. **LDR span under the gels.** White and black must differ by at least 64 LSB
   (~0.2V) per channel, and a small span reads noisily. Measure the gelled
   LDRs with a meter: if the geometric mean of their white/black resistance is
   far below ~120k, add a resistor of about that mean from tip to sleeve at
   each input plug to restore contrast.
2. **1V/oct scale on Audio In 1**: `kSemisQ8PerUnit = 9` is right for the
   documented ±6V, but the divider is 1% parts — expect to trim it.
3. **Mode 6 thresholds** `kMinBright` / `kMinChroma` are guesses; gelled
   LDRs never separate cleanly.
4. **CPU headroom.** Not profiled. Mode 2 (three morphing voices) is likely
   the heaviest. The card overclocks to 192MHz as the siblings do.
5. **The calibration save.** The Abort -> write -> reboot path is untested
   on hardware. Confirm a calibration survives a power cycle (no six-LED
   blink at boot) and that the card boots normally afterwards.

## RAM

| Consumer | Size |
|---|---|
| `gTape`, shared by Modes 4 and 7 (1.75s at 48kHz) | 168 KB |
| Mode 3's two takes, with their element lists | 11 KB |
| Sine LUT, engine/oscillator/filter state, library buffers | ~23 KB |
| **Total, measured at link** | **203 KB, 77% of 256KB** |

The tape is sized to leave ~54KB spare, so it is the first thing that will
run the card out of RAM. Watch `--print-memory-usage` if `Tape::kMaxLen`
changes; 2s (192KB) would leave only ~30KB.

## Build

Toolchain comes from the Pico VS Code extension install at `~/.pico-sdk/`.

From PowerShell:

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$env:PATH = "$env:USERPROFILE\.pico-sdk\cmake\v3.31.5\bin;$env:USERPROFILE\.pico-sdk\ninja\v1.12.1;$env:USERPROFILE\.pico-sdk\toolchain\14_2_Rel1\bin;$env:PATH"
cmake -B build -G Ninja
cmake --build build
```

Output: `build/lightpen.uf2`. Copy to `FLASHME/` for flashing (git-ignored).
`cmake`/`ninja` are **not** on the default PATH — always set it as above.

**Every new `.cpp` must be added to `add_executable` in `CMakeLists.txt`.**
Header-only modules (`sensors.h`, `controls.h`, `calibration.h`,
`calibstore.h`, `leds.h`, `osc.h`, `svf.h`, `engine.h`) need no entry.

**Only `main.cpp` may include `ComputerCard.h`.** It defines its member
functions in the header unless `COMPUTERCARD_NOIMPL` is set, so a second
includer is a multiple-definition link error. That is why `controls.h` has
its own `Sw` enum.

## DSP checks

`python tools/lpsim.py` gives integer-exact mirrors of fastmath, the sensor
laws and calibration mapping (including an inverted wand and the minimum
span), the oscillators, the SVF and blend, the fold, Mode 3's speed law and
edge finder, Mode 6's hue and note picker, and the switch debounce. It exits
non-zero on failure. Keep it in step with the C++: when you change a law in
the firmware, change its mirror too.

It has already caught one real bug: **the SVF's integrators froze at low
cutoff** (`f*hp >> 14` rounds to zero for |hp| < ~190), leaving up to 117 LSB
of DC stuck on the output. The fix is that the states carry 8 extra fractional
bits.

## Hard rules

Identical platform constraints to the other cards on this bench:

- `ProcessSample()` runs at **48 kHz** on core 0, inside a DMA interrupt.
  Allocation-free, no `malloc`, no blocking, no `float` in the hot path —
  fixed-point only. Every `AudioTick` is `__not_in_flash_func`; check with
  `arm-none-eabi-nm -C build/lightpen.elf | grep AudioTick` (addresses
  should be `2000xxxx`).
- `pow2_scale` and `note_to_inc` use 64-bit maths / a divide: **control rate
  only**.
- `CVOut2MIDINote` goes through the flash-resident `MIDIToDAC`: `main.cpp`
  only calls it when the note changes.
- Left-shifting a negative value is UB in C++17: write `x * 256`, not `x << 8`,
  for signed values.
- Audio/CV I/O is signed 12-bit (`-2048..2047`). `KnobVal()` is unsigned 12-bit
  (`0..4095`).
- **Never** do hardware setup in the `ComputerCard` constructor — it wedges the
  chip. Setup goes in `main()`. The card object is a function-local `static`
  (Mode 4's tape would overflow the stack).
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` is required for the Workshop
  Computer's crystal — without it the card fails to boot on a cold power-up
  (it works from a warm reset, which is what makes the bug so confusing).

## Release process (for when this card ships)

Own repo (`uglifruit/WorkshopLightPen`) is where development happens. When
ready to release, the card gets PR'd against `TomWhitwell/Workshop_Computer`
via the fork at `../Workshop_Computer` (`origin` =
`uglifruit/Workshop_Computer`, `upstream` = `TomWhitwell/Workshop_Computer`)
— add the card under `releases/<n>_LightPen/` there and open the PR from a
branch on the fork. See the sibling cards' CLAUDE.md files (e.g.
`../WorkshopNibbleDrum/CLAUDE.md`) for the exact PR history and conventions
to follow.
