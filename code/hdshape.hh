/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once
#include "draw.h"

bool HD_Try_Draw_Shape(Surface & surface, ConvertClass & convert, ShapeSet const * shapefile, int shapenum, Point2D const & point, Rect const & window, ShapeFlags_Type flags, unsigned char const * remap, int height_offset, ZGradientType zgrad, int intensity, ShapeSet const * z_shapefile, int z_shapenum, Point2D z_off);
