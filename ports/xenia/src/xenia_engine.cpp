/* Xenia (gearmulator's Waldorf Microwave II/XT emulator) as an MPC OS engine: mpc_engine()
 * (wrapper/engine.h) over gearmulator's own synthLib::Plugin + xt::Device, the same pair its JUCE
 * plugin drives.
 *
 * Threads: MPC's audio thread only ever copies finished blocks out of a ring buffer (render()).
 * A worker thread owns the emulator: it loads the ROM, boots the device (seconds with the DSP
 * interpreter), then keeps the ring RING_TARGET frames ahead, running synthLib::Plugin::process()
 * 128 frames at a time. The XT runs at 40 kHz; Plugin resamples to 44.1 kHz. The emulator itself
 * adds two threads (DSP56300, MC68331) behind Plugin::process(), which waits on them.
 * If the emulation can't keep up, the audio thread plays silence for the missing blocks instead of
 * stalling MPC, and the Status readout shows the load.
 *
 * Parameters: a curated set of single-mode sound parameters sent as Microwave II/XT single
 * parameter change SysEx (F0 3E 0E 7F 20 00 HH LL VV F7), plus program/bank changes and the DSP
 * clock (lower = less CPU, less polyphony). The XT's patch is not read back yet: knob positions are
 * the last values set here, not the loaded patch's. */
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <time.h>

#include "synthLib/plugin.h"
#include "synthLib/romLoader.h"
#include "xtLib/xtDevice.h"
#include "xtLib/xtRomLoader.h"

extern "C" {
#include "engine.h"
}

namespace
{
	constexpr int BLOCK = 128;               // MPC's period, and the chunk the worker renders
	constexpr int RING = 8192;               // frames, power of two
	constexpr int RING_TARGET = BLOCK * 4;   // how far ahead the worker keeps the ring (~11.6 ms)
	constexpr float HOST_RATE = 44100.0f;

	enum Kind { Special, Sound };
	struct Param
	{
		const char* key;
		Kind kind;
		int index;       // Sound: single parameter index; Special: unused
		int offset;      // added before sending (bipolar params are -64..63 on screen)
		int def;
	};

	// Keep in step with params.json (keys, defaults); order here doesn't matter
	const Param g_params[] = {
		{"program", Special, 0, 0, 0},
		{"bank", Special, 0, 0, 0},
		{"dsp_clock", Special, 0, 0, 5},
		{"wave", Sound, 25, 0, 0},
		{"w1_start", Sound, 26, 0, 0},
		{"w2_start", Sound, 36, 0, 0},
		{"mix_w1", Sound, 47, 0, 96},
		{"mix_w2", Sound, 48, 0, 96},
		{"cutoff", Sound, 62, 0, 127},
		{"resonance", Sound, 63, 0, 0},
		{"f1_type", Sound, 64, 0, 0},
		{"f1_env_amount", Sound, 66, 64, 0},
		{"f1_attack", Sound, 113, 0, 0},
		{"f1_decay", Sound, 114, 0, 0},
		{"f1_sustain", Sound, 115, 0, 127},
		{"f1_release", Sound, 116, 0, 0},
		{"amp_attack", Sound, 119, 0, 0},
		{"amp_decay", Sound, 120, 0, 0},
		{"amp_sustain", Sound, 121, 0, 127},
		{"amp_release", Sound, 122, 0, 0},
		{"lfo1_rate", Sound, 159, 0, 64},
		{"lfo1_shape", Sound, 160, 0, 0},
		{"glide_time", Sound, 90, 0, 0},
		{"volume", Sound, 77, 0, 96},
	};
	constexpr int NPARAMS = sizeof(g_params) / sizeof(g_params[0]);
	constexpr int DSP_CLOCKS[] = {50, 60, 70, 80, 90, 100};

	int findParam(const char* _key)
	{
		for (int i = 0; i < NPARAMS; ++i)
			if (!strcmp(g_params[i].key, _key))
				return i;
		return -1;
	}

	uint64_t nowNs()
	{
		timespec ts{};
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
	}

	enum class Stage { Loading, NoRom, Booting, Ready, Failed };

	struct Xenia
	{
		std::string dataDir;

