/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "hdshape.hh"
#include "hdruntime.hh"
#include "rendercontext.hh"
#include "renderstage.hh"
#include "shapeset.h"
#include "convert.h"
#include "lightcon.h"
#include "dsurface.h"
#include "zbuffer.h"
#include "abuffer.h"
#include "_zbuffer.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

unsigned short Blend(unsigned short source, unsigned short destination, unsigned alpha)
{
	if (alpha == 255) return source;
	if (!alpha) return destination;
	auto a = DSurface::Deconstruct_Hicolor_Pixel(source);
	auto b = DSurface::Deconstruct_Hicolor_Pixel(destination);
	return static_cast<unsigned short>(DSurface::Build_Hicolor_Pixel(
		(a.Get_Red() * alpha + b.Get_Red() * (255 - alpha) + 127) / 255,
		(a.Get_Green() * alpha + b.Get_Green() * (255 - alpha) + 127) / 255,
		(a.Get_Blue() * alpha + b.Get_Blue() * (255 - alpha) + 127) / 255));
}

int Floor_Div(int value, int divisor)
{
	return value / divisor - (value < 0 && value % divisor != 0);
}

unsigned short Color(HDAsset::Frame const & frame, std::size_t pixel, ConvertClass const & convert, unsigned char const * remap, int band, std::array<int, 3> const & tint)
{
	int levels = convert.Get_Intensity_Levels();
	if (!frame.Remap.empty() && frame.Remap[pixel]) {
		unsigned index = 15 + frame.Remap[pixel];
		if (remap) index = remap[index];
		return static_cast<unsigned short const *>(convert.Get_Intensity_Table())[band * 256 + index];
	}
	std::array<int, 3> rgb;
	for (unsigned channel = 0; channel < 3; ++channel) {
		std::int64_t factor = std::int64_t(tint[channel]) * 65536 / 1000;
		if (levels > 1) factor = factor * 2 * band / (levels - 1);
		rgb[channel] = static_cast<int>(std::clamp<std::int64_t>(frame.Color[pixel * 4 + channel] * factor / 65536, 0, 255));
	}
	return static_cast<unsigned short>(DSurface::Build_Hicolor_Pixel(rgb[0], rgb[1], rgb[2]));
}

}


