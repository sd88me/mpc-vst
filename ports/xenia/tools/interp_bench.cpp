/* DSP56300 interpreter throughput, the number that decides whether Xenia can run in real time on a
 * 32-bit ARM MPC (no JIT there). Runs a synth-like inner loop (parallel moves, MACs, modulo addressing)
 * on the same interpreter the plugin uses, for a few seconds, and prints emulated MIPS and the DSP
 * clock (cycles/s) it sustains. Needs no ROM.
 *
 * Compare "MHz" with the clock the XT firmware programs into the DSP's PLL (gearmulator logs
 * "Clock speed changed to: N Mhz" when it boots): real time at 100% DSP clock needs MHz >= N on ONE core,
 * with the MC68331 and the resampler on other cores. The plugin's DSP Clock parameter scales N down.
 *
 * Built by ../bench.sh for the device (armhf) and the host. Usage: interp_bench [seconds] */
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

using namespace dsp56k;

int main(int argc, char** argv)
{
	const double seconds = argc > 1 ? atof(argv[1]) : 3.0;

	static DefaultMemoryValidator validator;
	Peripherals56303 periphX;
	PeripheralsNop periphY;
	Memory mem(validator, 0x080000, 0x800000, 0x200000);
	DSP dsp(mem, &periphX, &periphY);

	// a two-tap filter/mixer step, the kind of loop a VA voice spends its time in
	const char* program[] = {
		"move #$100,r0",
		"move #$200,r4",
		"move #$300,r1",
		"move #$3f,m0",
		"move #$3f,m4",
		"move #$3f,m1",
		// loop body
		"move x:(r0)+,x0 y:(r4)+,y0",
		"mpy x0,y0,a x:(r0)+,x1 y:(r4)+,y1",
		"mac x1,y1,a",
		"move a,b",
		"asr b",
		"add x0,b",
		"move b,x:(r1)+",
		"tfr a,b",
		"jmp $6",
	};

	Assembler as;
	TWord pc = 0;
	for (const char* line : program)
	{
		const auto r = as.assemble(line);
		if (!r.success())
		{
			fprintf(stderr, "assembly failed: %s\n", line);
			return 1;
		}
		dsp.memWriteP(pc++, r.word[0]);
		if (r.wordCount > 1)
			dsp.memWriteP(pc++, r.word[1]);
	}
	dsp.setPC(0);

	printf("interpreter only: %s\n", g_useJIT ? "no (JIT build!)" : "yes");

	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	const auto i0 = dsp.getInstructionCounter();
	const auto c0 = dsp.getCycles();
	double elapsed = 0;
	while (elapsed < seconds)
	{
		for (int i = 0; i < 100000; ++i)
			dsp.execInterpreter();
		elapsed = std::chrono::duration<double>(clock::now() - t0).count();
	}
	const double instr = static_cast<double>(dsp.getInstructionCounter() - i0);
	const double cycles = static_cast<double>(dsp.getCycles() - c0);

	printf("%.1f MIPS, %.1f MHz of DSP clock (%.2f cycles/instr) over %.1f s\n",
		instr / elapsed / 1e6, cycles / elapsed / 1e6, cycles / instr, elapsed);
	return 0;
}
