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
- Audio: 44100 Hz, 128-frame period; the engine interface (`wrapper/engine.h`) renders in exactly those blocks.
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

## Open issues (reviewed 2026-09-25)

1. Note timing is quantised to 128-frame DSP blocks (the engine interface renders whole blocks; MIDI lands
   at the start of the next one).
2. **MIDI-output plugins: MPC OS ignores plugin MIDI out** (tested 2026-09-23 with `poc/midiout.c`).
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
   ports; it doesn't create them. **Still unconfirmed on a stock unit.** Latency is about one audio block
   (direct, unscheduled send).

Resolved (details in the sections below): option params render as image-button radio groups or `popup`s,
not knobs, and `comboBox` menus stay empty for VST2 (see "Native picker"); knob names come from `Label`
`"type": "Name"`; the port builder is generic (`tools/build_port.sh` + `vst.json`, 2026-09-24); custom
layouts render on device (every port since Maze Voice).

## Skin layout facts (verified 2026-09-23)

- Nested pages: several `tabs` entries with the same `fnKeyIndex` and `fnKeySubIndex` 0,1,2…; Q-Link map
  entries use `Tab` = fnKeyIndex+1, `SubTab` = fnKeySubIndex+1 (as in stock DrumSynthMulti).
- Q-Link numbering: the 4x4 grid counts from the bottom row up (stock: top-left control = Q-Link 13). With
  `Bank Direction: Column`, the Force's 8 knobs read bank 1 = Q-Links 13,9,5,1,14,10,6,2 and bank 2 = those +2.
  `gen_vst.py` lays each page out as 2 rows of 8 (row = knob bank) and maps through that order.
- Custom geometry (2026-09-23): `tools/shadow_skin.py` takes a Force Shadow style layout file per port
  (Maze: `mpc-vst-maze/vst/layout.conf`) and emits `TUI.json` with free placement, a per-tab
  background image (frames and labels baked in), a knob filmstrip per radius (128 frames, `numFrames` 127, as in
  stock strips), and on/off images for toggles, triggers and option segments. Shadow canvas y 86..714 maps to the
  1280x628 plugin area. Several `qlinks` lines in one tab become nested pages that share the design but have
  different Q-Link sets. Values are MPC `Label` `Value` components (Titillium; the baked labels use the shadow font).
  Seen on device with every port since.

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
field (`gen_vst.py`, from a parameter entry's `"display": "string"`), and `effGetParamDisplay`
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

## A static param bound silently clamps a value the DSP tracks dynamically (2026-09-24, jv880)
`preset`'s declared VST range was `min=0, max=127` (an early guess, back when only internal patches
were being tested). The real total is 4133 once all 19 SR-JV80 expansions are loaded on this
device (`get_param("total_patches")` has the live number). Any patch index above 127 -- which is
almost every expansion-backed patch, since internal-only patches stop at 191 and expansions start
past that -- got silently CLAMPED back to 127 by the step_target mechanism's own
`if (cur > tp->max) cur = tp->max`, landing in "Preset B"'s own internal-bank range. From the
user's side this read as "stepping patches while in an expansion keeps reverting to Preset B" --
a real, reproducible bug, not a display glitch, and the DSP's own num-patches count was never wrong;
only the WRAPPER's static idea of the param's range was. Same caveat as `expansion_index`'s
declared range: a VST param's bound is fixed at build time, but the real count is device/ROM-set-
dependent, so this needed a generously oversized static max (8191), not the exact number -- a
future refinement could read the live count via `get_param("total_patches")` at clamp time instead
of trusting the static declaration, for a port where this matters more precisely.

