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
4. `gen_vst.py` is Maze-specific (NAME/UID/TABS at the top). Next: read those from a small per-port
   `vst.json` next to `module.json` so any Schwung module (force-acid, …) ports with no code.
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
- Next: custom per-page geometry (grouped sections, bigger hero controls, artwork) like the stock plugins,
  i.e. a hand-editable layout file per port instead of the fixed grid.
