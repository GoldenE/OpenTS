/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#include "always.h"
#include "rendercontext.hh"
#include "globals.h"
#include <limits>
#include <stdexcept>
#include <vector>

namespace RenderContextState {
extern RenderSettings Settings;
extern std::uint64_t Generation;
extern thread_local Point2D Residual;
extern thread_local int ResidualDensity;
}
using namespace RenderContextState;

RenderSettings const & Get_Render_Settings() { return Settings; }

bool Set_Render_Settings(RenderSettings const & settings)
{
	if (settings.Mode != RenderMode::Classic && settings.Mode != RenderMode::HD) return false;
	if (settings.RasterScale < 1 || settings.RasterScale > 4 || settings.WorldArtScale < 1 || settings.WorldArtScale > 4 || settings.UIArtScale < 1 || settings.UIArtScale > 4 || settings.CacheBudgetBytes < 1048576 || settings.CacheBudgetBytes > 1073741824) return false;
	Settings = settings;
	++Generation;
	return true;
}

int Render_Raster_Scale(RenderDomain domain)
{
	return Settings.Mode == RenderMode::HD && (domain == RenderDomain::World || domain == RenderDomain::UI) ? Settings.RasterScale : 1;
}

int Render_Art_Scale(RenderDomain domain)
{
	if (Settings.Mode != RenderMode::HD) return 1;
	return domain == RenderDomain::World ? Settings.WorldArtScale : domain == RenderDomain::UI ? Settings.UIArtScale : 1;
}

std::uint64_t Render_Generation() { return Generation; }

int Checked_Raster_Size(int width, int height, int bytes_per_pixel, int density)
{
	if (width < 0 || height < 0 || bytes_per_pixel < 1 || bytes_per_pixel > 4 || density < 1 || density > 4) throw std::length_error("Invalid raster dimensions");
	std::uint64_t size = std::uint64_t(width) * height * bytes_per_pixel * density * density;
	if (std::uint64_t(width) * density > 16384 || std::uint64_t(height) * density > 16384 || size > 1073741824) throw std::length_error("Raster resource limit exceeded");
	return static_cast<int>(size);
}

RasterSurfaceView::RasterSurfaceView(Surface const & owner) : XSurface(owner.Get_Raster_Width(), owner.Get_Raster_Height()), Source(owner), Pixels(static_cast<std::uint8_t *>(owner.Lock_Raster())) {}
RasterSurfaceView::~RasterSurfaceView() { if (Pixels) Source.Unlock(); }
void * RasterSurfaceView::Lock(Point2D point) const { if (!Pixels) return nullptr; ++LockCount; return Pixels + point.Y * Stride() + point.X * Bytes_Per_Pixel(); }
bool RasterSurfaceView::Unlock() const { if (LockCount) --LockCount; return true; }
int RasterSurfaceView::Bytes_Per_Pixel() const { return Source.Bytes_Per_Pixel(); }
int RasterSurfaceView::Stride() const { return Source.Stride(); }
RasterView RasterSurfaceView::View() const { return {Pixels, Width, Height, Stride(), Bytes_Per_Pixel()}; }
Surface const & RasterSurfaceView::Owner() const { return Source; }

Rect Render_Rect_To_Raster(Surface const & surface, Rect const & logical)
{
	int scale = surface.Get_Raster_Scale();
	return Rect(logical.X * scale, logical.Y * scale, logical.Width * scale, logical.Height * scale);
}

Point2D Render_Point_To_Raster(Surface const & surface, Point2D const & logical) { return logical * surface.Get_Raster_Scale(); }
Point2D Render_Draw_Point(Surface const & surface, Point2D const & logical) { return Render_Point_To_Raster(surface, logical) + Render_Origin_Residual(surface.Get_Raster_Scale()); }

Point2D Project_Render_Coord(Coord const & world, int density)
{
	std::int64_t x = (std::int64_t(world.X) - world.Y) * 24 * density;
	std::int64_t y = (std::int64_t(world.X) + world.Y) * 12 * density;
	double factor = std::sin(RAD_60) * (ISO_TILE_PIXEL_W / CELL_LEPTON_DIAG);
	int fudge = world.Z >= CELL_LEPTON * 3 + CELL_LEPTON / 2 + 40 ? 1 : 0;
	return Point2D(static_cast<int>(x / CELL_LEPTON), static_cast<int>(y / CELL_LEPTON) - int((world.Z * factor + fudge) * density + 0.5));
}

