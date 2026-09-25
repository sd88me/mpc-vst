#pragma once

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#	define HAVE_SSE
#endif

#if defined(_M_X64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd__64__)
#	define HAVE_X86_64
#endif

#if defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	define HAVE_ARM64
#endif

// mpc-vst: 32-bit ARM (MPC OS) runs the interpreter only, but the JIT sources are still compiled (Dsp owns a
// Jit) and pick their codegen target from these macros, of which a 32-bit compiler sets neither. Point them at
// the x86 emitter; asmjit assembles for any target on any host and nothing it emits ever runs. HAVE_X86_64 is
// used nowhere outside the JIT and dspconfig.h, which DSP56K_FORCE_INTERPRETER already overrides.
#if defined(DSP56K_FORCE_INTERPRETER) && !defined(HAVE_X86_64) && !defined(HAVE_ARM64)
#	define HAVE_X86_64
#endif
