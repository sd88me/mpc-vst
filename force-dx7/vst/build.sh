#!/usr/bin/env bash
# Build Force DX7 as an MPC OS VST2 instrument. NOT tools/build_port.sh (that
# pipeline assumes a directly-linkable Schwung plugin_api_v2 DSP -- force-dx7's
# real DSP lives inside the standalone dx7_host process, controlled over a
# Unix socket; see dx7_vst.c's header comment and PORTING.md category 2).
#
# Output in build/: force_dx7.so, skin/<vendor> - VST - <name>/, params.h.
# Needs Docker (arm32v7/gcc:12 with QEMU) and a force-shadow checkout next to
# mpc-vst (FORCE_SHADOW, for shadow_art's skin renderer -- same as build_port.sh).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="$(cd "$HERE/../.." && pwd)"
FORCE_SHADOW="${FORCE_SHADOW:-$MV/../force-shadow}"
[ -f "$FORCE_SHADOW/tools/render_conf_preview.c" ] || { echo "need a force-shadow checkout (FORCE_SHADOW)" >&2; exit 1; }
U="$(id -u):$(id -g)"
mkdir -p "$HERE/build"

# 1. skin artwork renderer (host binary) -- same shadow_art.c mpc-vst ships,
#    reused as-is (never forked per port).
docker run --rm -u "$U" -v "$HERE":/w -v "$MV":/mv:ro -v "$FORCE_SHADOW":/fs:ro -w /w gcc:12 \
  gcc -O2 -I/fs/tools -o build/shadow_art /mv/tools/shadow_art.c -lm

# 2. params.h (port-specific gen_params.py, not tools/gen_vst.py) + skin
#    (tools/shadow_skin.py, reused as-is) from layout.conf.
docker run --rm -u "$U" -v "$HERE":/w -v "$MV":/mv:ro -w /w python:3.11-slim sh -c '
  pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1
  export PYTHONPATH=/tmp/p:/mv/tools
  python3 gen_params.py params.json build/params.h
  python3 - <<"PY"
import json, sys
sys.path.insert(0, "/mv/tools")
import shadow_skin
params = json.load(open("params.json"))
import shutil
shutil.rmtree("build/skin", ignore_errors=True)
d = shadow_skin.write_skin("build/skin", "sd88me", "Force DX7", "layout.conf", params, "build/shadow_art")
print("skin:", d)
PY
'

# 3. the plugin (armhf, glibc 2.36 container so it loads on the device's 2.39).
#    Needs libasound2-dev in the arm32v7 image for -lasound (ALSA seq MIDI
#    passthrough to dx7_host's virtual port) -- not present in arm32v7/gcc:12
#    by default, unlike x86 gcc:12 which poc/midiport.c's own build uses.
docker run --rm --platform linux/arm/v7 -u root -v "$HERE":/b -w /b arm32v7/gcc:12 bash -euc "
  apt-get update -qq && apt-get install -y -qq libasound2-dev >/dev/null
  gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -shared -fvisibility=hidden -std=gnu11 \
      -Ibuild dx7_vst.c -lasound -o build/force_dx7.so
  strip build/force_dx7.so
  chown $U build/force_dx7.so
  echo \"exported: \$(readelf --dyn-syms -W build/force_dx7.so | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{print \$8}' | tr '\n' ' ')\"
  echo \"highest glibc: \$(readelf -V build/force_dx7.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (device has 2.39)\"
"
md5sum "$HERE/build/force_dx7.so"

# 4. pluginlist-entry.xml (same shape gen_vst.py's entry() emits)
UID_HEX=$(python3 -c "print('%x' % int.from_bytes(b'FDx7','big'))")
cat > "$HERE/build/pluginlist-entry.xml" <<EOF
<PLUGIN name="Force DX7" descriptiveName="Force DX7" format="VST" category="Synth" manufacturer="sd88me" version="1.0" file="/sdcard/vst/force_dx7.so" uid="$UID_HEX" isInstrument="1" fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>
EOF
echo "pluginlist-entry.xml written"
