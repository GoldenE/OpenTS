/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

struct RGBStruct { unsigned char Red, Green, Blue; };
#include "tmp_layout.inc"

class Blitter {
public:
	virtual void BlitForward(void *, void const *, int, int = 0, void * = nullptr, void * = nullptr, int = 1000, int = 0) const = 0;
	virtual void BlitBackward(void *, void const *, int, int = 0, void * = nullptr, void * = nullptr, int = 1000) const = 0;
};
#include "blitter_templates.inc"

void Check(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

int main()
{
	try {
		static_assert(sizeof(IsoTileRecord) == 52);
		static_assert(sizeof(IsoTileSet) == 20);
		alignas(IsoTileSet) std::array<unsigned char, 256> bytes{};
		std::array<std::uint32_t, 7> header{3, 1, 60, 30, 32, 0, 96};
		std::memcpy(bytes.data(), header.data(), sizeof(header));
		auto * set = reinterpret_cast<IsoTileSet *>(bytes.data());
		auto const * const_set = set;
		Check(set->Fetch_Record_Pointer_Unsafe(0) == reinterpret_cast<IsoTileRecord *>(bytes.data() + 32), "TMP first offset");
		Check(const_set->Fetch_Record_Pointer_Unsafe(1) == nullptr, "TMP absent record");
		Check(const_set->Fetch_Record_Pointer(5) == reinterpret_cast<IsoTileRecord const *>(bytes.data() + 96), "TMP modulo record");
		Check(std::memcmp(bytes.data(), header.data(), sizeof(header)) == 0, "TMP offsets stay serialized");
		set->Fetch_Record_Pointer_Unsafe(2)->TileType = 9;
		Check(const_set->Fetch_Record_Pointer_Unsafe(2)->TileType == 9, "TMP mutable record view");

		std::array<unsigned char, 4> source{3, 0, 7, 5};
		std::array<unsigned char, 256> remap{};
		std::array<unsigned short, 256> palette{};
		for (unsigned index = 0; index < 256; ++index) { remap[index] = static_cast<unsigned char>(255 - index); palette[index] = static_cast<unsigned short>(index * 257); }
		std::array<unsigned char, 4> plain{9, 9, 9, 9};
		BlitTrans<unsigned char>{}.BlitForward(plain.data(), source.data(), 4);
		Check(plain == std::array<unsigned char, 4>{3, 9, 7, 5}, "transparent primary template");
		std::array<unsigned short, 4> translated{9, 9, 9, 9};
		BlitTransXlat<unsigned short>{palette.data()}.BlitForward(translated.data(), source.data(), 4);
		Check(translated == std::array<unsigned short, 4>{771, 9, 1799, 1285}, "translated primary template");
		BlitTransRemapXlat<unsigned short> remapper(remap.data(), palette.data());
		translated.fill(9);
		remapper.BlitForward(translated.data(), source.data(), 4);
		Check(translated == std::array<unsigned short, 4>{64764, 9, 63736, 9}, "legacy remap span excludes final pixel");
		auto previous = translated;
		remapper.BlitForward(translated.data(), source.data(), 0);
		remapper.BlitForward(translated.data(), source.data(), 1);
		Check(translated == previous, "empty remap spans");
		std::cout << "TMP disk layout and portable blitter contracts passed\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
