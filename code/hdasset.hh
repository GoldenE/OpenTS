/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace HDAsset {

enum class Kind : std::uint32_t { SHAPE, UI, TERRAIN, VOXEL, PALETTE, FONT, MOVIE };
enum class Policy : std::uint32_t { CACHED_MIX, LOOSE_FIRST };
using Digest = std::array<std::uint8_t, 32>;

struct Limits {
	std::uint32_t Dimension = 4096;
	std::uint32_t Frames = 16384;
	std::uint32_t Variants = 16;
	std::uint64_t Bytes = 256u * 1024u * 1024u;
};

struct Frame {
	std::uint32_t LogicalFrame = 0;
	std::uint32_t SubFrame = 0;
	std::uint32_t Facing = 0;
	std::int32_t Direction = 1;
	std::int32_t X = 0;
	std::int32_t Y = 0;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::vector<std::uint8_t> Color;
	std::vector<std::uint8_t> Remap;
	std::vector<std::uint8_t> Shadow;
	std::vector<std::int16_t> Depth;
};

struct Sequence {
	std::uint32_t First = 0;
	std::uint32_t Count = 0;
	std::uint32_t Temporal = 1;
	bool Reverse = false;
};

struct Variant {
	std::uint32_t Scale = 1;
	std::uint32_t LogicalWidth = 0;
	std::uint32_t LogicalHeight = 0;
	std::uint32_t LogicalFrames = 0;
	std::uint32_t Facings = 1;
	std::vector<Sequence> Sequences;
	std::vector<Frame> Frames;
	// Voxel bounds and pivots use signed 16.16 model units; HVA remains authoritative.
	std::array<std::int32_t, 9> Model = {};
	std::vector<std::uint8_t> Voxel;
	std::vector<std::uint8_t> Motion;
};

struct Pack {
	std::string Name;
	Kind Type = Kind::SHAPE;
	Digest ClassicDigest = {};
	std::vector<Variant> Variants;
};

struct Source {
	std::string Name;
	std::string Identity;
	Policy Lookup = Policy::CACHED_MIX;
	std::uint64_t Generation = 0;
	Digest Content = {};
};

bool Normalize_Name(std::string & name);
Digest Compute_Digest(std::span<std::uint8_t const> bytes);
bool Validate(Pack const & pack, std::string & error, Limits const & limits = {});
bool Decode(std::span<std::uint8_t const> bytes, Pack & pack, std::string & error, Limits const & limits = {});
bool Encode(Pack const & pack, std::vector<std::uint8_t> & bytes, std::string & error, Limits const & limits = {});
Variant const * Select_Variant(Pack const & pack, std::uint32_t scale);
Frame const * Select_Frame(Variant const & variant, std::uint32_t logical, std::uint32_t subframe = 0, std::uint32_t facing = 0, int direction = 1);
Frame const * Select_Animated_Frame(Variant const & variant, std::uint32_t logical, int direction, std::uint32_t fraction65536, std::uint32_t rawFacing256);
std::uint64_t Memory_Bytes(Pack const & pack);

class Cache {
public:
	explicit Cache(std::uint64_t budget = 256u * 1024u * 1024u);
	std::shared_ptr<Pack const> Find(Source const & source);
	bool Insert(Source const & source, Pack pack, std::string & error);
	void Invalidate(std::string const & identity);
	void Clear();
	void Set_Budget(std::uint64_t budget);
	std::uint64_t Bytes() const;
	std::uint64_t Available_Bytes() const;
private:
	struct Entry {
		Source Origin;
		std::shared_ptr<Pack const> Asset;
		std::uint64_t Bytes;
		std::uint64_t Stamp;
		bool Valid;
	};
	std::vector<Entry> Entries;
	std::uint64_t Budget;
	std::uint64_t Used = 0;
	std::uint64_t Clock = 0;
};

}
