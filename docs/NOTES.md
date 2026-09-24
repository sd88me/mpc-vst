# Notes: native VST2 plugins on MPC OS standalone

Origin: mpc-forums thread "Proof of Concept: Custom Standalone Plugins"
(viewtopic.php?f=48&t=220981, Sep 2026). Verified on a Force (MPC OS, with MockbaMod) 2026-09-23; paths below are
from the Force and may differ on MPC Live/One/X/Key (e.g. `Force Documents` vs `MPC Documents`).

## Facts (verified)

- `/usr/bin/MPC` contains `"pluginList"` + `"-arm"`, `KNOWNPLUGINS`, `VSTPluginMain`.
- Settings: `/media/az01-internal/Settings/MPC/MPC.settings`. Edit **with `acvs`
  stopped**; back it up first (malformed XML ⇒ MPC resets it to defaults).
- Entry format:
  `<PLUGIN name="X" descriptiveName="X" format="VST" category="Synth|Effect" manufacturer="V"
  version="1.0" file="/sdcard/vst/x.so" uid="<hex uniqueID>" isInstrument="0|1" fileTime="0"
  infoUpdateTime="0" numInputs="2" numOutputs="2" isShell="0"/>`
- Device: armv7l, glibc 2.39 (build with an older glibc, e.g. `arm32v7/gcc:12` docker = 2.36).
- Audio: 44100 Hz, 128-frame period, which is identical to the Move/Schwung, so DSP runs unmodified.
- AEffect magic must be `'VstP'` (0x56737450). **The forum snippet's magic is wrong.**
- Instruments: set `effFlagsIsSynth`, category 2, answer `effCanDo "receiveVstEvents"`;
  MIDI arrives via `effProcessEvents`.
- Tempo: `audioMasterGetTime` with `kVstTempoValid` works for synced LFOs.
- State: `effFlagsProgramChunks` + `effGetChunk`/`effSetChunk`.
- Skin search path is `SynthContentLocations` in MPC.settings; `/sdcard/Synths` is one of them
  (also `/media/662522/Synths`, `/usr/share/Akai/Content/Synths`).
- Skin folder name `<manufacturer> - VST - <plugin name>` matched. Reference skins:
  `/usr/share/Akai/Content/Synths/*/Plugin Skins/` (e.g. Decimator = simple, Bassline = 4 tabs,
  knobs + `btnBypass` + `slider` + `comboBox` + `Label` + `Focus`).
- `importFiles` in `TUI.json` resolve relative to the skin; use absolute
  `/usr/share/Akai/Content/Synths/...` paths when the skin lives elsewhere.
- Component library: `AKAI Components/AKAI Generic Components.json` (knobBlack/Blue/Green/Grip/
  Point/Red/Silver/Witch/Yellow). Bassline defines its own `btnBypass`, `comboBox`, `slider` locally;
  that's where to copy switch/button/menu definitions from.

## Open issues (from first Maze Voice test)

1. ~~Enums render as knobs.~~ Done: option params are radio groups of image `Button`s
   (`buttonId` i of `numButtonsInGroup` N, all bound to the same parameter, option text drawn into our PNGs,
   as in the stock Hype skin). A VST2 can't give MPC value lists, so `comboBox` menus open empty. Off/on params are toggle
   buttons; `access:"write"` params are triggers (the wrapper sends `audioMasterAutomate` 0 after they fire).
   Q-Link nudges step option params one option at a time (wrapper `setParameter`).
   Original note: Labels are right. Next: use a `comboBox` (menu) for multi-option
   enums and a button type (`btnBypass`-style, 2 states) for on/off and momentary (`rnd_go`).
   Copy the definitions from Bassline's `localComponentDefinitions`, since they are local there, not
   in the generic library.
2. ~~Some knobs show blank~~ Done: stock knobs only draw their value; names come from background
   art. We add `Label` `"type": "Name"`. Original note: **Some knobs on a page show blank** while their Q-Link works. Suspect: a missing `Label`
   component (stock skins add Labels and `Focus` overlays), or knob bounds/`showWhenDataModelInvalid`.
   Compare the rendered page against Bassline's component set.
3. Note timing is quantised to 128-frame DSP blocks (same as Move).
4. ~~`gen_vst.py` is Maze-specific.~~ Done 2026-09-24: `tools/build_port.sh` + a per-port `vst.json`; Maze builds through
   it byte-identically (same `.so` md5).
5. Maze's knob-touch note filter (notes 0..9) is compiled out with `-DMAZE_VST=1`, which is built but
   **not yet deployed** (md5 b10668a4…).
