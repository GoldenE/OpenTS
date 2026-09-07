/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "hdasset.hh"
#include "draw.hh"
#include "zgrad.hh"
#include "renderdomain.hh"
#include "renderstage.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Point2D { int X = 0, Y = 0; Point2D() = default; Point2D(int x, int y) : X(x), Y(y) {} };
struct Rect {
	int X = 0, Y = 0, Width = 0, Height = 0;
	Rect() = default;
	Rect(int x, int y, int w, int h) : X(x), Y(y), Width(w), Height(h) {}
	bool Is_Valid() const { return Width > 0 && Height > 0; }
};
Rect RECT_NONE;
Rect Intersect(Rect a, Rect b) { int x = std::max(a.X, b.X), y = std::max(a.Y, b.Y); return {x, y, std::max(0, std::min(a.X + a.Width, b.X + b.Width) - x), std::max(0, std::min(a.Y + a.Height, b.Y + b.Height) - y)}; }
struct Surface {
	std::vector<unsigned short> Pixels = std::vector<unsigned short>(64, 0x001f);
	int Scale = 2;
	int Bytes_Per_Pixel() const { return 2; }
	int Get_Raster_Scale() const { return Scale; }
	RenderDomain Get_Render_Domain() const { return RenderDomain::World; }
	Rect Get_Rect() const { return {0, 0, 8 / Scale, 8 / Scale}; }
};
struct RasterView { std::uint8_t * Pixels; int Pitch = 16; };
struct RasterSurfaceView { Surface & S; explicit RasterSurfaceView(Surface & s) : S(s) {} RasterView View() { return {reinterpret_cast<std::uint8_t *>(S.Pixels.data())}; } };
struct RGB { unsigned R, G, B; unsigned Get_Red() const { return R; } unsigned Get_Green() const { return G; } unsigned Get_Blue() const { return B; } };
struct DSurface {
	static RGB Deconstruct_Hicolor_Pixel(unsigned short color) { return {unsigned((color >> 11) * 8), unsigned(((color >> 5) & 63) * 4), unsigned((color & 31) * 8)}; }
	static unsigned short Build_Hicolor_Pixel(int r, int g, int b) { return static_cast<unsigned short>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
	static unsigned short Get_Halfbright_Mask() { return 0x7bef; }
	static unsigned short Get_Quarterbright_Mask() { return 0x39e7; }
};
struct ConvertClass {
	virtual ~ConvertClass() = default;
	std::vector<unsigned short> Table = std::vector<unsigned short>(256, 0x07e0);
	int Levels = 1;
	int Bytes_Per_Pixel() const { return 2; }
	int Get_Intensity_Levels() const { return Levels; }
	void const * Get_Intensity_Table() const { return Table.data(); }
};
struct LightConvertClass : ConvertClass { bool UseIonLighting = false; int IonRedTint = 0, IonGreenTint = 0, IonBlueTint = 0, NormalRedTint = 1000, NormalGreenTint = 1000, NormalBlueTint = 1000; };
struct ShapeSet {
	void const * Data = nullptr;
	Rect Crop = {0, 0, 2, 2};
	int Get_Count() const { return 1; } int Get_Width() const { return 2; } int Get_Height() const { return 2; }
	Rect Get_Rect(int) const { return Crop; }
	bool Is_RLE_Compressed(int) const { return false; }
	void const * Get_Data(int) const { return Data; }
};
struct Ring {
	Rect Bounds = {0, 0, 4, 4};
	std::array<unsigned short, 64> Values;
	Ring() { Values.fill(65535); }
	int Get_Raster_Scale() const { return 2; }
	int Get_Scroll_Delta(int y) const { return 128 - y; }
	std::uintptr_t Get_Raster_Offset(Point2D point) {
		if (point.X < 0 || point.Y < 0 || point.X >= 8 || point.Y >= 8) throw std::runtime_error("Ring addressing escaped bounds");
		return reinterpret_cast<std::uintptr_t>(&Values[point.Y * 8 + point.X]);
	}
};
Ring * DepthBuffer = nullptr, * AlphaBuffer = nullptr;
struct ZGradStruct { int LineIncrement = 1, StepNumerator = 2, StepDenominator = 1, WrapIncrement = 1; bool IsTopDown = true; };
ZGradStruct ZGradients[4];
struct Settings { RenderMode Mode = RenderMode::HD; } settings;
Settings const & Get_Render_Settings() { return settings; }
int Render_Art_Scale(RenderDomain) { return 2; }
Point2D residual;
Point2D Render_Draw_Point(Surface const & s, Point2D p) { return {p.X * s.Scale + residual.X, p.Y * s.Scale + residual.Y}; }
Rect Render_Rect_To_Raster(Surface const & s, Rect r) { return {r.X * s.Scale, r.Y * s.Scale, r.Width * s.Scale, r.Height * s.Scale}; }
RenderAnimationSample animation;
RenderAnimationSample Current_Render_Animation() { return animation; }
std::shared_ptr<HDAsset::Pack> active_pack;
namespace HDAsset { std::shared_ptr<Pack const> Fetch(void const *) { return active_pack; } void Report_Fallback(char const *, char const *) {} }

#include "hdshape_under_test.inc"

void Check(bool value, char const * message) { if (!value) throw std::runtime_error(message); }

int main()
{
	try {
		active_pack = std::make_shared<HDAsset::Pack>();
		active_pack->Name = "SYNTH.SHP";
		HDAsset::Variant variant; variant.Scale = 2; variant.LogicalWidth = 2; variant.LogicalHeight = 2; variant.LogicalFrames = 1;
		HDAsset::Frame frame; frame.Width = 4; frame.Height = 4; frame.Color.resize(64);
		for (int i = 0; i < 16; ++i) { frame.Color[i * 4] = static_cast<unsigned char>(128 + i * 8); frame.Color[i * 4 + 3] = 255; }
		variant.Frames.push_back(frame); active_pack->Variants.push_back(variant);
		Surface surface; ConvertClass convert; ShapeSet shape;
		auto draw = [&](ShapeFlags_Type flags = SHAPE_CENTER, Rect clip = Rect(0, 0, 4, 4), unsigned char const * remap = nullptr) { return HD_Try_Draw_Shape(surface, convert, &shape, 0, Point2D(2, 2), clip, flags, remap, 0, ZGRAD_GROUND, 1000, nullptr, 0, {}); };
		Check(draw(), "HD frame selected");
		Check(surface.Pixels[18] == 0x8000 && surface.Pixels[19] == 0x8800 && surface.Pixels[21] == 0x9800, "Exact-scale distinct samples survive");
		Check(surface.Pixels[17] == 31 && surface.Pixels[22] == 31, "Centered physical placement");
		auto & f = active_pack->Variants[0].Frames[0];
		f.Color[3] = 0; surface.Pixels.assign(64, 31); draw(); Check(surface.Pixels[18] == 31, "Zero alpha preserves destination");
		f.Color[3] = 128; surface.Pixels.assign(64, 31); draw(); Check(surface.Pixels[18] == 0x400f, "Straight alpha coverage blend");
		f.Color[3] = 255; f.Remap.resize(16); f.Remap[0] = 1;
		std::array<unsigned char, 256> remap; for (unsigned i = 0; i < 256; ++i) remap[i] = static_cast<unsigned char>(i);
		remap[16] = 42; convert.Table[42] = 0xffff; surface.Pixels.assign(64, 31); draw(SHAPE_CENTER, {0, 0, 4, 4}, remap.data());
		Check(surface.Pixels[18] == 0xffff && surface.Pixels[19] == 0x8800, "House mask alone remaps");
		remap[200] = 5; Check(!draw(SHAPE_CENTER, {0, 0, 4, 4}, remap.data()), "Non-house palette remapping falls back instead of silently approximating"); remap[200] = 200;
		f.Remap.clear(); surface.Pixels.assign(64, 31); draw(ShapeFlags_Type(SHAPE_CENTER | SHAPE_TRANSLUCENT50));
		Check(surface.Pixels[18] == 0x400f, "Classic packed half blend preserved");
		f.Shadow.assign(16, 255); surface.Pixels.assign(64, 0xffff); draw(); Check(surface.Pixels[18] == 0x7bef, "Semantic shadow darkens destination"); f.Shadow.clear();
		Ring depth; DepthBuffer = &depth; f.Depth.assign(16, 5); surface.Pixels.assign(64, 31);
		draw(ShapeFlags_Type(SHAPE_CENTER | SHAPE_ZWRITE)); Check(depth.Values[18] == 122, "Depth remains logical and semantic offset subtracts");
		surface.Pixels.assign(64, 31); draw(ShapeFlags_Type(SHAPE_CENTER | SHAPE_ZREAD)); Check(surface.Pixels[18] == 31, "Equal depth fails strict test");
		DepthBuffer = nullptr; f.Depth.clear();
		ZGradients[ZGRAD_45DEG] = {3, 3, 2, -1, true};
		f.Height = 6; f.Color.resize(96, 255); shape.Crop.Height = 3; depth.Values.fill(65535); DepthBuffer = &depth;
		Check(HD_Try_Draw_Shape(surface, convert, &shape, 0, {2, 2}, {0, 0, 4, 4}, ShapeFlags_Type(SHAPE_CENTER | SHAPE_ZWRITE), nullptr, 0, ZGRAD_45DEG, 1000, nullptr, 0, {}), "Gradient draw selected");
		Check(depth.Values[18] == 126 && depth.Values[34] == 126 && depth.Values[50] == 125, "Fractional gradient recurrence survives density");
		f.Height = 4; f.Color.resize(64); shape.Crop.Height = 2;
		std::array<std::int8_t, 4> zbytes = {-5, 0, 0, 0}; ShapeSet zshape; zshape.Data = zbytes.data(); depth.Values.fill(65535);
		Check(HD_Try_Draw_Shape(surface, convert, &shape, 0, {2, 2}, {0, 0, 4, 4}, ShapeFlags_Type(SHAPE_CENTER | SHAPE_ZWRITE), nullptr, 0, ZGRAD_GROUND, 1000, &zshape, 0, {1, 1}), "Legacy z-shape draw selected");
		Check(depth.Values[18] == 132, "Legacy signed z-shape offset retained"); DepthBuffer = nullptr;
		residual = {-3, -3}; surface.Pixels.assign(64, 31); draw(); Check(surface.Pixels[0] == 0xa800 && surface.Pixels[3] == 31, "Residual and clipping select correct source samples"); residual = {};
		active_pack->Variants[0].LogicalFrames = 2; Check(!draw(), "Logical frame mismatch falls back"); active_pack->Variants[0].LogicalFrames = 1;
		Check(!draw(SHAPE_ALPHA_WRITE_MULT), "Palette-intensity operation deliberately falls back");
		Check(HD_Try_Draw_Shape(surface, convert, &shape, 0, {2, 2}, {0, 0, 4, 4}, SHAPE_CENTER, nullptr, 0, ZGRAD_NONE, 1000, nullptr, 0, {}), "No-depth sentinel uses HD frame");
		Check(!HD_Try_Draw_Shape(surface, convert, &shape, 0, {2, 2}, {0, 0, 4, 4}, SHAPE_CENTER, nullptr, 0, static_cast<ZGradientType>(99), 1000, nullptr, 0, {}), "Invalid gradient rejected");
		f.X = f.Y = 1; f.Width = f.Height = 3; Check(draw(), "Inset frame touching classic right/bottom boundary accepted");
		f.X = -1; Check(!draw(), "Left overhang rejected"); f.X = 1;
		f.Y = -1; Check(!draw(), "Top overhang rejected"); f.Y = 1;
		f.Width = 4; Check(!draw(), "Right overhang rejected"); f.Width = 3;
		f.Height = 4; Check(!draw(), "Bottom overhang rejected"); f.Height = 3;
		f.Width = 0xffffffff; Check(!draw(), "Unsigned width is widened before edge addition"); f.Width = 3;
		f.Height = 0xffffffff; Check(!draw(), "Unsigned height is widened before edge addition"); f.Height = 3;
		shape.Crop = {1, 1, 1, 1}; f.X = f.Y = 2; f.Width = f.Height = 2; Check(draw(), "Nonzero classic crop origin honored");
		f.X = 1; Check(!draw(), "Canvas-contained image outside actual classic crop rejected");
		shape.Crop = {0, 0, 2, 2}; f.X = f.Y = 0; f.Width = f.Height = 4;
		auto frame_copy = f; auto & v = active_pack->Variants[0]; v.Facings = 2; v.Sequences = {{0, 1, 2, true}}; v.Frames.clear();
		for (unsigned facing = 0; facing < 2; ++facing) for (int direction : {-1, 1}) for (unsigned subframe = 0; subframe < 2; ++subframe) {
			frame_copy.Facing = facing; frame_copy.Direction = direction; frame_copy.SubFrame = subframe; frame_copy.Color[0] = facing ? 248 : 128; v.Frames.push_back(frame_copy);
		}
		animation.Progress = {32768, -1}; animation.Facing = 128;
		Check(draw() && surface.Pixels[18] == 0xf800, "Reverse temporal extended-facing frame inside classic envelope accepted");
		settings.Mode = RenderMode::Classic; Check(!draw(), "Classic dispatch remains untouched");
		std::cout << "Production HD shape density, centering, clipping, coverage, semantic remap/shadow, translucency, depth and fallback passed\n";
		return 0;
	} catch (std::exception const & error) { std::cerr << error.what() << '\n'; return 1; }
}
