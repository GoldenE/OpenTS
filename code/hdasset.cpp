/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "hdasset.hh"

#include <algorithm>
#include <bit>
#include <cstring>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <windows.h>
#include <bcrypt.h>

namespace HDAsset {
namespace {

bool Fail(std::string & error, char const * message)
{
	error = message;
	return false;
}

struct Reader {
	std::span<std::uint8_t const> Data;
	std::size_t Position = 0;
	bool Good = true;
	std::uint64_t Remaining = 0;
	bool Claim(std::uint64_t bytes)
	{
		if (bytes > Remaining) { Good = false; return false; }
		Remaining -= bytes;
		return true;
	}
	std::uint32_t U32()
	{
		if (Data.size() - Position < 4) { Good = false; return 0; }
		std::uint32_t value = 0;
		for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(Data[Position++]) << (i * 8);
		return value;
	}
	std::vector<std::uint8_t> Blob(std::uint64_t maximum)
	{
		auto size = U32();
		if (!Good || size > maximum || size > Data.size() - Position || !Claim(size)) { Good = false; return {}; }
		std::vector<std::uint8_t> result(Data.begin() + Position, Data.begin() + Position + size);
		Position += size;
		return result;
	}
};

void U32(std::vector<std::uint8_t> & data, std::uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}

void Blob(std::vector<std::uint8_t> & data, std::span<std::uint8_t const> value)
{
	U32(data, static_cast<std::uint32_t>(value.size()));
	data.insert(data.end(), value.begin(), value.end());
}

bool Same(Source const & a, Source const & b)
{
	return a.Name == b.Name && a.Identity == b.Identity && a.Lookup == b.Lookup && a.Generation == b.Generation && a.Content == b.Content;
}


bool Validate_Voxel(std::span<std::uint8_t const> bytes, std::string & error)
{
	if (bytes.size() < 32 || std::memcmp(bytes.data(), "Voxel Animation", 15) != 0) return Fail(error, "Invalid VXL header");
	auto u32 = [&](std::size_t offset) {
		return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24;
	};
	unsigned palettes = u32(16), layers = u32(20), infos = u32(24), data_size = u32(28);
	if (palettes != 1 || !layers || layers > 64 || infos < layers || infos > 64) return Fail(error, "Unsupported VXL palette or layer count");
	std::uint64_t headers = 32 + 770, body = headers + std::uint64_t(layers) * 28, tail = body + data_size;
	if (tail + std::uint64_t(infos) * 92 != bytes.size()) return Fail(error, "VXL body or tail size mismatch");
	for (unsigned i = 0; i < layers; ++i) if (u32(static_cast<std::size_t>(headers + i * 28 + 16)) >= infos) return Fail(error, "Invalid VXL layer info index");
	std::uint64_t volume = 0;
	for (unsigned i = 0; i < infos; ++i) {
		auto info = static_cast<std::size_t>(tail + i * 92);
		auto start = u32(info), end = u32(info + 4), data = u32(info + 8);
		unsigned x = bytes[info + 88], y = bytes[info + 89], z = bytes[info + 90], normals = bytes[info + 91];
		if (!x || !y || !z || normals > 4 || (volume += std::uint64_t(x) * y * z) > 16u * 1024u * 1024u) return Fail(error, "Invalid VXL dimensions or normal mode");
		for (unsigned field = 12; field < 88; field += 4) if (!std::isfinite(std::bit_cast<float>(u32(info + field)))) return Fail(error, "Nonfinite VXL transform");
		if (std::bit_cast<float>(u32(info + 12)) <= 0) return Fail(error, "Invalid VXL scale");
		for (unsigned axis = 0; axis < 3; ++axis) if (std::bit_cast<float>(u32(info + 64 + axis * 4)) >= std::bit_cast<float>(u32(info + 76 + axis * 4))) return Fail(error, "Invalid VXL bounds");
		std::uint64_t columns = x * y;
		if (std::uint64_t(start) + columns * 4 > data_size || std::uint64_t(end) + columns * 4 > data_size || data >= data_size) return Fail(error, "VXL span table outside body");
		for (unsigned c = 0; c < columns; ++c) {
			auto first = u32(static_cast<std::size_t>(body + start + c * 4)), last = u32(static_cast<std::size_t>(body + end + c * 4));
			if (first == 0xffffffff && last == 0xffffffff) continue;
			if (first > last || std::uint64_t(data) + last >= data_size) return Fail(error, "Invalid VXL column span");
			auto position = static_cast<std::size_t>(body + data + first);
			auto finish = static_cast<std::size_t>(body + data + last + 1);
			unsigned height = 0;
			while (height < z) {
				if (finish - position < 3) return Fail(error, "Truncated VXL run");
				unsigned skip = bytes[position++], count = bytes[position++];
				if (!skip && !count) return Fail(error, "VXL run makes no progress");
				if (skip + count > z - height || count * (normals ? 2u : 1u) + 1 > finish - position) return Fail(error, "VXL run exceeds column");
				for (unsigned n = 0; n < count; ++n) {
					++position;
					constexpr unsigned normal_counts[] = {0, 16, 36, 64, 244};
					if (normals && bytes[position++] >= normal_counts[normals]) return Fail(error, "VXL normal outside table");
				}
				if (bytes[position++] != count) return Fail(error, "VXL reverse run differs");
				height += skip + count;
			}
			if (position != finish) return Fail(error, "VXL end offset differs from forward stream");
		}
	}
	return true;
}

}

bool Normalize_Name(std::string & name)
{
	if (name.empty() || name.size() > 128) return false;
	for (char & c : name) {
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
	}
	return name.front() != '.' && name.find("..") == std::string::npos;
}

Digest Compute_Digest(std::span<std::uint8_t const> bytes)
{
	Digest result = {};
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return result;
	if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
		bool success = true;
		while (!bytes.empty()) {
			auto count = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), 1024 * 1024));
			if (BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), count, 0) < 0) { success = false; break; }
			bytes = bytes.subspan(count);
		}
		if (success && BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) < 0) result = {};
		BCryptDestroyHash(hash);
	}
	BCryptCloseAlgorithmProvider(algorithm, 0);
	return result;
}

