/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rastercache.hh"
#include <algorithm>
#include <cstring>
#include <limits>

std::size_t RasterAssetKeyHash::operator()(RasterAssetKey const & key) const
{
	std::size_t hash = static_cast<std::size_t>(key.Generation ^ (key.Generation >> 32));
	auto add = [&](std::size_t value) { hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2); };
	for (auto byte : key.Digest) add(byte);
	add(key.Frame); add(key.Width); add(key.Height); add(key.SourceDensity); add(key.Density); add(key.BytesPerPixel); add(key.RLE);
	return hash;
}

std::size_t CachedRasterFrame::Bytes() const
{
	return Pixels.capacity() + Encoded.capacity() + RowOffsets.capacity() * sizeof(std::size_t) + sizeof(CachedRasterFrame);
}

RasterAssetCache::RasterAssetCache(std::size_t budget) : Budget(budget) {}

void RasterAssetCache::Configure(std::size_t budget, std::uint64_t epoch)
{
	if (Budget == budget && Epoch == epoch) return;
	Budget = budget;
	Epoch = epoch;
	for (auto it = Frames.begin(); it != Frames.end();) {
		if (it->second.Epoch != epoch && it->second.Frame.use_count() == 1) { Resident -= it->second.Bytes; it = Frames.erase(it); }
		else ++it;
	}
	Make_Room(0);
}

bool RasterAssetCache::Make_Room(std::size_t bytes)
{
	if (bytes > Budget) return false;
	while (Resident > Budget - bytes || (bytes && Frames.size() >= 4096)) {
		auto oldest = Frames.end();
		for (auto it = Frames.begin(); it != Frames.end(); ++it) {
			if (it->second.Frame.use_count() == 1 && (oldest == Frames.end() || it->second.Touch < oldest->second.Touch)) oldest = it;
		}
		if (oldest == Frames.end()) return false;
		Resident -= oldest->second.Bytes;
		Frames.erase(oldest);
	}
	return true;
}

std::shared_ptr<CachedRasterFrame const> RasterAssetCache::Fetch(RasterAssetKey const & key, std::span<std::uint8_t const> data)
try
{
	auto found = Frames.find(key);
	if (found != Frames.end()) {
		if (found->second.Epoch == Epoch) { ++HitCount; found->second.Touch = ++Clock; return found->second.Frame; }
		if (found->second.Frame.use_count() != 1) return {};
		Resident -= found->second.Bytes; Frames.erase(found);
	}
	if (!key.Generation || key.Frame < 0 || key.Width <= 0 || key.Height <= 0 || key.Width > 4096 || key.Height > 4096 || key.Density < 1 || key.Density > 4 || key.SourceDensity < 1 || key.SourceDensity > 4 || key.BytesPerPixel < 1 || key.BytesPerPixel > 2 || (key.RLE && key.BytesPerPixel != 1)) return {};
	std::size_t source_width = std::size_t(key.Width) * key.SourceDensity;
	std::size_t source_rows = std::size_t(key.Height) * key.SourceDensity;
	std::size_t width = std::size_t(key.Width) * key.Density;
	std::size_t pixel_bytes = width * source_rows * key.BytesPerPixel;
	std::size_t encoded_bytes = key.RLE ? width * source_rows * 2 : 0;
	constexpr std::size_t overhead = sizeof(Entry) + sizeof(RasterAssetKey) * 2 + sizeof(CachedRasterFrame) + 128;
	std::size_t estimate = pixel_bytes + encoded_bytes + source_rows * sizeof(std::size_t) + overhead;
	if (!Make_Room(estimate)) return {};
	auto frame = std::make_shared<CachedRasterFrame>();
	frame->Width = static_cast<int>(width); frame->SourceRows = static_cast<int>(source_rows); frame->BytesPerPixel = key.BytesPerPixel;
	frame->Pixels.resize(pixel_bytes);
	if (key.RLE) { frame->Encoded.reserve(encoded_bytes); frame->RowOffsets.reserve(source_rows); }
	std::vector<std::uint8_t> decoded(source_width * key.BytesPerPixel);
	std::size_t offset = 0;
	for (std::size_t y = 0; y < source_rows; ++y) {
		std::uint8_t const * row;
		if (key.RLE) {
			if (offset > data.size() || data.size() - offset < 2) return {};
			std::size_t length = data[offset] | (std::size_t(data[offset + 1]) << 8);
			if (length < 2 || length > data.size() - offset) return {};
			std::fill(decoded.begin(), decoded.end(), 0);
			std::size_t x = 0, at = offset + 2, end = offset + length;
			while (x < source_width) {
				if (at >= end) return {};
				auto value = data[at++];
				if (value) decoded[x++] = value;
				else { if (at >= end || !data[at] || data[at] > source_width - x) return {}; x += data[at++]; }
			}
			offset = end; row = decoded.data();
			frame->RowOffsets.push_back(frame->Encoded.size());
		} else {
			std::size_t row_bytes = source_width * key.BytesPerPixel;
			if (offset > data.size() || row_bytes > data.size() - offset) return {};
			row = data.data() + offset; offset += row_bytes;
		}
		auto output = frame->Pixels.data() + y * width * key.BytesPerPixel;
		for (std::size_t x = 0; x < width; ++x) {
			std::size_t sx = x * key.SourceDensity / key.Density;
			memcpy(output + x * key.BytesPerPixel, row + sx * key.BytesPerPixel, key.BytesPerPixel);
			if (key.RLE) { frame->Encoded.push_back(output[x]); if (!output[x]) frame->Encoded.push_back(1); }
		}
	}
	std::size_t bytes = frame->Bytes() + overhead;
	if (!Make_Room(bytes)) return {};
	Frames.emplace(key, Entry{frame, bytes, Epoch, ++Clock});
	Resident += bytes; ++BuildCount;
	return frame;
}
catch (std::bad_alloc const &)
{
	return {};
}

std::size_t RasterAssetCache::Bytes() const { return Resident; }
std::size_t RasterAssetCache::Entries() const { return Frames.size(); }
std::uint64_t RasterAssetCache::Hits() const { return HitCount; }
std::uint64_t RasterAssetCache::Builds() const { return BuildCount; }
