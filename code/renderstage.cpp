/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "renderstage.hh"
#include "_renderstage.h"

#include <algorithm>
#include <functional>
#include <vector>

RenderStageProgress Sample_Render_Stage(std::int64_t elapsed, int interval, int step, bool running, int intra_tick)
{
	if (!running || interval <= 0 || (step != 1 && step != -1)) return {};
	std::int64_t bounded = std::clamp(elapsed, std::int64_t(0), std::int64_t(interval) + 1);
	std::int64_t duration = std::int64_t(interval) * 256;
	// Frame has advanced once after the completed logic tick; the render phase fills that interval.
	std::int64_t position = (bounded - 1) * 256 + std::clamp(intra_tick, 0, 256);
	position = std::clamp(position, std::int64_t(0), duration);
	return {static_cast<std::uint32_t>(position * 65536 / duration), step};
}


RenderAnimationSample Current_Render_Animation()
{
	return RenderAnimation;
}


void Bind_Render_Animation_Owner(void const * owner, RenderStageProgress progress, std::uint32_t facing)
{
	auto & owners = RenderAnimationOwners;
	auto entry = std::lower_bound(owners.begin(), owners.end(), owner, [](auto const & item, void const * key) { return std::less<void const *>{}(item.first, key); });
	RenderAnimationSample sample{progress, facing & 255u};
	if (entry != owners.end() && entry->first == owner) entry->second = sample;
	else owners.insert(entry, {owner, sample});
}


RenderAnimationSample Sample_Render_Animation_Owner(void const * owner, RenderAnimationSample fallback)
{
	auto const & owners = RenderAnimationOwners;
	auto entry = std::lower_bound(owners.begin(), owners.end(), owner, [](auto const & item, void const * key) { return std::less<void const *>{}(item.first, key); });
	return entry != owners.end() && entry->first == owner ? entry->second : fallback;
}


ScopedRenderAnimationOwners::ScopedRenderAnimationOwners()
{
	RenderAnimationOwners.clear();
	for (auto & animation : RenderCachedAnimations) animation.Seen = false;
}


ScopedRenderAnimationOwners::~ScopedRenderAnimationOwners()
{
	RenderAnimationOwners.clear();
	std::erase_if(RenderCachedAnimations, [](auto const & animation) { return !animation.Seen; });
}


bool Render_Cached_Animation_Changed(void const * owner, void const * frame)
{
	for (auto & animation : RenderCachedAnimations) {
		if (animation.Owner != owner) continue;
		bool changed = animation.Frame != frame;
		animation.Frame = frame;
		animation.Seen = true;
		return changed;
	}
	RenderCachedAnimations.push_back({owner, frame, true});
	return true;
}


ScopedRenderAnimation::ScopedRenderAnimation(RenderStageProgress progress, std::uint32_t facing) : Previous(RenderAnimation)
{
	RenderAnimation = {progress, facing & 255u};
}


ScopedRenderAnimation::~ScopedRenderAnimation()
{
	RenderAnimation = Previous;
}
