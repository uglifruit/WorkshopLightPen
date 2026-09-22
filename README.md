# LIGHTPEN

A program card for the [Music Thing Modular Workshop System Computer](https://github.com/TomWhitwell/Workshop_Computer) that turns an **RGB sensor wand** into a controller and a sound source. The wand is a whiteboard-marker body with three light-dependent resistors (LDRs) in the tip, under red, green and blue lighting gels. Wave it at the room, or scan printed colour with it (gradient maps, barcode stripes, rainbow strips), and the colour becomes CV, gates and audio.

**Status: in development.** All eight modes build and pass the desktop DSP checks in `tools/`. None has been played on hardware yet.

## Wiring the wand

| Jack | Connects to |
|---|---|
| CV Out 1 | common supply to all three LDRs (held at full scale) |
| CV In 1 | red LDR |
| CV In 2 | green LDR |
| Audio In 2 | blue LDR (read as a steady voltage) |

Each LDR goes straight from the CV Out 1 plug to one input plug — four wires, no other components. The Computer's inputs are each 120k to ground internally, which forms the other half of the divider, and its outputs already have a 1k series resistor, so the wand needs no resistors of its own and no ground wire. Each channel draws about 50µA.

This works best when the LDRs read somewhere near 120k through their gels. If yours are much lower in resistance, a resistor from tip to sleeve at each input plug (roughly the geometric mean of the LDR's light and dark resistance) will restore the contrast. Calibration handles the rest, including LDRs wired the other way round.

## Controls in every mode

- **X knob:** sensitivity (0.25x to 4x of the calibrated range, unity at noon)
- **Y knob:** smoothing, from light to about a third of a second
- **Switch Up:** next mode (one mode per click)
- **Switch Down (momentary):** the current mode's action
- **Main knob:** the current mode's parameter

**LEDs.** The six LEDs are in two columns of three:

- **Left column (LEDs 0, 2, 4)** — which mode you're in, as a three-bit number counting from zero: Mode 1 is all three dark, Mode 8 all three lit. It blinks for a second whenever you change mode, so the change is visible even on a dark pattern.
- **Right column (LEDs 1, 3, 5)** — a live meter of **red, green and blue**, top to bottom.

The meter shows what the modes actually act on, not the bare sensor: it's the reading after calibration, the X-knob sensitivity and the Y-knob smoothing. Dark is calibrated black, full brightness is calibrated white, and turning X up brightens the LEDs too. That makes it the quickest way to set the card up — if a colour sits pinned at full or stays dark as you move the wand, the modes are seeing the same thing, so back the sensitivity off or recalibrate.

Two exceptions: during calibration the right column shows the *raw* readings instead, since there's no calibration to apply yet; and when you change a mode's option (chord, rotation, scale), that option's number blinks across the first few LEDs for a second.

## Calibration

Calibrate once with the paper you'll scan and the light you'll use. The card remembers it through power-off. If all six LEDs blink when the card starts, it hasn't been calibrated yet.

1. Hold the switch **Down** while powering on, then release it.
2. **White:** LED 0 (top left) blinks. Point the wand at white paper and tap **Down**. The LEDs fill up while it measures.
3. **Black:** LEDs 0 and 2 blink. Point the wand at black card and tap **Down**.
4. All six LEDs light, the card saves the calibration and restarts in Mode 1.

While it waits for each tap, the right-hand LEDs show the raw red, green and blue readings, so you can see the wand is responding.

If a colour barely changes between white and black (a loose LDR, or both taps on the same surface), that colour's right-hand LED flashes fast for two seconds and calibration starts again at white. Pushing the switch **Up** at any point cancels and keeps the previous calibration.

After calibration, black reads as zero and white as full scale on every channel, so the three gels read on the same scale even though each passes a different amount of light.

## Modes

**1. RGB CV Mirror & Gate Tracker.** CV Out 2, Audio Out 1 and Audio Out 2 carry red, green and blue as 0-5V. Pulse Out 1 is high while red is above a threshold, and Pulse Out 2 while blue is. *Main:* threshold. *Down (hold):* freeze the three CVs.

**2. Triad Drone.** A three-note chord: red sets the root's level, green the third's, blue the fifth's. It plays on both audio outs. The root is A2, and Audio In 1 transposes it (1V/oct). *Main:* waveshape, sine → triangle → saw → square. *Down:* chord type, major → minor → sus2 → sus4.

**3. Barcode Reader.** Black and white, not colour — the three LDRs are summed into one brightness, which is what a printed barcode actually varies. Hold Down and swipe across a code; release and it loops, up to 2.7 seconds. The take is cut into *elements* — runs of black or white — so what loops is the code's own bar pattern rather than a sampled waveform:

- **Pulse Out 1** — every element, black or white
- **Pulse Out 2** — only elements wider than that code's average, so the wide bars accent
- **CV Out 2** — the scanned brightness
- **Audio Out 1** — the current element's width, with the average width at half scale
- **Audio Out 2** — high through black elements and low through white: the code itself as a gate

*Main:* playback speed, 1/8x to 8x, with original speed in a dead zone at noon.

Coloured stripes still work, read by their lightness. Two practical notes: keep **Y (smoothing) low** while scanning or the bars blur together, and remember LDRs respond in milliseconds rather than microseconds — a supermarket barcode swiped at speed is beyond them, but a code printed or photocopied up large reads well.

**4. Tape Scrubber.** Hold Down and Audio In 1 records for exactly as long as you hold it, up to 1.75 seconds — the length of the hold is the length of the loop. Release, and the green reading positions the playhead anywhere in that take, so moving the wand scratches the audio like tape. Red sets the filter cutoff and blue its resonance. With a 2D colour map (green across, red up), the page works like a KAOSS pad. *Main:* filter, low-pass → band-pass → high-pass.

**5. Synesthesia Voice.** A complete synth voice: pitch from Audio In 1 (1V/oct, 0V = C3), gate from Pulse In 1. By default red is filter cutoff, green is wavefolder depth and blue is decay length. The envelope also comes out of CV Out 2. *Main:* waveshape, sine → saw → square. *Down:* rotate which colour controls what.

**6. Colour Organ.** The colour's hue picks a note: two octaves of the chosen scale spread around the colour wheel, so scanning a rainbow plays a scale. CV Out 2 carries the note (calibrated 1V/oct) and Pulse Out 1 fires on each new note. Pulse Out 2 is high while a colour is detected, and the audio outs play a simple organ tone. *Main:* scale (chromatic / major / minor / pentatonic). *Down:* tap to transpose up a semitone; hold for a second to go back to C.

**7. Jog Wheel.** The same recording as Mode 4 — made the same way, by holding Down — but played the other way round. The loop runs by itself at normal speed and the wand sets its *speed*, like a hand resting on a turntable: mid-grey leaves it alone, brighter drives it faster, darker drags it down through a standstill and into reverse. Blue is the platter's weight, so a bright blue reading gives you a heavy flywheel that takes a moment to answer. Red is a low-pass filter. CV Out 2 follows the play position, Pulse Out 1 fires at the loop start, and Pulse Out 2 is high while it runs backwards. *Main:* how hard the wand pushes, from a gentle nudge to a full shuttle.

**8. Prism Voice.** Mode 5's voice wired to a different set of controls, and a different character: it *sustains* while the gate is high instead of plucking. Red is FM depth from a second oscillator an octave up, green crushes the sample rate from clean down to a sixty-fourth, and blue is the filter cutoff. Same pitch and gate inputs as Mode 5, and the envelope again comes out of CV Out 2. *Main:* waveshape, sine → triangle → square. *Down:* rotate which colour controls what.

## Building

Requires the Raspberry Pi Pico SDK 2.2.0 (see `pico_sdk_import.cmake`, and `CLAUDE.md` for the exact toolchain setup).

```
cmake -B build -G Ninja
cmake --build build
```

This produces `build/lightpen.uf2`.

## Development tools

`tools/lpsim.py` contains integer-exact Python versions of the card's fixed-point maths (filter, oscillators, knob curves, calibration, the barcode edge finder, the hue-to-note picker, the jog rate law and tape positioning, the switch debounce), with checks. Run it with `python tools/lpsim.py`. It needs no hardware.

## License

CC BY 4.0. See [LICENSE](LICENSE).
