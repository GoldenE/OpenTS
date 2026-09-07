/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "_rastercache.h"
#include "rendercontext.hh"
#include <algorithm>
#include <cassert>
#include <limits>

using namespace RasterRuntimeState;

ScopedRasterAsset::ScopedRasterAsset(HDAsset::Source const * source, int frame, void const * data, std::size_t bytes, std::uint64_t epoch) : Previous(Active), Data(data), Size(bytes)
{
	Assets.Configure(std::min<std::size_t>(64u * 1024u * 1024u, Get_Render_Settings().CacheBudgetBytes / 4), epoch);
	if (source && source->Generation && data && bytes) {
		Key.emplace(); Key->Generation = source->Generation; Key->Digest = source->Content; Key->Frame = frame;
	}
	Active = this;
}

ScopedRasterAsset::~ScopedRasterAsset() { Active = Previous; }

std::shared_ptr<CachedRasterFrame const> ScopedRasterAsset::Fetch(void const * data, int width, int height, int source_density, int density, int bpp, bool rle) const
{
	if (!Key || data != Data) return {};
	RasterAssetKey key = *Key;
	key.Width = width; key.Height = height; key.SourceDensity = source_density; key.Density = density; key.BytesPerPixel = bpp; key.RLE = rle;
	return Assets.Fetch(key, {static_cast<std::uint8_t const *>(Data), Size});
}

std::shared_ptr<CachedRasterFrame const> Fetch_Raster_Asset(void const * data, int width, int height, int source_density, int density, int bpp, bool rle)
{
	return Active ? Active->Fetch(data, width, height, source_density, density, bpp, rle) : nullptr;
}

std::size_t Raster_Asset_Cache_Bytes() { return Assets.Bytes(); }
std::uint64_t Raster_Asset_Cache_Hits() { return Assets.Hits(); }
std::uint64_t Raster_Asset_Cache_Builds() { return Assets.Builds(); }
std::size_t Raster_Asset_Cache_Entries() { return Assets.Entries(); }

RasterWorkLease::RasterWorkLease() : Index(WorkDepth)
{
	if (Index >= 4) Temporary = std::make_unique<RasterWorkBuffers>();
	else {
		while (Work.size() <= Index) Work.push_back(std::make_unique<RasterWorkBuffers>());
	}
	++WorkDepth;
}

RasterWorkLease::~RasterWorkLease()
{
	assert(WorkDepth == Index + 1);
	--WorkDepth;
	if (!Temporary) {
		auto & buffers = *Work[Index];
		std::size_t bytes = buffers.Rows.capacity() * sizeof(void *) + buffers.Decoded.capacity() + buffers.Scan.capacity() + buffers.Depth.capacity();
		if (bytes > 256u * 1024u) buffers = {};
	}
}

RasterWorkBuffers & RasterWorkLease::Buffers() { return Temporary ? *Temporary : *Work[Index]; }

std::size_t Raster_Surface_Scratch_Bytes()
{
	std::size_t bytes = 0;
	for (auto const & slot : Surfaces) bytes += slot.Bytes;
	return bytes;
}

RasterSurfaceLease::RasterSurfaceLease(int width, int height, int bpp, int density) : Index(std::numeric_limits<std::size_t>::max())
{
	std::size_t bytes = Checked_Raster_Size(width, height, bpp, density);
	std::size_t budget = std::min<std::size_t>(16u * 1024u * 1024u, Get_Render_Settings().CacheBudgetBytes / 16);
	for (auto & slot : Surfaces) {
		if (!slot.Busy && (slot.Bytes > budget || Raster_Surface_Scratch_Bytes() > budget)) { slot.Surface.reset(); slot.Bytes = 0; }
	}
	for (std::size_t i = 0; i < Surfaces.size(); ++i) {
		auto & slot = Surfaces[i];
		if (!slot.Busy && slot.Surface && slot.Surface->Get_Width() == width && slot.Surface->Get_Height() == height && slot.Surface->Bytes_Per_Pixel() == bpp && slot.Surface->Get_Raster_Scale() == density) {
			slot.Busy = true; Index = i; return;
		}
	}
	if (bytes <= budget) {
		for (auto & slot : Surfaces) {
			if (Raster_Surface_Scratch_Bytes() <= budget - bytes) break;
			if (!slot.Busy) { slot.Surface.reset(); slot.Bytes = 0; }
		}
		if (Raster_Surface_Scratch_Bytes() <= budget - bytes) {
			for (std::size_t i = 0; i < Surfaces.size(); ++i) if (!Surfaces[i].Busy && !Surfaces[i].Surface) { Index = i; break; }
			if (Index == std::numeric_limits<std::size_t>::max() && Surfaces.size() < 8) { Index = Surfaces.size(); Surfaces.emplace_back(); }
			if (Index == std::numeric_limits<std::size_t>::max()) {
				for (std::size_t i = 0; i < Surfaces.size(); ++i) {
					if (!Surfaces[i].Busy) { Surfaces[i].Surface.reset(); Surfaces[i].Bytes = 0; Index = i; break; }
				}
			}
			if (Index != std::numeric_limits<std::size_t>::max()) {
				auto & slot = Surfaces[Index];
				slot.Surface = std::make_unique<BSurface>(width, height, bpp, nullptr, density);
				slot.Bytes = bytes; slot.Busy = true; return;
			}
		}
	}
	Temporary = std::make_unique<BSurface>(width, height, bpp, nullptr, density);
}

RasterSurfaceLease::~RasterSurfaceLease()
{
	if (!Temporary) {
		auto & slot = Surfaces[Index];
		slot.Busy = false;
		std::size_t budget = std::min<std::size_t>(16u * 1024u * 1024u, Get_Render_Settings().CacheBudgetBytes / 16);
		if (Raster_Surface_Scratch_Bytes() > budget) { slot.Surface.reset(); slot.Bytes = 0; }
	}
}
BSurface & RasterSurfaceLease::Get() { return Temporary ? *Temporary : *Surfaces[Index].Surface; }
