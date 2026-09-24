# Porting an engine to an MPC OS plugin

## 0. Classify it
- **Block-rendering engine** (a synth/effect core): implement `mpc_engine()` (`wrapper/engine.h`) and wrap with
  `wrapper/vst2_wrap.c`; parameters come from a `params.json` (`tools/params.py`). An engine already written for
  another host plugs in through its adapter (`adapters/`, e.g. `adapters/schwung/` for Maze Voice, JV-880).
- **Engine with host-side glue** (control-socket keys that aren't DSP params, dynamic lists): write a port-specific
  wrapper on the same ABI (see the crate digger port) with virtual parameters for the glue.
- **MIDI generator** (sequencer/arp): MPC ignores VST MIDI out, so send through an ALSA seq port (`poc/midiport.c`).
- **App** (network, files, child processes): allowed, see NOTES "Beyond synths". Keep the audio thread
  non-blocking, use `posix_spawn` with LD_PRELOAD stripped (never `fork()`), and use libcurl for HTTPS.

## Quick start (block-rendering engine)
Add a `vst.json` next to the engine (format in `tools/gen_vst.py`'s docstring; example:
`force-maze/maze-voice/vst/vst.json`), then run `tools/build_port.sh path/to/vst.json`. That builds the skin from
`layout` (or from an auto-layout when there's none, which is a good first pass), `params.h`, the `.so` (linked with
`wrapper/vst2_wrap.c`) and `pluginlist-entry.xml`, all in `build/` next to `vst.json`. The port's own `build.sh` should
just call it. Don't vendor copies of the wrapper or tools. Then bench it (docs/BENCH.md) and package it (docs/RELEASING.md).

**Vendor the engine's own source into the port's repo; don't fetch it at build time.** If the DSP comes from a
third-party upstream (another project's synth module, an emulator core, anything not written in this repo), `git clone` it
into the port's repo as committed files (`src/dsp/` or similar), not into a gitignored scratch dir pulled fresh
on every build. A `git clone` at build time makes the port fail offline, on a network hiccup, or the moment the
upstream repo moves/is deleted -- none of which is "fully self-contained." Copy the philosophy the DX7 engine itself
uses for MSFA (a vendored, committed copy, not a fetch of Dexed): pull the upstream source in once, apply any
local fixes directly to the vendored copy (no runtime patch-apply step), add the upstream's `LICENSE` if it
differs from the port repo's own (most engines here are GPL-3.0-compatible; check), and write a short
`src/VENDORED.md` recording the exact upstream commit vendored from, and precisely what was changed locally, so
a future re-vendor from a newer upstream is a real diff, not archaeology (see `mpc-vst-dx7`'s `src/VENDORED.md`
for the pattern). This applies to every future port, not just ones that hit the problem the hard way.

## 1. Engine
- [ ] Third-party DSP source (not written in this repo) is vendored -- committed into the port's own repo,
      not fetched at build time. See the Quick Start section above for exactly how and why.
- [ ] Builds for armhf with glibc ≤ the device's (`arm32v7/gcc:12` is fine), exporting only `VSTPluginMain`.
- [ ] 44.1 kHz / 128-frame blocks (MPC's own period). Compile out host-specific quirks with `-D<NAME>_VST`.
- [ ] Build links with `-Wl,--no-undefined` (build_port.sh does): an unresolved symbol would otherwise only
      show up as MPC crashing when the plugin loads.
- [ ] Per-instance state; several instances may run at once.
- [ ] State saved via chunks (`effGetChunk`/`effSetChunk`).
- [ ] Offline x86 test: instances, parameter round-trip, MIDI → audio, chunk restore, under ASan.

## 2. Parameters
- [ ] Stable order (the VST index is what skins and projects bind to). Append only; never reorder a shipped plugin.
- [ ] Options: an index; nudges step one option (the wrapper does this). Triggers: `"momentary": true`, which springs back.
- [ ] Display strings are the only dynamic text channel into the skin (see NOTES on refresh behaviour).
      A param whose `get_param()` returns real text (a name, a status message), not a number, needs
      `"display": "string"` in its parameter entry -- otherwise the wrapper's default numeric
      reformatting mangles it down to "0" (see NOTES).

## 3. Skin
- [ ] Design in a layout `.conf` (Force Shadow widget syntax plus `qlinks`/`rows=`), or port an existing shadow page.
- [ ] **If the app has its own `addon/shadow_page.conf`, copy its `style=`/`theme_*` lines verbatim into
      the top of the port's `layout.conf` before anything else.** Without them the skin renders in
      `shadow_art`'s generic default palette (cream knobs, dark plate, orange accent) instead of the
      app's real look (e.g. force-acid's yellow chassis / red buttons) -- easy to miss because the build
      succeeds and the layout is otherwise correct; only the offline preview shows the mismatch.
      `shadow_skin.py` forwards the whole layout file to `shadow_art` as `theme|<layout.conf>`, which
      applies it exactly like force-shadow's own on-device renderer (`render_conf_preview.c`'s
      `load_conf`) -- every `theme_*` key a shadow page uses, not just the small subset
      `apply_theme()` uses Python-side for label text. If there's no shadow page to copy from, pick
      theme colours deliberately instead of leaving the default.
- [ ] Generate (`tools/shadow_skin.py` via the port's gen script), then look at an offline composite
      (`tools/studio.py preview`) before deploying -- compare it against the real shadow page's own
      screenshot/mockup if one exists (`docs/*.png` in the app's repo), not just "does it look plausible".
- [ ] Q-Links: 1–8 = knob bank 1, 9–16 = bank 2; nested pages via several `qlinks` lines.
- [ ] Choice lists: `enum_h`/`enum_v` (all options on screen) or `popup` (a field; a tap opens a drawn list, a
      pick closes it). Not `menu`: MPC's native picker opens empty for a VST2. A `popup` adds a hidden
      `<key>__open` param after the port's own (gen_vst.py), kept by `wrapper/vst2_wrap.c`; a hand-written
      wrapper (e.g. force-acid's) needs the same `popup_of` handling or its lists won't close. `studio.py preview`
      writes a `_open` image per page with popups.

## 4. Device
- [ ] `.so` → `/sdcard/vst/`, skin → `/sdcard/Synths/<vendor> - VST - <name>/`.
- [ ] `pluginList-arm` entry (MPC stopped, settings backed up), then restart (ask first).
- [ ] User test: list → insert → play → skin → Q-Links → save/reload project. Record results in NOTES.md.
