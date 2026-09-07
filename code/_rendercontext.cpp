/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"

namespace RenderContextState {
RenderSettings Settings{RenderMode::Classic, 1, 1, 1, 256u * 1024u * 1024u};
std::uint64_t Generation = 1;
thread_local Point2D Residual;
thread_local int ResidualDensity = 1;
}
