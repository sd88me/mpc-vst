#!/usr/bin/env bash
# Build tools/interp_bench for the device (armhf, against build/core-arm from ./build.sh) and, with an IP,
# run it there over SSH. Read-only on the device: the binary goes to /tmp and is removed afterwards.
#   ./bench.sh              build only: build/interp_bench
#   ./bench.sh <device-ip>  build, then run it on the device for 5 s
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
[ -f "$here/build/core-arm/libxenia_core.a" ] || { echo "run ./build.sh first (needs build/core-arm)" >&2; exit 1; }
G=src/gearmulator/source
docker run --rm --platform linux/arm/v7 -u "$(id -u):$(id -g)" -v "$here":/p -w /p arm32v7/gcc:12 \
  g++ -O2 -std=gnu++17 -DDSP56K_FORCE_INTERPRETER=1 -DDSP56300_DEBUGGER=0 -DASMJIT_STATIC -DASMJIT_NO_AARCH64 \
    -I$G/cpu/dsp56300/source -I$G/cpu/dsp56300/source/asmjit/src \
    tools/interp_bench.cpp build/core-arm/libxenia_core.a -lpthread -ldl -o build/interp_bench
echo "built build/interp_bench"
if [ -n "${1:-}" ]; then
  ssh "root@$1" 'cat > /tmp/interp_bench && chmod +x /tmp/interp_bench && /tmp/interp_bench 5; rm -f /tmp/interp_bench' \
    < "$here/build/interp_bench"
fi
