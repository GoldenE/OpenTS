/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "stage.h"
#include "hdasset.hh"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

int Frame = 0;
int Run_Production_Owner_Continuity();

namespace {
int Failures = 0;

void Check(bool condition, char const * message)
{
	if (!condition) { ++Failures; std::printf("FAIL: %s\n", message); }
}

struct Snapshot {
	std::vector<unsigned char> Bytes;
	template<typename T> void Serialize(T & value)
	{
		if constexpr (std::is_integral_v<T>) {
			auto begin = reinterpret_cast<unsigned char const *>(&value);
			Bytes.insert(Bytes.end(), begin, begin + sizeof(value));
		} else value.Serialize(*this);
	}
};

HDAsset::Variant Artwork(unsigned first, unsigned count)
{
	HDAsset::Variant variant;
	variant.Scale = 1;
	variant.LogicalWidth = variant.LogicalHeight = 1;
	variant.LogicalFrames = first + count;
	variant.Sequences.push_back({first, count, 4, true});
	for (unsigned logical = first; logical < first + count; ++logical) {
		for (int direction : {1, -1}) for (unsigned sub = 0; sub < 4; ++sub) {
			HDAsset::Frame frame;
			frame.LogicalFrame = logical;
			frame.SubFrame = sub;
			frame.Direction = direction;
			frame.Width = frame.Height = 1;
			int phase = (int(logical - first) * 4 + direction * int(sub) + int(count * 4)) % int(count * 4);
			frame.Color = {static_cast<unsigned char>(phase), 0, 0, 255};
			variant.Frames.push_back(frame);
		}
	}
	return variant;
}

int Sample(StageClass const & stage, HDAsset::Variant const & artwork, int intra = 256)
{
	auto progress = stage.Fetch_Render_Progress(intra);
	auto frame = HDAsset::Select_Animated_Frame(artwork, stage.Fetch_Stage(), progress.Direction, progress.Fraction, 0);
	Check(frame != nullptr, "Production selector finds the owner-selected logical frame");
	return frame ? frame->Color[0] : -1000;
}

void Check_Interval(StageClass & stage, HDAsset::Variant const & artwork, unsigned first, unsigned count, int direction)
{
	stage.Set_Step(direction);
	stage.Set_Rate(4);
	stage.Set_Stage(first + (direction < 0 ? count - 1 : 0));
	int prior = Sample(stage, artwork, 0);
	for (unsigned tick = 0; tick < count * 8; ++tick) {
		if (stage.Graphic_Logic()) {
			if (stage.Fetch_Stage() >= int(first + count)) stage.Set_Stage(first);
			if (stage.Fetch_Stage() < int(first)) stage.Set_Stage(first + count - 1);
		}
		++Frame;
		for (int intra : {0, 64, 128, 192, 256}) {
			int current = Sample(stage, artwork, intra);
			int delta = (direction * (current - prior) + int(count * 4)) % int(count * 4);
			Check(delta <= 1, "Forward/reverse and loop boundaries preserve authored color continuity");
			prior = current;
		}
	}
}
}