## A readout bound elsewhere (get=) needs audioMasterUpdateDisplay to ever refresh (2026-09-24, jv880)
Found live on a real device: patch_name/bank_name-style readouts (a stepper's get= binds its
displayed text to a DIFFERENT param than the one it steps, via a separate "Text" handle -- see the
"shows 0" entry above) painted correctly ONCE, then never updated again, regardless of whether the
underlying param changed via the stepper's own arrow tap or a direct Q-Link turn on the stepped
param. Root cause: such a readout is deliberately given a degenerate min==max range (its own
reported normalized value never changes, since nothing should ever Q-Link-nudge it meaningfully),
so MPC has no value-change signal telling it to re-poll THAT param's display text just because some
OTHER param changed it indirectly -- there's nothing to notice.
`audioMasterUpdateDisplay` (opcode 42) is the fix, already documented above as refreshing a Label
"Name" -- calling it makes MPC re-poll everything currently displayed, sidestepping the fact that it
has no way to know get='s cross-parameter dependency exists. **Not safe to call directly inside
setParameter()** though: this repo's wrapper already has an established rule against re-entering the
host from inside its own call to us (`w->release[]`'s existing comment, same reasoning -- momentary
triggers already defer their own `audioMasterAutomate` call to `processReplacing()` for exactly this
reason). Added a `need_update_display` flag alongside it, consumed the same way: coalesced to at
most one `audioMasterUpdateDisplay` per audio block, regardless of how many params changed within
it. Worth remembering for ANY future `get=`-bound (or otherwise cross-parameter-dependent) readout:
it needs this call somewhere, or it will only ever show its initial value.

## A continuously-nudgeable control on a synchronous, expensive DSP action can "hang" the plugin (2026-09-24, jv880)
Reported on device as "banks and patches hanging, says loading emulator". Root cause: jv880's
`jump_to_expansion` does a synchronous 8MB `memcpy` (plus a first-access disk read+unscramble --
measured ~900ms against real ROMs) with no debounce, and it was bound to a plain continuously-
nudgeable `knob`. One touch/turn gesture can fire several `setParameter` calls in quick succession
(each a real, distinct value along the drag), so a single knob nudge could queue up multiple ~1s
synchronous DSP calls back to back -- easily several real seconds of apparent hang. Notably, the
same DSP's own `preset` parameter handler already defers/debounces a cross-expansion patch change
by design (~9ms) for exactly this reason; `jump_to_expansion` just didn't have the same protection.
Fix was two-sided: added real `next_expansion`/`prev_expansion` DSP verbs (one bounded transition
per call, mirroring `next_bank`/`prev_bank`) and switched the control to a `stepper` -- an arrow tap
is structurally one discrete UI event, so it can't flood the DSP the way a knob drag can. **General
rule for a port: any VST parameter whose DSP-side `set_param` does real, slow, synchronous work
should be a discrete trigger/stepper, never a continuously-nudgeable knob or slider** -- a knob's
whole *value range* being reachable by one drag gesture means the DSP has to be able to absorb many
rapid calls, which is a much stronger requirement than "one value change is affordable".

## Split-screen Q-Link banks: tried, reverted -- the real fix was one bank per tab (2026-09-24, jv880)
The "every bank shows the same screen" architecture (see the section above) was built out into
real separate-screen pages, then reverted after user feedback on the actual device: a small tab
(Play/Sends, 17 controls -- one over the 16-key Q-Link limit) read as needlessly fragmented across
two screens when it fit comfortably on one combined page (this is, after all, exactly how the
original shadow page worked -- several Q-Link banks, one screen). The per-bank-page mechanism
(frame-based segments, `persistent=1`) was reverted entirely rather than left as a half-used code
path.

