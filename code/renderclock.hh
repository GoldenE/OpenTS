/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

class RenderClockClass {
	public:
		void Advance(std::uint32_t now);
		void Begin_Frame(std::uint32_t now, bool enabled);
		int Fetch_Alpha(void) const;
		std::uint32_t Fetch_Span(void) const;
		std::uint32_t Fetch_Elapsed(void) const;

	private:
		std::uint32_t Boundary = 0;
		std::uint32_t Span = 0;
		std::uint32_t Elapsed = 0;
		bool HasBoundary = false;
		int Alpha = 256;
};
