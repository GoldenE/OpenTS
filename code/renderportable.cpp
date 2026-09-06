/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#ifndef OPENTS_ASM_PARITY_STANDALONE
#include "always.h"
#endif
#include "renderportable.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace PortableRender {
namespace {

struct Format {
	int RedMask, GreenMask, BlueMask;
	int RedShift, GreenShift, BlueShift;
};

Format Get_Format(ColorFormat format)
{
	switch (format) {
		case ColorFormat::RGB555: return {248, 248, 248, 7, 2, 3};
		case ColorFormat::RGB556: return {248, 248, 252, 8, 3, 2};
		case ColorFormat::RGB655: return {252, 248, 248, 8, 2, 3};
		default: return {248, 252, 248, 8, 3, 3};
	}
}

unsigned short Pack(Format format, unsigned red, unsigned green, unsigned blue)
{
	return static_cast<unsigned short>(((red & format.RedMask) << format.RedShift) | ((green & format.GreenMask) << format.GreenShift) | ((blue & format.BlueMask) >> format.BlueShift));
}

unsigned Scale(unsigned color, int factor, PaletteArithmetic arithmetic)
{
	if (arithmetic == PaletteArithmetic::MMX) {
		int multiplier = std::clamp(factor >> 4, -32768, 32767);
		return static_cast<unsigned>(std::clamp((static_cast<int>(color * 16) * multiplier) >> 16, 0, 255));
	}
	std::uint32_t product = color * static_cast<std::uint32_t>(factor);
	return std::min(product >> 16, 255u);
}

void Interpolate_Row(unsigned char const * source, unsigned char * destination, int width, unsigned char const * table)
{
	for (int x = 0; x < width; ++x) {
		destination[x * 2] = source[x];
		destination[x * 2 + 1] = x + 1 < width ? table[source[x] | (source[x + 1] << 8)] : 0;
	}
}

}

void Adjust_Color(ColorFormat format, void const * source, void * destination, int red, int green, int blue, int intensity, char const * mask, PaletteArithmetic arithmetic)
{
	auto const * colors = static_cast<unsigned char const *>(source);
	auto * output = static_cast<unsigned short *>(destination);
	Format const pixel_format = Get_Format(format);
	output[0] = 0;
	for (int index = 1; index < 256; ++index) {
		output[index] = Pack(pixel_format, Scale(colors[index * 3], mask[index] ? red : intensity, arithmetic), Scale(colors[index * 3 + 1], mask[index] ? green : intensity, arithmetic), Scale(colors[index * 3 + 2], mask[index] ? blue : intensity, arithmetic));
	}
}

void Brighten_Color(ColorFormat format, unsigned char const * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height)
{
	Format const pixel_format = Get_Format(format);
	for (int y = 0; y < height; ++y) {
		auto * row = reinterpret_cast<unsigned short *>(reinterpret_cast<unsigned char *>(colors) + static_cast<std::ptrdiff_t>(y) * color_stride);
		for (int x = 0; x < width; ++x) {
			unsigned amount = multiplier[static_cast<std::ptrdiff_t>(y) * multiplier_stride + x];
			if (amount == 0) continue;
			unsigned pixel = row[x];
			unsigned red = (pixel >> pixel_format.RedShift) & pixel_format.RedMask;
			unsigned green = (pixel >> pixel_format.GreenShift) & pixel_format.GreenMask;
			unsigned blue = (pixel << pixel_format.BlueShift) & 255;
			auto brighten = [amount](unsigned value) { return std::min(value + ((value * amount) >> 8), 255u); };
			// The scalar 655 routine extracts red at bit 8, but packs it at bit 10.
			if (format == ColorFormat::RGB655) {
				row[x] = static_cast<unsigned short>(((brighten(red) >> 2) << 10) | ((brighten(green) >> 3) << 5) | (brighten(blue) >> 3));
			} else {
				row[x] = Pack(pixel_format, brighten(red), brighten(green), brighten(blue));
			}
		}
	}
}

void Interpolate(unsigned char const * source, unsigned char * destination, int lines, int width, int destination_stride, unsigned char const * table)
{
	for (int y = 0; y < lines; ++y) Interpolate_Row(source + static_cast<std::ptrdiff_t>(y) * width, destination + static_cast<std::ptrdiff_t>(y) * destination_stride, width, table);
}

void Brighten_Color_Lookup(ColorFormat format, unsigned char const * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height, int const * lookup)
{
	for (int y = 0; y < height; ++y) {
		auto * row = reinterpret_cast<unsigned short *>(reinterpret_cast<unsigned char *>(colors) + static_cast<std::ptrdiff_t>(y) * color_stride);
		for (int x = 0; x < width; ++x) {
			unsigned amount = multiplier[static_cast<std::ptrdiff_t>(y) * multiplier_stride + x];
			if (amount == 0) continue;
			unsigned pixel = static_cast<unsigned>(lookup[row[x]]);
			int shift = format == ColorFormat::RGB555 ? 3 : 2;
			auto brighten = [amount, shift](unsigned value) { return std::min(value + ((value * amount) >> 8), 255u) >> shift; };
			unsigned blue = brighten(pixel & 255);
			unsigned green = brighten((pixel >> 8) & 255);
			unsigned red = brighten((pixel >> 16) & 255);
			if (format == ColorFormat::RGB565 || format == ColorFormat::RGB655) blue >>= 1;
			green <<= format == ColorFormat::RGB556 ? 6 : format == ColorFormat::RGB655 ? 4 : 5;
			red <<= 10;
			// The inherited 655 lookup path masks green with this literal and leaves red unmasked.
			if (format == ColorFormat::RGB655) green &= 0x423a0a60;
			else red &= format == ColorFormat::RGB555 ? 0x7c00 : 0xf800;
			row[x] = static_cast<unsigned short>(blue | green | red);
		}
	}
}

void Interpolate_Line_Double(unsigned char const * source, unsigned char * destination, int lines, int width, int doubled_destination_stride, unsigned char const * table)
{
	int stride = doubled_destination_stride / 2;
	for (int y = 0; y < lines; ++y) {
		auto * row = destination + static_cast<std::ptrdiff_t>(y) * doubled_destination_stride;
		Interpolate_Row(source + static_cast<std::ptrdiff_t>(y) * width, row, width, table);
		std::memcpy(row + stride, row, width * 2);
	}
}

void Interpolate_Line_Interpolate(unsigned char const * source, unsigned char * destination, int lines, int width, int doubled_destination_stride, unsigned char const * table)
{
	if (lines < 2 || width <= 0) return;
	int stride = doubled_destination_stride / 2;
	std::vector<unsigned char> previous(width * 2), next(width * 2);
	Interpolate_Row(source, previous.data(), width, table);
	for (int y = 1; y < lines; ++y) {
		Interpolate_Row(source + static_cast<std::ptrdiff_t>(y) * width, next.data(), width, table);
		std::memcpy(destination, previous.data(), width * 2);
		for (int x = 0; x < width * 2; ++x) destination[stride + x] = table[previous[x] | (next[x] << 8)];
		destination += doubled_destination_stride;
		previous.swap(next);
	}
	Interpolate_Row(source + static_cast<std::ptrdiff_t>(lines) * width, destination, width, table);
}

}
