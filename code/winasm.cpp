/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/
#include "always.h"
#include "renderportable.h"

extern "C" {
extern char UseMMX, UseCMOV;
extern unsigned char PaletteInterpolationTable[256][256];

#define COLOR_FUNCTIONS(format) \
void __cdecl Adjust_Color_##format(void * source, void * destination, int red, int green, int blue, int intensity, char * mask) \
{ \
	PortableRender::Adjust_Color(PortableRender::ColorFormat::RGB##format, source, destination, red, green, blue, intensity, mask, UseMMX ? PortableRender::PaletteArithmetic::MMX : UseCMOV ? PortableRender::PaletteArithmetic::CMOV : PortableRender::PaletteArithmetic::Scalar); \
} \
void __cdecl Brighten_Color_##format(unsigned char * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height) \
{ \
	PortableRender::Brighten_Color(PortableRender::ColorFormat::RGB##format, multiplier, colors, multiplier_stride, color_stride, width, height); \
} \
void __cdecl MMX_Brighten_Color_##format(unsigned char * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height, int * lookup) \
{ \
	PortableRender::Brighten_Color_Lookup(PortableRender::ColorFormat::RGB##format, multiplier, colors, multiplier_stride, color_stride, width, height, lookup); \
}

COLOR_FUNCTIONS(565)
COLOR_FUNCTIONS(555)
COLOR_FUNCTIONS(556)
COLOR_FUNCTIONS(655)
#undef COLOR_FUNCTIONS

void __cdecl Asm_Interpolate(unsigned char * source, unsigned char * destination, int lines, int width, int stride)
{
	PortableRender::Interpolate(source, destination, lines, width, stride, &PaletteInterpolationTable[0][0]);
}

void __cdecl Asm_Interpolate_Line_Double(unsigned char * source, unsigned char * destination, int lines, int width, int stride)
{
	PortableRender::Interpolate_Line_Double(source, destination, lines, width, stride, &PaletteInterpolationTable[0][0]);
}

void __cdecl Asm_Interpolate_Line_Interpolate(unsigned char * source, unsigned char * destination, int lines, int width, int stride)
{
	PortableRender::Interpolate_Line_Interpolate(source, destination, lines, width, stride, &PaletteInterpolationTable[0][0]);
}
}
