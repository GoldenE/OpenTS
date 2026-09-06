/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "coord.h"

#include <cstdint>

inline constexpr std::int64_t RENDER_INTERP_SNAP_LEPTONS = 1024;

std::int64_t Render_Position_Delta(Coord const & current, Coord const & previous);
Coord Render_Position_Offset(Coord const & current, Coord const & previous, int alpha);
