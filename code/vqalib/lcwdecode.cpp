// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2025 Electronic Arts Inc.
// Copyright 2026 OpenTS contributors
// Contains material derived from Electronic Arts source code.
// Modified by OpenTS contributors, 2026.
// EA's GPLv3 Section 7 additional terms and supplemental warranty
// disclaimers apply; see LICENSE.md.

#include "cmp.h"
#include <algorithm>
#include <cstddef>

unsigned long __cdecl VQA_LCW_Uncompress(char const * source, char * dest, unsigned long length)
{
	auto const * input = reinterpret_cast<unsigned char const *>(source);
	auto * output = reinterpret_cast<unsigned char *>(dest);
	auto * begin = output;
	bool relative = *input == 0;
	if (relative) ++input;
	auto word = [&]() {
		unsigned value = input[0] | (input[1] << 8);
		input += 2;
		return value;
	};
	while (static_cast<unsigned long>(output - begin) < length) {
		unsigned opcode = *input++;
		unsigned count;
		unsigned char const * copy;
		unsigned remaining = length - static_cast<unsigned long>(output - begin);
		if (!(opcode & 0x80)) {
			count = (opcode >> 4) + 3;
			unsigned offset = ((opcode & 15) << 8) | *input++;
			copy = output - offset;
		} else if (!(opcode & 0x40)) {
			if (opcode == 0x80) break;
			count = std::min(opcode & 63, remaining);
			copy = input;
			input += count;
		} else if (opcode == 0xfe) {
			count = std::min(word(), remaining);
			unsigned char value = *input++;
			while (count--) *output++ = value;
			continue;
		} else {
			count = opcode == 0xff ? word() : (opcode & 63) + 3;
			unsigned offset = word();
			copy = relative ? output - offset : begin + offset;
		}
		count = std::min(count, remaining);
		while (count--) *output++ = *copy++;
	}
	return static_cast<unsigned long>(output - begin);
}


long __cdecl AudioUnzap(void * source, void * dest, long length)
{
	if (!source || !dest || length <= 0) return 0;
	auto const * input = static_cast<unsigned char const *>(source);
	auto const * begin = input;
	auto * output = static_cast<unsigned char *>(dest);
	unsigned char previous = 128;
	constexpr int DELTA2[] = {-2, -1, 0, 1};
	constexpr int DELTA4[] = {-9, -8, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 8};
	// Complete commands can emit more samples than the remaining requested count.
	while (length > 0) {
		unsigned opcode = *input++;
		unsigned count = (opcode & 63) + 1;
		switch (opcode >> 6) {
		case 0:
		case 1:
			for (unsigned i = 0; i < count; ++i) {
				unsigned packed = *input++;
				unsigned bits = (opcode >> 6) == 0 ? 2 : 4;
				for (unsigned shift = 0; shift < 8; shift += bits) {
					int delta = bits == 2 ? DELTA2[(packed >> shift) & 3] : DELTA4[(packed >> shift) & 15];
					previous = static_cast<unsigned char>(std::clamp(previous + delta, 0, 255));
					*output++ = previous;
					--length;
				}
			}
			break;
		case 2:
			if (opcode & 32) {
				int delta = static_cast<int>(opcode & 31);
				if (delta & 16) delta -= 32;
				previous = static_cast<unsigned char>(previous + delta);
				*output++ = previous;
				--length;
			} else {
				while (count--) {
					previous = *input++;
					*output++ = previous;
					--length;
				}
			}
			break;
		case 3:
			while (count--) {
				*output++ = previous;
				--length;
			}
			break;
		}
	}
	return static_cast<long>(input - begin);
}
