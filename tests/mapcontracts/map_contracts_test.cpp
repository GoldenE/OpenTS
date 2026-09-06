/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "coord.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>

static_assert(!std::is_constructible_v<Cell, int>);
static_assert(sizeof(Cell) == 4);
static_assert(MAP_CELL_W == 512 && MAP_CELL_H == 512);

enum IsometricTileType { ISOTILE_NONE = -1 };
struct IsometricTileTypeClass {
	static IsometricTileType Fixup_Tile_Type(IsometricTileType type) { return type; }
};

struct CellClass {
	IsometricTileType ITType = ISOTILE_NONE;
	unsigned char SubTile = 0;
	unsigned char Height = 0;
	bool IsVisible = false;
	bool IsMapped = false;
	bool IsToShroud = false;
};

struct CellArray : std::vector<CellClass *> {
	int Length() const { return static_cast<int>(size()); }
	CellClass * & operator[](std::size_t index) { return at(index); }
};

// These seams supply cells, decoded bytes and redraw callbacks; the algorithms below are extracted from production source.
struct Straw {
	std::vector<unsigned char> Bytes;
	std::size_t Offset = 0;
	int Get(void * target, int size) {
		if (Offset + size > Bytes.size()) throw "Read past the legacy payload";
		std::memcpy(target, Bytes.data() + Offset, size);
		Offset += size;
		return size;
	}
};

struct LCWStraw {
	enum { DECOMPRESS };
	explicit LCWStraw(int) {}
	Straw * Source = nullptr;
	void Get_From(Straw * source) { Source = source; }
	int Get(void * target, int size) { return Source->Get(target, size); }
};

struct Rect {
	int X, Y, Width, Height;
	Rect(int x = 0, int y = 0, int width = 0, int height = 0) : X(x), Y(y), Width(width), Height(height) {}
};

struct DSurface {
	Rect Bounds;
	int Blits = 0;
	explicit DSurface(int width, int height) : Bounds(0, 0, width, height) {}
	Rect Get_Rect() const { return Bounds; }
	int Get_Width() const { return Bounds.Width; }
	int Get_Height() const { return Bounds.Height; }
	int Bytes_Per_Pixel() const { return 2; }
	void Fill(int) {}
	void * Lock() { return nullptr; }
	void Unlock() {}
	void Put_Pixel(Point2D, int) {}
	static int Build_Hicolor_Pixel(int, int, int) { return 0; }
	void Blit_From(Rect, DSurface &, Rect, bool, bool) { Blits++; }
};

struct CCINIClass {
	Rect Size;
	int Entries = 0;
	Rect Get_Rect(char const *, char const *, Rect) const { return Size; }
	int Entry_Count(char const *) const { return Entries; }
	int Get_UUBlock(char const *, unsigned char *, int) const { return 0; }
};

struct BufferStraw : Straw { BufferStraw(unsigned char *, int) {} };
using LZOStraw = LCWStraw;

using HWND = void *;
struct RECT { int left, top, right, bottom; };
enum { IDC_PREVIEW_FRAME };
void ValidateRect(HWND, void *) {}
HWND GetDlgItem(HWND window, int) { return window; }
void Get_Display_Rect(HWND, RECT * rect) { *rect = {0, 0, 200, 100}; }
DSurface PreviewDestination(200, 100);
DSurface * AlternateSurface = &PreviewDestination;

struct MapPreviewClass {
	DSurface * SurfacePtr = nullptr;
	void Blit_Preview(HWND window);
	bool Read_INI(CCINIClass const & ini);
};

struct MapClass {
	struct { int Width = 0; int Height = 0; } PlayRect;
	Rect LocalRect;
	unsigned IterX = 0, IterY = 0, IterColumn = 0;
	CellClass ** IterCell = nullptr;
	CellArray Array;
	CellClass BlubCell;
	int TotalValue = 0;
	CellClass & operator[](Cell cell) {
		int index = cell.X + cell.Y * MAP_CELL_W;
		return index >= 0 && index < Array.Length() && Array[index] ? *Array[index] : BlubCell;
	}
	bool In_Radar(Cell const & cell) const;
	void Init_Cells(void);
	bool Read_Binary_1(Straw & straw);
	CellClass * Iterate(void);
	void Reset_Iterator(void);
};

MapClass Map;

enum { GS_REDRAW_TACTICAL };
struct DisplayClass : MapClass {
	std::vector<int> Shrouded;
	int LookCalls = 0;
	int RedrawCalls = 0;
	void Shroud_Cell(Cell cell) {
		Shrouded.push_back(cell.X + cell.Y * MAP_CELL_W);
		(*this)[cell].IsMapped = false;
	}
	void All_To_Look() { LookCalls++; }
	void Flag_To_Redraw(int) { RedrawCalls++; }
	void Encroach_Shadow(void);
};

#include "map_contract_functions.inc"

