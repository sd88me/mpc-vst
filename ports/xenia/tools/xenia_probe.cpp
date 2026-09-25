/* Device suitability probe for the Xenia port: boots the real Microwave II/XT firmware (your ROM) outside MPC,
 * then for each DSP clock setting and chord size renders audio as fast as the emulator can and reports
 *   speed  = seconds of audio produced per second of wall time (>= 1.0 is real time; the plugin needs headroom)
 *   rms    = output level, compared with the same chord at 100 % clock (a collapse means the firmware ran out of
 *            DSP cycles at that clock: dropped voices or broken audio)
 *   thread CPU % of the emulator's own threads (DSP56300, MC68331, this one)
 * Runs alongside MPC (it keeps playing), so the numbers include MPC's own load, as the plugin's would.
 *
 *   xenia_probe <rom-dir> [seconds-per-step] [-w]    (-w: also write /tmp/xenia_<clock>_<voices>.wav)
 * Built by ../devtest.sh (armhf) or by hand for the host. */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "synthLib/plugin.h"
#include "synthLib/romLoader.h"
#include "xtLib/xtDevice.h"
#include "xtLib/xtRomLoader.h"

namespace
{
	constexpr int BLOCK = 128;
	constexpr float RATE = 44100.0f;

	using Clock = std::chrono::steady_clock;

	double since(const Clock::time_point& _t)
	{
		return std::chrono::duration<double>(Clock::now() - _t).count();
	}

	// per-thread CPU ticks of this process, keyed by thread name
	std::map<std::string, long> threadTicks()
	{
		std::map<std::string, long> res;
		DIR* d = opendir("/proc/self/task");
		if (!d)
			return res;
		while (const dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.')
				continue;
			const std::string base = std::string("/proc/self/task/") + e->d_name;
			char name[64] = "?";
			if (FILE* f = fopen((base + "/comm").c_str(), "r"))
			{
				if (fgets(name, sizeof name, f))
					name[strcspn(name, "\n")] = 0;
				fclose(f);
			}
			if (FILE* f = fopen((base + "/stat").c_str(), "r"))
			{
				char buf[1024];
				const size_t n = fread(buf, 1, sizeof buf - 1, f);
				buf[n] = 0;
				fclose(f);
				// fields after the ")" of the comm: state is field 3, utime 14, stime 15
				const char* p = strrchr(buf, ')');
				long ut = 0, st = 0;
				if (p && sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld", &ut, &st) == 2)
					res[name] += ut + st;
			}
		}
		closedir(d);
		return res;
	}

	void writeWav(const std::string& _path, const std::vector<float>& _lr)
	{
		FILE* f = fopen(_path.c_str(), "wb");
		if (!f)
			return;
		const uint32_t dataBytes = static_cast<uint32_t>(_lr.size() * 2);
		auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
		auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
		fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); fwrite("WAVEfmt ", 1, 8, f);
		u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
		fwrite("data", 1, 4, f); u32(dataBytes);
		for (const float s : _lr)
		{
			const float c = std::max(-1.0f, std::min(1.0f, s));
			const int16_t v = static_cast<int16_t>(c * 32767.0f);
			fwrite(&v, 2, 1, f);
		}
		fclose(f);
	}

	struct Runner
	{
		synthLib::Plugin* plugin;
		std::vector<float> inL = std::vector<float>(BLOCK), inR = std::vector<float>(BLOCK);
		std::vector<float> outL = std::vector<float>(BLOCK), outR = std::vector<float>(BLOCK);
		synthLib::TAudioInputs ins{};
		synthLib::TAudioOutputs outs{};

		explicit Runner(synthLib::Plugin* _p) : plugin(_p)
		{
			ins[0] = inL.data(); ins[1] = inR.data();
			outs[0] = outL.data(); outs[1] = outR.data();
		}

		// renders _seconds of audio unpaced; returns wall seconds, fills rms and optionally the samples
		double render(const double _seconds, double& _rms, std::vector<float>* _keep = nullptr)
		{
			const int blocks = static_cast<int>(_seconds * RATE / BLOCK);
			double sum = 0;
			const auto t0 = Clock::now();
			for (int b = 0; b < blocks; ++b)
			{
				plugin->process(ins, outs, BLOCK, 0.0f, 0.0f, true, false);
				for (int i = 0; i < BLOCK; ++i)
				{
					sum += double(outL[i]) * outL[i] + double(outR[i]) * outR[i];
					if (_keep)
					{
						_keep->push_back(outL[i]);
						_keep->push_back(outR[i]);
					}
				}
			}
			const double wall = since(t0);
			_rms = std::sqrt(sum / (2.0 * blocks * BLOCK));
			return wall;
		}

