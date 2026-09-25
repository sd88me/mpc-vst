#!/bin/sh
# Skin Studio for Linux: run it (double-click, "Run in terminal" / "Run as a program") to open the skin editor
# in your browser, or: ./SkinStudio.sh [layout.conf | vst.json]
# Keep the terminal open while you edit; Quit in the page (or Ctrl+C) stops it.
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Skin Studio needs Python 3: https://www.python.org/downloads/"
  printf "Press Return to close. "; read -r _; exit 1
fi
python3 tools/studio.py serve --open "$@" || { printf "Press Return to close. "; read -r _; }