6. **MIDI-output plugins: MPC OS ignores plugin MIDI out** (tested 2026-09-23 with `poc/midiout.c`).
   MPC never asks canDo `sendVstEvents`/`sendVstMidiEvent` (only `receiveVstMidiEvent`, `bypass`);
   `audioMasterProcessEvents` is accepted silently and the events go nowhere. A plugin can't be picked as
   a MIDI input on another track, and the track's "MIDI send to" only forwards the notes coming *into* it.
   Transport/tempo via `audioMasterGetTime` do work (flags 0x7fc4, tempo and ppqPos valid), and so does MIDI in.
   **Workaround, verified 2026-09-23 (`poc/midiport.c`):** the plugin opens its own ALSA sequencer client/port
   (`snd_seq_create_simple_port`, CAP_READ|SUBS_READ; link `-lasound`, which ships with MPC OS) and sends notes
   with `snd_seq_event_output_direct`, synced to host `ppqPos`/tempo. MPC's own seq client ("MPC") hot-detects the
   new port, creates a matching input ("<client> <port>") and connects it with no restart. Enable Track on it in
   Preferences → MIDI, then any track can select it as MIDI input. Plugin sequencers/arps can drive other tracks.
   Most likely stock MPC OS behaviour: MockbaMod's MidiLoop (`tkgl_anyctrl_lt.so`) only filters or blacklists
   ports; it doesn't create them. Not yet confirmed on a stock unit. Latency is about one audio block (direct, unscheduled send).

## Skin layout facts (verified 2026-09-23)

- Nested pages: several `tabs` entries with the same `fnKeyIndex` and `fnKeySubIndex` 0,1,2…; Q-Link map
  entries use `Tab` = fnKeyIndex+1, `SubTab` = fnKeySubIndex+1 (as in stock DrumSynthMulti).
- Q-Link numbering: the 4x4 grid counts from the bottom row up (stock: top-left control = Q-Link 13). With
  `Bank Direction: Column`, the Force's 8 knobs read bank 1 = Q-Links 13,9,5,1,14,10,6,2 and bank 2 = those +2.
  `gen_vst.py` lays each page out as 2 rows of 8 (row = knob bank) and maps through that order.
- Custom geometry (2026-09-23): `tools/shadow_skin.py` takes a Force Shadow style layout file per port
  (Maze: `force-maze/maze-voice/vst/layout.conf`) and emits `TUI.json` with free placement, a per-tab
  background image (frames and labels baked in), a knob filmstrip per radius (128 frames, `numFrames` 127, as in
  stock strips), and on/off images for toggles, triggers and option segments. Shadow canvas y 86..714 maps to the
  1280x628 plugin area. Several `qlinks` lines in one tab become nested pages that share the design but have
  different Q-Link sets. Values are MPC `Label` `Value` components (Titillium; the baked labels use the shadow font).
  Not yet seen on device.

## Beyond synths: apps as plugins (probe run on a Force 2026-09-24)

A VST2 plugin is ordinary native code inside the MPC process, which has root, the network and the
filesystem. force-shadow (LD_PRELOAD in the same process) already makes HTTP calls and writes files from there,
so a plugin should be able to as well. `poc/netprobe.c` checks this on a device: DNS + HTTP, a file write into the
documents folder, `posix_spawn` of `/bin/sh` and of a script on /sdcard (noexec check). Log: `/tmp/netprobe.log`.

Rules for app-like plugins:
- Never block the audio thread: network, disk and child processes go on a worker thread; audio goes through a ring buffer.
- Spawn children with `posix_spawn` (vfork-style), not `fork()`: forking MPC's large, multithreaded,
  real-time process copies its page tables (audio dropouts), and only async-signal-safe calls are allowed before exec.
  The webstream/cratedigger core uses `fork()` in two places (yt-dlp daemon, ffmpeg pipe), so switch those.
- A plugin crash takes MPC down with it, so risky parts (yt-dlp/Python, ffmpeg) belong in a child process.
- UI is only the skin: parameters with display strings, static images, buttons. No text entry, no dynamic lists or
  images. Dynamic text works through parameter display strings, e.g. "Result 1..8" slot params whose value text
  is a track title, and filter enums for genre/style/decade.
- Files the plugin writes appear in MPC's browser; the plugin can't tell MPC to load a program.

