# Porting an engine to an MPC OS plugin

## 0. Classify it
- **Schwung `plugin_api_v2` DSP** (Maze Voice, Crate Digger's core, …): wrap with `wrapper/vst2_wrap.c`.
  Parameters come from `module.json` `chain_params`.
- **Engine with host-side glue** (control-socket keys that aren't DSP params, dynamic lists): write a port-specific
  wrapper on the same ABI (see the crate digger port) with virtual parameters for the glue.
- **MIDI generator** (sequencer/arp): MPC ignores VST MIDI out, so send through an ALSA seq port (`poc/midiport.c`).
- **App** (network, files, child processes): allowed, see NOTES "Beyond synths". Keep the audio thread
  non-blocking, use `posix_spawn` with LD_PRELOAD stripped (never `fork()`), and use libcurl for HTTPS.

## 1. Engine
- [ ] Builds for armhf with glibc ≤ the device's (`arm32v7/gcc:12` is fine), exporting only `VSTPluginMain`.
- [ ] 44.1 kHz / 128-frame blocks, as on the Move. Compile out Move-only quirks with `-D<NAME>_VST`.
- [ ] Per-instance state; several instances may run at once.
- [ ] State saved via chunks (`effGetChunk`/`effSetChunk`).
- [ ] Offline x86 test: instances, parameter round-trip, MIDI → audio, chunk restore, under ASan.

## 2. Parameters
- [ ] Stable order (the VST index is what skins and projects bind to). Append only; never reorder a shipped plugin.
- [ ] Options: an index; nudges step one option (the wrapper does this). Triggers: `access:"write"`, which springs back.
- [ ] Display strings are the only dynamic text channel into the skin (see NOTES on refresh behaviour).

## 3. Skin
- [ ] Design in a layout `.conf` (Force Shadow widget syntax plus `qlinks`/`rows=`), or port an existing shadow page.
- [ ] Generate (`tools/shadow_skin.py` via the port's gen script), then look at an offline composite before deploying.
- [ ] Q-Links: 1–8 = knob bank 1, 9–16 = bank 2; nested pages via several `qlinks` lines.

## 4. Device
- [ ] `.so` → `/sdcard/vst/`, skin → `/sdcard/Synths/<vendor> - VST - <name>/`.
- [ ] `pluginList-arm` entry (MPC stopped, settings backed up), then restart (ask first).
- [ ] User test: list → insert → play → skin → Q-Links → save/reload project. Record results in NOTES.md.
