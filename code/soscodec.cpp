// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 Electronic Arts Inc.
// Copyright 2026 OpenTS contributors
// Contains material derived from Electronic Arts source code.
// Modified by OpenTS contributors, 2026.
// Copyright (c) 1994, HMI, INC. All Rights Reserved
// EA's GPLv3 Section 7 additional terms and supplemental warranty
// disclaimers apply; see LICENSE.md.

#include "soscomp.h"
#include "vqalib/cmp.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {
constexpr int STEPS[] = {
	7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
	73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
	494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,
	2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,
	10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
constexpr int INDEX_DELTA[] = {-1,-1,-1,-1,2,4,6,8};

int Difference(unsigned code, int step)
{
	int delta = step >> 3;
	if (code & 4) delta += step;
	if (code & 2) delta += step >> 1;
	if (code & 1) delta += step >> 2;
	return code & 8 ? -delta : delta;
}

void Decode_Channel(unsigned char const * source, unsigned char * dest, unsigned samples, unsigned stride, long & predicted, short & scaled_index)
{
	int index = scaled_index / 32;
	for (unsigned sample = 0; sample < samples; ++sample) {
		unsigned code = (source[sample / 2] >> ((sample & 1) * 4)) & 15;
		predicted = std::clamp(predicted + Difference(code, STEPS[index]), -32768L, 32767L);
		index = std::clamp(index + INDEX_DELTA[code & 7], 0, 88);
		std::int16_t value = static_cast<std::int16_t>(predicted);
		std::memcpy(dest + sample * stride, &value, sizeof(value));
	}
	scaled_index = static_cast<short>(index * 32);
}
}


void __cdecl sosCODECInitStream(_SOS_COMPRESS_INFO * info)
{
	info->wIndex = info->wIndex2 = 0;
	info->dwPredicted = info->dwPredicted2 = 0;
}


unsigned long __cdecl sosCODECDecompressData(_SOS_COMPRESS_INFO * info, unsigned long bytes)
{
	if (info->wBitSize != 16 || info->wChannels != 1) return 0;
	Decode_Channel(reinterpret_cast<unsigned char const *>(info->lpSource), reinterpret_cast<unsigned char *>(info->lpDest), bytes / 2, 2, info->dwPredicted, info->wIndex);
	return bytes;
}


void __cdecl General_sosCODECInitStream(_SOS_COMPRESS_INFO * info)
{
	sosCODECInitStream(info);
	info->wStep = info->wStep2 = 7;
	info->dwSampleIndex = info->dwSampleIndex2 = 0;
}


unsigned long __cdecl General_sosCODECDecompressData(_SOS_COMPRESS_INFO * info, unsigned long bytes)
{
	unsigned channels = info->wChannels == 2 ? 2 : 1;
	unsigned sample_bytes = info->wBitSize == 16 ? 2 : 1;
	unsigned samples = bytes / sample_bytes / channels;
	info->dwSampleIndex = info->dwSampleIndex2 = 0;
	for (unsigned channel = 0; channel < channels; ++channel) {
		auto & predicted = channel ? info->dwPredicted2 : info->dwPredicted;
		auto & index = channel ? info->wIndex2 : info->wIndex;
		auto & step = channel ? info->wStep2 : info->wStep;
		auto & code_buffer = channel ? info->wCodeBuf2 : info->wCodeBuf;
		auto & code = channel ? info->wCode2 : info->wCode;
		auto & difference = channel ? info->dwDifference2 : info->dwDifference;
		auto & sample_index = channel ? info->dwSampleIndex2 : info->dwSampleIndex;
		auto const * source = reinterpret_cast<unsigned char const *>(info->lpSource) + channel;
		auto * dest = reinterpret_cast<unsigned char *>(info->lpDest) + channel * sample_bytes;
		for (unsigned sample = 0; sample < samples; ++sample) {
			if (!(sample & 1)) code_buffer = source[(sample / 2) * channels];
			code = static_cast<short>((code_buffer >> ((sample & 1) * 4)) & 15);
			difference = Difference(code, step);
			predicted = std::clamp(predicted + difference, -32768L, 32767L);
			if (sample_bytes == 2) {
				std::int16_t value = static_cast<std::int16_t>(predicted);
				std::memcpy(dest, &value, sizeof(value));
			} else {
				*dest = static_cast<unsigned char>((predicted >> 8) ^ 0x80);
			}
			dest += sample_bytes * channels;
			index = static_cast<short>(std::clamp(index + INDEX_DELTA[code & 7], 0, 88));
			step = static_cast<short>(STEPS[index]);
			++sample_index;
		}
	}
	return bytes;
}


void __cdecl VQA_sosCODECInitStream(_VQA_SOS_COMPRESS_INFO * info)
{
	info->dwPredicted = info->dwPredicted2 = 0;
	info->wIndex = info->wIndex2 = 0;
}


void __cdecl VQA_sosCODECDecompressData(void * source, void * dest, unsigned short bits, unsigned short channels, unsigned long bytes, _VQA_SOS_COMPRESS_INFO * info)
{
	if (bits != 16 || (channels != 1 && channels != 2)) return;
	unsigned samples = bytes / 2 / channels;
	auto const * input = static_cast<unsigned char const *>(source);
	auto * output = static_cast<unsigned char *>(dest);
	Decode_Channel(input, output, samples, 2 * channels, info->dwPredicted, info->wIndex);
	if (channels == 2) Decode_Channel(input + bytes / 8, output + 2, samples, 4, info->dwPredicted2, info->wIndex2);
}