That combined-screen revert still left MPC's own sub-page NAVIGATION in place (the dots/arrows
letting you swipe between Play and Sends), even once their content was identical -- confirmed with
the user this was still the actual complaint, not just a display bug, before changing anything
further. The real fix isn't in mpc-vst-plugins' shared tooling at all: a tab only gets multiple
Q-Link pages because its OWN `layout.conf` declares multiple `qlinks "..." = ...` lines --
`shadow_skin.py` just does whatever the layout asks for. So a port that wants ZERO sub-page
swiping, even for a genuinely busy tab (jv880's Tone tabs, 44 controls), emits exactly ONE
`qlinks` line per tab, capped at 16 keys by priority (a main knob or an envelope LEVEL first, an
envelope TIME or enum selector next, a toggle/trigger last) -- the rest stay on screen and
touchable, just without a dedicated Q-Link knob. Confirmed directly with the user which tabs
should get this treatment (all of them, accepting that Tone tabs lose knob access to roughly half
their controls) rather than guessing a third time on a design question this session had already
gotten wrong twice.

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

**Why Hype/TubeSynth's setup tabs look like they use it (checked on-device, 2026-09-24):** their
`TUI.json` (`/usr/share/Akai/Content/Synths/AIR Music Technology - MPC - {Hype,TubeSynth}/Plugin Skins/`)
does bind real `comboBox`/`blueComboBox` components straight to a plugin parameter (e.g. Hype's "Mode",
"Legato Mode", "MW Dest", "Ctrl LFO Shape"; TubeSynth's "Polyphony" is a `blueComboBox`), with no
option-text list embedded in the skin JSON — so the value list has to come from somewhere live, same
shape as our probe expected. But **Hype and TubeSynth are not external VST2 plugins at all**: there is
no `Hype`/`TubeSynth` `.so` anywhere under `/usr`, they never appear as `PLUGIN` entries in
`MPC.settings`, and `strings /usr/bin/MPC` shows them as internal DSP part types (`H3Part Type='Hype'`,
`Type='AnaloguePoly' Name='TubeSynth'`) baked directly into the MPC binary alongside AIR's other stock
instruments (Bassline, TubeDrive). They just reuse the VST-plugin-skin *format* (`TUI.json`,
`localComponentDefinitions`, the same component type names) for their UI. Because MPC owns the DSP
object directly with no VST2 ABI in between, it can supply the picker's live value list itself —
something a real, external, VST2-loaded plugin (including ours) structurally cannot get MPC to do,
since that path never calls `effGetParameterProperties`. Conclusion unchanged: for a real plugin, the
native picker is not available — this only rules out one theory for why *stock* skins can use it.

**Retest with a correct `.vstxml` (2026-09-24): still empty.** The first probe's `.vstxml` was malformed for
JUCE: `juce::VSTXMLInfo` (compiled into `/usr/bin/MPC`, as are `VSTParametersStructure`/`numberOfStates`)
only parses children of `<VSTParametersStructure>`, and the `<ValueType>` sat outside it, with no
`numberOfStates`. Fixed file (`poc/menuprobe.vstxml`, ValueType inside, `numberOfStates="4"`, plus a
states-only param) **was** read: MPC called `effGetParamName` only for the one param not in the xml, so JUCE
took names (and so the value strings) from it. All four menus still opened empty. So MPC's menu overlay
does not use JUCE's hosted-parameter value strings for a VST2 param; the list only exists for MPC's internal
instruments. Don't retry `.vstxml` / parameter properties.

## Conditional visibility works for VST2 params: `IndexedEnabling` (tested 2026-09-24, `poc/menuprobe_skin.py`)
A component's `bounds.additionalInvalidatingHandles: ["IndexedEnabling/<i>/<N>/Parameter <p>"]` shows it
only while parameter p, read as an N-way choice, is at index i (stock use: AIR Amp Sim swaps its whole
background image per amp model; also AIR Diff Delay, TouchFX, Hype's GUI-Popout). On the probe, four stacked
Value labels per param with `IndexedEnabling/0..3/4/Parameter p` showed exactly one at a time, following the
knob, for both a param with `.vstxml` states and one without — so MPC computes the index from the skin's N
and the normalized value itself; the plugin needs no metadata. Stock skins pair it with
`showWhenDataModelInvalid: "Show"`. Opens up: mode-dependent panels (show a different control set per osc
type), pictures that follow a value (per-waveform image), and a self-drawn pop-up picker (a hidden "open"
param toggled by tapping the field, an option list visible only while it's open).

**Pop-up picker prototype: works (2026-09-24, probe's PICKER tab).** Field = local component with
`Mouse Down`/`Enter Pressed` → `Toggle Switch` on the "open" param (Data handle), showing the enum's value via
a second `Text` handle. Panel image + one radio-group `Button` per option (bound to the enum), all with
`IndexedEnabling/1/2/Parameter <open>` and placed after the other page components. On device: tap opens it;
a visible panel takes the touch over a control underneath (no pass-through); a hidden panel takes no touches.
Auto-close: the plugin clears "open" when the enum is set while open and reports it with
`audioMasterAutomate(open, 0)` from `processReplacing` (not from inside `setParameter`); MPC re-evaluates the
visibility and the panel closes. Caveat: a Q-Link nudge of the enum while open also closes it (the plugin
can't tell a touch from a Q-Link).

**Now a layout control: `popup` (2026-09-25).** `shadow_skin.py` `popup cx= cy= w= h= key=<enum> [cols=]`;
the list opens below the field, else above, adding columns until it fits. `gen_vst.py` appends a hidden
`<key>__open` param (`popup_of` in params.h); `vst2_wrap.c` keeps it locally (not sent to the DSP, not in the
chunk) and closes it only on an exact option value, so a Q-Link nudge (between options) leaves it open.
**Verified on the Force 2026-09-25** (Maze Voice test build, LFO1 SYNC DIV as an 8-option popup): opens as a
two-column list under the field, a pick closes it and shows the choice, a Q-Link turn steps the value with the
list left open.

**Rolled out (2026-09-25; seen working on a Force the same day):** `studio.py auto` picks `popup` for 7+
options. Ports: Maze (LFO sync divisions), JV-880 (reverb type), Acid (scale, root, regen) and Euclidier (lane
divisions, randomise lane); the last two have hand-written wrappers and use `wrapper/popup.h`. Not used where a
list is filled at run time (Crate Digger's genre/style steppers): popup option text is baked into the artwork.
The artwork font gained `#` (force-shadow fa456fc), so note names like C# show; it still has no brackets.

**Mode panels: `when=<param>:<option>` (verified on a Force 2026-09-25, "Maze Skin Test": LFO1 SYNC swaps the RATE knob for a SYNC DIV popup and shows mode-only art).** Any layout line
can carry it. Its components get the same `IndexedEnabling/<option>/<count>/Parameter <p>` handle (with
`showWhenDataModelInvalid: "Show"`) as the popup list; its baked parts (frame, title, text boxes, group labels)
are drawn into a per-mode image, the page background redrawn with that mode's parts and cropped to them, placed
over the base background (which leaves them out). A popup's list itself isn't tagged, so a list left open
while the mode changes stays open until a pick.

## Browser-rendered artwork (verified on a Force 2026-09-25, "Maze Skin Test")
`tools/html_art.py` takes shadow_art.c's stdin commands and draws them as SVG in headless Chromium (Playwright
1.47, `tools/html_art/Dockerfile`), so the layout and skin builder are unchanged. Maze Voice's whole skin renders
in about 7 s. Differences that matter on a device: knob/slider filmstrips and the images of toggles, buttons and
option segments are **RGBA** (transparent edges, so they sit on art); stock skins' filmstrips are PNGs with
alpha, and MPC also honours alpha in a `Button`'s on/off images: controls showed clean edges over a gradient.
Real fonts, `art_css=` restyling and `art file=` SVG art all showed as previewed. Backgrounds, mode images and popup
panels stay opaque. `art file=` (SVG art) is only drawn by this renderer; shadow_skin refuses it otherwise.

## Patching MPC's own picker: not practical (checked 2026-09-25)
`/usr/bin/MPC` links JUCE statically and is stripped (no `.symtab`); of ~8000 exported dynamic symbols none
names a menu/overlay/combo/parameter class (only ~92 JUCE-related, all typeinfo/vtables of unrelated
templates). So `LD_PRELOAD` interposition can't reach the code that fills the menu overlay; the only route
would be reverse-engineering the 73 MB `.text` and patching it in memory per firmware build — crash risk to
MPC and breaks on every update. The skin-drawn `popup` covers the need.

## `.so` update without restarting MPC (verified 2026-09-24, menuprobe)
Replacing `/sdcard/vst/x.so` (staged `.new` + `mv`) and then removing **every** instance of the plugin and
inserting it again loaded the new build (version line in the probe log), no MPC restart. JUCE drops the module
once its last instance is gone and re-opens it on the next insert. A restart is still needed for a new
`MPC.settings` entry.

## Skin fonts: Titillium Web + Roboto only (tested 2026-09-24, `poc/menuprobe_skin.py`)
`Label` components name their font per component (`textStyle.font.name/style/height`). Stock skins use
`Titillium Web` (Regular/SemiBold/Light/Italic…) and `Roboto` (Regular/SemiBold); both are embedded in
`/usr/bin/MPC` in every weight, and both render. `Liberation Mono`/`Liberation Serif` (installed in
`/usr/share/fonts/ttf`, a fontconfig dir) and a bogus name all fell back to Titillium, so MPC does not resolve
system fonts by name and installing a `.ttf` on the device won't give skins a new native font. Choice for
live (value/name) text: those two families at any weight/size. Any other typeface has to be baked into PNGs
(`vst.json` `"title_font"`).

## VST3: not supported by MPC OS (checked on a Force, OS base 5.0.17, 2026-09-24, `tools/probe_device.sh`)
MPC's JUCE host has only `juce::VSTPluginFormat` compiled in. The binary has no `VST3PluginFormat` /
`VST3PluginInstance` RTTI and no `GetPluginFactory` string (which JUCE needs to load any VST3 module), and no LV2
either. The only `VST3` / `.vst3` strings are JUCE's wrapper-type names and a desktop-project file-extension list.
So a VST3 bundle can't be loaded, whatever the settings say, and VST3 value lists can't fix the empty picker.
Rerun the probe after firmware updates and on other models.

## MIDI-generator VST wrapping a standalone-process engine (Force Acid, 2026-09-24)
Force Acid (`force-acid`, a MockbaMod standalone process using RtMidi + a timer thread as its own
"chain host" for `acid_core.c`) ports to a VST2 the same way as a block-rendering engine for
the MIDI-out and clock problems, but needed a hand-written wrapper (`force-acid/vst/acid_vst.cpp`, not
`wrapper/vst2_wrap.c`, which assumes `render_block` audio DSP): `tools/gen_vst.py` still generates
params.h + the skin from a hand-written parameter file (transcribed from the standalone
build's CC table, kept in sync by hand) since that pipeline only cares about the key/name/min/max/options/
momentary shape, not the real host API.
- **Clock, without a physical MIDI cable:** the standalone build derives BPM/transport from real 0xF8/
  0xFA/0xFC MIDI clock (EMA of inter-pulse interval). A VST host hands this over cleanly instead:
  `audioMasterGetTime` gives exact `tempo` and `ppqPos` already, so the wrapper synthesizes the same
  24-PPQN clock byte stream from the ppqPos delta each block (`ceil(last/step)*step .. end`, step =
  1/24 quarter note) and feeds it to the engine's own `process_midi()` unchanged -- no core changes
  needed.
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

## DX7 port: moved to sd88me/mpc-vst-dx7 (2026-09-25)
Its findings (a retired control-socket attempt, then an in-process schwung-dx7 build that works) are in that
repo's `docs/NOTES.md`. The generic lessons are in `PORTING.md`: check for an in-process engine build before
writing a control-socket wrapper, and check an engine's data-folder convention under `MODULE_DIR`.

## CPU layout (Force, 2026-09-24)
RK3288, 4x Cortex-A17 @ 1.8 GHz (governor `performance`), `isolcpus=2-3`. MPC runs `AudioWorker0-3` (SCHED_FIFO),
one pinned per core, plus `Audio Processing` (prio 20). Plugins run on these workers, so tracks spread across
cores. `tools/bench.sh` measures a plugin against the 2902 µs block (docs/BENCH.md).

## Xenia (gearmulator's Microwave II/XT) port started: `ports/xenia/` (2026-09-25)
The generalised findings (vendoring recipe, the 32-bit ARM build patches, CPU budget, fallback options) are
written up in `docs/DSP56300.md` for any future gearmulator/DSP56300 port -- read that first for a second one;
this entry stays as the dated log of what happened on this specific port.

First port of a gearmulator synth. Engine = gearmulator's own `synthLib::Plugin` + `xt::Device` behind
`mpc_engine()`, on a worker thread that keeps a 4-block ring ahead; the audio thread only copies blocks out and
plays silence (counted as xruns) when the emulator falls behind, so a too-slow emulator can't stall MPC. Builds
for armhf and passes `test_port.sh` under ASan without a ROM; **not yet run on a device**. Findings so far:
- **No DSP JIT on MPC OS.** dsp56300's JIT targets x86-64 and AArch64 only; MPC runs a 32-bit ARM process, so
  the DSP56300 runs on the interpreter (`DSP56K_FORCE_INTERPRETER`). The JIT sources still have to compile
  (the `DSP` owns a `Jit`); two small vendored patches make them build on 32-bit (see `ports/xenia/src/VENDORED.md`).
- **The emulator is cycle-driven.** The firmware programs the DSP's PLL and audio frames are paced by emulated
  cycles, so real time at 100 % needs the interpreter to sustain that clock on one core (gearmulator logs it at
  boot: "Clock speed changed to: N Mhz"; the DSP56303 tops out at 100 MHz). The DSP Clock parameter (50-100 %)
  trades polyphony for CPU.
- **Interpreter throughput** (`ports/xenia/tools/interp_bench.cpp`, synth-like loop, no ROM needed): ~61 MIPS
  = ~71 MHz of DSP clock on one core of a 2.8 GHz Xeon (the cloud build host). The same binary built for armhf
  runs under QEMU (the DSP's MMU memory setup works in a 32-bit address space). **Measured on the Force
  (2026-09-25, `devtest.sh`, RK3288 Cortex-A17 @ 1.8 GHz, `isolcpus=2-3`, MPC running normally): 13.8 MIPS,
  16.2 MHz of DSP clock over 5.0 s** — about 4.4x slower than the x86 build host, and well short of the
  DSP56303's 100 MHz ceiling even at low DSP Clock settings. gearmulator's own history (its
  `doc/dsp_performance_history.md`) had the 2022 interpreter at 5.8 MIPS on a Cortex-A76, against 234-421 MIPS
  for the AArch64 JIT, so this A17 number is in the same range as expected for interpreter-only ARM.
- **Firmware boot test blocked on a bad ROM file, not yet run (2026-09-25).** `devtest.sh 192.168.1.44
  upper_Am29F010.bin lower_Am29F010.bin` built and uploaded cleanly in one ssh call (the fix from the prior
  session works), but `lower_Am29F010.bin` on the build host turned out to be a 122-byte terminal capture of
  an `xxd` dump (ANSI colour codes, not ROM bytes) rather than the real 131072-byte half-ROM — `upper_Am29F010.bin`
  is a good 131072-byte file. `xenia_probe` correctly reported "no ROM found" (the rom-dir listing in
  `build/devtest-report.txt` shows the size mismatch). Once a real `lower_Am29F010.bin` half-ROM is supplied,
  re-run `devtest.sh` to get the `xenia_probe` speed/level numbers across DSP clocks and chord sizes.
- **Thread placement matters here more than for other ports.** The Force boots with `isolcpus=2-3` and MPC's
  SCHED_FIFO `AudioWorker`s on every core (see "CPU layout"), so the emulator's three SCHED_OTHER threads
  (worker, DSP56300, MC68331) share cores 0-1 with MPC's UI and are preempted by the audio workers there.
- **Build:** the core is ~190 C++17 files; building it inside `arm32v7/gcc:12` under QEMU takes a long time the
  first time (~20-45 min on 4 cores; incremental afterwards). A host cross compiler is much faster but Ubuntu 24.04's targets glibc
  2.39, whose C++ headers redirect `strtol` & co. to `__isoc23_*` (GLIBC_2.38), which the 2.36 link in
  build_port.sh rejects. So the core is built in the same image the plugin is linked in.
- Skin: studio auto-layout for now (2 pages, filter type as a popup; offline preview checked). This port hit a
  gen_vst bug, now fixed: with no `layout`, the auto-layout's popups got no hidden `__open` params.
- If the A17 falls well short, options by effort: the DSP Clock parameter; a helper process on units with a
  64-bit kernel (AArch64 JIT, audio over shared memory); gearmulator's DSP bridge (DSP on a networked computer);
  an ARMv7 backend for the dsp56300 JIT.
