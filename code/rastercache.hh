/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once

#include "hdasset.hh"
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

struct RasterAssetKey {
	std::uint64_t Generation = 0;
	HDAsset::Digest Digest{};
	int Frame = 0;
	int Width = 0;
	int Height = 0;
	int SourceDensity = 1;
	int Density = 1;
	int BytesPerPixel = 1;
	bool RLE = false;
	bool operator==(RasterAssetKey const &) const = default;
};

struct RasterAssetKeyHash {
	std::size_t operator()(RasterAssetKey const & key) const;
};

struct CachedRasterFrame {
	int Width = 0;
	int SourceRows = 0;
	int BytesPerPixel = 1;
	std::vector<std::uint8_t> Pixels;
	std::vector<std::uint8_t> Encoded;
	std::vector<std::size_t> RowOffsets;
	std::size_t Bytes() const;
};

class RasterAssetCache {
public:
	explicit RasterAssetCache(std::size_t budget = 0);
	void Configure(std::size_t budget, std::uint64_t epoch);
	std::shared_ptr<CachedRasterFrame const> Fetch(RasterAssetKey const & key, std::span<std::uint8_t const> data);
	std::size_t Bytes() const;
	std::size_t Entries() const;
	std::uint64_t Hits() const;
	std::uint64_t Builds() const;
private:
	struct Entry {
		std::shared_ptr<CachedRasterFrame const> Frame;
		std::size_t Bytes;
		std::uint64_t Epoch;
		std::uint64_t Touch;
	};
	bool Make_Room(std::size_t bytes);
	std::size_t Budget = 0;
	std::size_t Resident = 0;
	std::uint64_t Epoch = 0;
	std::uint64_t Clock = 0;
	std::uint64_t HitCount = 0;
	std::uint64_t BuildCount = 0;
	std::unordered_map<RasterAssetKey, Entry, RasterAssetKeyHash> Frames;
};

class ScopedRasterAsset {
public:
	ScopedRasterAsset(HDAsset::Source const * source, int frame, void const * data, std::size_t bytes, std::uint64_t epoch);
	~ScopedRasterAsset();
	ScopedRasterAsset(ScopedRasterAsset const &) = delete;
	ScopedRasterAsset & operator=(ScopedRasterAsset const &) = delete;
	std::shared_ptr<CachedRasterFrame const> Fetch(void const * data, int width, int height, int source_density, int density, int bpp, bool rle) const;
private:
	ScopedRasterAsset const * Previous;
	std::optional<RasterAssetKey> Key;
	void const * Data;
	std::size_t Size;
};

std::shared_ptr<CachedRasterFrame const> Fetch_Raster_Asset(void const * data, int width, int height, int source_density, int density, int bpp, bool rle);
std::size_t Raster_Asset_Cache_Bytes();
std::uint64_t Raster_Asset_Cache_Hits();
std::uint64_t Raster_Asset_Cache_Builds();
std::size_t Raster_Asset_Cache_Entries();
std::size_t Raster_Surface_Scratch_Bytes();

struct RasterWorkBuffers {
	std::vector<std::uint8_t const *> Rows;
	std::vector<std::uint8_t> Decoded;
	std::vector<std::uint8_t> Scan;
	std::vector<std::uint8_t> Depth;
};

class RasterWorkLease {
public:
	RasterWorkLease();
	~RasterWorkLease();
	RasterWorkBuffers & Buffers();
	RasterWorkLease(RasterWorkLease const &) = delete;
	RasterWorkLease & operator=(RasterWorkLease const &) = delete;
private:
	std::size_t Index;
	std::unique_ptr<RasterWorkBuffers> Temporary;
};

class BSurface;
class RasterSurfaceLease {
public:
	RasterSurfaceLease(int width, int height, int bpp, int density);
	~RasterSurfaceLease();
	BSurface & Get();
	RasterSurfaceLease(RasterSurfaceLease const &) = delete;
	RasterSurfaceLease & operator=(RasterSurfaceLease const &) = delete;
private:
	std::size_t Index;
	std::unique_ptr<BSurface> Temporary;
};