Probe results (Force, MPC OS + MockbaMod, 2026-09-24): the plugin runs as **uid 0**. DNS + HTTP work
(example.com 200; api.discogs.com answers 301 to HTTPS, so real APIs need TLS). Writing to
`/sdcard/Force Documents/` works. `/sdcard` is ext4 mounted rw with exec allowed.
**Spawning children:** `posix_spawn` itself works, but the child inherits MPC's environment, and on MockbaMod
units that includes `LD_PRELOAD` of C++ libraries (mockbaMagic.so, tkgl_anyctrl_lt.so) that fail in a plain
process ("undefined symbol _ZSt4cout", exit 127). Always spawn with a cleaned environment (drop LD_PRELOAD).
Stock MPC OS has no such preload.
- HTTPS: MPC OS ships `/usr/lib/libcurl.so.4` (8.x), `libssl.so.3` and `libcrypto.so.3`, so a plugin can `dlopen("libcurl.so.4")` for HTTPS without bundling TLS.

## Dynamic text in skins (verified on a Force, 2026-09-24, `poc/textprobe.c`)
MPC polls parameter **value text** (`effGetParamDisplay`) by itself: a counter the plugin changes with no
notification at all ticks live on a `Label` `Value`. Parameter **names** (`Label` `Name`) also update live when the
plugin calls `audioMasterUpdateDisplay` (opcode 42). So readouts (status, time, now playing), filter names and
result lists can be plain parameters whose display text the plugin changes. Name changes without
UpdateDisplay are untested; call it whenever a name changes.

## Skin studio text: readability findings (2026-09-24, building the jv880 port)
- `label_cmds()`'s baked text (knob/toggle/slider/enum_h/enum_v names) and `shadow_art.c`'s
  `seg`/`button` commands, plus `render_conf_preview.c`'s `frame_box()`/`widget_button()` titles, all
  used a fixed 1.5x scale on the 9x9 bitmap font (`font8x8.h`). That font DOES have lowercase
  (`font_chars` includes a-z), but converted layouts that kept the original all-caps shadow_page.conf
  labels ("CUTOFF", "TVF DEPTH") at 1.5x read as too wide/shouty. Fixed by lowering the scale to 1.15
  everywhere it's used (`shadow_skin.py`'s `LABEL_SCALE`/`text_width`, `shadow_art.c`'s `seg` command,
  and `render_conf_preview.c`'s `frame_box()`/`widget_button()`) and switching generated label/title/
  option text to Title Case (an acronym allowlist keeps e.g. LFO/TVF/FXM from becoming "Lfo"/"Tvf").
  `render_conf_preview.c` is mpc-vst's actual skin-asset renderer (shadow_art.c `#include`s it), not a
  preview-only tool here, but it's a separate hand-ported copy from force_shadow.c's own on-device
  renderer, so this change doesn't touch the real Force's live rendering.
- A long `enum_h` option row (e.g. an 8-option reverb TYPE) reads cramped in one row; wrapping to
  `rows=2` (already-supported layout.conf syntax) once options exceed ~6 fixes it.
- An `env`-style bar/knob group (or any control whose *component bounding box* is taller than its own
  visible art, e.g. a slider's box padded for its value-label text below) must be positioned by that
  box's real height, not by eyeballing the visible art's center — sizing off a frame's own content
  area (skip its title+divider band, ~44px) and computing the component center from the box height
  keeps it from poking into the frame's title text. Diagnosed by comparing a skin's baked `sh_bg_N.png`
  (correct) against `tools/studio.py preview`'s composited output (showed the corruption) — the studio.py
  preview pastes each component's real filmstrip art at its real bounds, so it catches oversized-bounds
  bugs invisible in the background PNG alone.

## Control names: native Label "Name" beats baked bitmap text (2026-09-24)
The font-scale/Title-Case fixes above (previous entry) still read as "monospace" for real words:
`font8x8.h`'s glyphs mostly fill their whole 9-column cell, so trimming the advance to each glyph's
actual ink width (also tried) barely helped -- the letterforms themselves are blocky pixel art, not a
real typeface, so no amount of scale/advance tuning gets genuinely "normal typed font" spacing out of
it. The real fix: MPC's own native `Label` `"type": "Name"` component (same mechanism as the existing
`"type": "Value"` one) shows the assigned parameter's name using MPC's own on-device Titillium Web
font -- real proportional metrics, rendered by the device itself, zero relation to shadow_art.c's
baked font. `shadow_skin.py`'s knob/toggle/slider_v/slider_h defs now include a `_name_label()` sub
alongside the existing `_value_label()`; `label_cmds()` no longer bakes text for those three kinds
(it still does for frame titles and enum_h/enum_v's own group label + per-option segment text, none of
which bind to a single parameter index the way Name/Value can).
Consequence: `tools/studio.py preview` couldn't show this at all (it only drew an outline box for any
`Label`, since it has no access to a real on-device font) -- upgraded it to render actual text for
`Name` labels via Pillow's own bundled scalable font (`ImageFont.load_default(size=...)`, present in
Pillow 10.1+; the `python:3.11-slim` container's `pip install pillow` pulls a recent enough one). Not
pixel-identical to Titillium Web, but proportional and good enough to sanity-check spacing/overlap
offline before ever touching a device.