std::uint64_t Memory_Bytes(Pack const & pack)
{
	std::uint64_t result = sizeof(Pack) + pack.Name.capacity() + pack.Variants.capacity() * sizeof(Variant);
	for (auto const & variant : pack.Variants) {
		result += variant.Frames.capacity() * sizeof(Frame) + variant.Sequences.capacity() * sizeof(Sequence) + variant.Voxel.capacity() + variant.Motion.capacity();
		for (auto const & frame : variant.Frames) result += frame.Color.capacity() + frame.Remap.capacity() + frame.Shadow.capacity() + frame.Depth.capacity() * 2;
	}
	return result;
}

bool Validate(Pack const & pack, std::string & error, Limits const & limits)
{
	if (limits.Dimension > 4096 || limits.Frames > 16384 || limits.Variants > 16 || limits.Bytes > 256u * 1024u * 1024u) return Fail(error, "Limits exceed format ceilings");
	std::string name = pack.Name;
	if (!Normalize_Name(name) || name != pack.Name) return Fail(error, "Invalid canonical asset name");
	if (pack.Type > Kind::MOVIE || pack.Variants.empty() || pack.Variants.size() > limits.Variants) return Fail(error, "Unsupported asset kind or variant count");
	if (pack.ClassicDigest == Digest{}) return Fail(error, "Missing classic SHA-256 digest");
	if (Memory_Bytes(pack) > limits.Bytes) return Fail(error, "Pack exceeds memory limit");
	std::set<std::uint32_t> scales;
	std::uint64_t total_frames = 0;
	for (auto const & v : pack.Variants) {
		if (v.Scale < 1 || v.Scale > 8 || !scales.insert(v.Scale).second) return Fail(error, "Invalid or duplicate scale");
		if (!v.LogicalWidth || !v.LogicalHeight || v.LogicalWidth > limits.Dimension / v.Scale || v.LogicalHeight > limits.Dimension / v.Scale) return Fail(error, "Invalid logical canvas");
		if (!v.LogicalFrames || v.LogicalFrames > limits.Frames || !v.Facings || v.Facings > 256) return Fail(error, "Invalid frame or facing count");
		if ((total_frames += v.Frames.size()) > limits.Frames || v.Sequences.size() > limits.Frames) return Fail(error, "Too many frames or sequences");
		std::vector<std::uint32_t> temporal(v.LogicalFrames, 1);
		std::vector<bool> reverse(v.LogicalFrames, false), assigned(v.LogicalFrames, false);
		for (auto const & s : v.Sequences) {
			if (!s.Count || s.First >= v.LogicalFrames || s.Count > v.LogicalFrames - s.First || !s.Temporal || s.Temporal > 32) return Fail(error, "Invalid sequence interval");
			for (auto i = s.First; i < s.First + s.Count; ++i) {
				if (assigned[i]) return Fail(error, "Overlapping sequences");
				assigned[i] = true; temporal[i] = s.Temporal; reverse[i] = s.Reverse;
			}
		}
		std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, int>> keys;
		for (auto const & f : v.Frames) {
			if (f.LogicalFrame >= v.LogicalFrames || f.Facing >= v.Facings || (f.Direction != 1 && f.Direction != -1) || f.SubFrame >= temporal[f.LogicalFrame]) return Fail(error, "Invalid frame identity");
			if (f.Direction == -1 && !reverse[f.LogicalFrame]) return Fail(error, "Unexpected reverse frame");
			if (!keys.emplace(f.LogicalFrame, f.SubFrame, f.Facing, f.Direction).second) return Fail(error, "Duplicate frame identity");
			if (!f.Width || !f.Height || f.Width > limits.Dimension || f.Height > limits.Dimension || std::int64_t(f.X) < -std::int64_t(limits.Dimension) || std::int64_t(f.X) > limits.Dimension || std::int64_t(f.Y) < -std::int64_t(limits.Dimension) || std::int64_t(f.Y) > limits.Dimension) return Fail(error, "Invalid frame rectangle");
			auto pixels = std::uint64_t(f.Width) * f.Height;
			if (f.Color.size() != pixels * 4 || (!f.Remap.empty() && f.Remap.size() != pixels) || (!f.Shadow.empty() && f.Shadow.size() != pixels) || (!f.Depth.empty() && f.Depth.size() != pixels)) return Fail(error, "Plane dimensions differ");
			if (std::any_of(f.Remap.begin(), f.Remap.end(), [](auto p) { return p > 16; })) return Fail(error, "Remap mask must be 0 or a house shade 1..16");
			if (pack.Type == Kind::TERRAIN && f.Depth.empty()) return Fail(error, "Terrain requires depth");
		}
		if (pack.Type != Kind::VOXEL) {
			if (!v.Voxel.empty() || !v.Motion.empty()) return Fail(error, "Voxel payload in a raster asset");
			if (v.Model != std::array<std::int32_t, 9>{}) return Fail(error, "Model transform in a raster asset");
			for (auto const & f : v.Frames) if (!keys.contains({f.LogicalFrame, 0, 0, 1})) return Fail(error, "Frame block lacks its forward origin");
			for (std::uint32_t i = 0; i < v.LogicalFrames; ++i) {
				bool present = keys.contains({i, 0, 0, 1});
				if (!present) continue;
				for (std::uint32_t f = 0; f < v.Facings; ++f) for (std::uint32_t t = 0; t < temporal[i]; ++t) {
					if (!keys.contains({i, t, f, 1}) || (reverse[i] && !keys.contains({i, t, f, -1}))) return Fail(error, "Incomplete temporal or facing block");
				}
			}
			if (v.Frames.empty()) return Fail(error, "Empty raster variant");
		} else {
			if (v.Voxel.empty() || !v.Frames.empty() || !v.Sequences.empty()) return Fail(error, "Invalid voxel payload");
			if (!v.Motion.empty()) return Fail(error, "HVA replacements are unsupported; classic motion remains authoritative");
			for (unsigned axis = 0; axis < 3; ++axis) if (v.Model[axis + 3] <= v.Model[axis]) return Fail(error, "Invalid voxel model bounds");
			if (!Validate_Voxel(v.Voxel, error)) return false;
		}
	}
	error.clear();
	return true;
}

