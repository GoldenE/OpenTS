/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "interp.h"

#include "dbgprint.h"
#include "_tactica.h"
#include "display.h"
#include "globals.h"
#include "goptions.h"
#include "tactical.h"
#include "win.h"

#include "renderclock.hh"
#include "renderposition.hh"

#include <array>

namespace {

std::int64_t LargestRenderOffset = 0;

RenderClockClass & Render_Clock(void)
{
	static RenderClockClass clock;
	return(clock);
}


bool Trace_Enabled(void)
{
	static bool const enabled = [] {
		char value[2];
		return(GetEnvironmentVariableA("OPENTS_INTERP_TRACE", value, sizeof(value)) == 1 && value[0] == '1');
	}();
	return(enabled);
}


// Disposable Phase 1 evidence: buffer consecutive frames before writing the log.
struct ClockTraceSample {
	std::uint32_t Now;
	std::uint32_t Elapsed;
	std::uint32_t Span;
	int Alpha;
	int FrameNumber;
	int Speed;
	bool Smooth;
	int ObjectID = -1;
	int ObjectRTTI = -1;
	Coord Position = Coord(0, 0, 0);
	Coord Previous = Coord(0, 0, 0);
	Coord Offset = Coord(0, 0, 0);
	Point2D Pixel = Point2D(0, 0);
};

struct ClockTrace {
	std::array<ClockTraceSample, 4096> Samples;
	std::size_t Count = 0;
	int Boundaries = 0;
	int PreviousFrame = 0;
	int PreviousSpeed = -1;
	bool PreviousSmooth = false;
	bool Armed = true;
	unsigned WindowsRemaining = 8;
};


ClockTrace & Render_Trace(void)
{
	static ClockTrace trace;
	return(trace);
}


void Arm_Trace(void)
{
	ClockTrace & trace = Render_Trace();
	trace.Count = 0;
	trace.Boundaries = 0;
	trace.Armed = trace.WindowsRemaining != 0;
}


void Capture_Clock(std::uint32_t now)
{
	ClockTrace & trace = Render_Trace();
	if (trace.PreviousSpeed != Options.GameSpeed || trace.PreviousSmooth != Options.SmoothMotion) {
		Arm_Trace();
		trace.PreviousSpeed = Options.GameSpeed;
		trace.PreviousSmooth = Options.SmoothMotion;
	}
	if (!trace.Armed) return;

	int frame = static_cast<int>(Frame);
	RenderClockClass const & clock = Render_Clock();
	if (trace.Count == 0 && (frame == 0 || (clock.Fetch_Span() == 0 && Options.GameSpeed != 0))) return;
	if (trace.Count != 0 && trace.PreviousFrame != frame) trace.Boundaries++;
	trace.PreviousFrame = frame;
	trace.Samples[trace.Count++] = {now, clock.Fetch_Elapsed(), clock.Fetch_Span(), clock.Fetch_Alpha(), frame, Options.GameSpeed, Options.SmoothMotion};
	ClockTraceSample & captured = trace.Samples[trace.Count - 1];
	for (int layer = LAYER_FIRST; layer < LAYER_COUNT && captured.ObjectID == -1; layer++) {
		LayerClass const & list = DisplayClass::Layer[layer];
		for (int index = 0; index < list.Count(); index++) {
			ObjectClass const * object = list[index];
			if (object == nullptr || (!object->IsSelected && object->RTTI != RTTI_AIRCRAFT && object->RTTI != RTTI_BULLET)) continue;
			if (Render_Position_Delta(object->Position, object->RenderPrevious) == 0) continue;
			captured.ObjectID = object->Fetch_ID();
			captured.ObjectRTTI = object->RTTI;
			captured.Position = object->Position;
			captured.Previous = object->RenderPrevious;
			captured.Offset = object->Fetch_Render_Offset();
			TacticalMap->Coord_To_Pixel(object->Render_Coord() + captured.Offset, captured.Pixel);
			break;
		}
	}
	if (trace.Boundaries < 4 && trace.Count < trace.Samples.size()) return;

	trace.Armed = false;
	trace.WindowsRemaining--;
	DebugString("InterpClock: buffered frames=%u boundaries=%d\n", static_cast<unsigned>(trace.Count), trace.Boundaries);
	for (std::size_t i = 0; i < trace.Count; i++) {
		ClockTraceSample const & sample = trace.Samples[i];
		DebugString("InterpClock: frame=%d now=%u elapsed=%u span=%u alpha=%d speed=%d smooth=%d\n",
			sample.FrameNumber, sample.Now, sample.Elapsed, sample.Span, sample.Alpha, sample.Speed, sample.Smooth);
		if (sample.ObjectID != -1) {
			DebugString("InterpPosition: frame=%d now=%u id=%d rtti=%d pos=%d,%d,%d previous=%d,%d,%d offset=%d,%d,%d pixel=%d,%d\n",
				sample.FrameNumber, sample.Now, sample.ObjectID, sample.ObjectRTTI,
				sample.Position.X, sample.Position.Y, sample.Position.Z, sample.Previous.X, sample.Previous.Y, sample.Previous.Z,
				sample.Offset.X, sample.Offset.Y, sample.Offset.Z, sample.Pixel.X, sample.Pixel.Y);
		}
	}
}

}


/// <summary>Stamps the boundary immediately before simulation advances.</summary>
void Sim_Tick_Advance(void)
{
	RenderClockClass & clock = Render_Clock();
	clock.Advance(timeGetTime());
	if (Trace_Enabled() && clock.Fetch_Span() > 250) {
		DebugString("InterpClock: rejected span=%u frame=%d\n", clock.Fetch_Span(), static_cast<int>(Frame));
		Arm_Trace();
	}
	for (int layer = LAYER_FIRST; layer < LAYER_COUNT; layer++) {
		LayerClass const & list = DisplayClass::Layer[layer];
		for (int index = 0; index < list.Count(); index++) {
			if (list[index] != nullptr) list[index]->Invalidate_Render_Interpolation();
		}
	}
}


void Sim_Tick_End(void)
{
	for (int layer = LAYER_FIRST; layer < LAYER_COUNT; layer++) {
		LayerClass const & list = DisplayClass::Layer[layer];
		for (int index = 0; index < list.Count(); index++) {
			ObjectClass * object = list[index];
			if (object != nullptr && Render_Position_Delta(object->Position, object->RenderPrevious) > RENDER_INTERP_SNAP_LEPTONS) {
				object->Invalidate_Render_Interpolation();
			}
		}
	}
}


/// <summary>Samples the measured render clock without changing simulation state.</summary>
void Render_Frame_Begin(void)
{
	std::uint32_t now = timeGetTime();
	Render_Clock().Begin_Frame(now, Options.SmoothMotion && Options.GameSpeed != 0);
	if (Trace_Enabled()) Capture_Clock(now);
	for (int layer = LAYER_FIRST; layer < LAYER_COUNT; layer++) {
		LayerClass const & list = DisplayClass::Layer[layer];
		for (int index = 0; index < list.Count(); index++) {
			if (list[index] != nullptr) {
				LargestRenderOffset = std::max(LargestRenderOffset, Render_Position_Delta(list[index]->Fetch_Render_Offset(), Coord(0, 0, 0)));
			}
		}
	}
}


void Report_Render_Offsets(void)
{
	DebugString("Measure:   max-render-offset=%lld leptons\n", LargestRenderOffset);
	LargestRenderOffset = 0;
}


int Fetch_Render_Alpha(void)
{
	return(Render_Clock().Fetch_Alpha());
}