## Real-device findings from the jv880 skin's first hardware test (2026-09-24)
A screenshot of the actual device (not the offline `studio.py preview`, which can't catch these)
turned up two more bugs, both now fixed:
- **Stepper arrows showed the word "Button"** literally overlapping the arrow glyph. A `_button()`
  with `onImage`/`offImage` = `""` (no asset) makes MPC render a generic placeholder caption instead
  of nothing. Fixed by cropping the arrow glyph already baked into that tab's background (drawn by
  `widget_stepper`/`dot_stepper`) as the tap-zone's own image, instead of an empty string.
- **MPC's bottom function-key tab strip is genuinely unreadable with long qlink bank names.** Nested
  `qlinks "<name>" = ...` banks (multiple sub-pages per tab, docs/PORTING.md) each set that sub-page's
  name in MPC's own tab strip -- and that strip is not sized for 20+ character names, even
  truncated: on device it read as one unbroken, mid-word-truncated run-on across the whole strip
  ("OUTPUT + MACROS (OF CONTROL + BEND / POR TONE 1 WAVE / PITCH + ..."). Stock skins (single qlinks
  bank per tab, e.g. Maze) never exercise this since they only ever show ONE short tab name. **Keep
  every qlinks bank name short (one or two words, no concatenated frame titles)** -- jv880 uses
  "Play"/"Sends", "Patch"/"FX", "Tone N"/"Env N"/"LFO N".

