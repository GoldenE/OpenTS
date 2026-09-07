/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#pragma once

class DirType;

int Render_Voxel_Facing_Count();
int Render_Voxel_Facing(DirType const & direction);
double Render_Voxel_Radians(DirType const & direction);
int Render_Voxel_Key(int prefix, int value, int count);

struct TerrainRasterSpan {
	int First;
	int Count;
	int Source;
};

TerrainRasterSpan Render_Terrain_Span(int row, int density);
