# mpc-vst

Native plugins for **Akai MPC OS standalone devices** (MPC Live/One/X/Key, Force).
Build Linux VST2 `.so` plugins that MPC OS loads through its built-in JUCE plugin
host. They show up as first-class track instruments and effects, with a **native MPC screen
skin** and Q-Link mapping. There's no LD_PRELOAD, no JACK bridge, and no separate process.

Status: proof of concept, working on real hardware (2026-09-23, verified on a Force):
a gain effect, a test synth, and [Maze Voice](https://github.com/sd88me/force-maze)
(a full Schwung DSP port) all play from pads/sequencer, with custom skins and Q-Links.
It should behave the same on other Gen1 MPC OS hardware, since the same `MPC` binary is involved; reports are welcome.
Gen2 devices (e.g. Live III) are reported to be more locked down.

<img width="906" height="570" alt="image" src="https://github.com/user-attachments/assets/3bb29544-0cd0-409f-b5b4-7df67ddbaf9a" />

## How it works

1. MPC OS reads `<VALUE name="pluginList-arm"><KNOWNPLUGINS>…` from its
   `MPC.settings` at startup (on the Force:
   `/media/az01-internal/Settings/MPC/MPC.settings`) and adds each
   `<PLUGIN format="VST" file="/sdcard/vst/x.so" …/>` to its plugin list.
2. The `.so` exports `VSTPluginMain` (VST2 ABI, hand-written, no Steinberg SDK).
3. A skin folder `/sdcard/Synths/<manufacturer> - VST - <name>/` (`version.xml`,
   `Plugin Skins/TUI.json`, `Q-Links.json`) gives it a native screen. Knobs bind
   to `"Parameter N"`, which is the VST parameter index.

See [docs/NOTES.md](docs/NOTES.md) for details, gotchas, and open issues.

## Layout

- `wrapper/vst2_wrap.c`: a generic VST2 ⇄ Schwung `plugin_api_v2` wrapper. You link
  it with any Schwung DSP plus a generated `params.h`.
- `tools/gen_vst.py`: generates `params.h`, the skin, and the `pluginList` entry from
  a Schwung `module.json` (currently configured for Maze Voice; see TODO).
- `tools/shadow_skin.py` + `tools/shadow_art.c`: build a skin from a Force Shadow style layout
  (`shadow_page.conf` widget syntax: frames, knobs, toggles, triggers, option segments, plus
  `qlinks` lines for nested pages). The artwork (backgrounds, knob filmstrips, button states) is drawn by
  [force-shadow](https://github.com/sd88me/force-shadow)'s own renderer, so the MPC page matches the
  shadow page pixel for pixel. Needs a force-shadow checkout to build `shadow_art`.
- `tools/studio.py` + `tools/skin_template.svg`: the skin studio. Auto-layout from a module's
  parameters, an Inkscape/Penpot SVG round trip, and page previews. See [docs/SKIN_STUDIO.md](docs/SKIN_STUDIO.md).
- `tools/bench.sh` + `tools/bench.c`: a CPU stress test run on the device, with a PASS/WARN/FAIL verdict for Gen1
  hardware. See [docs/BENCH.md](docs/BENCH.md).
- `tools/release.py`: packages a plugin as one shareable zip with an installer, an uninstaller and generated
  INSTALL.md. See [docs/RELEASING.md](docs/RELEASING.md).
- `tools/probe_device.sh`: a read-only device report (CPU, 32/64-bit MPC, audio threads, plugin formats: VST2 yes,
  VST3 no on current firmware).
- `tools/host_test.c`: an offline x86 host test (instances, params, MIDI→audio, chunks).
- `poc/gain.c`, `poc/synth.c`: minimal effect / instrument examples.
- `poc/midiport.c`: a MIDI-generating plugin (tempo-synced) that drives other tracks through an ALSA port
  (MPC OS ignores VST MIDI output; `poc/midiout.c` shows that).

## Credits

The route was first described on the MPC-Forums thread
"Proof of Concept: Custom Standalone Plugins" (Sep 2026) by NoQuestion and dustyslices.

## Legal

VST2 is a deprecated Steinberg format; this project uses a hand-written ABI
header and is for personal, non-commercial experimentation on hardware you own.
No Akai content is redistributed. Editing `MPC.settings` is at your own risk; back it up first.
