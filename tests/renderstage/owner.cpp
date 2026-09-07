/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "stage.h"
#include "hdasset.hh"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
struct OwnerType {
	int Start;
	int Stages;
	int LoopStart;
	int LoopEnd;
	bool IsReverse;
};

#include "render_owner_predicates.inc"

HDAsset::Variant Owner_Artwork(bool reverse_loop)
{
	HDAsset::Variant variant;
	variant.Scale = 1;
	variant.LogicalWidth = variant.LogicalHeight = 1;
	variant.LogicalFrames = 4;
	variant.Sequences.push_back({0, 4, 4, true});
	for (unsigned logical = 0; logical < 4; ++logical) {
		for (int direction : {1, -1}) for (unsigned subframe = 0; subframe < 4; ++subframe) {
			HDAsset::Frame frame;
			frame.LogicalFrame = logical;
			frame.Direction = direction;
			frame.SubFrame = subframe;
			frame.Width = frame.Height = 1;
			int phase = int(logical) * 4 + direction * int(subframe);
			if (reverse_loop && direction < 0 && logical > 0) phase = ((3 - int(logical)) * 4 + int(subframe)) % 12;
			frame.Color = {static_cast<unsigned char>((phase + 16) % 16), 0, 0, 255};
			variant.Frames.push_back(frame);
		}
	}
	return variant;
}

int Draw_Phase(StageClass const & stage, HDAsset::Variant const & artwork, int intra)
{
	auto progress = stage.Fetch_Render_Progress(intra);
	auto frame = HDAsset::Select_Animated_Frame(artwork, stage.Fetch_Stage(), progress.Direction, progress.Fraction, 0);
	return frame ? frame->Color[0] : -1000;
}

bool Run_Owner(bool reverse, bool pingpong)
{
	OwnerType owner{0, 4, 0, pingpong || reverse ? 3 : 4, reverse};
	auto artwork = Owner_Artwork(reverse);
	StageClass stage;
	stage.Set_Stage(reverse ? owner.LoopEnd : 0);
	stage.Set_Step(reverse ? -1 : 1);
	stage.Set_Rate(4);
	int prior = Draw_Phase(stage, artwork, 0);
	int resets = 0, flips = 0;
	for (int tick = 0; tick < 120; ++tick) {
		if (stage.Graphic_Logic()) {
			int logical = stage.Fetch_Stage();
			if (pingpong && Owner_PingPong(logical, 255, &owner)) {
				stage.Set_Step(-stage.Fetch_Step());
				++flips;
			} else if (Owner_Loop(logical, 255, &owner)) {
				stage.Set_Stage(reverse ? owner.LoopEnd : owner.LoopStart - owner.Start);
				++resets;
			}
		}
		++Frame;
		if (reverse && stage.Fetch_Stage() == 0) return false;
		for (int intra : {0, 64, 128, 192, 256}) {
			int current = Draw_Phase(stage, artwork, intra);
			int delta = pingpong ? std::abs(current - prior) : (current - prior + (reverse ? 12 : 16)) % (reverse ? 12 : 16);
			if (current < 0 || delta > 1) return false;
			prior = current;
		}
	}
	return pingpong ? flips > 2 : resets > 2;
}
}

int Run_Production_Owner_Continuity()
{
	int failures = 0;
	for (auto mode : {0, 1, 2}) {
		if (!Run_Owner(mode == 1, mode == 2)) {
			std::printf("FAIL: production animation owner continuity mode %d\n", mode);
			++failures;
		}
	}
	return failures;
}
