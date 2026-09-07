/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

struct RenderStageProgress {
	std::uint32_t Fraction = 0;
	int Direction = 0;
};

struct RenderAnimationSample {
	RenderStageProgress Progress;
	std::uint32_t Facing = 0;
};

RenderStageProgress Sample_Render_Stage(std::int64_t elapsed, int interval, int step, bool running, int intra_tick);
RenderAnimationSample Current_Render_Animation();
void Bind_Render_Animation_Owner(void const * owner, RenderStageProgress progress, std::uint32_t facing);
RenderAnimationSample Sample_Render_Animation_Owner(void const * owner, RenderAnimationSample fallback);
bool Render_Cached_Animation_Changed(void const * owner, void const * frame);

class ScopedRenderAnimationOwners {
	public:
		ScopedRenderAnimationOwners();
		~ScopedRenderAnimationOwners();
		ScopedRenderAnimationOwners(ScopedRenderAnimationOwners const &) = delete;
		ScopedRenderAnimationOwners & operator=(ScopedRenderAnimationOwners const &) = delete;
};

class ScopedRenderAnimation {
	public:
		ScopedRenderAnimation(RenderStageProgress progress, std::uint32_t facing = 0);
		~ScopedRenderAnimation();
		ScopedRenderAnimation(ScopedRenderAnimation const &) = delete;
		ScopedRenderAnimation & operator=(ScopedRenderAnimation const &) = delete;

	private:
		RenderAnimationSample Previous;
};
