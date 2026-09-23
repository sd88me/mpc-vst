#!/usr/bin/env bash
# Build a Schwung-module port as an MPC OS VST2 instrument from its vst.json (see tools/gen_vst.py).
#   tools/build_port.sh path/to/vst.json
# Output in <vst.json folder>/build/: <so>, skin/<vendor> - VST - <name>/, pluginlist-entry.xml, params.h.
# Needs Docker (with QEMU for arm32v7) and a force-shadow checkout for the skin artwork
# (FORCE_SHADOW, default: ../force-shadow next to this repo).
set -euo pipefail
MV="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
eval "$(python3 "$MV/tools/gen_vst.py" "$CFG" --shell)"
FORCE_SHADOW="${FORCE_SHADOW:-$MV/../force-shadow}"
[ -f "$FORCE_SHADOW/tools/render_conf_preview.c" ] || { echo "need a force-shadow checkout (FORCE_SHADOW)" >&2; exit 1; }
U="$(id -u):$(id -g)"
mkdir -p "$ROOT/$PORT/build"

# 1. skin artwork renderer (host binary)
docker run --rm -u "$U" -v "$ROOT":/w -v "$MV":/mv:ro -v "$FORCE_SHADOW":/fs:ro -w /w gcc:12 \
  gcc -O2 -I/fs/tools -o "$PORT/build/shadow_art" /mv/tools/shadow_art.c -lm

# 2. params.h, skin, plugin-list entry
docker run --rm -u "$U" -v "$ROOT":/w -v "$MV":/mv:ro -w /w python:3.11-slim sh -c \
  "pip install -q --no-warn-script-location --target /tmp/p pillow >/dev/null 2>&1; PYTHONPATH=/tmp/p python3 /mv/tools/gen_vst.py '$PORT/vst.json'"

# 3. the plugin (armhf, glibc 2.36 so it loads on the device's 2.39)
docker run --rm --platform linux/arm/v7 -u "$U" -v "$ROOT":/b -v "$MV":/mv:ro -w /b arm32v7/gcc:12 bash -euc "
  gcc -O2 -Wall -Wextra -Wno-unused-parameter -fPIC -shared -fvisibility=hidden -std=gnu11 $CFLAGS -I'$PORT/build' \
      $SOURCES /mv/wrapper/vst2_wrap.c $LIBS -o '$PORT/build/$SO'
  strip '$PORT/build/$SO'
  echo \"exported: \$(readelf --dyn-syms -W '$PORT/build/$SO' | grep -E ' GLOBAL .* [0-9]+ [A-Za-z]' | grep -v UND | awk '{print \$8}' | tr '\n' ' ')\"
  echo \"highest glibc: \$(readelf -V '$PORT/build/$SO' | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1) (device has 2.39)\"
"
md5sum "$ROOT/$PORT/build/$SO"
