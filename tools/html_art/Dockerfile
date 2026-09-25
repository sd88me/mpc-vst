# Browser-rendered skin artwork (tools/html_art.py): headless Chromium via Playwright, plus Pillow.
#   docker build -t mpc-vst-html-art tools/html_art      (tools/build_port.sh does this when vst.json has "art": "html")
FROM mcr.microsoft.com/playwright/python:v1.47.0-jammy
RUN pip install --no-cache-dir playwright==1.47.0 pillow==10.4.0
