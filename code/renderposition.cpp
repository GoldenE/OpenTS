/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "renderposition.hh"

#include <algorithm>
#include <cstdlib>

std::int64_t Render_Position_Delta(Coord const & current, Coord const & previous)
{
	return(std::max({std::abs(std::int64_t(current.X) - previous.X), std::abs(std::int64_t(current.Y) - previous.Y), std::abs(std::int64_t(current.Z) - previous.Z)}));
}


Coord Render_Position_Offset(Coord const & current, Coord const & previous, int alpha)
{
	int back = 256 - alpha;
	if (back == 0) return(Coord(0, 0, 0));

	// Widen before subtraction and multiplication, including coordinates from modded scenarios.
	auto offset = [back](int now, int before) {
		return(static_cast<int>((std::int64_t(before) - now) * back / 256));
	};
	return(Coord(offset(current.X, previous.X), offset(current.Y, previous.Y), offset(current.Z, previous.Z)));
}