bool Encode(Pack const & pack, std::vector<std::uint8_t> & bytes, std::string & error, Limits const & limits)
{
	if (!Validate(pack, error, limits)) return false;
	std::vector<std::uint8_t> out;
	U32(out, 0x5048444f); U32(out, 1); U32(out, static_cast<std::uint32_t>(pack.Type));
	Blob(out, {reinterpret_cast<std::uint8_t const *>(pack.Name.data()), pack.Name.size()});
	Blob(out, pack.ClassicDigest); U32(out, static_cast<std::uint32_t>(pack.Variants.size()));
	for (auto const & v : pack.Variants) {
		for (auto n : {v.Scale, v.LogicalWidth, v.LogicalHeight, v.LogicalFrames, v.Facings}) U32(out, n);
		for (auto n : v.Model) U32(out, std::bit_cast<std::uint32_t>(n));
		Blob(out, v.Voxel); Blob(out, v.Motion);
		U32(out, static_cast<std::uint32_t>(v.Sequences.size()));
		for (auto const & s : v.Sequences) for (auto n : {s.First, s.Count, s.Temporal, std::uint32_t(s.Reverse)}) U32(out, n);
		U32(out, static_cast<std::uint32_t>(v.Frames.size()));
		for (auto const & f : v.Frames) {
			for (auto n : {f.LogicalFrame, f.SubFrame, f.Facing, std::bit_cast<std::uint32_t>(f.Direction), std::bit_cast<std::uint32_t>(f.X), std::bit_cast<std::uint32_t>(f.Y), f.Width, f.Height}) U32(out, n);
			Blob(out, f.Color); Blob(out, f.Remap); Blob(out, f.Shadow);
			U32(out, static_cast<std::uint32_t>(f.Depth.size() * 2));
			for (auto d : f.Depth) { auto u = std::bit_cast<std::uint16_t>(d); out.push_back(static_cast<std::uint8_t>(u)); out.push_back(static_cast<std::uint8_t>(u >> 8)); }
		}
	}
	if (out.size() > limits.Bytes) return Fail(error, "Encoded pack exceeds limit");
	bytes = std::move(out);
	return true;
}

