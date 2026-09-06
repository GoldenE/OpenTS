/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include "opents_version.h"

#include <cstdint>

static_assert(sizeof(void *) == 4 || sizeof(void *) == 8);
static_assert((OPENTS_VERSION_PACKED & 0xff000000u) == 0);

// Keep Win32's existing identity; pointer-bearing state requires a distinct x64 stamp.
inline constexpr std::uint32_t OPENTS_ARCHITECTURE_STAMP = sizeof(void *) == 8 ? 0x01000000u : 0u;
inline constexpr std::uint32_t OPENTS_STATE_VERSION = OPENTS_VERSION_PACKED | OPENTS_ARCHITECTURE_STAMP;
// Debug records an additional header field, so configurations cannot share recordings.
#if defined(_DEBUG)
inline constexpr char const * OPENTS_RECORDING_TAG = sizeof(void *) == 8 ? "OTSREC2" : "OTSREC1";
#else
inline constexpr char const * OPENTS_RECORDING_TAG = sizeof(void *) == 8 ? "OTSREL2" : "OTSREL1";
#endif
