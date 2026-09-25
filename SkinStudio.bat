@echo off
rem Skin Studio for Windows: double-click to open the skin editor in your browser.
rem Drop a layout.conf or a port's vst.json on this file to open it straight away.
rem Keep this window open while you edit; Quit in the page (or closing the window) stops it.
cd /d "%~dp0"
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY where python >nul 2>nul && set "PY=python"
if not defined PY (
  echo Skin Studio needs Python 3: https://www.python.org/downloads/
  echo When installing, tick "Add python.exe to PATH".
  pause
  exit /b 1
)
%PY% tools\studio.py serve --open %*
if errorlevel 1 pause
