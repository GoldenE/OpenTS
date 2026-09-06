/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "renderclock.hh"
#include "renderposition.hh"
#include "options.h"

#include <cstddef>
#include <cstdio>
#include <limits>

static_assert(sizeof(OptionsClass) == 112);
static_assert(offsetof(OptionsClass, IntegerScaling) == 52);
static_assert(offsetof(OptionsClass, VSync) == 53);
static_assert(offsetof(OptionsClass, SmoothMotion) == 54);
static_assert(offsetof(OptionsClass, Renderer) == 56);
static_assert(offsetof(OptionsClass, CursorScale) == 60);

namespace {

int Failures = 0;

void Check(bool condition, char const * description)
{
	std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
	if (!condition) Failures++;
}

}


int main()
{
	Coord priorposition(100, 200, 300);
	Coord current(120, 160, 380);
	Coord half = Render_Position_Offset(current, priorposition, 128);
	Check(half.X == -10 && half.Y == 20 && half.Z == -40, "Positive, negative and altitude movement interpolate together");
	Check(Render_Position_Offset(current, priorposition, 0) + current == priorposition, "Zero alpha draws the previous position");
	Check(Render_Position_Offset(current, priorposition, 256) == Coord(0, 0, 0), "Disabled or completed interpolation has no offset");
	Check(Render_Position_Offset(current, current, 0) == Coord(0, 0, 0), "Invalidated positions stay snapped for the entire tick");
	Check(Render_Position_Delta(Coord(0, 0, 1024), Coord(0, 0, 0)) == RENDER_INTERP_SNAP_LEPTONS, "Altitude uses the same inclusive threshold as horizontal travel");
	Check(Render_Position_Delta(Coord(-1025, 1, 0), Coord(0, 0, 0)) > RENDER_INTERP_SNAP_LEPTONS, "Negative relocation beyond the threshold snaps");
	Check(Render_Position_Delta(Coord((std::numeric_limits<int>::max)(), 0, 0), Coord((std::numeric_limits<int>::min)(), 0, 0)) == 4294967295LL, "Relocation measurement widens before subtraction");
	Check(Render_Position_Offset(Coord(1000000000, 0, 0), Coord(999999000, 0, 0), 128).X == -500, "Large coordinates retain small displacement precision");
	RenderClockClass clock;
	clock.Begin_Frame(0, true);
	Check(clock.Fetch_Alpha() == 256, "No boundary draws current state");
	clock.Advance(0);
	clock.Begin_Frame(10, true);
	Check(clock.Fetch_Alpha() == 256, "Timestamp zero is a valid first boundary, not a measured interval");
	clock.Advance(48);
	clock.Begin_Frame(60, true);
	Check(clock.Fetch_Span() == 48 && clock.Fetch_Elapsed() == 12 && clock.Fetch_Alpha() == 64, "Quarter tick at the measured 48 ms cadence");
	clock.Begin_Frame(72, true);
	Check(clock.Fetch_Alpha() == 128 && clock.Fetch_Alpha() == 128, "Repeated reads use the frame's latched half-tick alpha");
	clock.Begin_Frame(84, true);
	Check(clock.Fetch_Alpha() == 192, "Three-quarter tick");
	clock.Begin_Frame(96, true);
	Check(clock.Fetch_Alpha() == 256, "Exact interval end clamps to current state");
	clock.Begin_Frame((std::numeric_limits<std::uint32_t>::max)(), true);
	Check(clock.Fetch_Alpha() == 256, "Very large elapsed time clamps before multiplication can overflow");

	clock.Advance(96);
	clock.Begin_Frame(120, false);
	Check(clock.Fetch_Alpha() == 256, "Disabled smoothing draws current state during a valid span");
	clock.Begin_Frame(120, true);
	Check(clock.Fetch_Alpha() == 128, "Re-enabling smoothing uses current measured time");
	clock.Advance(128);
	clock.Begin_Frame(136, true);
	Check(clock.Fetch_Span() == 32 && clock.Fetch_Alpha() == 64, "Cadence change replaces the old interval");

	clock.Advance(10128);
	clock.Begin_Frame(10129, true);
	Check(clock.Fetch_Span() == 10000 && clock.Fetch_Alpha() == 256, "A ten-second stall does not reuse a stale interval");
	clock.Advance(10176);
	clock.Begin_Frame(10200, true);
	Check(clock.Fetch_Alpha() == 128, "A valid interval after a stall restores interpolation");
	clock.Advance(10176);
	clock.Begin_Frame(10176, true);
	Check(clock.Fetch_Alpha() == 256, "Two boundaries in one millisecond reject a zero span");
	clock.Advance(10177);
	clock.Begin_Frame(10177, true);
	Check(clock.Fetch_Alpha() == 0, "One millisecond is an accepted span");
	clock.Advance(10427);
	clock.Begin_Frame(10552, true);
	Check(clock.Fetch_Alpha() == 128, "250 milliseconds is an accepted span");
	clock.Advance(10678);
	clock.Begin_Frame(10679, true);
	Check(clock.Fetch_Alpha() == 256, "251 milliseconds rejects the span");

	RenderClockClass wrapping;
	wrapping.Advance(0xfffffff0u);
	wrapping.Advance(0x20u);
	wrapping.Begin_Frame(0x38u, true);
	Check(wrapping.Fetch_Span() == 48 && wrapping.Fetch_Alpha() == 128, "Boundary measurement survives the 32-bit timer rollover");

	RenderClockClass regular;
	regular.Advance(1000);
	regular.Advance(1048);
	bool uniform = true;
	int previous = 0;
	for (std::uint32_t elapsed = 0; elapsed <= 48; elapsed++) {
		regular.Begin_Frame(1048 + elapsed, true);
		int alpha = regular.Fetch_Alpha();
		uniform &= alpha >= previous && alpha >= 0 && alpha <= 256;
		if (elapsed != 0) uniform &= alpha - previous == 5 || alpha - previous == 6;
		previous = alpha;
	}
	Check(uniform, "Each millisecond advances alpha uniformly to fixed-point rounding precision");
	regular.Begin_Frame(1094, true);
	int before = regular.Fetch_Alpha();
	regular.Advance(1096);
	regular.Begin_Frame(1105, true);
	Check(before == 245 && regular.Fetch_Alpha() == 48, "Across a boundary the unsampled AI gap retains uniform time progression");

	RenderClockClass jitter;
	jitter.Advance(1000);
	jitter.Advance(1049);
	jitter.Begin_Frame(1093, true);
	int phase_before = jitter.Fetch_Alpha();
	jitter.Advance(1094);
	jitter.Begin_Frame(1096, true);
	int phase_delta = 256 + jitter.Fetch_Alpha() - phase_before;
	Check(phase_before == 229 && jitter.Fetch_Alpha() == 11 && phase_delta == 38,
		"Unequal 49/45 ms spans advance 38 phase units across 3 ms: formula correctness does not imply uniform motion");

	std::printf("Failures: %d\n", Failures);
	return(Failures == 0 ? 0 : 1);
}
