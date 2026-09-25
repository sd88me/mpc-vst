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
- [ ] **Meters.** The `meter` widget (a display-only filmstrip) is built; check on a device whether MPC redraws a
      FilmStrip when the engine changes the parameter by itself, and at what rate, with an engine that sets one.
- [ ] **Tab images.** MPC draws the function-key tab bar; find how stock skins give tabs on/off images (from a
      stock TUI.json, described in NOTES, not committed) and whether a plugin skin can. `Indicator`
      (`indicatorId`, `numIndicatorsInGroup`, on/off images — see below) is an unverified candidate.
- [ ] **A native `Meter` component.** The `meter` widget bakes a `Knob`/FilmStrip readout (called "Meter" only in
      its JSON name, not a real one). A real `Meter` type (`direction`, `peakImage`, `rmsImage`, `peakHandle`,
      `rmsHandle`, `peakHoldHandle`/`peakHoldSize`/`peakHoldColour`, `invert`) may be a better foundation —
      prototyped, unverified, on `claude/native-meter-prototype`. Check both against a real device once it's
      reachable.
- [ ] **Native overlays (unverified — from a 2026-01/02 binary reverse-engineering pass cross-checked against 41
      shipping `TUI.json` files, not our own hardware testing).** Every knob/slider/stepper already fires
      `Show Overlay "knob overlay"` on Double Click (`shadow_skin.py`'s knob/slider/stepper `_action` calls), but
      nothing in NOTES.md records what it shows on a device for a VST2 parameter — worth confirming, since
      (unlike the menu overlay) a big-knob overlay only needs the bound `Data` handle's normalized value, not a
      live option-text list, so it plausibly already works as shipped. If it does, `NumericOverlay` (not wired up
      anywhere yet; a typed-entry keypad, same low requirement) is a natural follow-on for precise values.
      `MenuOverlay` is a different case: it is very likely the same native list picker already confirmed **empty
      for VST2** (NOTES.md "Native picker (menu overlay): not available to VST2", 2026-09-24) under its real
      component name, not a new option — `popup` stays the way to do a list.
- [ ] **`Envelope`/`EnvelopeOverlay`, `XYPad`/`Plotter` (unverified, and in tension with an existing finding).**
      NOTES.md's "No draggable/graph widgets in plugin skins" (2026-09-24) pulled real `TUI.json` off a device
      from three stock synths, including TubeSynth (which has genuine ADSR envelopes), and found no
      Envelope/graph/XY-pad component anywhere — TubeSynth fakes its envelopes with plain knobs, and the
      conclusion drawn was that no such widget is portable to a plugin skin. The reverse-engineering reference
      claims both types *are* template-verified terminal components in its own, larger 41-file survey, with a
      real-looking `Envelope` JSON example (`envelopeName: "Pitch"`, `envelopeType: "ADSFR"`, full handle set).
      These aren't necessarily contradictory (different instruments, different panels) but need resolving the
      same way the original finding was made — pull more stock `TUI.json` off a device and find a skin that
      actually uses one (which stock instrument has a "Pitch" envelope?) — before building anything on it; don't
      trust a binary string table over an inspected real file.

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
