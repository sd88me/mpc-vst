#!/bin/sh
# Skin Studio for macOS: double-click in Finder to open the skin editor in your browser.
# (First time: right-click it, Open, then Open again, if macOS says it's from an unidentified developer.)
# Keep the Terminal window open while you edit; Quit in the page (or closing the window) stops it.
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Skin Studio needs Python 3: https://www.python.org/downloads/"
  printf "Press Return to close. "; read -r _; exit 1
fi
python3 tools/studio.py serve --open "$@" || { printf "Press Return to close. "; read -r _; }