int main()
{
	StageClass stage;
	Frame = 100;
	stage.Set_Stage(2);
	stage.Set_Rate(4);
	++Frame;
	Check(stage.Fetch_Render_Progress(0).Fraction == 0, "First completed tick starts the current frame at subframe zero");
	Check(stage.Fetch_Render_Progress(128).Fraction == 8192, "Half render tick is one eighth of a four-tick stage");
	Frame += 2;
	auto before = stage.Fetch_Render_Progress(128);
	stage.Adjust_Rate(20);
	Check(stage.Fetch_Render_Progress(128).Fraction == before.Fraction, "Rate adjustment preserves the active countdown's denominator");
	stage.Just_Set_Rate(7);
	Check(stage.Fetch_Render_Progress(128).Fraction == before.Fraction, "Direct future-rate change preserves the active interval");
	Frame += 30;
	Check(stage.Fetch_Render_Progress(256).Fraction == 65536 && stage.Fetch_Stage() == 2, "Expired timer holds terminal subframe without predicting simulation state");
	Check(stage.Graphic_Logic() && stage.Fetch_Stage() == 3, "Only production Graphic_Logic advances the logical stage");
	++Frame;
	Check(stage.Fetch_Render_Progress(0).Fraction == 0, "New interval starts continuously after the owning logic tick");
	Check(stage.Fetch_Render_Progress(256).Fraction == 65536 / 7, "New countdown adopts the changed rate");

	Snapshot initial;
	stage.Serialize(initial);
	Check(initial.Bytes.size() == 20, "Serialized stage keeps the five legacy 32-bit values");
	for (int i = 0; i <= 256; ++i) stage.Fetch_Render_Progress(i);
	Snapshot final;
	stage.Serialize(final);
	Check(initial.Bytes == final.Bytes, "Render sampling leaves every serialized stage/timer byte unchanged");

	for (int step : {0, 2, -2}) {
		stage.Set_Step(step);
		Check(stage.Fetch_Render_Progress(256).Direction == 0, "Held and unsupported steps have no temporal direction");
	}
	stage.Set_Step(1);
	stage.Just_Set_Rate(0);
	Check(stage.Fetch_Render_Progress(256).Direction == 0, "A held rate suppresses temporal advancement");
	Check(Sample_Render_Stage((std::numeric_limits<std::int64_t>::max)(), (std::numeric_limits<int>::max)(), 1, true, 256).Fraction == 65536, "Long stalls clamp before multiplication");
	Check(Sample_Render_Stage(-20, 4, 1, true, -100).Fraction == 0, "Negative elapsed time and render input clamp to interval start");
	Check(Sample_Render_Stage(5, 4, 1, false, 256).Direction == 0, "Paused timer is held");

	auto loop = Artwork(10, 3);
	Check_Interval(stage, loop, 10, 3, 1);
	Check_Interval(stage, loop, 10, 3, -1);
	auto building = Artwork(40, 4);
	Check_Interval(stage, building, 40, 4, 1);
	Check_Interval(stage, building, 40, 4, -1);

	stage.Set_Stage(10);
	stage.Set_Rate(4);
	stage.Set_Step(1);
	int prior = Sample(stage, loop, 0);
	for (int tick = 0; tick < 40; ++tick) {
		if (stage.Graphic_Logic() && (stage.Fetch_Stage() == 10 || stage.Fetch_Stage() == 12)) stage.Set_Step(-stage.Fetch_Step());
		++Frame;
		for (int intra : {0, 64, 128, 192, 256}) {
			int current = Sample(stage, loop, intra);
			Check(std::abs(current - prior) <= 1, "Ping-pong direction flip joins the two authored blocks continuously");
			prior = current;
		}
	}

	stage.Set_Stage(10);
	stage.Set_Rate(4);
	Frame += 2;
	Sample(stage, loop);
	stage.Set_Stage(40);
	stage.Set_Rate(8);
	++Frame;
	Check(Sample(stage, building, 0) == 0, "Sequence replacement starts at new owner frame without retaining prior interpolation");
	Check(stage.Fetch_Render_Progress(0).Fraction == 0, "Disabled smooth-motion input pins the intra-tick component to zero");

	Check(Current_Render_Animation().Progress.Direction == 0, "Default draw has no inherited temporal stage");
	{
		ScopedRenderAnimation outer({1234, -1}, 260);
		{
			ScopedRenderAnimation child({4321, 1}, 7);
			Check(Current_Render_Animation().Progress.Fraction == 4321, "Nested animation uses its own timing");
		}
		Check(Current_Render_Animation().Progress.Fraction == 1234 && Current_Render_Animation().Facing == 4, "Nested draw restores parent timing and wrapped facing");
	}
	Check(Current_Render_Animation().Progress.Direction == 0, "Animation scope restores the default after drawing");
	{
		ScopedRenderAnimationOwners owners;
		Bind_Render_Animation_Owner(&stage, {1234, -1}, 19);
		Check(Sample_Render_Animation_Owner(&stage, {}).Progress.Fraction == 1234, "Externally driven animation samples its production owner's stage");
		Check(Sample_Render_Animation_Owner(&loop, {{6789, 1}, 0}).Progress.Fraction == 6789, "Independent nested animation retains its own timer");
		Bind_Render_Animation_Owner(&stage, {}, 33);
		Check(Sample_Render_Animation_Owner(&stage, {}).Facing == 33 && Sample_Render_Animation_Owner(&stage, {}).Progress.Direction == 0, "Facing-driven turret holds temporal stage and samples actual facing");
	}
	Check(Sample_Render_Animation_Owner(&stage, {}).Facing == 0, "Frame exit invalidates all non-owning animation bindings");
	{
		ScopedRenderAnimationOwners owners;
		Check(Render_Cached_Animation_Changed(&stage, &loop), "First temporal building frame invalidates its cached background");
		Check(!Render_Cached_Animation_Changed(&stage, &loop), "Repeated render sample does not redraw an unchanged building subframe");
		Check(Render_Cached_Animation_Changed(&stage, &building), "Changed building subframe invalidates its cached background");
	}
	{
		ScopedRenderAnimationOwners owners;
		Check(!Render_Cached_Animation_Changed(&stage, &building), "Building subframe identity survives adjacent display frames");
	}
	{ ScopedRenderAnimationOwners owners; }
	{
		ScopedRenderAnimationOwners owners;
		Check(Render_Cached_Animation_Changed(&stage, &building), "An absent owner's cached pointer expires before address reuse");
	}
	Failures += Run_Production_Owner_Continuity();
	std::printf("Render stage contracts: %d failures\n", Failures);
	return Failures ? 1 : 0;
}
