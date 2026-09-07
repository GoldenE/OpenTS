/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "_rastercache.h"

namespace RasterRuntimeState {
thread_local RasterAssetCache Assets;
thread_local ScopedRasterAsset const * Active = nullptr;
thread_local std::vector<std::unique_ptr<RasterWorkBuffers>> Work;
thread_local std::size_t WorkDepth = 0;
thread_local std::vector<SurfaceSlot> Surfaces;
}
