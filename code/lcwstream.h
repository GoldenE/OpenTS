// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors
// See LICENSE.md for applicable additional terms and warranty disclaimers.

#pragma once

#include <stdexcept>

inline int LCW_Stream_Margin(int blocksize, bool compress)
{
	if (blocksize < 1 || blocksize > 65535) throw std::invalid_argument("LCW block size must fit its 16-bit header");
	int overhead = (blocksize + 62) / 63 + 1;
	if (compress && blocksize + overhead > 65535) throw std::invalid_argument("LCW compressed block must fit its 16-bit header");
	// Include the stream header and the decompressor's final word-store padding.
	return overhead + 4 + 4;
}
