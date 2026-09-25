#!/usr/bin/env bash
# Device suitability test, run BEFORE installing the plugin: nothing is installed, MPC keeps running and is not
# restarted, and everything goes to /tmp/xenia-devtest on the device and is deleted afterwards.
#   ./devtest.sh build                        build the armhf test binaries + build/xenia-devtest.tar (no device)
#   ./devtest.sh <device-ip> [rom-file ...]   run it on the device (ROM from the file(s), else /sdcard/vst/xenia;
#                                              a half-ROM dump is two .bin files -- pass both)
# Report: interpreter throughput (no ROM needed), then the firmware booted from your ROM and played at DSP
# clocks 100/75/50 % with 0-10 held voices: speed (>= 1.0 = real time), level vs 100 % (voice loss), thread CPU.
# It keeps two to three cores busy for a few minutes: don't run it during a session you care about.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/build/devtest"
tarball="$here/build/xenia-devtest.tar"

build() {
  [ -f "$here/build/core-arm/libxenia_core.a" ] || { echo "run ./build.sh first (needs build/core-arm)" >&2; exit 1; }
  mkdir -p "$out"
  local G=src/gearmulator/source
  local FL="-O2 -std=gnu++17 -DDSP56K_FORCE_INTERPRETER=1 -DDSP56300_DEBUGGER=0 -DASMJIT_STATIC -DASMJIT_NO_AARCH64"
  local INC="-isystem $G/framework -isystem $G/waldi/xt -isystem $G/waldi/common -isystem $G/cpu"
  INC="$INC -isystem $G/cpu/dsp56300/source -isystem $G/cpu/dsp56300/source/asmjit/src -isystem $G/3rdparty"
  docker run --rm --platform linux/arm/v7 -u "$(id -u):$(id -g)" -v "$here":/p -w /p arm32v7/gcc:12 bash -euc "
    g++ $FL $INC tools/interp_bench.cpp build/core-arm/libxenia_core.a -lpthread -ldl -o build/devtest/interp_bench
    g++ $FL $INC tools/xenia_probe.cpp build/core-arm/libxenia_core.a -lpthread -ldl -o build/devtest/xenia_probe
    strip build/devtest/interp_bench build/devtest/xenia_probe"
  cat > "$out/run.sh" <<'RUN'
#!/bin/sh
# runs on the device from /tmp/xenia-devtest; $1 = ROM folder
cd /tmp/xenia-devtest
echo "== device"
uname -m; grep -m1 "model name" /proc/cpuinfo; grep -m1 "Hardware" /proc/cpuinfo; grep -m1 "CPU part" /proc/cpuinfo; grep -c ^processor /proc/cpuinfo
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null
cat /proc/cmdline | tr ' ' '\n' | grep isolcpus
echo "MPC pid: $(pidof MPC)  load: $(cat /proc/loadavg)"
echo "== interpreter"
./interp_bench 5 2>&1 | grep -v "@"
echo "== rom dir ($1)"
ls -la "$1" 2>&1
for f in "$1"/*; do
  [ -f "$f" ] || continue
  printf '%s: %d bytes, first 4 (octal): ' "$f" "$(wc -c < "$f")"
  dd if="$f" bs=1 count=4 2>/dev/null | od -b | head -1
done
echo "== firmware"
./xenia_probe "$1" 2 2>&1
RUN
  chmod +x "$out/run.sh"
  tar -C "$out" -cf "$tarball" interp_bench xenia_probe run.sh
  echo "built $tarball"
}

if [ "${1:-}" = build ]; then build; exit 0; fi
[ -n "${1:-}" ] || { sed -n '2,10p' "$0" >&2; exit 2; }
ip="$1"; shift
[ -f "$tarball" ] || build
ssh "root@$ip" 'rm -rf /tmp/xenia-devtest && mkdir -p /tmp/xenia-devtest/rom && tar -xf - -C /tmp/xenia-devtest' < "$tarball"
romdir=/sdcard/vst/xenia
if [ "$#" -gt 0 ]; then
  # One ssh call for every ROM file (a tar stream), not one per file: several separate ssh
  # invocations mean several separate password prompts if key auth isn't set up, and a missed
  # one silently truncates that file's transfer instead of failing loudly. -C per file (GNU tar
  # applies each -C to the args that follow it) stores every file at its basename, regardless of
  # what directory it was given from, so it lands flat in rom/ on the device.
  tarargs=()
  for rom in "$@"; do
    tarargs+=(-C "$(cd "$(dirname "$rom")" && pwd)" "$(basename "$rom")")
  done
  tar -cf - "${tarargs[@]}" | ssh "root@$ip" 'tar -xf - -C /tmp/xenia-devtest/rom'
  romdir=/tmp/xenia-devtest/rom
fi
ssh "root@$ip" "/tmp/xenia-devtest/run.sh $romdir; rm -rf /tmp/xenia-devtest" | tee "$here/build/devtest-report.txt"
echo "report saved to build/devtest-report.txt"
