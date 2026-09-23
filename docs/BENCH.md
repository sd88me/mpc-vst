# CPU check: will a port run on Gen1 hardware?

`tools/bench.sh` plays a plugin the way MPC does and reports how much of one audio block it uses. Run it on a
real device before releasing anything. Numbers from a PC say nothing about an MPC.

```
tools/bench.sh build/x.so <device-ip> [-s secs] [-v 1,4,8,16] [-p] [-j]   # on the device (armhf)
tools/bench.sh build/x-x86.so local                                     # on a PC: relative numbers only
```

Nothing is installed: the bench and a copy of the `.so` go to `/tmp` on the device and are deleted afterwards. MPC
can keep running.

## What it does
- 44100 Hz, 128-frame blocks (the budget is 2902 µs per block), host transport playing at 120 BPM.
- Stages: **idle**. For instruments, **N-voice chords** re-struck every half second (default 1, 4, 8 and 16 voices;
  effects get noise input instead). Then a **Q-Link sweep** (4 random parameters per block while holding 8 voices;
  `-p` sweeps every parameter). Finally the **release tail** after note-off, which catches denormal slowdowns.
- Each block is timed with the thread's own CPU clock (`CLOCK_THREAD_CPUTIME_ID`), so time MPC's real-time threads
  take from us doesn't count. The plugin's own worker threads are reported separately (`threads%`).
- The bench runs pinned to core 1 at normal priority, so MPC's audio always wins and nothing glitches.

## Verdict
The p99 and worst block, as a percentage of one block on one core:

| Verdict | p99 | worst block | Meaning |
|---|---|---|---|
| PASS | ≤ 15% | ≤ 50% | Several instances alongside a normal project |
| WARN | ≤ 35% | ≤ 80% | One or two instances; say so in the release notes |
| FAIL | above | above | Will glitch in real projects: optimise first |

Why those numbers: on the Force, MPC runs one audio worker per core (`AudioWorker0-3`, SCHED_FIFO, cores 2-3
isolated) and spreads tracks across them. Stock engines, effects and other plugins share the same four
2902 µs windows, so a single plugin should take a small slice, and never a spike near a whole block.

Exit code is 1 on FAIL, so the bench can gate a release script. `-j` adds a JSON line; `tools/release.py --bench`
puts it into the release's INSTALL.md.

Limits: the bench drives the audio path only. App-style plugins (e.g. Crate Digger) do their real work in worker
threads and child processes that the bench doesn't trigger. Watch `top` on the device while using them instead.

## Reference results (Force, RK3288 Cortex-A17 @ 1.8 GHz, MPC running, 2026-09-24)

| Plugin | idle | 16 voices / audio | Q-Link sweep p99 | worst block | Verdict |
|---|---|---|---|---|---|
| Maze Voice (mono synth) | 6.8% | 7.1% | 9.6% | 10.3% | PASS |
| Crate Digger (stream player, idle) | 0.1% | 0.1% | 1.0% | 5.9% | PASS |

## Gen2 devices
Untested: nobody on the project owns one yet. `tools/probe_device.sh` (read-only) reports the CPU, whether the
`MPC` binary is 32- or 64-bit, the audio worker layout and which plugin formats MPC has compiled in. If the
binary is 64-bit, ports need an aarch64 build (`arm64v8/gcc:12`) and possibly a different `pluginList-…` key, and
`bench.c` needs building for aarch64 too. A PASS on Gen1 should be comfortable on faster Gen2 CPUs, but only a
run on the device confirms it. Please post probe and bench output from Gen2 units.