Point2D Render_Origin_Residual(int density) { return density == ResidualDensity ? Residual : Point2D(); }

bool Render_Blit_Surface(Surface & dest, Rect const & dclip, Rect const & drect, Surface const & source, Rect const & sclip, Rect const & srect, bool transparent)
{
	if (!drect.Is_Valid() || !srect.Is_Valid() || dest.Bytes_Per_Pixel() != source.Bytes_Per_Pixel()) return false;
	Rect target = Render_Rect_To_Raster(dest, drect.Bias_To(dclip));
	Rect src = Render_Rect_To_Raster(source, srect.Bias_To(sclip));
	Rect clipped = Intersect(target, Render_Rect_To_Raster(dest, Intersect(dclip, dest.Get_Rect())));
	Rect source_bounds = Render_Rect_To_Raster(source, Intersect(sclip, source.Get_Rect()));
	if (!clipped.Is_Valid() || !source_bounds.Is_Valid()) return false;
	RasterSurfaceView output(dest), input(source);
	auto out = output.View(); auto in = input.View();
	if (!out.Pixels || !in.Pixels) return false;
	std::vector<std::uint8_t> snapshot;
	if (&dest == &source) {
		snapshot.assign(in.Pixels, in.Pixels + std::size_t(in.Pitch) * in.Height);
		in.Pixels = snapshot.data();
	}
	if (!transparent && target.Width == src.Width && target.Height == src.Height && src.X >= source_bounds.X && src.Y >= source_bounds.Y && src.X + src.Width <= source_bounds.X + source_bounds.Width && src.Y + src.Height <= source_bounds.Y + source_bounds.Height) {
		for (int y = clipped.Y; y < clipped.Y + clipped.Height; ++y) {
			memcpy(out.Pixels + y * out.Pitch + clipped.X * out.BytesPerPixel, in.Pixels + (src.Y + y - target.Y) * in.Pitch + (src.X + clipped.X - target.X) * in.BytesPerPixel, std::size_t(clipped.Width) * out.BytesPerPixel);
		}
		return true;
	}
	for (int y = clipped.Y; y < clipped.Y + clipped.Height; ++y) {
		int sy = src.Y + static_cast<int>(std::int64_t(y - target.Y) * src.Height / target.Height);
		if (sy < source_bounds.Y || sy >= source_bounds.Y + source_bounds.Height) continue;
		for (int x = clipped.X; x < clipped.X + clipped.Width; ++x) {
			int sx = src.X + static_cast<int>(std::int64_t(x - target.X) * src.Width / target.Width);
			if (sx < source_bounds.X || sx >= source_bounds.X + source_bounds.Width) continue;
			auto p = in.Pixels + sy * in.Pitch + sx * in.BytesPerPixel;
			if (transparent && (in.BytesPerPixel == 1 ? *p == 0 : *reinterpret_cast<std::uint16_t *>(p) == 0)) continue;
			memcpy(out.Pixels + y * out.Pitch + x * out.BytesPerPixel, p, in.BytesPerPixel);
		}
	}
	return true;
}
ScopedWorldOrigin::ScopedWorldOrigin(Coord const & world, Point2D const & anchor) : ScopedWorldOrigin(anchor, Project_Render_Coord(world, Render_Raster_Scale()), Render_Raster_Scale()) {}
ScopedWorldOrigin::ScopedWorldOrigin(Point2D const & anchor, Point2D const & physical, int density) : PreviousResidual(Residual), PreviousDensity(ResidualDensity) { Residual = physical - anchor * density; ResidualDensity = density; }
ScopedWorldOrigin::~ScopedWorldOrigin() { Residual = PreviousResidual; ResidualDensity = PreviousDensity; }
ScopedScreenSpace::ScopedScreenSpace() : PreviousResidual(Residual), PreviousDensity(ResidualDensity) { Residual = Point2D(); ResidualDensity = 1; }
ScopedScreenSpace::~ScopedScreenSpace() { Residual = PreviousResidual; ResidualDensity = PreviousDensity; }
