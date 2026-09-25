# Roadmap

Repo-level features still to do, in rough priority order. Verified behaviour goes in `NOTES.md`; move an item
there (with the date) once it's done and seen on a device. Port-specific work lives in each port's repo.

## Skin controls
Building on parameter-driven visibility (`IndexedEnabling`, NOTES "Conditional visibility").

- [ ] **Font choice for live text.** Let a layout pick `Titillium Web` or `Roboto` and the weight/size for
      names and values (the only two families MPC resolves).
- [ ] **Build and preview from the browser editor.** A button in `studio.py serve` that builds the port's skin and
      shows `studio.py preview`'s pages (needs the port's vst.json and the renderer's Docker image).
- [ ] **Looks and images on a device.** Built and previewed offline (2026-09-25): check a skin with image knobs,
      an imported filmstrip, image toggles/buttons/segments, a panel picture, a popup list picture and a `picture`
      (one image per option) on a Force.
- [ ] **Engine-driven live updates need a new wrapper mechanism.** Confirmed on a Force 2026-09-25
      (`poc/meterprobe`): `wrapper/vst2_wrap.c` never calls `audioMasterAutomate`/`audioMasterUpdateDisplay`
      for a parameter the DSP engine changes on its own between host-initiated calls (only in response to a
      touch/Q-Link, via `setParameter`'s `need_update_display`) — so a filmstrip `meter` or any other display
      bound to a free-running engine value never visibly updates, even during playback. Needs the engine
      interface (`wrapper/engine.h`) or wrapper to gain a way for an engine to flag "this key changed" so
      `run_block()` can call the host back regardless of whether `setParameter()` fired. See NOTES.md.
- [ ] **Tab images.** MPC draws the function-key tab bar; find how stock skins give tabs on/off images (from a
      stock TUI.json, described in NOTES, not committed) and whether a plugin skin can. `Indicator`
      (`indicatorId`, `numIndicatorsInGroup`, on/off images — see below) is an unverified candidate.
- [ ] **`MenuOverlay` naming check.** Very likely the same native list picker already confirmed **empty for
      VST2** (NOTES.md "Native picker (menu overlay): not available to VST2", 2026-09-24) under its real
      component name, not a new option — `popup` stays the way to do a list. Not separately verified.
## Porting and tooling
- [ ] **A reference port on `engine.h` + `params.json`** (e.g. `poc/synth.c` turned into a full example), so
      the repo shows a port that needs no adapter.

## Verification
- [ ] **Stock, unmodded MPC and other models:** the ALSA MIDI-out port (`poc/midiport.c`) without MockbaMod,
      and `tools/probe_device.sh` after firmware updates. Needs the hardware.

## Done
- [x] Control looks and images, offline (2026-09-25): built-in looks (knobs moog/chicken/metal/cap, slider fader,
      toggles led/switch), turning knob images with a still base, filmstrip import (knobs, sliders, meters), slider
      thumb/track, on/off images for toggles, buttons and segments, frame and popup panel pictures, bitmap and
      placed `art`, `picture` (images that follow a value), per-kind layout defaults; editor Look section and
      Assets tab. A skin without looks builds byte-identical to before.
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
- [x] `Envelope`/`EnvelopeOverlay`, `XYPad`/`Plotter`: checked on a Force (2026-09-25) — no stock `TUI.json`
      defines a component of any of these types; the one grep hit (TubeSynth) was a tab name, not a component
      type. Not available; see NOTES.md.
- [x] `KnobOverlay`: verified on a Force (2026-09-25, Maze Voice) — works fully for VST2 params (value, name,
      settable, reflects live Q-Link/automated changes). `NumericOverlay`: tested on a Force (2026-09-25,
      Maze Skin Test) — not a recognized overlay name, shows a blank/stuck panel instead. See NOTES.md.
- [x] Native `Meter` component: tested on a Force (2026-09-25, `poc/meterprobe`) — breaks the whole plugin
      screen (blank), not just left unrendered. Not usable; `look=native` now refuses at build time
      (`tools/skin_assets.py`) instead of building a broken skin (the filmstrip-fake `meter` stays the
      only way to show a level). See NOTES.md.
