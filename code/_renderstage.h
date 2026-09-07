/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once

#include "renderstage.hh"
#include <utility>
#include <vector>

extern thread_local RenderAnimationSample RenderAnimation;
extern thread_local std::vector<std::pair<void const *, RenderAnimationSample>> RenderAnimationOwners;

struct RenderCachedAnimation {
	void const * Owner;
	void const * Frame;
	bool Seen;
};

extern thread_local std::vector<RenderCachedAnimation> RenderCachedAnimations;
