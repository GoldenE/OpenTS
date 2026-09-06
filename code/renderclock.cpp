/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "renderclock.hh"


/// <summary>Measures a simulation boundary using modular millisecond timestamps.</summary>
void RenderClockClass::Advance(std::uint32_t now)
{
	Span = HasBoundary ? now - Boundary : 0;
	Boundary = now;
	HasBoundary = true;
}


/// <summary>Samples one fixed-point alpha for every object in the forthcoming frame.</summary>
void RenderClockClass::Begin_Frame(std::uint32_t now, bool enabled)
{
	Elapsed = HasBoundary ? now - Boundary : 0;
	Alpha = 256;
	if (enabled && Span >= 1 && Span <= 250 && Elapsed < Span) {
		Alpha = static_cast<int>((Elapsed * 256) / Span);
	}
}


int RenderClockClass::Fetch_Alpha(void) const
{
	return(Alpha);
}


std::uint32_t RenderClockClass::Fetch_Span(void) const
{
	return(Span);
}


std::uint32_t RenderClockClass::Fetch_Elapsed(void) const
{
	return(Elapsed);
}
