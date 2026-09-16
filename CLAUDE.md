# LIGHTPEN — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer**
(RP2040), built on the header-only **ComputerCard** library. Sibling project
to `../WorkshopNibbleDrum`, `../Workshop2D2`, `../WorkshopBio` and the other
`Workshop*` card folders in `../` — reuse their conventions and structure
where they fit.

## Current status: bare scaffold, v0.1.0

Bootstrapped from the same framework files, build setup, and directory
conventions used by the sibling cards — `ComputerCard.h`, `CMakeLists.txt`,
`info.yaml`, and empty `panels/`, `reference/`, `docs/`, `tools/` dirs. No
synthesis yet — the design is still to be defined.

`info.yaml` is `draft: true`, `Status: In development`. Own repo:
`uglifruit/WorkshopLightPen`.

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

## Hard rules

Identical platform constraints to the other cards on this bench:

- `ProcessSample()` runs at **48 kHz** on core 0, inside a DMA interrupt.
  Allocation-free, no `malloc`, no blocking, no `float` in the hot path —
  fixed-point only.
- Audio/CV I/O is signed 12-bit (`-2048..2047`). `KnobVal()` is unsigned 12-bit
  (`0..4095`).
- **Never** do hardware setup in the `ComputerCard` constructor — it wedges the
  chip. Setup goes in `main()`.
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