		// audio ring: written by the worker, read by render()
		int16_t ring[RING * 2] = {};
		std::atomic<uint32_t> ringWrite{0}, ringRead{0};
		std::atomic<uint32_t> underruns{0};

		// parameter values (screen range) and which ones still have to reach the device
		std::mutex paramLock;
		int values[NPARAMS] = {};
		bool dirty[NPARAMS] = {};
		bool programDirty = false;

		// MIDI from the host, drained by the worker
		std::mutex midiLock;
		std::vector<synthLib::SMidiEvent> midiIn;

		std::atomic<Stage> stage{Stage::Loading};
		std::atomic<int> loadPercent{0};   // wall time inside Plugin::process() per block, vs. the block's duration
		std::string romName;

		std::atomic<bool> quit{false};
		std::mutex wakeLock;
		std::condition_variable wake;
		std::thread worker;

		std::unique_ptr<xt::Device> device;
		std::unique_ptr<synthLib::Plugin> plugin;
		int appliedClock = -1;
	};

	void queueEvent(Xenia* _x, synthLib::SMidiEvent&& _ev)
	{
		std::lock_guard lock(_x->midiLock);
		_x->midiIn.emplace_back(std::move(_ev));
	}

	synthLib::SMidiEvent soundParamSysex(const int _index, const int _value)
	{
		synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);
		ev.sysex = {0xf0, 0x3e, 0x0e, 0x7f, 0x20, 0x00,
			static_cast<uint8_t>(_index >> 7), static_cast<uint8_t>(_index & 0x7f),
			static_cast<uint8_t>(_value < 0 ? 0 : _value > 127 ? 127 : _value), 0xf7};
		return ev;
	}

	// worker thread: turn dirty parameters into device events
	void flushParams(Xenia* _x)
	{
		std::vector<synthLib::SMidiEvent> evs;
		int clock = -1;
		{
			std::lock_guard lock(_x->paramLock);
			if (_x->programDirty)
			{
				const int bank = _x->values[findParam("bank")];
				const int prog = _x->values[findParam("program")];
				// Bank select both ways (CC 0 and CC 32); neither is mapped to a sound parameter on the XT
				evs.emplace_back(synthLib::MidiEventSource::Host, 0xb0, 0, bank);
				evs.emplace_back(synthLib::MidiEventSource::Host, 0xb0, 32, bank);
				evs.emplace_back(synthLib::MidiEventSource::Host, 0xc0, prog, 0);
				_x->programDirty = false;
			}
			for (int i = 0; i < NPARAMS; ++i)
			{
				if (!_x->dirty[i])
					continue;
				_x->dirty[i] = false;
				const Param& p = g_params[i];
				if (p.kind == Sound)
					evs.emplace_back(soundParamSysex(p.index, _x->values[i] + p.offset));
				else if (!strcmp(p.key, "dsp_clock"))
					clock = DSP_CLOCKS[_x->values[i]];
			}
		}
		for (auto& e : evs)
			_x->plugin->addMidiEvent(e);
		if (clock > 0 && clock != _x->appliedClock)
		{
			_x->device->setDspClockPercent(static_cast<uint32_t>(clock));
			_x->appliedClock = clock;
		}
	}

	void drainMidi(Xenia* _x)
	{
		std::vector<synthLib::SMidiEvent> evs;
		{
			std::lock_guard lock(_x->midiLock);
			evs.swap(_x->midiIn);
		}
		for (auto& e : evs)
			_x->plugin->addMidiEvent(e);
	}

	bool boot(Xenia* _x)
	{
		if (!_x->dataDir.empty())
			synthLib::RomLoader::addSearchPath(_x->dataDir, true);

		const auto rom = xt::RomLoader::findROM();
		if (!rom.isValid())
		{
			_x->stage = Stage::NoRom;
			return false;
		}
		_x->romName = rom.getFilename();
		const auto slash = _x->romName.find_last_of('/');
		if (slash != std::string::npos)
			_x->romName = _x->romName.substr(slash + 1);

		_x->stage = Stage::Booting;

		synthLib::DeviceCreateParams params;
		params.romData = rom.getData();
		params.romName = rom.getFilename();
		params.hostSamplerate = HOST_RATE;
		params.preferredSamplerate = 0.0f;

		// boots the firmware before returning (xt::Device's constructor runs it until the DSP is up)
		_x->device = std::make_unique<xt::Device>(params);
		if (!_x->device->isValid())
		{
			_x->device.reset();
			_x->stage = Stage::Failed;
			return false;
		}

		_x->plugin = std::make_unique<synthLib::Plugin>(_x->device.get(), [](synthLib::Device* _d) { return _d; });
		_x->plugin->setMidiClockEnabled(false);
		_x->plugin->setHostSamplerate(HOST_RATE, 0.0f);
		_x->plugin->setBlockSize(BLOCK);
		_x->plugin->applyPendingDeviceSamplerate();
		return true;
	}

	void workerMain(Xenia* _x)
	{
		pthread_setname_np(pthread_self(), "xenia-emu");

		if (!boot(_x))
			return;

		// state restored before boot (a project load) and every knob touched since goes out now
		{
			std::lock_guard lock(_x->paramLock);
			_x->programDirty = true;
		}
		_x->stage = Stage::Ready;

		std::vector<float> inL(BLOCK, 0.0f), inR(BLOCK, 0.0f), outL(BLOCK), outR(BLOCK);
		synthLib::TAudioInputs ins{};
		synthLib::TAudioOutputs outs{};
		ins[0] = inL.data();
		ins[1] = inR.data();
		outs[0] = outL.data();
		outs[1] = outR.data();

		uint64_t winNs = 0, winBlocks = 0;

		while (!_x->quit)
		{
			flushParams(_x);
			drainMidi(_x);

			const uint32_t w = _x->ringWrite.load(std::memory_order_relaxed);
			const uint32_t r = _x->ringRead.load(std::memory_order_acquire);
			if (w - r >= static_cast<uint32_t>(RING_TARGET))
			{
				std::unique_lock lock(_x->wakeLock);
				_x->wake.wait_for(lock, std::chrono::milliseconds(1));
				continue;
			}

			const uint64_t t0 = nowNs();
			_x->plugin->process(ins, outs, BLOCK, 0.0f, 0.0f, true, false);
			if (_x->plugin->hasPendingDeviceSamplerate())
				_x->plugin->applyPendingDeviceSamplerate();
			winNs += nowNs() - t0;

			for (int i = 0; i < BLOCK; ++i)
			{
				const uint32_t pos = ((w + i) & (RING - 1)) * 2;
				auto conv = [](float _f)
				{
					const float s = _f * 32767.0f;
					return static_cast<int16_t>(s > 32767.0f ? 32767 : s < -32768.0f ? -32768 : s);
				};
				_x->ring[pos] = conv(outL[i]);
				_x->ring[pos + 1] = conv(outR[i]);
			}
			_x->ringWrite.store(w + BLOCK, std::memory_order_release);

			// process() waits for the DSP thread to deliver the block, so its wall time is how long the
			// emulator takes per block: 100 % is the real-time limit, above it the ring drains (xruns)
			if (++winBlocks == 344)   // ~1 s
			{
				const double blockNs = BLOCK * 1e9 / HOST_RATE;
				_x->loadPercent = static_cast<int>(winNs * 100.0 / (winBlocks * blockNs));
				winNs = winBlocks = 0;
			}
		}
	}

	void* create(const char* _dataDir)
	{
		auto* x = new Xenia();
		if (_dataDir)
			x->dataDir = _dataDir;
		for (int i = 0; i < NPARAMS; ++i)
			x->values[i] = g_params[i].def;
		x->worker = std::thread(workerMain, x);
		return x;
	}

	void destroy(void* _inst)
	{
		auto* x = static_cast<Xenia*>(_inst);
		x->quit = true;
		x->wake.notify_all();
		if (x->worker.joinable())
			x->worker.join();   // a worker still booting finishes the boot first
		x->plugin.reset();
		x->device.reset();
		delete x;
	}

	void midi(void* _inst, const uint8_t* _msg, int _len)
	{
		auto* x = static_cast<Xenia*>(_inst);
		if (_len < 1 || x->stage != Stage::Ready)
			return;
		queueEvent(x, synthLib::SMidiEvent(synthLib::MidiEventSource::Host, _msg[0], _len > 1 ? _msg[1] : 0, _len > 2 ? _msg[2] : 0));
	}

	void setValue(Xenia* _x, const int _i, const int _v)
	{
		std::lock_guard lock(_x->paramLock);
		_x->values[_i] = _v;
		if (!strcmp(g_params[_i].key, "program") || !strcmp(g_params[_i].key, "bank"))
			_x->programDirty = true;
		else
			_x->dirty[_i] = true;
	}

	// state chunk: "key=value;..." (screen values), restored into the cache and resent to the device
	void setState(Xenia* _x, const char* _s)
	{
		std::string s(_s);
		size_t pos = 0;
		while (pos < s.size())
		{
			size_t end = s.find(';', pos);
			if (end == std::string::npos)
				end = s.size();
			const auto item = s.substr(pos, end - pos);
			const auto eq = item.find('=');
			if (eq != std::string::npos)
			{
				const int i = findParam(item.substr(0, eq).c_str());
				if (i >= 0)
					setValue(_x, i, atoi(item.c_str() + eq + 1));
			}
			pos = end + 1;
		}
	}

	void setParam(void* _inst, const char* _key, const char* _val)
	{
		auto* x = static_cast<Xenia*>(_inst);
		if (!strcmp(_key, "state"))
		{
			setState(x, _val);
			return;
		}
		const int i = findParam(_key);
		if (i < 0)
			return;
		int v = static_cast<int>(lround(atof(_val)));
		if (!strcmp(_key, "dsp_clock"))
			v = v < 0 ? 0 : v > 5 ? 5 : v;
		setValue(x, i, v);
	}

	int getParam(void* _inst, const char* _key, char* _buf, int _len)
	{
		auto* x = static_cast<Xenia*>(_inst);
		if (!strcmp(_key, "status"))
		{
			switch (x->stage.load())
			{
			case Stage::Loading: return snprintf(_buf, _len, "Loading");
			case Stage::NoRom: return snprintf(_buf, _len, "No ROM");
			case Stage::Booting: return snprintf(_buf, _len, "Booting");
			case Stage::Failed: return snprintf(_buf, _len, "ROM failed");
			case Stage::Ready:
				return snprintf(_buf, _len, "%d%% %u xrun", x->loadPercent.load(), x->underruns.load());
			}
			return 0;
		}
		if (!strcmp(_key, "state"))
		{
			std::string s;
			std::lock_guard lock(x->paramLock);
			for (int i = 0; i < NPARAMS; ++i)
				s += std::string(g_params[i].key) + "=" + std::to_string(x->values[i]) + ";";
			if (static_cast<int>(s.size()) >= _len)
				return 0;
			memcpy(_buf, s.c_str(), s.size() + 1);
			return static_cast<int>(s.size());
		}
		const int i = findParam(_key);
		if (i < 0)
			return 0;
		std::lock_guard lock(x->paramLock);
		return snprintf(_buf, _len, "%d", x->values[i]);
	}

	void render(void* _inst, int16_t* _out, int _frames)
	{
		auto* x = static_cast<Xenia*>(_inst);
		const uint32_t r = x->ringRead.load(std::memory_order_relaxed);
		const uint32_t w = x->ringWrite.load(std::memory_order_acquire);
		if (w - r < static_cast<uint32_t>(_frames))
		{
			memset(_out, 0, sizeof(int16_t) * 2 * _frames);
			if (x->stage == Stage::Ready)
				x->underruns.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			for (int i = 0; i < _frames; ++i)
			{
				const uint32_t pos = ((r + i) & (RING - 1)) * 2;
				_out[i * 2] = x->ring[pos];
				_out[i * 2 + 1] = x->ring[pos + 1];
			}
			x->ringRead.store(r + _frames, std::memory_order_release);
		}
		x->wake.notify_one();
	}

	const mpc_engine_t g_engine = {create, destroy, midi, setParam, getParam, render};
}

extern "C" const mpc_engine_t* mpc_engine(void)
{
	return &g_engine;
}
