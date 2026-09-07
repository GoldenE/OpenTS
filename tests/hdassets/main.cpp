/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "hdasset.hh"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>
#include <bit>
#include <stdexcept>

using namespace HDAsset;

void Check(bool value, char const * name)
{
	if (!value) throw std::runtime_error(name);
}

Pack Synthetic()
{
	Pack p; p.Name = "SYNTH.SHP";
	std::array<std::uint8_t, 3> input = {'a', 'b', 'c'};
	p.ClassicDigest = Compute_Digest(input);
	Variant v; v.Scale = 2; v.LogicalWidth = 2; v.LogicalHeight = 2; v.LogicalFrames = 2;
	Frame f; f.Width = 4; f.Height = 4; f.Color.resize(64); f.Remap.resize(16); f.Shadow.resize(16); f.Depth.resize(16);
	for (unsigned i = 0; i < 16; ++i) { f.Color[i * 4] = static_cast<std::uint8_t>(i * 17); f.Color[i * 4 + 3] = 255; f.Remap[i] = static_cast<std::uint8_t>(i); f.Depth[i] = static_cast<std::int16_t>(int(i) - 8); }
	v.Frames.push_back(f); p.Variants.push_back(v);
	return p;
}


std::vector<std::uint8_t> Synthetic_Voxel()
{
	std::vector<std::uint8_t> data(32 + 770 + 28 + 13 + 92);
	auto word = [&](unsigned offset, std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) data[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); };
	std::string name = "Voxel Animation"; std::copy(name.begin(), name.end(), data.begin());
	word(16, 1); word(20, 1); word(24, 1); word(28, 13);
	unsigned body = 32 + 770 + 28;
	word(body, 0); word(body + 4, 4);
	data[body + 8] = 0; data[body + 9] = 1; data[body + 10] = 5; data[body + 11] = 0; data[body + 12] = 1;
	unsigned info = body + 13;
	word(info, 0); word(info + 4, 4); word(info + 8, 8); word(info + 12, std::bit_cast<std::uint32_t>(1.0f));
	for (unsigned axis = 0; axis < 3; ++axis) word(info + 76 + axis * 4, std::bit_cast<std::uint32_t>(1.0f));
	data[info + 88] = data[info + 89] = data[info + 90] = data[info + 91] = 1;
	return data;
}

