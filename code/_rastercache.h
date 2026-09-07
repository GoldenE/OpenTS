/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once
#include "rastercache.hh"
#include "bsurface.h"

namespace RasterRuntimeState {
struct SurfaceSlot {
	std::unique_ptr<BSurface> Surface;
	std::size_t Bytes = 0;
	bool Busy = false;
};
extern thread_local RasterAssetCache Assets;
extern thread_local ScopedRasterAsset const * Active;
extern thread_local std::vector<std::unique_ptr<RasterWorkBuffers>> Work;
extern thread_local std::size_t WorkDepth;
extern thread_local std::vector<SurfaceSlot> Surfaces;
}
