---
name: mpc-vst-plugin
description: Build, skin, register and test native VST2 plugins for the built-in JUCE plugin host of Akai MPC OS standalone devices (MPC Live/One/X/Key, Force) — porting Schwung DSP modules or other engines to real track instruments/effects with native MPC screen skins (TUI.json) and Q-Links. Use whenever the task mentions MPC/Force VST, mpc-vst-plugins, pluginList-arm, MPC.settings plugin entries, plugin skins/TUI.json, /sdcard/Synths, or porting something "as a plugin" / "native instrument" on MPC or Force.
---


# MPC OS native VST2 plugins

This skill lives in the repo (https://github.com/sd88me/mpc-vst-plugins). Read `docs/NOTES.md` first (verified facts,
open issues, resume point) and `docs/PORTING.md` (step-by-step checklist). Reference port: Maze Voice in
https://github.com/sd88me/force-maze, `maze-voice/vst/` (vst.json, layout.conf; build.sh just calls `tools/build_port.sh`).
Device: reached over SSH as root. BusyBox userland (`head -n 5`, no `grep -b`), and the IP is DHCP, so ask
the user for it. **Ask before restarting MPC** (`systemctl restart acvs`), because it takes the screen down.
Stop any separately attached audio engines first.

## Pipeline
1. **DSP**: a Schwung `plugin_api_v2` module links against `wrapper/vst2_wrap.c` + generated
   `params.h` → one `.so` exporting only `VSTPluginMain` (+ DSP init). The Force runs at 44.1k/128 frames,
   the same as the Move, so no DSP changes are needed. Compile out Move-only quirks with a `-D<NAME>_VST` flag
   (e.g. Maze's notes 0..9 knob-touch filter).
2. **Generate + build**: `tools/build_port.sh <port>/vst.json` (steps 2-3 in one; Docker). `gen_vst.py` makes the
   params table from `module.json` chain_params (VST index = order), the skin folder `<vendor> - VST - <name>/`
   (from vst.json's `layout`, else a studio auto-layout) and `pluginlist-entry.xml`. The compile uses
   `arm32v7/gcc:12` (glibc ≤ 2.39), `-fvisibility=hidden -shared -fPIC`, and links `wrapper/vst2_wrap.c` from this repo.
3. **Bench**: `tools/bench.sh build/x.so <ip>` must PASS before release (docs/BENCH.md).
4. **Offline test first**: `tools/host_test.c` on x86 with ASan (`gcc:12` image), checking two instances,
   param set/get/display, note→non-zero RMS, chunk round-trip.
5. **Deploy (staged)**: `.so` → `/sdcard/vst/x.so.new` then `mv`; skin via `tar | ssh tar -C /sdcard/Synths -xf -`
   (**don't scp paths with spaces**: escaping created a folder with literal backslashes once). Verify md5.
6. **Register** (needs MPC restart, **ask the user first**, and stop attached voice engines such as dx7_host/maze_host first):
   stop acvs → back up `MPC.settings` → insert the `<PLUGIN …/>` line before `</KNOWNPLUGINS>` (first time:
   add a whole `<VALUE name="pluginList-arm"><KNOWNPLUGINS>…</KNOWNPLUGINS></VALUE>` before `</PROPERTIES>`)
   → start acvs → check force_shadow.so is still in MPC's environ. An `.so` update alone (same path) needs only a
   restart, not a settings edit. A skin-only change needs **no restart**: swap the folder, then re-insert the
   plugin or reload the project.
7. The user tests on the device: plugin list → insert → play → edit screen → Q-Links → save/reload project.

## Gotchas
- AEffect magic `'VstP'` 0x56737450 (the forum PoC's value is wrong).
- `effGetParamName` / `effGetParamDisplay`: JUCE gives large buffers, but still cap your copies.
- Enum params: send the index as a number string to Schwung `set_param`; map `get_param` labels back.
- Skin `importFiles`/images: absolute `/usr/share/Akai/Content/Synths/...` paths.
- Never commit/publish Akai's stock skin JSON/PNGs; only describe them.

## Skin components (verified; details in mpc-vst docs/NOTES.md)
- Names: `Label` with `"type": "Name"`; values: `"type": "Value"`. Stock knobs draw no name.
- Option params: radio group of image `Button`s (`buttonId` i, `numButtonsInGroup` N, same param), with option text
  drawn into our own PNGs. `comboBox` menus open EMPTY for VST2 params, so don't use them.
- Off/on: one `Button` + "Enter Pressed → Toggle Switch". Triggers (`access:"write"`): the wrapper sends
  `audioMasterAutomate` 0 after firing so the highlight drops. The wrapper steps options on Q-Link nudges.
- Q-Links: grid numbered bottom-up; Force knob bank 1 = Q-Links 13,9,5,1,14,10,6,2 and bank 2 = those +2.
- Nested pages: same `fnKeyIndex`, `fnKeySubIndex` 0..n; Q-Link map `Tab`/`SubTab` are 1-based.

Stock reference skins are in `/usr/share/Akai/Content/Synths/*/Plugin Skins/TUI.json` (pull them to the
scratchpad to read). Generic knobs: `knobYellow` etc. from `AKAI Components/AKAI Generic Components.json`.
Switches/buttons/menus/sliders/labels (`btnBypass`, `comboBox`, `slider`, `Label`, `Focus`) are defined
**locally** in stock skins (e.g. Bassline); copy those definitions into our `localComponentDefinitions`.

## MIDI-generating plugins (sequencers/arps)
MPC OS ignores VST MIDI output (`audioMasterProcessEvents` goes nowhere). Instead, open an ALSA seq port from
the plugin (`poc/midiport.c`: `snd_seq_open` → `snd_seq_create_simple_port` READ|SUBS_READ → `snd_seq_event_output_direct`,
link `-lasound`; build needs `apt install libasound2-dev` in the arm32v7/gcc:12 container). MPC hot-detects the
port with no restart; the user enables Track on it in Preferences → MIDI. Sync from `audioMasterGetTime` ppqPos/tempo.
Name ports plainly (e.g. client "<Plugin>", port "MIDI Out"): no "(Mockba)" suffix; the user wants MockbaMod
references kept out of mpc-vst.

## Custom layouts from Force Shadow pages (preferred for final skins)
Put a `layout.conf` next to the port's vst.json (Maze: `force-maze/maze-voice/vst/layout.conf`), in
shadow_page.conf widget syntax plus `qlinks "PAGE" = key,...` lines (each one is a nested page with the same design and
its own Q-Links) and `rows=` on enum_h, and set `"layout"` in vst.json. gen_vst.py then calls `shadow_skin.py` (mpc-vst/tools), which drives
`shadow_art` (built from `shadow_art.c` with `-I<force-shadow>/tools`, since it #includes render_conf_preview.c) to draw
backgrounds, knob filmstrips and button states. Shadow y−86 = skin y. Option counts must match module.json.
Check offline before deploying: composite TUI.json + PNGs into a preview image (paste each component at its bounds)
and look at it. Skin-only changes need no restart.

## Param opt-ins beyond a plain knob (added porting jv880; details + rationale in docs/NOTES.md)
`module.json` chain_params entries feeding `gen_vst.py` can carry:
- `"display": "string"` -- `param_t.string_display`: the wrapper passes `get_param`'s text straight to
  `effGetParamDisplay` instead of `atof()`-reformatting it. Needed for any readout that's a name/label, not
  a number (bank/patch names) -- without it the text collapses to "0".
- `"display": "int"` -- `param_t.int_display`: forces a whole-number `%.0f` instead of one decimal place.
- `"step_of": "<key>", "step_delta": N` on a `"kind": "trigger"` param -- `param_t.step_target`/`step_delta`:
  a stepper arrow that nudges a DIFFERENT param by reading its live DSP value and writing back `+N`, clamped
  to that param's declared min/max. Use when the DSP has no native `_prev`/`_next` verb for the value you
  want to step (jv880's `preset`: no such verb existed, so `preset_next`/`preset_prev` silently did nothing
  until switched to this mechanism). **Set the target's declared max generously** -- it's a static VST bound
  hand-written ahead of time, and if it undershoots what the DSP can actually reach (e.g. with optional
  expansion content loaded), values above it get silently clamped back down mid-browsing.
- A stepper's displayed text can read from a *different* key than the one it steps (`get=` in layout.conf,
  see below) -- e.g. step `preset` but display `patch_name`.
- Any param whose `set_param` does real synchronous work (memcpy, disk read/unscramble, file load) must be
  a discrete trigger/stepper, **never** a knob/slider -- a continuous control can fire many rapid calls from
  one touch/drag gesture and stack up into a multi-second hang.
- A readout with a deliberately degenerate `min==max` range (so MPC can't detect its own reported value
  changing) needs the plugin to call `audioMasterUpdateDisplay` itself whenever the text should refresh, or
  it never re-polls. Defer it: set a `need_update_display` flag in `setParameter` and fire the host call from
  `processReplacing` (same pattern as the existing `w->release[]` deferred-automate array) -- calling the
  host directly from inside its own call into the plugin is the established no-no in this wrapper.
- `vst.json`'s `"title_font"` (a `.ttf`/`.otf` path, e.g. a real downloaded font under an OFL-style licence,
  never a recreation of a manufacturer's proprietary font) overlays frame titles in that font via PIL after
  the PNGs are drawn; off by default, every other port keeps its current look.
- In `layout.conf`, a stepper's `prev=`/`next=` can call a different param's key than the one it displays,
  and `get=` (paired with the widget's own separate "Text" handle) can display a different key than the one
  it steps -- both needed together when the DSP's stepping verb and its human-readable name live on
  different params.
- **One Q-Link bank per tab, always** -- if a tab's control count would otherwise force a second Q-Link
  bank, MPC still shows its own sub-page navigation UI (dots/arrows) even when both banks render identical
  content, which reads as broken. Prefer curating each tab down to <=16 Q-Link-worthy controls (a priority
  ranking -- knobs/levels first, enums/time-stage controls next, toggles/buttons last -- picks which ones
  keep a physical knob; everything else stays touch-only) over ever letting a tab spill into a second bank.
- `tools/bench.sh` understates CPU cost for a plugin whose real work runs on an independently wall-clock-paced
  background thread: the bench harness has no pacing and races through blocks far faster than real time. For
  such a plugin, sample real cost live instead: `/proc/<pid>/task/<tid>/stat` deltas against `/proc/uptime`
  while actually playing it on-device.

## Skin studio (layout design)
`tools/studio.py`: `auto` (params → first-pass layout.conf), `to-svg` / `from-svg` (Inkscape round trip; tabs are layers,
controls are labelled groups, Q-Links in layer descriptions), `preview` (built skin → PNGs). Read docs/SKIN_STUDIO.md.
Always `preview` before deploying. Enum `options=` are optional in layouts (they default to the parameter's own).

## CPU check and release
- `tools/bench.sh build/x.so <ip> -j`: plays the plugin on the device (idle, chords, Q-Link sweep, release tail),
  thread-CPU timed, verdict PASS/WARN/FAIL against the 2902 µs block (docs/BENCH.md). Nothing installed; MPC keeps running.
- `tools/release.py`: one shareable zip (payload + install.sh/uninstall.sh + generated INSTALL.md + SHA256SUMS); the
  installer stops/restarts MPC, so installing a release on the user's device needs their go-ahead (docs/RELEASING.md).
- `tools/probe_device.sh` (read-only): arch, CPU, audio workers, plugin formats. VST3 is **not** compiled into MPC OS
  (Force, 2026-09-24): don't build VST3 ports.
