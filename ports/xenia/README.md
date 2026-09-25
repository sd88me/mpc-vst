# Xenia for MPC OS (closed -- not viable on this hardware)

A port of [gearmulator](https://github.com/dsp56300/gearmulator)'s **Xenia**, the Waldorf Microwave II/XT
emulator (the original OS running on emulated DSP56300 + MC68331 chips), to a native VST2 instrument for Akai
MPC OS standalone devices, built with this repo's wrapper and tools.

**Status: closed, 2026-09-25.** Builds for armhf, passes the offline host test, and booted the real XT
firmware on a Force -- but the DSP56300 interpreter (the only option on MPC's 32-bit ARM process; gearmulator's
JIT only targets x86-64 and AArch64) can't sustain the firmware's real-time clock even at the lowest DSP Clock
setting, and every fallback checked (64-bit helper process, network DSP bridge, a new ARMv7 JIT backend) is
closed off too, for this hardware. See "CPU" below and `docs/NOTES.md`'s Xenia entry (repo root) for the full
trail. Left in the repo as a reference and in case a fallback's constraint changes later (e.g. an
AArch64-capable Akai unit comes into scope).

## Layout

| Path | What |
|---|---|
| `src/xenia_engine.cpp` | `mpc_engine()` over gearmulator's `synthLib::Plugin` + `xt::Device` |
| `src/gearmulator/` | vendored gearmulator subset (see `src/VENDORED.md`) |
| `core/Makefile` | builds that subset into `libxenia_core.a` (interpreter only) |
| `params.json`, `vst.json` | parameters and port config for `tools/gen_vst.py` / `tools/build_port.sh` |
| `tools/interp_bench.cpp` | DSP56300 interpreter throughput on the device, no ROM needed |
| `docs/ROMS.md` | the ROM you have to supply |

This folder is laid out like the standalone port repos (mpc-vst-maze, -jv880): it can move to its own
`mpc-vst-xenia` repo as it is; the scripts then find mpc-vst-plugins next to it or through `MPC_VST`.

## Build and test

```
./test.sh      # host x86 core under ASan/UBSan + tools/test_port.sh (no ROM needed)
./build.sh     # armhf core inside arm32v7/gcc:12 (QEMU; slow the first time), then tools/build_port.sh
./bench.sh [device-ip]   # interpreter throughput on the device
```

`build/core/` holds whichever core (host or armhf) the last script built; each script copies its own in
before use. Output in `build/`: `xenia.so`, `skin/sd88me - VST - Xenia/`, `pluginlist-entry.xml`. The skin is the skin
studio's auto-layout for now.

## Device test (before installing anything)

`./devtest.sh <device-ip> [rom-file]` copies two test programs to `/tmp` on the device, runs them next to MPC
(no install, no restart) and deletes them again. The report goes to `build/devtest-report.txt`:
- `interp_bench`: DSP56300 interpreter throughput, no ROM needed.
- `xenia_probe`: boots the firmware from your ROM, then holds 0/1/4/8/10-note chords at DSP clock 100/75/50 %
  and reports `speed` (seconds of audio per second, >= 1.0 is real time), the level vs. the same chord at
  100 % (a drop = the firmware runs out of DSP time: the polyphony limit at that clock), and each emulator
  thread's CPU.

Without Docker: `./devtest.sh build` makes `build/xenia-devtest.tar`; then by hand:
```
ssh root@<ip> 'mkdir -p /tmp/xenia-devtest/rom && tar -xf - -C /tmp/xenia-devtest' < xenia-devtest.tar
ssh root@<ip> 'cat > /tmp/xenia-devtest/rom/rom1.bin' < rom1.bin
ssh root@<ip> 'cat > /tmp/xenia-devtest/rom/rom2.bin' < rom2.bin   # only if you have a half-ROM pair
ssh root@<ip> '/tmp/xenia-devtest/run.sh /tmp/xenia-devtest/rom; rm -rf /tmp/xenia-devtest'
```

## How it works

- A worker thread owns the emulator: finds the ROM, boots the firmware (the constructor runs it until the DSP
  is up), then keeps a ring buffer about 11.6 ms (4 blocks) ahead by calling `synthLib::Plugin::process()`
  128 frames at a time. Behind that call, gearmulator's own DSP56300 and MC68331 threads run the firmware.
  The XT runs at 40 kHz; `synthLib::Plugin` resamples to 44.1 kHz.
- MPC's audio thread only copies blocks out of the ring. If the emulation falls behind, it plays silence for
  that block and counts an xrun instead of stalling MPC.
- Parameters: program/bank change, the DSP clock (50-100 %, less = less CPU and fewer voices), and a first
  set of single-mode sound parameters sent as Microwave II/XT parameter-change SysEx. The patch isn't read
  back yet, so knobs show the last value set here, not the loaded patch's.
- State chunk: the parameter values (`key=value;...`), resent after the device boots.

## CPU

The XT firmware programs the DSP's clock through its PLL, and the emulator is cycle-driven: real time at
100 % needs the interpreter to execute that many DSP cycles per second on one core (the MC68331 and
resampler run on others). gearmulator logs the programmed clock at boot ("Clock speed changed to: N Mhz").
`tools/interp_bench` measures what the interpreter sustains on a synth-like loop:

| Machine | Interpreter |
|---|---|
| x86-64 Xeon @ 2.8 GHz (cloud build host, one core) | ~61 MIPS = ~71 MHz of DSP clock |
| 32-bit ARM under QEMU | runs (memory setup, interpreter); speed meaningless |
| MPC / Force (Cortex-A17, 32-bit) | **13.6-13.8 MIPS = ~16 MHz of DSP clock** (measured 2026-09-25, `devtest.sh`) |

**Result, real firmware on the Force (2026-09-25):** Xenia's XT ROM runs its PLL at **81.9 MHz** at DSP Clock
100 % -- about 5x the interpreter's ~16 MHz ceiling on this device. `xenia_probe`'s chord/clock sweep (0-10
held voices at DSP Clock 100/75/50 %) never got close to real time: speed stayed at 0.12 (100 %), 0.14
(75 %), 0.21 (50 %), against >= 1.0 needed. **Not viable in-process on this hardware, and no fallback closes
the gap either:** a 64-bit helper process needs an AArch64-capable SoC, which this Cortex-A17/RK3288 Force is
not (confirmed via `/proc/cpuinfo`, no kernel workaround possible); gearmulator's network DSP bridge works but
wasn't wanted for this port; an ARMv7 JIT backend for dsp56300 turned out to need an AArch32 assembler built
from scratch (asmjit has none), pushing it from weeks to a multi-month undertaking not justified by one port.
Full trail in `docs/NOTES.md`'s Xenia entry and `docs/DSP56300.md`.

## Licence

GPL-3.0, from gearmulator (`src/gearmulator/LICENSE.md`). The ROM is Waldorf's and is not included.
