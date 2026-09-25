# Vendored emulator source

`gearmulator/` is a vendored subset of [gearmulator](https://github.com/dsp56300/gearmulator) (the
Osirus/Xenia/Vavra/... emulator project by dsp56300 and contributors), committed here instead of fetched at
build time. It holds exactly what the Xenia (Waldorf Microwave II/XT) device library needs, with upstream's
folder layout under `gearmulator/source/`:

| Path | What | Upstream |
|---|---|---|
| `source/waldi/xt/xtLib` | Microwave II/XT device (ROM loader, MC68331 board, DSP glue, state) | gearmulator |
| `source/waldi/common/wLib` | code shared by the Waldorf devices | gearmulator |
| `source/framework/{synthLib,baseLib,hardwareLib}` | device base class, resampler, MIDI, LCD/flash chips | gearmulator |
| `source/cpu/dsp56300/source/{dsp56kEmu,dsp56kBase}` | DSP56300 emulator | [dsp56300](https://github.com/dsp56300/dsp56300) submodule |
| `source/cpu/dsp56300/source/asmjit/src` | asmjit (the JIT's assembler; compiled, never run here) | [dsp56300/asmjit](https://github.com/dsp56300/asmjit) submodule |
| `source/cpu/mc68k` | MC68331 emulator (Musashi based) | [mc68k](https://github.com/dsp56300/mc68k) submodule |
| `source/3rdparty/libresample` | resampler used by synthLib | gearmulator |

- **Vendored from**: gearmulator `373a439543fed3fcf3218225610ac028dd9a5bd2` (`main`, 2026-09-24), with its
  submodules at dsp56300 `2afc1c4fe274e945556c12641d0c7535ccdda4b3`, mc68k
  `1606a4db30a32f8cdcce4a391480f95471bcbc06`, asmjit `3577608cab0bc509f856ebf6e41b2f9d9f71acc4`.
- **Licenses**: gearmulator and dsp56300 are GPL-3.0 (`gearmulator/LICENSE.md`,
  `source/cpu/dsp56300/LICENSE.md`), so this port is GPL-3.0 too. asmjit is zlib
  (`source/cpu/dsp56300/source/asmjit/LICENSE.md`); Musashi has its own permissive licence (in its sources);
  libresample is LGPL/BSD (`source/3rdparty/libresample/LICENSE-*.txt`).
- **Left out**: everything else in gearmulator (JUCE plugins, UI, the other synths, tools, tests), upstream's
  CMake glue for the libraries we build (`../core/Makefile` replaces it), Visual Studio project files,
  Musashi's `example/`, and libresample's autotools/Windows/test files. The CMakeLists.txt files are kept
  for reference only.

## Local changes

Everything not listed here is byte-for-byte upstream.

1. **`source/framework/synthLib/buildconfig.h` (added).** Upstream generates it with CMake from
   `buildconfig.h.in`; this is that output with `SYNTHLIB_DEMO_MODE` off. Upstream's `.gitignore` next to it
   ignores it, so it is committed with `git add -f`.
2. **`source/cpu/dsp56300/source/dsp56kBase/buildconfig.h`: a 32-bit fallback at the end.** With
   `DSP56K_FORCE_INTERPRETER` and neither `HAVE_X86_64` nor `HAVE_ARM64` (32-bit ARM), it defines
   `HAVE_X86_64`. The JIT sources are always compiled (`DSP` owns a `Jit`), and they choose their codegen
   target with those two macros, mixing `#ifdef HAVE_X86_64` and `#ifndef HAVE_ARM64` guards, so with neither
   set they don't compile. Pointing them at the x86 emitter makes them compile; with the interpreter forced,
   nothing they emit ever runs. Outside the JIT, `HAVE_X86_64` is only read by `dspconfig.h`, whose JIT switch
   the interpreter flag overrides. Doing it in the header (not per target) keeps the core and the engine's
   translation unit on identical class layouts.
3. **`source/cpu/dsp56300/source/dsp56kEmu/jitops_helper.cpp`:** `static_assert(sizeof(m_interruptFunc) == 8)`
   became `static_assert(!g_jitSupported || ...)`. A function pointer is 4 bytes on 32-bit ARM; the check only
   matters when the JIT runs.

## Updating from upstream

1. Clone gearmulator with `git submodule update --init source/cpu/dsp56300 source/cpu/mc68k`, then
   `git submodule update --init source/asmjit` inside `source/cpu/dsp56300`.
2. Copy the same folders over these (see the table), drop the same left-out files, and reapply the three
   local changes.
3. Update the commit hashes above.
4. `../test.sh` (host, ASan) and `../build.sh` (armhf) must both pass; new upstream source files are picked up by
   `../core/Makefile`'s wildcards, so check its exclusion list if a new test file or platform file appears.
5. Device smoke test with a real ROM before releasing.
