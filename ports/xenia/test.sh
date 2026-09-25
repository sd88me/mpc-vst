#!/usr/bin/env bash
# Offline x86 test (mpc-vst-plugins' tools/test_port.sh) with the emulator core built for the host
# under ASan/UBSan. Without a ROM it covers everything but the sound (status "No ROM"). With one in
# build/ (gearmulator also searches the executable's folder) the firmware boots in the background; the
# test's note comes before the boot finishes, so it still reports silence as a warning.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${MPC_VST:-}" ]; then
  for d in "$here/../.." "$here/../mpc-vst-plugins"; do
    [ -x "$d/tools/test_port.sh" ] && MPC_VST="$(cd "$d" && pwd)" && break
  done
fi
[ -x "${MPC_VST:-}/tools/test_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }

make -C "$here/core" OUT="$here/build/core-host" SANITIZE=1 -j"$(nproc)"
mkdir -p "$here/build/core"
cp "$here/build/core-host/libxenia_core.a" "$here/build/core/libxenia_core.a"
exec "$MPC_VST/tools/test_port.sh" "$here/vst.json"