int main(int argc, char ** argv)
{
	try {
		if (argc == 3 && std::string(argv[1]) == "--validate") {
			std::ifstream file(argv[2], std::ios::binary | std::ios::ate);
			Check(bool(file) && file.tellg() > 0 && file.tellg() <= 256 * 1024 * 1024, "Invalid pack input size");
			std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
			file.seekg(0); file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
			Pack decoded; std::string error;
			Check(Decode(bytes, decoded, error), error.c_str());
			std::vector<std::uint8_t> canonical;
			Check(Encode(decoded, canonical, error) && canonical == bytes, "Production canonical encoding differs");
			std::cout << decoded.Name << ": production decoder accepted " << bytes.size() << " bytes\n";
			return 0;
		}
		auto p = Synthetic(); std::string error; std::vector<std::uint8_t> bytes;
		Check(p.ClassicDigest[0] == 0xba && p.ClassicDigest[31] == 0xad, "SHA256 abc vector");
		Check(Encode(p, bytes, error), error.c_str());
		Pack decoded;
		Check(Decode(bytes, decoded, error), error.c_str());
		std::vector<std::uint8_t> again;
		Check(Encode(decoded, again, error) && bytes == again, "Canonical round trip");
		Check(decoded.Variants[0].Frames[0].Depth[0] == -8, "Signed depth endian");
		for (std::size_t n = 0; n < bytes.size(); ++n) Check(!Decode(std::span(bytes).first(n), decoded, error), "Every truncation rejected");
		again = bytes; again.push_back(0); Check(!Decode(again, decoded, error), "Trailing bytes rejected");
		again = bytes; again[4] = 2; Check(!Decode(again, decoded, error), "Future version rejected");
		for (auto name : {"../A.SHP", "C:A.SHP", "A/B.SHP", "A\\B.SHP", ".A", "A..SHP"}) { std::string s = name; Check(!Normalize_Name(s), "Unsafe name"); }
		auto bad = p; bad.Variants[0].Frames[0].Width = 0xffffffff; Check(!Validate(bad, error), "Oversized dimensions");
		bad = p; bad.Variants[0].Frames[0].Remap[0] = 17; Check(!Validate(bad, error), "Semantic remap range");
		bad = p; bad.Variants[0].Frames[0].Color.pop_back(); Check(!Validate(bad, error), "Mismatched color plane");
		bad = p; bad.Variants.push_back(bad.Variants[0]); Check(!Validate(bad, error), "Duplicate scale");
		bad = p; bad.Variants[0].Sequences.push_back({0, 1, 3, true}); Check(!Validate(bad, error), "Missing reverse block");
		bad = p; bad.Type = Kind::TERRAIN; bad.Variants[0].Frames[0].Depth.clear(); Check(!Validate(bad, error), "Terrain depth required");
		bad = p; bad.Variants[0].Facings = 2; bad.Variants[0].Frames[0].Facing = 1; Check(!Validate(bad, error), "Orphan facing block rejected");
		bad = p; bad.Type = Kind::VOXEL; bad.Variants[0].Frames.clear(); bad.Variants[0].Voxel = Synthetic_Voxel();
		bad.Variants[0].Model = {0, 0, 0, 65536, 65536, 65536, 0, 0, 0};
		Check(Validate(bad, error), error.c_str());
		auto voxel = bad;
		for (std::size_t n = 0; n < voxel.Variants[0].Voxel.size(); ++n) { bad = voxel; bad.Variants[0].Voxel.resize(n); Check(!Validate(bad, error), "Every VXL truncation rejected"); }
		bad = voxel; bad.Variants[0].Voxel[32 + 770 + 28 + 11] = 16; Check(!Validate(bad, error), "VXL normal out of range rejected");
		bad = voxel; bad.Variants[0].Voxel[32 + 770 + 28 + 9] = 2; Check(!Validate(bad, error), "VXL run exceeding column rejected");
		bad = voxel; bad.Variants[0].Voxel[32 + 770 + 28 + 12] = 0; Check(!Validate(bad, error), "VXL reverse run mismatch rejected");
		bad = voxel; bad.Variants[0].Motion = {1}; Check(!Validate(bad, error), "Unconsumed HVA payload rejected");
		Source base{p.Name, "MIX:BASE.MIX", Policy::CACHED_MIX, 1, p.ClassicDigest};
		Cache cache(Memory_Bytes(p) + 16);
		Check(cache.Insert(base, p, error), error.c_str());
		auto pin = cache.Find(base); Check(bool(pin), "Cache hit");
		auto override_source = base; override_source.Identity = "MIX:MOD.MIX";
		Check(!cache.Find(override_source), "Higher-priority classic never inherits base HD");
		override_source = base; override_source.Lookup = Policy::LOOSE_FIRST;
		Check(!cache.Find(override_source), "Caller lookup policy retained");
		override_source = base; ++override_source.Generation;
		Check(!cache.Find(override_source), "Mount generation retained");
		Check(!cache.Insert(override_source, p, error), "Pinned cache refuses over-budget allocation");
		cache.Invalidate(base.Identity); Check(!cache.Find(base), "Invalidation hides old binding");
		Check(pin->Name == p.Name && cache.Bytes() > 0, "Invalidated pin still valid and budgeted");
		pin.reset(); cache.Clear(); Check(cache.Bytes() == 0, "Unpinned memory released");
		override_source.Content[0] ^= 1; Check(!cache.Insert(override_source, p, error), "Classic digest mismatch");
		auto & v = p.Variants[0]; v.Sequences = {{0, 1, 3, true}}; v.Frames.clear();
		for (int direction : {-1, 1}) for (unsigned sub = 0; sub < 3; ++sub) { auto f = Synthetic().Variants[0].Frames[0]; f.Direction = direction; f.SubFrame = sub; v.Frames.push_back(f); }
		Check(Validate(p, error), error.c_str());
		Check(Select_Animated_Frame(v, 0, -1, 65536, 0)->SubFrame == 2, "Reverse terminal does not predict next frame");
		Check(Select_Animated_Frame(v, 0, 0, 65536, 0)->SubFrame == 0, "Held stage has no interpolation");
		Check(Select_Animated_Frame(v, 0, 1, 32768, 0)->SubFrame == 1, "Mid-interval selection");
		Check(Select_Animated_Frame(v, 1, 1, 0, 0) == nullptr, "Missing classic frame falls back");
		auto more = v; more.Scale = 4; p.Variants.push_back(more);
		Check(Select_Variant(p, 2)->Scale == 2 && Select_Variant(p, 3)->Scale == 4 && Select_Variant(p, 8)->Scale == 4, "Exact then higher then lower scale");
		std::cout << "HD asset validation, endian round trip, SHA256, truncations, source identity, cache pinning and temporal selection passed\n";
		return 0;
	} catch (std::exception const & error) { std::cerr << error.what() << '\n'; return 1; }
}
