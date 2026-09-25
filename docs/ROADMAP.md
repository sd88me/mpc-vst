# Roadmap

Repo-level features still to do, in rough priority order. Verified behaviour goes in `NOTES.md`; move an item
there (with the date) once it's done and seen on a device. Port-specific work lives in each port's repo.

## Skin controls
Building on parameter-driven visibility (`IndexedEnabling`, NOTES "Conditional visibility").

- [ ] **Mode panels on a device.** `when=<param>:<option>` is built and previewed (2026-09-25); check it on a
      Force (suggested first use: Maze's LFO rate knob vs sync division).
- [ ] **Real-font baked text.** Popup options and `enum_*` segments use shadow_art's small bitmap font; draw them
      with a `.ttf` (as `title_font` does for frame titles) so option text reads like the rest of the page.
- [ ] **Font choice for live text.** Let a layout pick `Titillium Web` or `Roboto` and the weight/size for
      names and values (the only two families MPC resolves).
- [ ] **Images that follow a value.** A per-option image set (e.g. a waveform picture per wave type) shown by
      `IndexedEnabling` on the parameter.

## Porting and tooling
- [ ] **A reference port on `engine.h` + `params.json`** (e.g. `poc/synth.c` turned into a full example), so
      the repo shows a port that needs no adapter.
- [ ] **SVG drawings as background art.** Unlabelled shapes/text/images in the studio's SVG (anything drawn in
      Inkscape) become part of the page background.
- [ ] **Browser-rendered widgets.** Draw backgrounds, knob filmstrips and button states as HTML/CSS/SVG in headless
      Chromium instead of the vendored Force Shadow renderer, from the same layout files: any font, knob style,
      gradient or shadow. Would also cover "Real-font baked text" above, and pairs with SVG background art.

## Verification
- [ ] **Stock, unmodded MPC and other models:** the ALSA MIDI-out port (`poc/midiport.c`) without MockbaMod,
      and `tools/probe_device.sh` after firmware updates. Needs the hardware.

## Done
- [x] Auto-layout picks `popup` for 7+ options; `wrapper/popup.h` shared by hand-written wrappers (2026-09-25).
- [x] One-command offline test: `tools/test_port.sh <vst.json>` (2026-09-25).
- [x] NOTES open issues reviewed and resolved items folded down (2026-09-25).
- [x] `popup` control, verified on a Force (2026-09-25).
- [x] Generic engine interface + `adapters/schwung` (2026-09-25).