bool Decode(std::span<std::uint8_t const> bytes, Pack & pack, std::string & error, Limits const & limits) try
{
	if (limits.Dimension > 4096 || limits.Frames > 16384 || limits.Variants > 16 || limits.Bytes > 256u * 1024u * 1024u) return Fail(error, "Limits exceed format ceilings");
	if (bytes.size() > limits.Bytes) return Fail(error, "Input exceeds limit");
	Reader r{bytes, 0, true, limits.Bytes};
	if (!r.Claim(sizeof(Pack))) return Fail(error, "Insufficient decode budget");
	if (r.U32() != 0x5048444f || r.U32() != 1) return Fail(error, "Unsupported HD pack signature/version");
	Pack result;
	result.Type = static_cast<Kind>(r.U32());
	auto name = r.Blob(128); result.Name.assign(name.begin(), name.end());
	auto digest = r.Blob(32);
	if (digest.size() != 32) return Fail(error, "Invalid digest");
	std::copy(digest.begin(), digest.end(), result.ClassicDigest.begin());
	auto variants = r.U32();
	if (!r.Good || variants > limits.Variants) return Fail(error, "Invalid variant count");
	if (!r.Claim(std::uint64_t(variants) * sizeof(Variant))) return Fail(error, "Decoded metadata exceeds limit");
	result.Variants.reserve(variants);
	std::uint64_t total_frames = 0;
	for (std::uint32_t i = 0; i < variants && r.Good; ++i) {
		Variant v;
		v.Scale = r.U32(); v.LogicalWidth = r.U32(); v.LogicalHeight = r.U32(); v.LogicalFrames = r.U32(); v.Facings = r.U32();
		for (auto & n : v.Model) n = std::bit_cast<std::int32_t>(r.U32());
		v.Voxel = r.Blob(limits.Bytes); v.Motion = r.Blob(limits.Bytes);
		auto sequences = r.U32();
		if (!r.Good || sequences > limits.Frames) return Fail(error, "Invalid sequence count");
		if (!r.Claim(std::uint64_t(sequences) * sizeof(Sequence))) return Fail(error, "Decoded metadata exceeds limit");
		v.Sequences.reserve(sequences);
		for (std::uint32_t j = 0; j < sequences && r.Good; ++j) {
			Sequence s; s.First = r.U32(); s.Count = r.U32(); s.Temporal = r.U32(); auto reverse = r.U32();
			if (reverse > 1) return Fail(error, "Invalid reverse flag");
			s.Reverse = reverse != 0; v.Sequences.push_back(s);
		}
		auto frames = r.U32();
		if (!r.Good || (total_frames += frames) > limits.Frames) return Fail(error, "Invalid frame count");
		if (!r.Claim(std::uint64_t(frames) * sizeof(Frame))) return Fail(error, "Decoded metadata exceeds limit");
		v.Frames.reserve(frames);
		for (std::uint32_t j = 0; j < frames && r.Good; ++j) {
			Frame f;
			f.LogicalFrame = r.U32(); f.SubFrame = r.U32(); f.Facing = r.U32(); f.Direction = std::bit_cast<std::int32_t>(r.U32());
			f.X = std::bit_cast<std::int32_t>(r.U32()); f.Y = std::bit_cast<std::int32_t>(r.U32()); f.Width = r.U32(); f.Height = r.U32();
			if (!f.Width || !f.Height || f.Width > limits.Dimension || f.Height > limits.Dimension) return Fail(error, "Frame dimensions exceed limit");
			auto pixels = std::uint64_t(f.Width) * f.Height;
			f.Color = r.Blob(pixels * 4); f.Remap = r.Blob(pixels); f.Shadow = r.Blob(pixels);
			auto depth = r.Blob(pixels * 2);
			if (depth.size() % 2) return Fail(error, "Odd depth plane length");
			if (!r.Claim(depth.size())) return Fail(error, "Decoded depth exceeds limit");
			f.Depth.reserve(depth.size() / 2);
			for (std::size_t d = 0; d < depth.size(); d += 2) f.Depth.push_back(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(depth[d] | unsigned(depth[d + 1]) << 8)));
			v.Frames.push_back(std::move(f));
		}
		result.Variants.push_back(std::move(v));
	}
	if (!r.Good || r.Position != bytes.size()) return Fail(error, "Truncated pack or trailing data");
	if (!Validate(result, error, limits)) return false;
	pack = std::move(result);
	return true;
}
catch (std::bad_alloc const &)
{
	return Fail(error, "Insufficient memory to decode pack");
}

