/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#include "parity.h"
#include "renderportable.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct Vector3i16 { short I, J, K; };
struct VoxelFuncArgumentStruct {
	unsigned char * StartOffset;
	unsigned char * EndOffset;
	unsigned char * DataOffset;
	int StartIndex, StrideX, StrideY;
	Vector3i16 TransformMatrix[4];
	unsigned char XSize, YSize, ZSize;
};
#if OPENTS_ASM_REFERENCE
static_assert(sizeof(VoxelFuncArgumentStruct) == 52);
#endif
constexpr int VOXEL_BITMAP_WIDTH = 256;
extern "C" {
	short VoxelPixelDeltaTable[256][2];
	unsigned char VoxelNormalTranslateTable[256];
	unsigned char VoxelDrawBuffer[65537];
	unsigned char VoxelPaletteTranslateTable[32][256];
	unsigned char PaletteInterpolationTable[65536];
	void * InterpolationPalette;
#if OPENTS_ASM_REFERENCE
	extern char UseMMX, UseCMOV;
#define DECLARE_FORMAT(format) \
	void Adjust_Color_##format(void *, void *, int, int, int, int, char *); \
	void Brighten_Color_##format(unsigned char *, unsigned short *, int, int, int, int); \
	void MMX_Brighten_Color_##format(unsigned char *, unsigned short *, int, int, int, int, int *);
	DECLARE_FORMAT(565)
	DECLARE_FORMAT(555)
	DECLARE_FORMAT(556)
	DECLARE_FORMAT(655)
#undef DECLARE_FORMAT
	void Asm_Interpolate(unsigned char *, unsigned char *, int, int, int);
	void Asm_Interpolate_Line_Double(unsigned char *, unsigned char *, int, int, int);
	void Asm_Interpolate_Line_Interpolate(unsigned char *, unsigned char *, int, int, int);
	void Draw_Voxel_Regular_Normals_ASM(VoxelFuncArgumentStruct *);
	void Draw_Voxel_Reverse_Normals_ASM(VoxelFuncArgumentStruct *);
	void Draw_Voxel_Regular_Lighting_Normals_ASM(VoxelFuncArgumentStruct *);
	void Draw_Voxel_Reverse_Lighting_Normals_ASM(VoxelFuncArgumentStruct *);
	void Draw_Voxel_Regular_ASM(VoxelFuncArgumentStruct *);
	void Draw_Voxel_Reverse_ASM(VoxelFuncArgumentStruct *);
#endif
}
#include "voxel_functions.inc"

namespace {
template<class T> std::span<unsigned char const> Bytes(T const & value)
{
	return {reinterpret_cast<unsigned char const *>(value.data()), value.size() * sizeof(value[0])};
}
void Append(std::vector<unsigned char> & destination, std::span<unsigned char const> bytes)
{
	destination.insert(destination.end(), bytes.begin(), bytes.end());
}
void Number(std::vector<unsigned char> & destination, unsigned value)
{
	for (int shift = 0; shift < 32; shift += 8) destination.push_back(static_cast<unsigned char>(value >> shift));
}
}

