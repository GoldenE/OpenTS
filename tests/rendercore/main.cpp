/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"
#include "bsurface.h"
#include "dsurface.h"
#include "blit.h"
#include "blitblit.h"
#include "rlerle.h"
#include "abuffer.h"
#include "zbuffer.h"
#include "globals.h"
#include <iostream>
#include <stdexcept>

ABuffer * AlphaBuffer = nullptr;
int VideoModeWidth = 16, VideoModeHeight = 12;
void Video_Mark_Dirty() {}
void Run_Render_UI_Tests();
void Run_Render_Cache_Tests();
void Run_Render_Startup_Tests();
namespace HDAsset { void Forget(void const *) {} }
const double RAD_60 = 60 * 0.017453292519943295;
const double CELL_LEPTON_DIAG = std::sqrt(256.0 * 256.0 * 2);
const int ISO_TILE_PIXEL_W = 48;

static void Check(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

class DepthRecorder : public Blitter {
public:
	void BlitForward(void * dest, void const *, int length, int z, void *, void *, int, int) const override { std::fill_n(static_cast<unsigned short *>(dest), length, static_cast<unsigned short>(z)); }
	void BlitBackward(void *, void const *, int, int, void *, void *, int) const override { throw std::runtime_error("unexpected backward draw"); }
};

int main()
{
	try {
		Check(!Set_Render_Settings({RenderMode::HD, 9, 2, 2, 1048576}), "invalid scale accepted");
		Check(Set_Render_Settings({RenderMode::HD, 2, 2, 2, 1048576}), "HD settings rejected");
		Check(Render_Raster_Scale(RenderDomain::Source) == 1 && Render_Raster_Scale(RenderDomain::UI) == 2, "scale domains");
		Check(Checked_Raster_Size(3, 5, 2, 2) == 120, "physical allocation");
		bool rejected = false;
		try { Checked_Raster_Size(INT_MAX, 2, 2, 4); } catch (std::length_error const &) { rejected = true; }
		Check(rejected, "oversize allocation accepted");
		BSurface source(3, 2, 2), target(8, 6, 2, nullptr, 2, RenderDomain::World);
		source.Fill(0x1234); source.Put_Pixel(Point2D(1, 0), 0x5678); target.Fill(0);
		Check(target.Blit_From(Rect(2, 1, 3, 2), source, source.Get_Rect()), "fallback copy failed");
		RasterSurfaceView pixels(target);
		Check(pixels.Get_Pixel(Point2D(6, 2)) == 0x5678 && pixels.Get_Pixel(Point2D(7, 3)) == 0x5678, "fallback did not replicate physical samples");
		Check(pixels.Get_Pixel(Point2D(3, 2)) == 0 && pixels.Get_Pixel(Point2D(10, 2)) == 0, "fallback escaped bounds");
		BSurface copy(8, 6, 2, nullptr, 2);
		pixels.Put_Pixel(Point2D(7, 3), 0xabcd);
		copy.Blit_From(target);
		RasterSurfaceView copied(copy);
		Check(copied.Get_Pixel(Point2D(7, 3)) == 0xabcd && copied.Get_Pixel(Point2D(6, 3)) == 0x5678, "same-density copy lost fine detail");
		target.Fill_Rect(Rect(0, 0, 8, 6), Rect(0, 0, 1, 1), 0xffff);
		Check(pixels.Get_Pixel(Point2D(1, 1)) == 0xffff && pixels.Get_Pixel(Point2D(2, 1)) == 0, "fill did not scale rectangle");
		Point2D logical(10, 20);
		{
			ScopedWorldOrigin origin(logical, Point2D(21, 39), 2);
			Check(Render_Draw_Point(target, Point2D(4, 5)) == Point2D(9, 9), "subpixel residual missing");
			Check(Render_Rect_To_Raster(target, Rect(4, 5, 2, 3)).Top_Left() == Point2D(8, 10), "origin contaminated clip");
			{ ScopedScreenSpace ui; Check(Render_Draw_Point(target, Point2D(4, 5)) == Point2D(8, 10), "UI inherited origin"); }
			Check(Render_Draw_Point(target, Point2D(4, 5)) == Point2D(9, 9), "scope did not restore origin");
		}
		Check(Render_Origin_Residual(2) == Point2D(), "origin escaped scope");
		Check(Project_Render_Coord(Coord(8, 0, 0), 1).X == 0 && Project_Render_Coord(Coord(8, 0, 0), 2).X == 1, "projection divided before density");
		unsigned short palette[256]; for (int i = 0; i < 256; ++i) palette[i] = static_cast<unsigned short>(i * 17);
		unsigned char encoded[] = {6, 0, 1, 0, 1, 2, 6, 0, 0, 1, 3, 4};
		BSurface rle_source(3, 2, 1, encoded);
		target.Fill(0xaaaa);
		Check(RLE_Blit(target, Rect(1, 1, 3, 2), rle_source, rle_source.Get_Rect(), RLEBlitTransXlat<unsigned short>(palette)), "RLE fallback failed");
		Check(pixels.Get_Pixel(Point2D(2, 2)) == 17 && pixels.Get_Pixel(Point2D(3, 3)) == 17, "RLE physical replication");
		Check(pixels.Get_Pixel(Point2D(4, 2)) == 0xaaaa && pixels.Get_Pixel(Point2D(6, 4)) == 68, "RLE transparency or frame rows");
		unsigned char dense_encoded[] = {6, 0, 1, 2, 3, 4, 6, 0, 5, 6, 7, 8, 6, 0, 9, 10, 11, 12, 6, 0, 13, 14, 15, 16};
		BSurface dense_rle(2, 2, 1, dense_encoded, 2), preview(4, 4, 2);
		preview.Fill(0);
		RLE_Blit(preview, Rect(1, 1, 2, 2), dense_rle, dense_rle.Get_Rect(), RLEBlitTransXlat<unsigned short>(palette));
		Check(preview.Get_Pixel(Point2D(2, 2)) == 11 * 17, "physical RLE source did not downsample into preview domain");
		Check(RLE_Blit(target, Rect(1, 1, 3, 2), rle_source, rle_source.Get_Rect(), RLEBlitTransXlat<unsigned short>(palette), 0, ZGRAD_NONE), "no-depth sprite sentinel rejected");
		for (int scale = 1; scale <= 4; ++scale) {
			BSurface dense(4, 3, 2, nullptr, scale);
			dense.Fill(0);
			Check(dense.Blit_From(Rect(-1, 1, 3, 2), source, source.Get_Rect()), "clipped scaled blit failed");
			RasterSurfaceView physical(dense);
			for (int y = scale; y < 2 * scale; ++y) for (int x = 0; x < scale; ++x) Check(physical.Get_Pixel(Point2D(x, y)) == 0x5678, "clipping changed source sample alignment");
			physical.Put_Pixel(Point2D(0, 0), 0x91);
			physical.Put_Pixel(Point2D(scale, 0), 0x92);
			dense.Blit_From(Rect(1, 0, 3, 1), dense, Rect(0, 0, 3, 1));
			Check(physical.Get_Pixel(Point2D(scale, 0)) == 0x91 && physical.Get_Pixel(Point2D(2 * scale, 0)) == 0x92, "overlap destroyed unread source samples");
			dense.Fill(0);
			dense.Draw_Line(Point2D(1, 1), Point2D(3, 1), 0xffff);
			Check(physical.Get_Pixel(Point2D(4 * scale - 1, 2 * scale - 1)) == 0xffff && physical.Get_Pixel(Point2D(3 * scale, 2 * scale)) == 0, "dense line lost inclusive endpoint block or changed thickness");
			dense.Fill(0);
			dense.Draw_Line(Point2D(1, 2), Point2D(1, 0), 0xffff);
			Check(physical.Get_Pixel(Point2D(2 * scale - 1, 3 * scale - 1)) == 0xffff, "reverse line lost start endpoint block");
		}
		for (int mode = 0; mode < ZGRAD_COUNT; ++mode) {
			BSurface input(4, 7, 1), classic(16, 12, 2), hd(16, 12, 2, nullptr, 2);
			ABuffer a1(Rect(0, 0, 16, 12)); ZBuffer z1(Rect(0, 0, 16, 12));
			AlphaBuffer = &a1; DepthBuffer = &z1;
			Bit_Blit(classic, Rect(2, 1, 4, 7), input, input.Get_Rect(), DepthRecorder(), -10, static_cast<ZGradientType>(mode));
			ABuffer a2(Rect(0, 0, 16, 12), 2); ZBuffer z2(Rect(0, 0, 16, 12), 2);
			AlphaBuffer = &a2; DepthBuffer = &z2;
			Bit_Blit(hd, Rect(2, 1, 4, 7), input, input.Get_Rect(), DepthRecorder(), -10, static_cast<ZGradientType>(mode));
			RasterSurfaceView raster(hd);
			for (int y = 0; y < 7; ++y) for (int sub = 0; sub < 2; ++sub) Check(classic.Get_Pixel(Point2D(2, y + 1)) == raster.Get_Pixel(Point2D(4, (y + 1) * 2 + sub)), "HD logical gradient diverged from Classic");
			AlphaBuffer = nullptr; DepthBuffer = nullptr;
		}
		ABuffer alpha(Rect(0, 0, 16, 12), 2); ZBuffer depth(Rect(0, 0, 16, 12), 2);
		AlphaBuffer = &alpha; DepthBuffer = &depth;
		DSurface frame(16, 12, RenderDomain::World); frame.Fill(0);
		HDC dc = frame.GetDC();
		RECT rectangle{2, 1, 3, 2};
		FillRect(dc, &rectangle, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
		frame.ReleaseDC(dc);
		{
			RasterSurfaceView gdi(frame);
			Check(gdi.Get_Pixel(Point2D(4, 2)) == 0xffff && gdi.Get_Pixel(Point2D(5, 3)) == 0xffff && gdi.Get_Pixel(Point2D(6, 3)) == 0, "GDI did not preserve logical layout at physical density");
		}
		frame.Draw_Depth_Shaded_Line(frame.Get_Rect(), Point2D(1, 3), Point2D(12, 3), 0xf800, -10, -10, true);
		RasterSurfaceView physical(frame);
		Check(physical.Get_Pixel(Point2D(8, 6)) != 0, "HD depth line not drawn");
		Check(*reinterpret_cast<unsigned short *>(depth.Get_Raster_Offset(Point2D(8, 6))) < 32768, "HD line did not write physical depth");
		AlphaBuffer = nullptr; DepthBuffer = nullptr;
		Run_Render_UI_Tests();
		Run_Render_Cache_Tests();
		Run_Render_Startup_Tests();
		std::cout << "Render core contracts passed\n";
		return 0;
	} catch (std::exception const & error) { std::cerr << error.what() << '\n'; return 1; }
}
