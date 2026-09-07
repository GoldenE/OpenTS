/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"
#include "rastercache.hh"
#include "bsurface.h"
#include "dsurface.h"
#include "ownrdraw.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

unsigned short ODRComponentMask = 0xf800;
unsigned short ODGComponentMask = 0x07e0;
unsigned short ODBComponentMask = 0x001f;

// These production painters do not access the cache dictionary.
class SurfaceCacheClass {
public:
	bool Draw(Rect const &, Surface &, Surface &, int, int);
	bool DrawTrans(Rect const &, Surface &, Surface &, short);
	bool DrawMasked(Rect const &, Surface &, Surface &, Surface &, void *, bool, int, int);
};

#include "render_ui_functions.inc"

namespace {
void Check_UI(bool condition, char const * description)
{
	if (!condition) throw std::runtime_error(description);
}
}

void Run_Render_UI_Tests()
{
	SurfaceCacheClass cache;
	for (int density = 1; density <= 4; ++density) {
		BSurface target(16, 12, 2, nullptr, density, RenderDomain::UI);
		BSurface source(2, 2, 2), mask(2, 2, 1);
		source.Fill(0xffff);
		source.Put_Pixel(Point2D(0, 0), 0xf800);
		source.Put_Pixel(Point2D(1, 1), 0);
		mask.Fill(128);
		mask.Put_Pixel(Point2D(1, 1), 0);
		RasterSurfaceView raster(target);
		target.Fill(0x07e0);
		Check_UI(cache.Draw(Rect(3, 2, 6, 4), target, source, 0, 0), "UI tiled background failed");
		for (int y = 0; y < 12 * density; ++y) for (int x = 0; x < 16 * density; ++x) {
			unsigned short expected = 0x07e0;
			if (x >= 3 * density && x < 9 * density && y >= 2 * density && y < 6 * density) {
				expected = static_cast<unsigned short>(source.Get_Pixel(Point2D((x / density - 3) % 2, (y / density - 2) % 2)));
			}
			Check_UI(raster.Get_Pixel(Point2D(x, y)) == expected, "Tiled cache wrote wrong pixel or escaped rectangle");
		}
		target.Fill(0x07e0);
		Check_UI(cache.DrawTrans(Rect(3, 2, 99, 99), target, source, 0), "UI keyed copy failed");
		Check_UI(raster.Get_Pixel(Point2D(3 * density, 2 * density)) == 0xf800, "UI keyed copy missed logical anchor");
		Check_UI(raster.Get_Pixel(Point2D(5 * density - 1, 4 * density - 1)) == 0x07e0, "UI keyed copy overwrote transparent samples");
		Check_UI(raster.Get_Pixel(Point2D(5 * density, 2 * density)) == 0x07e0, "UI keyed copy used rectangle extent instead of source size");

		for (int y = 0; y < 12 * density; ++y) for (int x = 0; x < 16 * density; ++x) raster.Put_Pixel(Point2D(x, y), (x + y) % 2 ? 0x07e0 : 0x001f);
		Check_UI(cache.DrawMasked(Rect(3, 2, 2, 2), target, source, mask, nullptr, false, 0, 0), "UI masked blend failed");
		for (int y = 2 * density; y < 4 * density; ++y) for (int x = 3 * density; x < 5 * density; ++x) {
			unsigned short underlying = (x + y) % 2 ? 0x07e0 : 0x001f;
			int sx = x / density - 3, sy = y / density - 2;
			unsigned short expected = sx == 1 && sy == 1 ? underlying : OD_Blend_Color(underlying, source.Get_Pixel(Point2D(sx, sy)), 128);
			Check_UI(raster.Get_Pixel(Point2D(x, y)) == expected, "UI alpha blend destroyed underlying subpixel detail");
		}
		source.Put_Pixel(Point2D(0, 0), 0x07e0);
		mask.Fill(255);
		target.Fill(0x001f);
		Check_UI(cache.DrawMasked(Rect(3, 2, 2, 2), target, source, mask, nullptr, false, 0, 0), "Mutable UI blend failed");
		Check_UI(raster.Get_Pixel(Point2D(3 * density, 2 * density)) == OD_Blend_Color(0x001f, 0x07e0, 255), "Reused UI scratch retained stale source pixels");
		mask.Fill(0);
		target.Fill(0xf800);
		cache.DrawMasked(Rect(3, 2, 2, 2), target, source, mask, nullptr, false, 0, 0);
		Check_UI(raster.Get_Pixel(Point2D(3 * density, 2 * density)) == 0xf800, "Reused UI scratch retained stale mask pixels");
		target.Fill(0xffff);
		ODFillRectTrans(Rect(3, 2, 4, 4), target, 0, 128);
		Check_UI(raster.Get_Pixel(Point2D(3 * density, 2 * density)) == OD_Blend_Color(0xffff, 0, 128), "Translucent fill starts at physical anchor");
		Check_UI(raster.Get_Pixel(Point2D(7 * density - 1, 6 * density - 1)) == OD_Blend_Color(0xffff, 0, 128), "Translucent fill covers final physical sample");
		Check_UI(raster.Get_Pixel(Point2D(7 * density, 6 * density)) == 0xffff, "Translucent fill escaped bounds");
		target.Fill(0);
		ODDrawGradientRect(Rect(3, 2, 4, 4), target, 0xffff, 65536);
		Check_UI(raster.Get_Pixel(Point2D(5 * density, 4 * density)) == 0xffff, "Gradient failed to reach logical interior");
		Check_UI(raster.Get_Pixel(Point2D(7 * density, 4 * density)) == 0, "Gradient escaped rectangle");
		target.Fill(0xffff);
		ODDrawBevelDarken(Rect(3, 2, 4, 4), target, 1, 1);
		Check_UI(raster.Get_Pixel(Point2D(4 * density - 1, 5 * density)) != 0xffff && raster.Get_Pixel(Point2D(4 * density, 5 * density)) == 0xffff, "Bevel width does not follow raster density");
		target.Fill(0);
		Check_UI(ODDrawEdgeGlow(target, Point2D(2, 2), Point2D(8, 2), 0xffff, 128), "UI edge glow failed");
		Check_UI(raster.Get_Pixel(Point2D(4 * density, 3 * density - 1)) != 0 && raster.Get_Pixel(Point2D(4 * density, 3 * density)) == 0, "UI edge thickness does not follow density");
		Check_UI(raster.Get_Pixel(Point2D(9 * density - 1, 3 * density - 1)) != 0 && raster.Get_Pixel(Point2D(9 * density, 2 * density)) == 0, "UI inclusive edge lost its final physical endpoint");
		target.Fill(0);
		Check_UI(ODDrawEdgeGlow(target, Point2D(2, 8), Point2D(2, 2), 0xffff, 128), "Reverse vertical edge glow failed");
		Check_UI(raster.Get_Pixel(Point2D(3 * density - 1, 9 * density - 1)) != 0 && raster.Get_Pixel(Point2D(2 * density, 9 * density)) == 0, "Reverse vertical edge lost its inclusive endpoint");
	}
}