void Run_Suite(asmparity::Context & context)
{
	using namespace PortableRender;
	constexpr ColorFormat formats[] = {ColorFormat::RGB565, ColorFormat::RGB555, ColorFormat::RGB556, ColorFormat::RGB655};
#if OPENTS_ASM_REFERENCE
	auto adjust = std::array{Adjust_Color_565, Adjust_Color_555, Adjust_Color_556, Adjust_Color_655};
	auto brighten = std::array{Brighten_Color_565, Brighten_Color_555, Brighten_Color_556, Brighten_Color_655};
	auto mmx_brighten = std::array{MMX_Brighten_Color_565, MMX_Brighten_Color_555, MMX_Brighten_Color_556, MMX_Brighten_Color_655};
#endif
	for (unsigned seed = 1; seed <= 24; ++seed) {
		asmparity::Random random(seed);
		std::array<unsigned char, 772> palette;
		std::array<unsigned char, 256> mask;
		random.Fill(palette); random.Fill(mask);
		for (int i = 0; i < 256; i += 3) mask[i] = 0;
		std::array<int, 4> factors;
		for (auto & factor : factors) factor = static_cast<int>(random.Next() % 1048576);
		if (seed <= 6) factors.fill(std::array{0, 65535, 65536, 65537, 524287, -1}[seed - 1]);
		for (int mode = 0; mode < 3; ++mode) for (int format = 0; format < 4; ++format) {
			std::array<unsigned short, 260> actual, reference;
			actual.fill(0xa55a); reference = actual;
			Adjust_Color(formats[format], palette.data(), actual.data(), factors[0], factors[1], factors[2], factors[3], reinterpret_cast<char *>(mask.data()), static_cast<PaletteArithmetic>(mode));
			std::vector<unsigned char> input;
			Number(input, format); Number(input, mode); Append(input, Bytes(palette)); Append(input, Bytes(mask)); Append(input, Bytes(factors));
#if OPENTS_ASM_REFERENCE
			UseMMX = mode == 2; UseCMOV = mode == 1;
			adjust[format](palette.data(), reference.data(), factors[0], factors[1], factors[2], factors[3], reinterpret_cast<char *>(mask.data()));
#endif
			context.Check("adjust/" + std::to_string(format) + "/" + std::to_string(mode), seed, input, Bytes(actual), context.Has_Reference() ? Bytes(reference) : std::span<unsigned char const>{});
		}
	}
	for (int format = 0; format < 4; ++format) {
		std::uint64_t mmx_difference_count = 0;
		for (unsigned amount : {0u, 1u, 63u, 127u, 128u, 254u, 255u}) {
			std::vector<unsigned short> actual(65536 + 8), reference;
			for (unsigned color = 0; color < actual.size(); ++color) actual[color] = static_cast<unsigned short>(color);
			reference = actual;
			std::vector<unsigned char> multiplier(65536 + 8, static_cast<unsigned char>(amount));
			std::vector<unsigned char> input;
			Number(input, format); Number(input, amount); Append(input, Bytes(actual)); Append(input, multiplier);
			Brighten_Color(formats[format], multiplier.data(), actual.data(), 256, 512, 256, 256);
#if OPENTS_ASM_REFERENCE
			brighten[format](multiplier.data(), reference.data(), 256, 512, 256, 256);
#endif
			auto mmx = reference;
			std::vector<int> lookup(65536 + 1);
			for (unsigned color = 0; color < 65536; ++color) {
				int rbits = format == 3 ? 6 : 5, gbits = format == 0 ? 6 : 5, bbits = format == 2 ? 6 : 5;
				int red = ((color >> (gbits + bbits)) & ((1 << rbits) - 1)) << (8 - rbits);
				int green = ((color >> bbits) & ((1 << gbits) - 1)) << (8 - gbits);
				int blue = (color & ((1 << bbits) - 1)) << (8 - bbits);
				lookup[color] = (red << 16) | (green << 8) | blue;
				mmx[color] = static_cast<unsigned short>(color);
			}
			auto lookup_actual = mmx;
			Brighten_Color_Lookup(formats[format], multiplier.data(), lookup_actual.data(), 256, 512, 256, 256, lookup.data());
#if OPENTS_ASM_REFERENCE
			mmx_brighten[format](multiplier.data(), mmx.data(), 256, 512, 256, 256, lookup.data());
			for (int color = 0; color < 65536; ++color) mmx_difference_count += reference[color] != mmx[color];
#endif
			context.Check("brighten/" + std::to_string(format), amount, input, Bytes(actual), context.Has_Reference() ? Bytes(reference) : std::span<unsigned char const>{});
			Append(input, Bytes(lookup));
			context.Check("brighten-lookup/" + std::to_string(format), amount, input, Bytes(lookup_actual), context.Has_Reference() ? Bytes(mmx) : std::span<unsigned char const>{});
		}
#if OPENTS_ASM_REFERENCE
		std::cout << "MMX versus scalar format " << format << ": " << mmx_difference_count << " differing pixels / 458752\n";
#endif
	}
	for (unsigned seed = 1; seed <= 12; ++seed) {
		asmparity::Random random(seed + 100);
		random.Fill(PaletteInterpolationTable);
		int width = (seed + 1) * 2, lines = seed + 1, pitch = width * 2 + 8;
		std::vector<unsigned char> source(width * (lines + 1) + 4);
		random.Fill(source);
		for (int mode = 0; mode < 3; ++mode) {
			std::vector<unsigned char> actual(pitch * (lines * 2 + 1), 0xa5), reference = actual;
			int stride = mode == 0 ? pitch : pitch * 2;
			auto portable = std::array{Interpolate, Interpolate_Line_Double, Interpolate_Line_Interpolate};
			portable[mode](source.data(), actual.data(), lines, width, stride, PaletteInterpolationTable);
			std::vector<unsigned char> input;
			Number(input, mode); Number(input, width); Number(input, lines); Number(input, stride); Append(input, source); Append(input, PaletteInterpolationTable);
#if OPENTS_ASM_REFERENCE
			auto oracle = std::array{Asm_Interpolate, Asm_Interpolate_Line_Double, Asm_Interpolate_Line_Interpolate};
			oracle[mode](source.data(), reference.data(), lines, width, stride);
#endif
			context.Check("interpolate/" + std::to_string(mode), seed, input, actual, context.Has_Reference() ? std::span<unsigned char const>(reference) : std::span<unsigned char const>{});
		}
	}
	for (unsigned seed = 1; seed <= 32; ++seed) for (int drawer = 0; drawer < 6; ++drawer) {
		asmparity::Random random(seed + 200);
		for (auto & normal : VoxelNormalTranslateTable) normal = static_cast<unsigned char>(random.Next() % 32);
		for (auto & row : VoxelPaletteTranslateTable) random.Fill(row);
		std::array<unsigned int, 12> starts, ends;
		std::vector<unsigned char> data;
		for (int column = 0; column < 12; ++column) {
			starts[column] = static_cast<unsigned>(data.size());
			for (int run = 0; run < 2; ++run) {
				data.push_back(1); data.push_back(3);
				for (int voxel = 0; voxel < 3; ++voxel) {
					data.push_back(static_cast<unsigned char>(random.Next()));
					if (drawer < 4) data.push_back(static_cast<unsigned char>(random.Next()));
				}
				data.push_back(3);
			}
			ends[column] = static_cast<unsigned>(data.size() - 1);
			if (column % 5 == 4) starts[column] = ends[column] = UINT_MAX;
		}
		VoxelFuncArgumentStruct state{};
		state.StartOffset = reinterpret_cast<unsigned char *>(starts.data()); state.EndOffset = reinterpret_cast<unsigned char *>(ends.data()); state.DataOffset = data.data();
		state.XSize = 4; state.YSize = 3; state.ZSize = 8;
		state.StartIndex = seed % 2 ? 0 : 11; state.StrideX = seed % 2 ? 1 : -1; state.StrideY = seed % 2 ? 4 : -4;
		for (auto & vector : state.TransformMatrix) vector = {static_cast<short>(random.Next()), static_cast<short>(random.Next()), static_cast<short>(random.Next())};
		std::vector<unsigned char> input;
		Number(input, drawer); Number(input, state.StartIndex); Number(input, state.StrideX); Number(input, state.StrideY);
		for (auto vector : state.TransformMatrix) { Number(input, vector.I); Number(input, vector.J); Number(input, vector.K); }
		Append(input, Bytes(starts)); Append(input, Bytes(ends)); Append(input, data); Append(input, VoxelNormalTranslateTable); Append(input, {&VoxelPaletteTranslateTable[0][0], 8192});
		std::array<unsigned char, 65537> initial, actual, reference;
		random.Fill(initial); Append(input, initial);
		std::memcpy(VoxelDrawBuffer, initial.data(), initial.size());
		auto portable = std::array{Draw_Voxel_Regular_Normals, Draw_Voxel_Reverse_Normals, Draw_Voxel_Regular_Normals_Lighting, Draw_Voxel_Reverse_Normals_Lighting, Draw_Voxel_Regular, Draw_Voxel_Reverse};
		auto portable_state = state;
		portable[drawer](&portable_state); std::memcpy(actual.data(), VoxelDrawBuffer, actual.size());
		std::vector<unsigned char> output(actual.begin(), actual.end()), expected;
		Number(output, portable_state.StartIndex);
		for (auto vector : portable_state.TransformMatrix) { Number(output, vector.I); Number(output, vector.J); Number(output, vector.K); }
#if OPENTS_ASM_REFERENCE
		std::memcpy(VoxelDrawBuffer, initial.data(), initial.size());
		auto oracle = std::array{Draw_Voxel_Regular_Normals_ASM, Draw_Voxel_Reverse_Normals_ASM, Draw_Voxel_Regular_Lighting_Normals_ASM, Draw_Voxel_Reverse_Lighting_Normals_ASM, Draw_Voxel_Regular_ASM, Draw_Voxel_Reverse_ASM};
		oracle[drawer](&state); std::memcpy(reference.data(), VoxelDrawBuffer, reference.size());
		expected.assign(reference.begin(), reference.end()); Number(expected, state.StartIndex);
		for (auto vector : state.TransformMatrix) { Number(expected, vector.I); Number(expected, vector.J); Number(expected, vector.K); }
#endif
		context.Check("voxel/" + std::to_string(drawer), seed, input, output, expected);
	}
}
