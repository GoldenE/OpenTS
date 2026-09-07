/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "_renderstage.h"

thread_local RenderAnimationSample RenderAnimation;
thread_local std::vector<std::pair<void const *, RenderAnimationSample>> RenderAnimationOwners;
thread_local std::vector<RenderCachedAnimation> RenderCachedAnimations;
