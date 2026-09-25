# Roadmap

Repo-level features still to do, in rough priority order. Verified behaviour goes in `NOTES.md`; move an item
there (with the date) once it's done and seen on a device. Port-specific work lives in each port's repo.

## Skin controls
Building on parameter-driven visibility (`IndexedEnabling`, NOTES "Conditional visibility").

- [ ] **Font choice for live text.** Let a layout pick `Titillium Web` or `Roboto` and the weight/size for
      names and values (the only two families MPC resolves).
- [ ] **Build and preview from the browser editor.** A button in `studio.py serve` that builds the port's skin and
      shows `studio.py preview`'s pages (needs the port's vst.json and the renderer's Docker image).
- [ ] **Images that follow a value.** A per-option image set (e.g. a waveform picture per wave type) shown by
      `IndexedEnabling` on the parameter.

## Porting and tooling
- [ ] **A reference port on `engine.h` + `params.json`** (e.g. `poc/synth.c` turned into a full example), so
      the repo shows a port that needs no adapter.

## Verification
- [ ] **Stock, unmodded MPC and other models:** the ALSA MIDI-out port (`poc/midiport.c`) without MockbaMod,
      and `tools/probe_device.sh` after firmware updates. Needs the hardware.

## Done
- [x] Browser editor for layouts, `studio.py serve` (2026-09-25): canvas drawn by the browser renderer, move/resize,
      inspector, tabs, modes, Q-Link sets, theme colours, `art_css` editing with fonts, SVG art import, checks.
      Verified offline in Chromium: load and save without edits gives the identical file. Double-click launchers
      (`SkinStudio.command` / `.bat` / `.sh`) with a start screen; the `.sh` one tested on Linux, the macOS and
      Windows ones not yet run on those systems.
- [x] Browser renderer (`"art": "html"`, `art_css=`), SVG background art (`art file=`, studio round trip) and
      mode panels (`when=`), verified on a Force (2026-09-25); real-font baked text comes with the browser renderer.
- [x] Popups in Maze, JV-880, Acid and Euclidier, verified on a Force (2026-09-25).
- [x] Auto-layout picks `popup` for 7+ options; `wrapper/popup.h` shared by hand-written wrappers (2026-09-25).
- [x] One-command offline test: `tools/test_port.sh <vst.json>` (2026-09-25).
- [x] NOTES open issues reviewed and resolved items folded down (2026-09-25).
- [x] `popup` control, verified on a Force (2026-09-25).
- [x] Generic engine interface + `adapters/schwung` (2026-09-25).