Variant const * Select_Variant(Pack const & pack, std::uint32_t scale)
{
	Variant const * higher = nullptr, * lower = nullptr;
	for (auto const & v : pack.Variants) {
		if (v.Scale == scale) return &v;
		if (v.Scale > scale && (!higher || v.Scale < higher->Scale)) higher = &v;
		if (v.Scale < scale && (!lower || v.Scale > lower->Scale)) lower = &v;
	}
	return higher ? higher : lower;
}

Frame const * Select_Frame(Variant const & variant, std::uint32_t logical, std::uint32_t subframe, std::uint32_t facing, int direction)
{
	for (auto const & f : variant.Frames) if (f.LogicalFrame == logical && f.SubFrame == subframe && f.Facing == facing && f.Direction == direction) return &f;
	return nullptr;
}

Frame const * Select_Animated_Frame(Variant const & variant, std::uint32_t logical, int direction, std::uint32_t fraction65536, std::uint32_t rawFacing256)
{
	std::uint32_t temporal = 1;
	for (auto const & s : variant.Sequences) if (logical >= s.First && logical - s.First < s.Count) temporal = s.Temporal;
	auto subframe = direction == 0 ? 0u : static_cast<std::uint32_t>(std::min<std::uint64_t>(temporal - 1, std::uint64_t(std::min(fraction65536, 65536u)) * temporal / 65536));
	auto facing = ((rawFacing256 & 255u) * variant.Facings + 128u) / 256u % variant.Facings;
	return Select_Frame(variant, logical, subframe, facing, direction < 0 ? -1 : 1);
}

Cache::Cache(std::uint64_t budget) : Budget(budget) {}

std::shared_ptr<Pack const> Cache::Find(Source const & source)
{
	for (auto & e : Entries) if (e.Valid && Same(e.Origin, source)) { e.Stamp = ++Clock; return e.Asset; }
	return {};
}

bool Cache::Insert(Source const & source, Pack pack, std::string & error)
{
	if (pack.Name != source.Name || pack.ClassicDigest != source.Content) return Fail(error, "Pack does not match winning classic source");
	if (!Validate(pack, error)) return false;
	auto bytes = Memory_Bytes(pack);
	if (bytes > Budget) return Fail(error, "Asset exceeds cache budget");
	if (Find(source)) return true;
	while (Used > Budget - bytes) {
		auto victim = Entries.end();
		for (auto i = Entries.begin(); i != Entries.end(); ++i) if (i->Asset.use_count() == 1 && (victim == Entries.end() || i->Stamp < victim->Stamp)) victim = i;
		if (victim == Entries.end()) return Fail(error, "Cache budget pinned by active draws");
		Used -= victim->Bytes; Entries.erase(victim);
	}
	Entries.push_back({source, std::make_shared<Pack const>(std::move(pack)), bytes, ++Clock, true}); Used += bytes;
	return true;
}

void Cache::Invalidate(std::string const & identity)
{
	for (auto & e : Entries) if (e.Origin.Identity == identity) e.Valid = false;
}

void Cache::Clear()
{
	for (auto & e : Entries) e.Valid = false;
	for (auto i = Entries.begin(); i != Entries.end();) {
		if (i->Asset.use_count() == 1) { Used -= i->Bytes; i = Entries.erase(i); } else ++i;
	}
}

std::uint64_t Cache::Bytes() const { return Used; }


std::uint64_t Cache::Available_Bytes() const
{
	std::uint64_t pinned = 0;
	for (auto const & entry : Entries) if (entry.Asset.use_count() > 1) pinned += entry.Bytes;
	return Budget - std::min(Budget, pinned);
}


void Cache::Set_Budget(std::uint64_t budget)
{
	Clear();
	Budget = budget;
}

}
