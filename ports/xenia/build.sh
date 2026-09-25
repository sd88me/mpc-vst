#!/usr/bin/env bash
# Build Xenia as an MPC OS VST2 instrument.
#   build/xenia.so              -> /sdcard/vst/ on the device
#   build/skin/<folder>/        -> /sdcard/Synths/ on the device
#   build/pluginlist-entry.xml  the <PLUGIN> line for MPC.settings' pluginList-arm
# Step 1 builds the emulator core (vendored gearmulator subset, core/Makefile) inside arm32v7/gcc:12, the
# image tools/build_port.sh links in, so the core sees the same glibc 2.36 headers. It runs under QEMU
# and takes a while the first time; later runs only rebuild what changed. Step 2 is the usual port build.
# Needs Docker with QEMU for arm32v7, and mpc-vst-plugins (MPC_VST; found automatically when this port
# sits in its ports/ folder or next to a checkout).
# The ROM is NOT part of the build: see docs/ROMS.md.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${MPC_VST:-}" ]; then
  for d in "$here/../.." "$here/../mpc-vst-plugins"; do
    [ -x "$d/tools/build_port.sh" ] && MPC_VST="$(cd "$d" && pwd)" && break
  done
fi
[ -x "${MPC_VST:-}/tools/build_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }

docker run --rm --platform linux/arm/v7 -u "$(id -u):$(id -g)" -v "$here":/p -w /p arm32v7/gcc:12 \
  make -C core OUT=/p/build/core-arm -j"$(nproc)"
mkdir -p "$here/build/core"
cp "$here/build/core-arm/libxenia_core.a" "$here/build/core/libxenia_core.a"
exec "$MPC_VST/tools/build_port.sh" "$here/vst.json"
