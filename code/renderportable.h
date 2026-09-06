/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/
#pragma once

namespace PortableRender {

enum class ColorFormat { RGB565, RGB555, RGB556, RGB655 };
enum class PaletteArithmetic { Scalar, CMOV, MMX };

void Adjust_Color(ColorFormat format, void const * source, void * destination, int red, int green, int blue, int intensity, char const * mask, PaletteArithmetic arithmetic);
void Brighten_Color(ColorFormat format, unsigned char const * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height);
void Brighten_Color_Lookup(ColorFormat format, unsigned char const * multiplier, unsigned short * colors, int multiplier_stride, int color_stride, int width, int height, int const * lookup);
void Interpolate(unsigned char const * source, unsigned char * destination, int lines, int width, int destination_stride, unsigned char const * table);
void Interpolate_Line_Double(unsigned char const * source, unsigned char * destination, int lines, int width, int doubled_destination_stride, unsigned char const * table);
// The inherited vertical interpolator consumes lines + 1 padded source rows.
void Interpolate_Line_Interpolate(unsigned char const * source, unsigned char * destination, int lines, int width, int doubled_destination_stride, unsigned char const * table);

}
