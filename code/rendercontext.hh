/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#pragma once

#include "coord.h"
#include "rect.h"
#include "renderdomain.hh"
#include "xsurface.h"

#include <cstddef>
#include <cstdint>

class CCINIClass;

struct RenderSettings {
	RenderMode Mode;
	int RasterScale;
	int WorldArtScale;
	int UIArtScale;
	std::size_t CacheBudgetBytes;
};

RenderSettings const & Get_Render_Settings();
bool Set_Render_Settings(RenderSettings const & settings);
void Load_Render_Settings(CCINIClass const & ini);
int Render_Raster_Scale(RenderDomain domain = RenderDomain::World);
int Render_Art_Scale(RenderDomain domain);
std::uint64_t Render_Generation();
int Checked_Raster_Size(int width, int height, int bytes_per_pixel, int density);
bool Render_Blit_Surface(Surface & dest, Rect const & dclip, Rect const & drect, Surface const & source, Rect const & sclip, Rect const & srect, bool transparent);

struct RasterView {
	std::uint8_t * Pixels;
	int Width;
	int Height;
	int Pitch;
	int BytesPerPixel;
};

// A unit-density, non-owning view of a surface's physical pixels; it holds the owner locked.
class RasterSurfaceView : public XSurface {
public:
	explicit RasterSurfaceView(Surface const & owner);
	~RasterSurfaceView() override;
	RasterSurfaceView(RasterSurfaceView const &) = delete;
	RasterSurfaceView & operator=(RasterSurfaceView const &) = delete;
	void * Lock(Point2D point = Point2D(0, 0)) const override;
	bool Unlock() const override;
	int Bytes_Per_Pixel() const override;
	int Stride() const override;
	RasterView View() const;
	Surface const & Owner() const;

private:
	Surface const & Source;
	std::uint8_t * Pixels;
};

// Rectangle conversion never inherits an object origin; draw-point conversion does.
Rect Render_Rect_To_Raster(Surface const & surface, Rect const & logical);
Point2D Render_Point_To_Raster(Surface const & surface, Point2D const & logical);
Point2D Render_Draw_Point(Surface const & surface, Point2D const & logical);
Point2D Project_Render_Coord(Coord const & world, int density);
Point2D Render_Origin_Residual(int density);

class ScopedWorldOrigin {
public:
	ScopedWorldOrigin(Coord const & world, Point2D const & legacy_absolute_anchor);
	ScopedWorldOrigin(Point2D const & legacy_absolute_anchor, Point2D const & physical_absolute_anchor, int density);
	~ScopedWorldOrigin();
	ScopedWorldOrigin(ScopedWorldOrigin const &) = delete;
	ScopedWorldOrigin & operator=(ScopedWorldOrigin const &) = delete;
private:
	Point2D PreviousResidual;
	int PreviousDensity;
};

class ScopedScreenSpace {
public:
	ScopedScreenSpace();
	~ScopedScreenSpace();
	ScopedScreenSpace(ScopedScreenSpace const &) = delete;
	ScopedScreenSpace & operator=(ScopedScreenSpace const &) = delete;
private:
	Point2D PreviousResidual;
	int PreviousDensity;
};