int main()
{
	int failures = 0;
	auto check = [&failures](bool pass, char const * name) {
		std::printf("%s: %s\n", pass ? "PASS" : "FAIL", name);
		failures += !pass;
	};

	DisplayClass map;
	map.Array.resize(MAP_CELL_TOTAL);
	std::vector<CellClass> cells(MAP_CELL_TOTAL);
	for (int i = 0; i < MAP_CELL_TOTAL; i++) map.Array[i] = &cells[i];
	bool same = true;
	int scenarios = 0;
	std::array<std::array<int, 2>, 12> const shapes{{{1, 1}, {1, 511}, {511, 1}, {125, 125}, {256, 256}, {400, 112}, {112, 400}, {298, 149}, {120, 150}, {125, 129}, {50, 50}, {2, 510}}};
	for (int scenario = 0; scenario < 24; scenario++) {
		auto dimensions = shapes[scenario % shapes.size()];
		map.PlayRect.Width = dimensions[0];
		map.PlayRect.Height = dimensions[1];
		std::vector<int> expected;
		for (int y = 0; y < MAP_CELL_H; y++) {
			for (int x = 0; x < MAP_CELL_W; x++) {
				int i = x + y * MAP_CELL_W;
				cells[i] = CellClass{};
				cells[i].IsVisible = i % 3 == 0;
				cells[i].IsMapped = i % 5 != 0;
				cells[i].IsToShroud = scenario >= 12 || i % 7 == 0;
				if (map.In_Radar(Cell(x, y)) && (cells[i].IsToShroud || (!cells[i].IsVisible && cells[i].IsMapped))) expected.push_back(i);
			}
		}
		map.Shrouded.clear();
		map.Encroach_Shadow();
		same &= map.Shrouded == expected;
		for (int i : expected) same &= !cells[i].IsMapped && !cells[i].IsToShroud;
		scenarios++;
	}
	check(same && map.LookCalls == scenarios && map.RedrawCalls == scenarios, "Production shadow scans preserve full-grid cell order, flags and final callbacks across 12 map shapes with mixed flags and every cell marked");
	bool iteration = true;
	for (auto dimensions : shapes) {
		if (dimensions[0] < 2) continue;
		map.PlayRect.Width = dimensions[0];
		map.PlayRect.Height = dimensions[1];
		std::vector<int> expected;
		for (int i = 0; i < MAP_CELL_TOTAL; i++) {
			bool inside = map.In_Radar(Cell(i % MAP_CELL_W, i / MAP_CELL_W));
			map.Array[i] = inside ? &cells[i] : nullptr;
			if (inside) expected.push_back(i);
		}
		std::sort(expected.begin(), expected.end(), [](int a, int b) {
			int ad = a % MAP_CELL_W + a / MAP_CELL_W;
			int bd = b % MAP_CELL_W + b / MAP_CELL_W;
			return ad != bd ? ad < bd : a % MAP_CELL_W < b % MAP_CELL_W;
		});
		map.Reset_Iterator();
		for (int index : expected) iteration &= map.Iterate() == &cells[index];
		iteration &= map.Iterate() == nullptr;
	}
	check(iteration, "Production iterator visits every diamond cell in diagonal order and terminates at the table ceiling");
	for (int i = 0; i < MAP_CELL_TOTAL; i++) map.Array[i] = &cells[i];

	map.PlayRect.Width = map.PlayRect.Height = 50;
	map.Array[0] = nullptr;
	for (auto & cell : cells) cell.Height = 91;
	map.TotalValue = 123;
	map.Init_Cells();
	check(map.TotalValue == 0 && cells[0].Height == 91 && cells[1].Height == 0 && cells.back().Height == 0,
		"Production initialization resets allocated cells beyond the diamond and preserves null slots");
	map.Array.resize(7);
	map.Array[6]->Height = 91;
	map.Init_Cells();
	check(map.Array[6]->Height == 0, "Initialization respects a shorter pointer table");
	map.Array.resize(MAP_CELL_TOTAL, nullptr);
	for (int i = 0; i < MAP_CELL_TOTAL; i++) map.Array[i] = &cells[i];

	Straw payload;
	payload.Bytes.resize(65536 + 1, 0);
	for (int i = 0; i < 16384; i++) {
		payload.Bytes[i * 2] = i & 255;
		payload.Bytes[i * 2 + 1] = (i >> 8) & 255;
		payload.Bytes[32768 + i] = i % 251;
		payload.Bytes[49152 + i] = i % 14;
	}
	payload.Bytes.back() = 0xA7;
	map.Read_Binary_1(payload);
	bool legacy = payload.Offset == 65536 && payload.Bytes[payload.Offset] == 0xA7;
	for (int i = 0; i < 16384; i++) {
		auto const & cell = map[Cell(i % 128, i / 128)];
		legacy &= (static_cast<int>(cell.ITType) & 0xffff) == i && cell.SubTile == i % 251 && cell.Height == i % 14;
	}
	check(legacy, "Production legacy reader consumes 65536 decoded bytes, preserves 128-stride coordinates and leaves trailing data unread");

	MapPreviewClass preview;
	preview.SurfacePtr = new DSurface(20, 10);
	CCINIClass missing;
	check(!preview.Read_INI(missing) && preview.SurfacePtr == nullptr, "Missing preview discards an old image and leaves no zero-size surface");
	CCINIClass no_pack;
	no_pack.Size = Rect(0, 0, 20, 10);
	check(!preview.Read_INI(no_pack) && preview.SurfacePtr == nullptr, "A size without a preview pack is not a recovered image");
	CCINIClass invalid;
	invalid.Size = Rect(0, 0, -1, 10);
	invalid.Entries = 1;
	check(!preview.Read_INI(invalid) && preview.SurfacePtr == nullptr, "Nonpositive preview dimensions are rejected before allocating");
	DSurface empty(0, 10);
	preview.SurfacePtr = &empty;
	preview.Blit_Preview(nullptr);
	check(PreviewDestination.Blits == 0, "Zero-width preview cannot divide by zero or blit");
	DSurface valid(20, 10);
	preview.SurfacePtr = &valid;
	preview.Blit_Preview(nullptr);
	check(PreviewDestination.Blits == 1, "Positive preview dimensions still blit");
	return failures == 0 ? 0 : 1;
}