bool HD_Try_Draw_Shape(Surface & surface, ConvertClass & convert, ShapeSet const * shapefile, int shapenum, Point2D const & point, Rect const & window, ShapeFlags_Type flags, unsigned char const * remap, int height_offset, ZGradientType zgrad, int intensity, ShapeSet const * z_shapefile, int z_shapenum, Point2D z_off)
{
	if (Get_Render_Settings().Mode != RenderMode::HD || !shapefile || shapenum < 0 || shapenum >= shapefile->Get_Count() || surface.Bytes_Per_Pixel() != 2 || convert.Bytes_Per_Pixel() != 2) return false;
	// These effects use palette indices as light contributions or displaced samples.
	if (flags & (SHAPE_PREDATOR | SHAPE_ALPHA_WRITE | SHAPE_ALPHA_WRITE_MULT | SHAPE_ALPHA_BLEND)) return false;
	if (remap) for (unsigned index = 0; index < 256; ++index) if ((index < 16 || index > 31) && remap[index] != index) return false;
	if (zgrad == ZGRAD_NONE && !(flags & (SHAPE_ZREAD | SHAPE_ZGRAD | SHAPE_ZWRITE))) zgrad = ZGRAD_GROUND;
	if (zgrad < ZGRAD_FIRST || zgrad >= ZGRAD_COUNT) return false;
	auto pack = HDAsset::Fetch(shapefile);
	if (!pack || (pack->Type != HDAsset::Kind::SHAPE && pack->Type != HDAsset::Kind::UI && pack->Type != HDAsset::Kind::FONT)) return false;
	auto domain = pack->Type == HDAsset::Kind::UI || pack->Type == HDAsset::Kind::FONT ? RenderDomain::UI : surface.Get_Render_Domain();
	auto variant = HDAsset::Select_Variant(*pack, Render_Art_Scale(domain));
	if (!variant) return false;
	if (variant->LogicalWidth != static_cast<unsigned>(shapefile->Get_Width()) || variant->LogicalHeight != static_cast<unsigned>(shapefile->Get_Height()) || variant->LogicalFrames != static_cast<unsigned>(shapefile->Get_Count())) {
		HDAsset::Report_Fallback(pack->Name.c_str(), "Logical shape metadata differs from classic");
		return false;
	}
	auto animation = Current_Render_Animation();
	auto frame = HDAsset::Select_Animated_Frame(*variant, shapenum, animation.Progress.Direction, animation.Progress.Fraction, animation.Facing);
	if (!frame) return false;
	Rect classic = shapefile->Get_Rect(shapenum);
	std::int64_t art_scale = variant->Scale;
	if (!classic.Is_Valid() || std::int64_t(frame->X) < std::int64_t(classic.X) * art_scale || std::int64_t(frame->Y) < std::int64_t(classic.Y) * art_scale ||
		std::int64_t(frame->X) + frame->Width > (std::int64_t(classic.X) + classic.Width) * art_scale ||
		std::int64_t(frame->Y) + frame->Height > (std::int64_t(classic.Y) + classic.Height) * art_scale) {
		HDAsset::Report_Fallback(pack->Name.c_str(), "Frame rectangle exceeds the classic visual envelope");
		return false;
	}
	if (z_shapefile && frame->Depth.empty() && (z_shapenum < 0 || z_shapenum >= z_shapefile->Get_Count() || z_shapefile->Is_RLE_Compressed(z_shapenum))) return false;
	bool ztest = (flags & (SHAPE_ZREAD | SHAPE_ZGRAD | SHAPE_ZWRITE)) != 0;
	bool alpha_light = (flags & SHAPE_ALPHA) != 0;
	int scale = surface.Get_Raster_Scale();
	if (ztest && (!DepthBuffer || DepthBuffer->Get_Raster_Scale() != scale)) return false;
	if ((alpha_light || (flags & (SHAPE_ZERO_ALPHA | SHAPE_NONZERO_ALPHA))) && (!AlphaBuffer || AlphaBuffer->Get_Raster_Scale() != scale)) return false;
	int x = point.X, y = point.Y;
	if (flags & SHAPE_CENTER) { x -= shapefile->Get_Width() / 2; y -= shapefile->Get_Height() / 2; }
	auto origin = Render_Draw_Point(surface, Point2D(x + window.X, y + window.Y));
	int left = origin.X + Floor_Div(frame->X * scale, variant->Scale);
	int top = origin.Y + Floor_Div(frame->Y * scale, variant->Scale);
	int right = origin.X + Floor_Div((frame->X + static_cast<int>(frame->Width)) * scale + variant->Scale - 1, variant->Scale);
	int bottom = origin.Y + Floor_Div((frame->Y + static_cast<int>(frame->Height)) * scale + variant->Scale - 1, variant->Scale);
	Rect clip = Intersect(Rect(left, top, right - left, bottom - top), Render_Rect_To_Raster(surface, Intersect(window, surface.Get_Rect())));
	if (ztest) clip = Intersect(clip, Render_Rect_To_Raster(surface, DepthBuffer->Bounds));
	if (alpha_light || (flags & (SHAPE_ZERO_ALPHA | SHAPE_NONZERO_ALPHA))) clip = Intersect(clip, Render_Rect_To_Raster(surface, AlphaBuffer->Bounds));
	if (!clip.Is_Valid()) return true;
	RasterSurfaceView output(surface);
	auto raster = output.View();
	if (!raster.Pixels) return false;
	std::array<int, 3> tint = {1000, 1000, 1000};
	if (auto light = dynamic_cast<LightConvertClass const *>(&convert)) tint = light->UseIonLighting ? std::array<int, 3>{light->IonRedTint, light->IonGreenTint, light->IonBlueTint} : std::array<int, 3>{light->NormalRedTint, light->NormalGreenTint, light->NormalBlueTint};
	Rect zrect = z_shapefile ? z_shapefile->Get_Rect(z_shapenum) : RECT_NONE;
	auto zdata = z_shapefile ? static_cast<std::int8_t const *>(z_shapefile->Get_Data(z_shapenum)) : nullptr;
	ZGradStruct const & gradient = ZGradients[zgrad];
	bool remapping = remap || (flags & SHAPE_REMAP);
	int translucency = remapping ? 0 : flags & SHAPE_TRANSLUCENT75;
	bool darken = !remapping && !translucency && (flags & SHAPE_DARKEN);
	int levels = convert.Get_Intensity_Levels();
	int first_logical_y = Floor_Div(clip.Y - origin.Y, scale);
	int initial_z = 0, initial_fraction = 0;
	bool explicit_depth = z_shapefile || !frame->Depth.empty();
	if (ztest) {
		if (gradient.IsTopDown) {
			initial_z = static_cast<unsigned short>(DepthBuffer->Get_Scroll_Delta(window.Y + y + first_logical_y - DepthBuffer->Bounds.Y)) + height_offset;
			initial_z = initial_z / gradient.LineIncrement * gradient.LineIncrement;
		} else {
			initial_z = static_cast<unsigned short>(DepthBuffer->Get_Scroll_Delta(window.Y + y + classic.Y + classic.Height - 1 - DepthBuffer->Bounds.Y)) + height_offset;
			if (!explicit_depth) {
				int ratio = gradient.StepNumerator / gradient.StepDenominator;
				int remaining = classic.Y + classic.Height - first_logical_y;
				initial_z = initial_z / ratio * ratio - remaining / ratio;
				initial_fraction = gradient.StepNumerator - remaining % ratio;
				if (initial_fraction == gradient.StepNumerator) { initial_fraction = 0; initial_z += gradient.WrapIncrement; }
			}
		}
	}
	for (int py = clip.Y; py < clip.Y + clip.Height; ++py) {
		int sy = std::clamp((py - top) * static_cast<int>(frame->Height) / (bottom - top), 0, static_cast<int>(frame->Height) - 1);
		int logical_y = Floor_Div(py - origin.Y, scale);
		int baseline = initial_z;
		if (ztest && !explicit_depth) baseline += (initial_fraction + (logical_y - first_logical_y) * gradient.StepDenominator) / gradient.StepNumerator * gradient.WrapIncrement;
		for (int px = clip.X; px < clip.X + clip.Width; ++px) {
			int sx = std::clamp((px - left) * static_cast<int>(frame->Width) / (right - left), 0, static_cast<int>(frame->Width) - 1);
			std::size_t sample = std::size_t(sy) * frame->Width + sx;
			unsigned coverage = frame->Color[sample * 4 + 3];
			if (!coverage) continue;
			auto depth = ztest ? reinterpret_cast<unsigned short *>(DepthBuffer->Get_Raster_Offset(Point2D(px, py - DepthBuffer->Bounds.Y * scale))) : nullptr;
			int value = baseline;
			if (!frame->Depth.empty()) value -= frame->Depth[sample];
			else if (zdata) {
				int zx = z_off.X - shapefile->Get_Width() / 2 + zrect.X + Floor_Div(px - origin.X, scale);
				int zy = z_off.Y - shapefile->Get_Height() / 2 + zrect.Y + logical_y;
				if (zx >= 0 && zy >= 0 && zx < zrect.Width && zy < zrect.Height) value -= zdata[zy * zrect.Width + zx];
			}
			if (depth && value >= *depth) continue;
			unsigned light = 127;
			if (AlphaBuffer && (alpha_light || (flags & (SHAPE_ZERO_ALPHA | SHAPE_NONZERO_ALPHA)))) light = std::min<unsigned>(255, *reinterpret_cast<unsigned short *>(AlphaBuffer->Get_Raster_Offset(Point2D(px, py - AlphaBuffer->Bounds.Y * scale))));
			if (!remapping && !alpha_light && !ztest && (translucency == SHAPE_TRANSLUCENT50 || translucency == SHAPE_TRANSLUCENT75)) {
				if ((flags & SHAPE_ZERO_ALPHA) && light != 0) continue;
				if (!(flags & SHAPE_ZERO_ALPHA) && (flags & SHAPE_NONZERO_ALPHA) && light == 0) continue;
			}
			int band = (levels - 1) / 2;
			if (alpha_light) {
				int level = static_cast<int>(std::clamp<std::int64_t>(std::int64_t(std::max(0, intensity)) * 261 / 2048, 0, 254));
				band = std::min(levels - 1, static_cast<int>(light * level * (levels - 1) / 32258));
			}
			auto destination = reinterpret_cast<unsigned short *>(raster.Pixels + py * raster.Pitch) + px;
			unsigned short color = Color(*frame, sample, convert, remap, band, tint);
			if (darken) color = (*destination >> 1) & DSurface::Get_Halfbright_Mask();
			else if (translucency == SHAPE_TRANSLUCENT50) color = ((*destination >> 1) & DSurface::Get_Halfbright_Mask()) + ((color >> 1) & DSurface::Get_Halfbright_Mask());
			else if (translucency) {
				unsigned source_quarter = (color >> 2) & DSurface::Get_Quarterbright_Mask();
				unsigned destination_quarter = (*destination >> 2) & DSurface::Get_Quarterbright_Mask();
				color = static_cast<unsigned short>(translucency == SHAPE_TRANSLUCENT25 ? source_quarter * 3 + destination_quarter : source_quarter + destination_quarter * 3);
			}
			if (!darken && !frame->Shadow.empty() && frame->Shadow[sample]) color = Blend((*destination >> 1) & DSurface::Get_Halfbright_Mask(), color, frame->Shadow[sample]);
			*destination = Blend(color, *destination, coverage);
			if (depth && (flags & SHAPE_ZWRITE)) *depth = static_cast<unsigned short>(value);
		}
	}
	return true;
}