## String-valued display params showed "0" (2026-09-24, jv880: bank/patch name readouts)
`vst2_wrap.c`'s `effGetParamDisplay` unconditionally reformatted every parameter's `get_param()`
string through `atof()` + `snprintf("%.*f", ...)` -- fine for a real numeric display ("63.5"), but
it silently destroys any non-numeric string (a bank name, a patch name, a status message) down to
whatever leading digits `atof` can parse, which for text like "Preset A" or "A.Piano 1" is nothing,
hence the field just showed "0". Fixed with an explicit opt-in: `param_t` gained a `string_display`
field (`gen_vst.py`, from a chain_params entry's `"display": "string"`), and `effGetParamDisplay`
copies the DSP's string straight through when it's set instead of reformatting it. Verified against
real ROMs on x86 (`patch_name` -> `'A.Piano 1   '`, `bank_name` -> `'Preset A'`) before redeploying.
A second, related bug: a `stepper`'s displayed text was hardcoded to the SAME parameter it Q-Link
steps (e.g. jv880's numeric `preset` index), when the design wants a DIFFERENT parameter's text
(`patch_name`) shown instead. Fixed by giving `stepper` an optional `get=<key>` attribute (mirroring
Force Shadow's own shadow_page.conf attribute of the same name, dropped during the jv880 conversion)
that binds the text label to its own `"Text"` handle (`_placed()`'s `extra=` param) independent of
the stepper's own `"Data"` handle -- so the arrows/Q-Link still nudge `preset`, but the center text
shows `get_param("patch_name")`.

## bench.sh understates real cost for plugins with a real-time-paced background thread (2026-09-24)
jv880's real synthesis work happens on its own thread (`jv880-emu`), which paces itself against the
actual wall clock to keep a small ring buffer full for real-time playback -- not against how many
`render_block()`/`processReplacing()` calls have happened. `tools/bench.c` has no `sleep`/pacing
anywhere: it calls `processReplacing()` back-to-back as fast as the CPU allows, so a stage that's
*supposed* to represent ~1s of real playback can complete in a few ms of actual wall-clock time. The
background thread, pacing itself to the real clock, sees almost no elapsed time and does almost no
work in that window -- but `threads%`'s formula (`(proc - self) / (nblocks * BUDGET_US)`) divides by
the *real-time-equivalent* duration regardless of how little wall-clock time actually passed, so the
true cost gets divided away to near-zero. Measured on the device: `tools/bench.sh` reported
0.2-0.5% "threads%" for jv880 (a clean PASS), but sampling the real `jv880-emu` thread's CPU ticks
via `/proc/<pid>/task/<tid>/stat` against `/proc/uptime` while it was actually loaded on a track and
playing gave **20.8% of one core, sustained** -- roughly matching force-jv880's own "2.81x real-time"
claim (`1/2.81 ≈ 36%`), and nothing like the bench's number. This is exactly the class of plugin
docs/BENCH.md's own "Limits" section already warns about ("app-style plugins... do their real work in
worker threads... watch `top` on the device instead") -- jv880 just wasn't recognized as fitting that
category until checked directly. **For any port whose real work runs on a background thread paced to
the wall clock (not to render_block() call count), don't trust `bench.sh`'s `threads%` at all** --
sample the real thread's ticks on the device during actual playback instead:
```
u1=$(awk '{print $14}' /proc/<pid>/task/<tid>/stat); s1=$(awk '{print $15}' ...); t1=$(awk '{print $1}' /proc/uptime)
# ...play for N seconds...
u2=...; s2=...; t2=...
# cpu% = (u2-u1 + s2-s1) / 100 (ticks/sec, usually HZ=100) / (t2-t1) * 100
```

## Qlink curation for tabs with >16 controls (2026-09-24, jv880 port)
A tab with more controls than one 16-key Q-Link bank needs several `qlinks "<name>" = ...` lines
(already-supported nested-page mechanism, docs/PORTING.md). A naive "first 16 in source order"
split is a bad default for a busy tab: it silently drops every control past the 16th and often grabs
all of one section while missing others entirely (e.g. a Tone tab's Pitch Env only, missing Filter/
Amp Env and both LFOs). Better: group by FRAME first (accumulate whole frame-sections into a bank
until the next one would push past 16, then start a new bank named after the section(s) it holds),
so every control ends up in some bank and each bank reads as one coherent area (e.g. jv880's Tone
tabs split cleanly into "Wave/Pitch + Pitch Env" / "Filter Env + Amp Env" / "LFO 1 + LFO 2", 44
controls in 3 evenly-sized banks instead of losing everything past the first section).

## Stepper "_prev"/"_next" needs a real DSP verb, or a step_of/step_delta opt-in (2026-09-24, jv880)
A stepper's arrows were bound to "<key>_prev"/"<key>_next" as if the DSP understood those literal
keys as increment/decrement verbs -- it doesn't have to. jv880's `preset` has no such verb (only an
absolute `set_param("preset", N)`), so the arrows silently did nothing (confirmed on device, then
root-caused and fixed before touching it again). Two independent fixes, both needed depending on
what the DSP actually offers:
- **A real verb under a different name** (jv880's bank: `next_bank`/`prev_bank`): give `stepper` an
  explicit `prev=<key>`/`next=<key>` override (mirrors Force Shadow's own shadow_page.conf attribute
  of the same name) so the arrows call the real verb directly, while the stepper's own `key` can be
  an inert dummy (Q-Link nudge on it is a no-op).
- **No verb at all** (jv880's preset): a param can declare `"step_of": "<key>", "step_delta": N`
  (gen_vst.py, -> `param_t.step_target`/`step_delta`) to nudge that OTHER param by a fixed amount
  instead -- the wrapper reads its current value straight from the DSP, adds the delta, clamps to
  its min/max, and sets it back. This trigger's own key is never sent to the DSP at all.
Verified against real ROMs on x86 before redeploying: `preset_next` x3 advances the patch (name
text updates each step), `preset_prev` reverses it, `next_bank` switches banks with `patch_name`
updating to match.

## Q-Link banks used to share one screen; frames must stay atomic across banks (2026-09-24, jv880)
Every Q-Link bank of a multi-bank tab rendered the SAME screen -- only the physical Q-Link mapping
differed underneath (`build()`'s `componentsData` was one `kids` list built once per TAB and reused
for every bank's page def). Fine for a stock skin that only ever uses one bank per tab, but not a
real multi-page design (confirmed via user feedback + an offline preview: Play/Sends both showed
Output+Macros+Effect Sends together). Fixed: widgets are grouped into frame-based segments, and each
bank's page gets its OWN background + component list, containing only the frame(s) that have a key
in that bank (a `persistent=1` readout/stepper, e.g. a tab-level bank/patch bar, opts into every
bank without needing its own frame). This makes frame membership a hard constraint: `make_banks()`
(the port's own converter) must never split one frame's keys across two banks, or `build()`'s
"include a frame if ANY of its keys are in this bank" rule pulls the WHOLE frame into both (found
exactly this way: Effect Sends' reverb key had been grouped into the Play bank, so the Sends bank,
which had the frame's OTHER keys, showed the whole frame too -- chorus/tones included).

## Optional real-TrueType frame titles (2026-09-24, jv880)
shadow_art.c's baked 9x9 bitmap font, even Title-Cased and tightened (see the font-spacing entries
above), is blocky pixel art, not a real typeface -- there's a ceiling on how good "normal typed
spacing" can look baked that way. `vst.json`'s optional `"title_font"` (a `.ttf`/`.otf` path) makes
`shadow_skin.py` draw frame titles with a real font via Pillow instead: the background script emits
`frameblank` (box only, no baked text) and a PIL pass draws the title afterward once the PNG exists.
Off by default (`SHADOW_TITLE_FONT` unset) -- every existing port keeps its exact current look.
Google Fonts' GitHub repo (`raw.githubusercontent.com/google/fonts/main/ofl/<name>/<Name>-Regular.ttf`)
is a reliable direct-download source when `fonts.google.com/download` itself returns an HTML page,
not a zip, for the same request.

## Dotted-arc knobs (2026-09-24, jv880)
shadow_art.c's `knob_body()` drew a solid ring; changed to a dotted arc (dot count/size scale with
radius) to match the JV-880 shadow mockups' "dark knob, green dotted arc, small pointer" look. Only
in shadow_art.c (this repo's own offline asset renderer) -- force-shadow's shared, on-device
`render_conf_preview.c` keeps its plain ring, so this doesn't touch how any real Force page looks.

## No draggable/graph widgets in plugin skins (checked 2026-09-24)
Pulled and inspected several stock `TUI.json` skins off the device, including AIR's own **TubeSynth**
(which has real ADSR envelopes) and **Electric**/**Hype**. The full set of distinct `type` values across
them is only knobs (`greyKnob`, `blueKnob`, `hypeKnobLarge`, …), `comboBox` variants, `fader`, `Button`,
`switchButton`, `bypassButton`, `Label`, `Value`, `Image`, `Decorator` — no graph/curve/XY-pad component
anywhere. So a Force Shadow-style draggable envelope graph (`env` widget) is not portable: Shadow can draw
one because it owns the whole touchscreen framebuffer and its own touch driver, but MPC's plugin skin is a
declarative JUCE component list bound directly to VST parameters, with no custom-drawn/gesture widget
escape hatch. Even TubeSynth, which needed one, uses plain knobs per envelope stage instead. Port envelope
UIs as knob rows (time/level per stage), not graphs.

## Native picker (menu overlay): not available to VST2 (tested 2026-09-24, `poc/menuprobe.c`)
MPC's menu overlay (`comboBox` / `Show Overlay "menu overlay"`) opens **empty** for VST2 parameters. MPC never
calls `effGetParameterProperties` (opcode 56; absent from the probe log), and a `<plugin>.vstxml` ValueType next to
the .so made no difference. Use image-button selectors (`enum_h`/`enum_v`) or steppers instead.

## VST3: not supported by MPC OS (checked on a Force, OS base 5.0.17, 2026-09-24, `tools/probe_device.sh`)
MPC's JUCE host has only `juce::VSTPluginFormat` compiled in. The binary has no `VST3PluginFormat` /
`VST3PluginInstance` RTTI and no `GetPluginFactory` string (which JUCE needs to load any VST3 module), and no LV2
either. The only `VST3` / `.vst3` strings are JUCE's wrapper-type names and a desktop-project file-extension list.
So a VST3 bundle can't be loaded, whatever the settings say, and VST3 value lists can't fix the empty picker.
Rerun the probe after firmware updates and on other models.

## MIDI-generator VST wrapping a standalone-process engine (Force Acid, 2026-09-24)
Force Acid (`force-acid`, a MockbaMod standalone process using RtMidi + a timer thread as its own
"chain host" for `acid_core.c`, midi_fx_api_v1) ports to a VST2 the same way as a plugin_api_v2 DSP for
the MIDI-out and clock problems, but needed a hand-written wrapper (`force-acid/vst/acid_vst.cpp`, not
`wrapper/vst2_wrap.c`, which assumes `render_block` audio DSP): `tools/gen_vst.py` still generates
params.h + the skin from a synthetic module.json (`chain_params` hand-transcribed from the standalone
build's CC table, kept in sync by hand) since that pipeline only cares about the key/name/min/max/options/
momentary shape, not the real host API.
- **Clock, without a physical MIDI cable:** the standalone build derives BPM/transport from real 0xF8/
  0xFA/0xFC MIDI clock (EMA of inter-pulse interval). A VST host hands this over cleanly instead:
  `audioMasterGetTime` gives exact `tempo` and `ppqPos` already, so the wrapper synthesizes the same
  24-PPQN clock byte stream from the ppqPos delta each block (`ceil(last/step)*step .. end`, step =
  1/24 quarter note) and feeds it to the engine's own `process_midi()` unchanged -- no core changes
  needed, exactly the "no new code in the core" case DESIGN.md describes for the Move->Force port.
- **Host API with no instance argument** (`host_api_v1_t.get_bpm`/`get_clock_status`, acid_core.h): fine
  to leave process-wide (one set of atomics, `move_midi_fx_init` called once), since MPC has one shared
  transport for every plugin instance anyway -- matches host_shim.cpp's own simplification.
- **MIDI out still needs the ALSA seq port workaround** (see "MIDI-generating plugins" above):
  `effProcessEvents`/VST MIDI out reaches nowhere, so generated notes go out `snd_seq_event_output_direct`
  from inside `processReplacing`, same as `poc/midiport.c`. Silence is written to the VST audio outputs
  (`numOutputs=2`, no DSP) since the plugin only exists to reach MPC's plugin-parameter automation and the
  MIDI routing UI.
- **Chunk save without a "state" key in the core:** upstream/host_shim has no preset serialisation
  (DESIGN.md's own "Known limitations"), so the wrapper builds its own `key=value;...` chunk from every
  non-momentary param's `get_param()` and replays it with `set_param()` on `effSetChunk` -- no core changes.
- Bench: **an app-style/MIDI-generator plugin's own work (clock synthesis + ALSA send) happens inside
  `processReplacing`** here (not a background thread like Crate Digger's stream player), so unlike Crate
  Digger, `tools/bench.sh` *does* exercise the real per-block cost. x86 local run (relative numbers only):
  PASS, worst block 2.0%, p99 0.1%. **Device run (Force, 2026-09-24): PASS, worst p99 0.7%, worst block
  1.4%, threads 0.0%** -- comfortable headroom for several instances alongside a live project.
- Verified with x86 host test under ASan/UBSan (two instances, enum/float param round-trip, a 400-block
  synthesized-clock run, chunk round-trip), an offline skin preview (`tools/studio.py preview`), and
  `tools/bench.sh` on a real Force. Installed and registered on a Force 2026-09-24 (`.so` on
  `/sdcard/vst`, skin on `/sdcard/Synths`, `pluginList-arm` entry added, `MPC.settings` backed up first).
  User plugin-list/insert/play/Q-Link/save-reload test on the touchscreen still pending.

## A skin needs its app's own theme copied in, not left at the tool's default (Force Acid, 2026-09-24)
Force Acid's first skin pass built and previewed without error -- correct layout, correct controls,
looked like a plausible plugin skin -- but didn't look anything like the real force-acid shadow page
(which is yellow chassis / red buttons / dark knobs, from `addon/shadow_page.conf`'s `theme_*` lines).
Cause: `vst/layout.conf` had no `style=`/`theme_*` lines at all, so `shadow_art` rendered with its own
generic default palette (cream knobs, dark plate, orange accent) -- the same palette Crate Digger's and
Maze's *un-themed* previews would also fall back to, except those two ports happened to copy their
source app's theme into `layout.conf` already, so the gap wasn't visible before. Fix: copy the
`style=`/`theme_*` block from the app's own `addon/shadow_page.conf` verbatim into the top of the
port's `layout.conf`. Mechanism: `shadow_skin.py`'s `build()` sends the whole layout file to `shadow_art`
as `theme|<layout.conf>`, which loads it with `render_conf_preview.c`'s own `load_conf()` -- the exact
theme system force-shadow's on-device renderer uses, every `theme_*` key, not just the dozen or so
`apply_theme()` uses Python-side for label text colour. This is now step 1 of docs/PORTING.md's Skin
section and called out in the skill's "Custom layouts from Force Shadow pages" section -- do this before
laying out a single control, and always compare the preview against the app's own screenshot/mockup
(not just "does this look like a plausible skin") before calling a skin done.

## Force DX7: engine-with-host-side-glue port, audio bridge left open (2026-09-24)
Ported force-dx7 (a standalone `dx7_host` process wrapping Dexed/MSFA, controlled over a
Unix control socket -- see `force-dx7/src/dx7_host.cpp`) to `force-dx7/vst/` in this repo.
Not built through `tools/build_port.sh`/`wrapper/vst2_wrap.c` (no directly-linkable DSP
module here, category 2 of docs/PORTING.md): a port-specific wrapper, `force-dx7/vst/
dx7_vst.c`, plus its own `build.sh`, `gen_params.py`, `params.json` (152 params, hand-derived
from `addon/shadow_page.conf`'s full control surface -- module.json's own chain_params only
lists a curated ~20, the Move-style "knobs" subset, not the full per-operator surface a
Force page/VST would want) and `layout.conf` (theme + GLOBAL/OP1-6 tabs copied from
shadow_page.conf; the BANKS tab was dropped -- its `list` widgets scan a live bank/patch
folder, which has no static-VST-parameter analogue).
- **Params, MIDI, chunk save/restore: wired and offline-tested.** The wrapper spawns
  dx7_host via `posix_spawn` (LD_PRELOAD stripped) if none is reachable at its control
  socket, else attaches to the one already running (matches the device's real model: one
  shared dx7_host, started from /moduler, not one per plugin instance). Every param get/set
  is a `SET key val\n`/`GET key\n` round trip; VST MIDI-in is forwarded to dx7_host's ALSA
  seq port (`DX7:In (Mockba)`) by client/port name lookup, same approach as `poc/midiport.c`
  but as the sender connecting to an existing input port rather than exposing our own for
  MPC to route into; chunk save/restore builds its own `key=value;...` blob (dx7_host has no
  "state" key), matching force-acid's `acid_vst.cpp` pattern.
- **Audio passthrough is NOT implemented -- processReplacing outputs silence.** dx7_host
  renders into a POSIX shared-memory ring (`forceAudioInject.h`) that is explicitly
  documented single-producer/single-consumer, with ForceAudioJack.so as the sole consumer
  advancing `tail` on MPC's own real-time capture thread. This VST could shadow-read `head`
  without ever touching `tail` (safe, never claims the consumer role), but that's real
  unwritten work (own read cursor, resample from the ring's 44.1k/128-frame producer cadence
  into whatever block size the host calls with) and is untestable offline without a running
  dx7_host + shm segment. Smallest viable fix instead: teach dx7_host an alternate output
  mode (e.g. `--vst-shm <name>`, or claim an otherwise-unused mix slot) that a VST wrapper
  opens as sole owner -- a small, additive change to `force-dx7/src/dx7_host.cpp`, no risk to
  the existing shadow-GUI path. Until then this port is a remote-control + MIDI-conduit
  plugin for whichever dx7_host is running; actual sound still reaches speakers only via
  ForceAudioJack, exactly as today's shadow-GUI addon.
- **Offline x86 test** (`gcc:12` container, ASan+UBSan): built `dx7_vst.c` against a
  hand-written fake control-socket server (`fake_dx7_host.c`, not force-dx7's real
  dx7_host/MSFA -- exercises the wrapper's own socket-client code, not DSP correctness) --
  PASSED: two instances, magic/flags, param set→display→getParameter round-trip (op1_level),
  chunk capture + restore onto a second instance, `effProcessEvents` with no ALSA port
  reachable (graceful no-op, no crash). **Structurally untestable offline** (both need a real
  dx7_host + force-audio-jack running on a device, not just this repo's tools): MIDI actually
  reaching Dexed's engine (needs `DX7:In (Mockba)` to exist), and anything about the audio
  ring (see above -- there is no audio path yet to test).
- **Skin preview**: `tools/studio.py preview` on the built skin composited cleanly (14 pages:
  GLOBAL, GLOBAL 2, OP1-OP6 x2). Confirmed the cyan-on-slate LCD look from
  `addon/shadow_page.conf`'s `theme_*` block came through (near-black `0f1214` background,
  cyan-ish frame lines/accent, dark knob faces) -- not `shadow_art`'s generic default
  palette -- by copying the theme block verbatim to the top of `layout.conf` per PORTING.md.
- `tools/bench.sh` not run: this port has no real per-block DSP work in `processReplacing`
  (it's silence; the real synthesis, when the audio bridge exists, runs in dx7_host's own
  process/thread, off this VST's call stack entirely -- more like Crate Digger's "app-style"
  case than a normal in-process DSP plugin, see BENCH.md's Limits section) and was not
  installed/registered on a device this session (explicitly out of scope -- device
  registration needs the user's own go-ahead).

## CPU layout (Force, 2026-09-24)
RK3288, 4x Cortex-A17 @ 1.8 GHz (governor `performance`), `isolcpus=2-3`. MPC runs `AudioWorker0-3` (SCHED_FIFO),
one pinned per core, plus `Audio Processing` (prio 20). Plugins run on these workers, so tracks spread across
cores. `tools/bench.sh` measures a plugin against the 2902 µs block (docs/BENCH.md).
