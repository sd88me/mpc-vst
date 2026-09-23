# Releasing a plugin

A release is **one zip** people can share around: `<Name>-<version>-mpc-armv7.zip`. It unpacks to a folder with
the plugin, its skin, `install.sh` / `uninstall.sh` and a generated `INSTALL.md` (scripted and manual steps,
requirements, CPU result, checksums).

## Checklist
1. **Build** with the port's `build.sh` (armhf, `arm32v7/gcc:12`; highest GLIBC symbol ≤ 2.36).
2. **Host test** (x86, ASan): `tools/host_test.c` or the port's own test. It must be clean.
3. **Skin preview**: `tools/studio.py preview "<skin>/Plugin Skins" -o page_%d.png`, and look at every page.
4. **CPU**: `tools/bench.sh build/x.so <ip> -j | tee build/bench.txt`. It must PASS, or WARN with a note
   (docs/BENCH.md).
5. **Device smoke test**: install with the zip's own `install.sh` (step 6 first). Load the plugin on a track, play
   it, turn every page and Q-Link, save and reload a project, then `uninstall.sh`.
6. **Package**:
   ```
   tools/release.py --so build/x.so --skin "build/skin/<vendor> - VST - <Name>" \
       --entry build/pluginlist-entry.xml --version 1.2.0 --bench build/bench.txt \
       --about "One line about the plugin." [--extra engine:vst/x] -o dist
   ```
   `--extra SRC:vst/DEST` ships extra runtime files next to the `.so` (an engine bundle, presets).
7. **Publish**: tag `<port>-vX.Y.Z` in the port's repo and attach the zip:
   `gh release create maze-voice-vst-v1.2.0 dist/Maze-Voice-1.2.0-mpc-armv7.zip --notes-file ...`
   Paste the zip's INSTALL.md "Requirements" and "Install" sections into the notes.

## Versioning
- `X.Y.Z` in the zip name and INSTALL.md. Bump Z for fixes, Y for new parameters or pages, X when parameter
  indices change. Changing the indices breaks saved projects, because MPC stores values by index.
- Keep the plugin `uid` and `.so` name fixed across versions: the installer replaces the entry with the same
  `file=`, and projects find the plugin by uid.

## What the installer does
Run on the device as root (`sh install.sh [-y]`):
1. Checks root, armv7, that `MPC.settings` exists and `SHA256SUMS`, and asks for confirmation.
2. Stops MPC (`systemctl stop acvs`) and waits for it to exit. A trap restarts MPC on any error.
3. Copies the `.so` (as `.new`, then `mv`), the extras and the skin (`/sdcard/Synths/<skin>`).
4. Backs up `MPC.settings` to `MPC.settings.bak-<so>-<date>`. `plugin_list.awk` (BusyBox awk) drops any entry with
   the same `file=` and inserts the new one into `pluginList-arm`, creating the list if needed. The result is checked
   (exactly one entry, valid XML when python3 exists) before it replaces the original.
5. Starts MPC.

The settings edit was tested 2026-09-24 against a copy of a real Force `MPC.settings`: replacing an entry, running
twice (identical output), removing, a missing `pluginList-arm`, and a self-closing `<KNOWNPLUGINS/>`. A full
scripted install on a device (which restarts MPC) is step 5 of the checklist.

Audience: root access is needed to edit `MPC.settings`, so releases are for modded units. Say so up front.
