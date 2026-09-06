// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors
// See LICENSE.md for applicable additional terms and warranty disclaimers.

#include "ahandle.h"
#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<std::string> Lines;
void DebugString(char const * format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	Lines.emplace_back(line);
}

#include "audio_trace.inc"

void Require(bool result, char const * message)
{
	if (!result) throw std::runtime_error(message);
}

unsigned Count(char const * text)
{
	unsigned count = 0;
	for (auto const & line : Lines) if (line.find(text) != std::string::npos) ++count;
	return count;
}

int main(int argc, char ** argv)
{
	try {
		bool enabled = argc > 1 && std::strcmp(argv[1], "on") == 0;
		_putenv_s("OPENTS_VQA_AUDIO_TRACE", enabled ? "1" : "0");
		Ahandle audio{};
		audio.SampleRate = 22050;
		audio.Channels = 2;
		audio.BitsPerSample = 16;
		unsigned char pcm16[] = {255, 127, 0, 128, 99};
		Trace_Audio_PCM(false, &audio, pcm16, sizeof(pcm16));
		if (enabled) Require(Lines.back().find("scanned=4 samples=2 bits=16 channels=2 min=-32768 max=32767 nonzero=2") != std::string::npos, "16-bit extrema or odd-tail handling differs");
		audio.BitsPerSample = 8;
		unsigned char pcm8[] = {0, 128, 255};
		Trace_Audio_PCM(false, &audio, pcm8, sizeof(pcm8));
		if (enabled) Require(Lines.back().find("samples=3 bits=8 channels=2 min=-128 max=127 nonzero=2") != std::string::npos, "8-bit PCM was not centered on 128");
		std::vector<unsigned char> large(20000, 128);
		Trace_Audio_PCM(false, &audio, large.data(), static_cast<unsigned>(large.size()));
		if (enabled) Require(Lines.back().find("bytes=20000 scanned=16384 samples=16384") != std::string::npos, "PCM scan did not stop at 16 KiB");
		for (int index = 0; index < 100; ++index) Trace_Audio_PCM(false, &audio, pcm8, sizeof(pcm8));
		audio.BitsPerSample = 16;
		unsigned char silence[] = {0, 0};
		for (int index = 0; index < 100; ++index) Trace_Audio_PCM(true, &audio, silence, sizeof(silence));
		if (enabled) Require(Count("submitted PCM") == 7, "Silence consumed the reserved first-signal record");
		unsigned char signal[] = {1, 0};
		Trace_Audio_PCM(true, &audio, signal, sizeof(signal));
		if (enabled) Require(Lines.back().find("submitted PCM[7]: fill=100") != std::string::npos && Lines.back().find("nonzero=1") != std::string::npos, "Late first signal was not captured");
		for (int index = 0; index < 100; ++index) {
			Trace_Audio_PCM(true, &audio, signal, sizeof(signal));
			Trace_Audio_Result("Lock", S_OK, &audio, 0, 8192);
		}
		if (enabled) {
			Require(Count("decoded PCM") == 8 && Count("submitted PCM") == 8 && Count("Lock:") == 24, "Audio trace record budget changed");
			Require(Lines.size() == 40, "Audio trace produced unexpected output");
		} else {
			Require(Lines.empty(), "Disabled audio trace emitted output");
		}
		std::puts("Passed bounded VQA PCM metrics, late signal, and opt-in checks");
		return 0;
	} catch (std::exception const & error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