		void midi(uint8_t _a, uint8_t _b, uint8_t _c)
		{
			plugin->addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Host, _a, _b, _c));
		}
	};

	const uint8_t CHORD[] = {48, 55, 60, 64, 67, 71, 74, 77, 81, 84, 88, 91, 36, 43};
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: %s <rom-dir> [seconds-per-step] [-w]\n", argv[0]);
		return 2;
	}
	const std::string romDir = argv[1];
	double stepSeconds = 2.0;
	bool wav = false;
	for (int i = 2; i < argc; ++i)
	{
		if (!strcmp(argv[i], "-w"))
			wav = true;
		else
			stepSeconds = atof(argv[i]);
	}

	printf("== xenia_probe: %ld CPUs online\n", sysconf(_SC_NPROCESSORS_ONLN));
	synthLib::RomLoader::addSearchPath(romDir, true);
	const auto rom = xt::RomLoader::findROM();
	if (!rom.isValid())
	{
		printf("RESULT no ROM found in %s (see docs/ROMS.md)\n", romDir.c_str());
		return 1;
	}
	printf("ROM: %s\n", rom.getFilename().c_str());

	synthLib::DeviceCreateParams params;
	params.romData = rom.getData();
	params.romName = rom.getFilename();
	params.hostSamplerate = RATE;

	auto t0 = Clock::now();
	auto device = std::make_unique<xt::Device>(params);
	const double bootSeconds = since(t0);
	if (!device->isValid())
	{
		printf("RESULT device invalid after boot\n");
		return 1;
	}
	printf("boot: %.1f s, DSP clock %.1f MHz at 100 %%\n", bootSeconds, device->getDspClockHz() / 1e6);

	synthLib::Plugin plugin(device.get(), [](synthLib::Device* _d) { return _d; });
	plugin.setMidiClockEnabled(false);
	plugin.setHostSamplerate(RATE, 0.0f);
	plugin.setBlockSize(BLOCK);
	plugin.applyPendingDeviceSamplerate();
	Runner run(&plugin);

	// let the firmware settle (init state, first patch) at full clock
	double rms = 0;
	run.render(2.0, rms);
	if (plugin.hasPendingDeviceSamplerate())
		plugin.applyPendingDeviceSamplerate();

	const int clocks[] = {100, 75, 50};
	const int voices[] = {0, 1, 4, 8, 10};
	std::map<int, double> refRms;   // voices -> rms at 100 %
	const long hz = sysconf(_SC_CLK_TCK);

	printf("\n%-6s %-6s %-7s %-9s %-7s %s\n", "clock", "voices", "speed", "rms", "vs100%", "thread CPU % (of one core)");
	for (const int clk : clocks)
	{
		device->setDspClockPercent(static_cast<uint32_t>(clk));
		for (const int v : voices)
		{
			// silence everything, let tails die, then hold a chord of v notes
			run.midi(0xb0, 123, 0);
			run.render(0.5, rms);
			for (int n = 0; n < v; ++n)
				run.midi(0x90, CHORD[n], 100);
			run.render(0.3, rms);   // attack, not measured

			std::vector<float> keep;
			const auto before = threadTicks();
			const double wall = run.render(stepSeconds, rms, wav ? &keep : nullptr);
			const auto after = threadTicks();

			for (int n = 0; n < v; ++n)
				run.midi(0x80, CHORD[n], 0);

			if (clk == 100)
				refRms[v] = rms;
			const double ref = refRms[v];
			char vs[16] = "-";
			if (clk != 100 && ref > 1e-6)
				snprintf(vs, sizeof vs, "%.2f", rms / ref);

			std::string threads;
			for (const auto& [name, ticks] : after)
			{
				const auto it = before.find(name);
				const long d = ticks - (it == before.end() ? 0 : it->second);
				const double pct = 100.0 * d / hz / wall;
				if (pct >= 2.0)
				{
					char b[64];
					snprintf(b, sizeof b, "%s %.0f  ", name.c_str(), pct);
					threads += b;
				}
			}
			printf("%-6d %-6d %-7.2f %-9.5f %-7s %s\n", clk, v, stepSeconds / wall, rms, vs, threads.c_str());
			fflush(stdout);

			if (wav)
			{
				char path[64];
				snprintf(path, sizeof path, "/tmp/xenia_%d_%d.wav", clk, v);
				writeWav(path, keep);
			}
		}
	}
	printf("\nspeed >= 1.0: that clock runs in real time here; the plugin also needs headroom (aim for >= 1.2).\n");
	printf("vs100%% well below 1.0 with voices held: the firmware runs out of DSP time at that clock.\n");
	return 0;
}
