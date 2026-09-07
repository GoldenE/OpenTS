/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "renderworld.hh"
#include "hdasset.hh"

#define DEG_TO_RAD(x) ((x) * 3.14159265358979323846 / 180.0)
enum class RenderMode {Classic, HD};
struct RenderSettings { RenderMode Mode = RenderMode::Classic; };
RenderSettings Settings;
RenderSettings const & Get_Render_Settings() { return Settings; }
class DirType {
public:
	explicit DirType(int raw) : Raw(raw) {}
	int As_Int() const { return Raw; }
private:
	int Raw;
};

struct Point2D {
	int X = 0, Y = 0;
	Point2D(int x = 0, int y = 0) : X(x), Y(y) {}
};
struct Rect {
	int X, Y, Width, Height;
	Rect(int x, int y, int width, int height) : X(x), Y(y), Width(width), Height(height) {}
};
Rect Intersect(Rect a, Rect b)
{
	int const x = std::max(a.X,b.X), y = std::max(a.Y,b.Y);
	return Rect(x,y,std::max(0,std::min(a.X+a.Width,b.X+b.Width)-x),std::max(0,std::min(a.Y+a.Height,b.Y+b.Height)-y));
}
enum class RenderDomain {World};
class Surface {
public:
	Surface(int width, int height, int density, int bpp = 1) : Width(width), Height(height), Density(density), Bpp(bpp), Pixels(width * height * density * density * bpp, 0) {}
	int Get_Width() const { return Width; }
	int Get_Height() const { return Height; }
	int Get_Raster_Scale() const { return Density; }
	int Bytes_Per_Pixel() const { return Bpp; }
	int Stride() const { return Width * Density * Bpp; }
	void * Lock(Point2D p = {}) { return Pixels.data() + p.Y * Stride() + p.X * Bpp; }
	void Unlock() {}
	Rect Get_Rect() const { return Rect(0,0,Width,Height); }
	RenderDomain Get_Render_Domain() const { return RenderDomain::World; }
	int Width, Height, Density, Bpp;
	std::vector<unsigned char> Pixels;
};
#include "stbuffer_production.inc"
#include "world_production.inc"

struct Vector3 {
	float X, Y, Z;
	Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
};
namespace VoxelDrawSystem { bool EnableZBuffer = false; }
#include "dense_production.inc"

struct RGBClass {
	int R,G,B;
	int Get_Red() const { return R; }
	int Get_Green() const { return G; }
	int Get_Blue() const { return B; }
};
struct DSurface {
	static unsigned short Get_Halfbright_Mask() { return 0x7bef; }
	static unsigned short Build_Hicolor_Pixel(int r,int g,int b) { return (r>>3)<<11 | (g>>2)<<5 | (b>>3); }
	static RGBClass Deconstruct_Hicolor_Pixel(unsigned short p) { return {((p>>11)&31)<<3,((p>>5)&63)<<2,(p&31)<<3}; }
};
struct LightConvertClass {
	bool UseIonLighting = false;
	int NormalRedTint = 1000, NormalGreenTint = 1000, NormalBlueTint = 1000;
	int IonRedTint = 1000, IonGreenTint = 1000, IonBlueTint = 1000;
};
struct AlphaRemap {
	std::array<unsigned short,65536> Table{};
	unsigned short const * Get_Table(int) { return Table.data(); }
};
struct AlphaRemapManager {
	AlphaRemap Data;
	AlphaRemap * Init(int) { Data.Table.fill(256); return &Data; }
	void Deinit(AlphaRemap *) {}
} AlphaLightingRemapInit;
struct TestRing {
	Rect Bounds{0,0,80,80};
	int Density = 1;
	std::vector<unsigned short> Pixels;
	unsigned short Get_Scroll() const { return 32768; }
	int Get_Raster_Scale() const { return Density; }
	std::uintptr_t Get_Raster_Offset(Point2D p) { return reinterpret_cast<std::uintptr_t>(&Pixels[p.Y * Bounds.Width * Density + p.X]); }
} ZRing, ARing;
TestRing * DepthBuffer = &ZRing;
TestRing * AlphaBuffer = &ARing;
Rect TacticalRect(0,0,80,80);
Rect Render_Rect_To_Raster(Surface const & surface, Rect rect)
{
	int d = surface.Density;
	return Rect(rect.X*d,rect.Y*d,rect.Width*d,rect.Height*d);
}
int ArtScale = 2;
int Render_Art_Scale(RenderDomain) { return ArtScale; }
namespace HDAsset {
	std::shared_ptr<Pack const> TestPack;
	int Fallbacks = 0;
	void Report_Fallback(char const *, char const *) { ++Fallbacks; }
	std::shared_ptr<Pack const> Fetch(void const *) { return TestPack; }
	Variant const * Select_Variant(Pack const & p, std::uint32_t) { return &p.Variants[0]; }
	Frame const * Select_Frame(Variant const & v,std::uint32_t,std::uint32_t,std::uint32_t,int) { return &v.Frames[0]; }
}
struct IsoTileRecord {
	bool IsHasZData = true, IsHasExtraData = false;
	int X=0,Y=0,ExtraX=0,ExtraY=0,ExtraWidth=0,ExtraHeight=0,ExtraOffset=0,ExtraZOffset=0;
	int ZDataOffset=sizeof(IsoTileRecord)+576;
};
struct TilePayload { IsoTileRecord Record; std::array<unsigned char,576> Color, Depth; std::array<unsigned char,2> ExtraColor, ExtraDepth; };
struct IsoTileSet {
	TilePayload Data;
	IsoTileRecord const * Fetch_Record_Pointer(int) const { return &Data.Record; }
	int Pixel_Width() const { return 48; }
	int Pixel_Height() const { return 24; }
	int Tile_Count() const { return 1; }
};
#define ISO_DRAW_HEIGHT 23
#include "terrain_production.inc"
#include "alpha_production.inc"

void Check(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

void Check_Facing()
{
	for (auto mode : {RenderMode::Classic, RenderMode::HD}) {
		Settings.Mode = mode;
		int const count = Render_Voxel_Facing_Count();
		std::vector<bool> used(count);
		for (int raw = 0; raw <= 65535; ++raw) {
			int const facing = Render_Voxel_Facing(DirType(raw));
			int const expected = ((raw + 32768 / count) / (65536 / count)) % count;
			Check(facing == expected, "facing rounding differs from independent grid oracle");
			Check(std::abs(Render_Voxel_Radians(DirType(raw)) - (facing - count / 4) * -DEG_TO_RAD(360.0 / count)) < 1e-12, "transform and cache facing disagree");
			used[facing] = true;
		}
		Check(std::all_of(used.begin(), used.end(), [](bool b) { return b; }), "some facing buckets are never rasterized");
		std::vector<int> keys;
		for (int ramp = 0; ramp < 64; ++ramp) for (int facing = 0; facing < count; ++facing) for (int frame = 0; frame < 32; ++frame) {
			keys.push_back(Render_Voxel_Key(Render_Voxel_Key(ramp, facing, count), frame, 32));
		}
		std::sort(keys.begin(), keys.end());
		Check(std::adjacent_find(keys.begin(), keys.end()) == keys.end(), "ramp facing frame key collision");
	}
	Check(Render_Voxel_Key(-1, 0, 32) == -1 && Render_Voxel_Key(INT_MAX, 31, 32) == -1 && Render_Voxel_Key(5, 32, 32) == -1, "invalid keys must be uncached");
}

void Check_Terrain()
{
	constexpr int offsets[] = {0,4,12,24,40,60,84,112,144,180,220,264,312,356,396,432,464,492,516,536,552,564,572};
	for (int density = 1; density <= 4; ++density) {
		int pixels = 0;
		for (int row = 0; row < 23 * density; ++row) {
			auto span = Render_Terrain_Span(row, density);
			int const logical = row / density;
			Check(span.Source == offsets[logical], "terrain source offset changed");
			Check(span.First >= 0 && span.First * 2 + span.Count == 48 * density, "diamond is off center");
			pixels += span.Count;
			for (int col = 0; col < span.Count; ++col) Check(span.Source + col / density < 576, "terrain source overrun");
		}
		Check(pixels == 576 * density * density, "terrain coverage differs across scale");
		Check(Render_Terrain_Span(23 * density, density).Count == 0, "bottom guard row drawn");
	}
}

void Check_Cache()
{
	for (int density = 1; density <= 4; ++density) {
		Surface surface(256, 5, density);
		for (std::size_t i = 0; i < surface.Pixels.size(); ++i) surface.Pixels[i] = (i % 259 < 257) ? 0 : static_cast<unsigned char>(i % 255 + 1);
		StaticBufferClass cache(100000);
		SurfaceRegion region{Point2D(-12, 5), Rect(0, 0, 256, 5)};
		for (int repeat = 0; repeat < 2; ++repeat) {
			auto * entry = cache.Add(surface, region);
			Check(entry && entry->Density == density && entry->Width == 256 && entry->Height == 5 && entry->X == -12, "cache metadata");
			Check(reinterpret_cast<std::uintptr_t>(entry) % alignof(StaticBufferClass::Entry) == 0, "unaligned cache entry");
			auto const * data = entry->Data;
			for (int row = 0; row < 5 * density; ++row) {
				int const length = data[0] | data[1] << 8;
				std::vector<unsigned char> decoded;
				for (int i = 2; i < length;) {
					unsigned char const value = data[i++];
					if (value) decoded.push_back(value);
					else { int const run = data[i++]; decoded.insert(decoded.end(), run, 0); }
				}
				Check(decoded.size() == 256 * density, "RLE row width mismatch");
				Check(std::equal(decoded.begin(), decoded.end(), surface.Pixels.begin() + row * 256 * density), "RLE cache content mismatch");
				data += length;
			}
		}
		StaticBufferClass tiny(48);
		Check(tiny.Add(surface, region) == nullptr, "cache budget exceeded");
		Check(tiny.Add(surface, Rect(0,0,1,1), 0, 0) != nullptr, "failed reservation was not rolled back");
	}
}

void Check_Voxel_Projection()
{
	for (int density : {2,3,4}) {
		int const pitch = 256 * density;
		std::vector<unsigned char> pixels(pitch * pitch + 2, 77), depth(pitch * pitch + 2, 0);
		DenseVoxelRaster raster{density, pitch, pixels.data() + 1, depth.data() + 1};
		Stamp_Dense_Voxel(raster, Vector3(12.75f, 14.5f, 5), 23, 1);
		int const x = static_cast<int>(12.75f * density), y = static_cast<int>(14.5f * density);
		Check(raster.Pixels[y * pitch + x] == 23, "voxel projection lost fractional logical position");
		Check(raster.Pixels[y * pitch + 12 * density] == 77, "voxel raster was merely enlarged after logical truncation");
		VoxelDrawSystem::EnableZBuffer = true;
		Stamp_Dense_Voxel(raster, Vector3(12.75f, 14.5f, 10), 31, 1);
		Stamp_Dense_Voxel(raster, Vector3(12.75f, 14.5f, 9), 41, 1);
		Check(raster.Pixels[y * pitch + x] == 31 && raster.Depth[y * pitch + x] == 10, "voxel depth units or occlusion changed");
		Stamp_Dense_Voxel(raster, Vector3(-1, -1, 30), 1, 2);
		Stamp_Dense_Voxel(raster, Vector3(255.75f, 255.75f, 30), 2, 2);
		Check(pixels.front() == 77 && pixels.back() == 77 && depth.front() == 0 && depth.back() == 0, "voxel clip escaped scratch surface");
		VoxelDrawSystem::EnableZBuffer = false;
	}
}

void Check_Terrain_Composition()
{
	IsoTileSet tile;
	tile.Data.Color.fill(7);
	tile.Data.Depth.fill(9);
	LightConvertClass drawer;
	std::array<unsigned short,65536> translator{};
	translator[263] = 0x1234;
	for (int density : {2,3,4}) {
		Surface surface(80,80,density,2);
		ZRing.Density = ARing.Density = density;
		ZRing.Pixels.assign(80*80*density*density,65535);
		ARing.Pixels.assign(80*80*density*density,127);
		Draw_Terrain_Raster(tile,0,drawer,surface,10,20,Rect(0,0,80,80),2,1000,true,false,false,false,0,3,translator.data());
		int count = 0;
		auto const * pixels = reinterpret_cast<unsigned short const *>(surface.Pixels.data());
		for (std::size_t p = 0; p < ZRing.Pixels.size(); ++p) if (pixels[p]) { ++count; Check(pixels[p] == 0x1234 && ZRing.Pixels[p] == 32768-20-24-24+9,"classic fallback lighting or logical terrain depth changed"); }
		Check(count == 576*density*density,"terrain fallback coverage");
		auto pack = std::make_shared<HDAsset::Pack>();
		pack->Type = HDAsset::Kind::TERRAIN;
		pack->Variants.resize(1);
		auto & v = pack->Variants[0];
		v.Scale=density; v.LogicalWidth=48; v.LogicalHeight=24; v.LogicalFrames=1; v.Frames.resize(1);
		auto & f = v.Frames[0]; f.Width=48*density; f.Height=24*density;
		f.Color.assign(f.Width*f.Height*4,255); f.Depth.assign(f.Width*f.Height,-12);
		for(std::size_t p=0;p<f.Depth.size();++p) { f.Color[p*4]=(p%f.Width)&1 ? 255:0; f.Color[p*4+1]=0; f.Color[p*4+2]=255; }
		HDAsset::TestPack=pack;
		ZRing.Pixels.assign(ZRing.Pixels.size(),65535);
		Draw_Terrain_Raster(tile,0,drawer,surface,10,20,Rect(0,0,80,80),2,1000,true,false,false,false,0,3,translator.data());
		int const row=(20*density+11*density)*80*density+10*density;
		Check(pixels[row] == DSurface::Build_Hicolor_Pixel(0,0,255) && pixels[row+1] == DSurface::Build_Hicolor_Pixel(255,0,255),"HD terrain samples collapsed to logical pixels");
		Check(ZRing.Pixels[row] == 32768-20-24-24+12,"HD terrain depth sign or unit changed");
		Draw_Terrain_Raster(tile,0,drawer,surface,10,20,Rect(0,0,80,80),2,1000,true,false,false,true,0,3,translator.data());
		Check(ZRing.Pixels[row] == 0 && pixels[row+1] == (0x7bef & (DSurface::Build_Hicolor_Pixel(255,0,255)>>1)),"fog failed physical terrain pixel/depth update");
		tile.Data.Record.IsHasExtraData=true;
		tile.Data.Record.ExtraX=22; tile.Data.Record.ExtraY=11; tile.Data.Record.ExtraWidth=2; tile.Data.Record.ExtraHeight=1;
		tile.Data.Record.ExtraOffset=offsetof(TilePayload,ExtraColor); tile.Data.Record.ExtraZOffset=offsetof(TilePayload,ExtraDepth);
		tile.Data.ExtraColor.fill(8); tile.Data.ExtraDepth.fill(8); translator[264]=0xf800;
		int const overlap=row+22*density;
		constexpr unsigned short shadow_expected[3][3] = {{0xc50c,0xc50c,0xc50c},{0x6295,0x7b2f,0x93c9},{0x001f,0x3152,0x6286}};
		constexpr unsigned char coverages[] = {0,128,255};
		for (int a=0;a<3;++a) for (int s=0;s<3;++s) {
			f.Shadow.assign(f.Depth.size(),coverages[s]);
			for (std::size_t p=0;p<f.Depth.size();++p) f.Color[p*4+3]=coverages[a];
			std::fill(reinterpret_cast<unsigned short *>(surface.Pixels.data()),reinterpret_cast<unsigned short *>(surface.Pixels.data())+ZRing.Pixels.size(),0xc50c);
			ZRing.Pixels.assign(ZRing.Pixels.size(),65535);
			Draw_Terrain_Raster(tile,0,drawer,surface,10,20,Rect(0,0,80,80),2,1000,true,false,false,false,0,3,translator.data());
			Check(pixels[row] == shadow_expected[a][s],"terrain shadow mask must precede alpha blending against original destination");
			Check(pixels[overlap] == shadow_expected[a][s],"overlapping terrain extra imagery composed an HD sample twice");
			Check(ZRing.Pixels[row] == (a ? 32768-20-24-24+12 : 65535),"transparent terrain shadow changed depth");
		}
		v.LogicalWidth = 47;
		HDAsset::Fallbacks = 0;
		ZRing.Pixels.assign(ZRing.Pixels.size(),65535);
		Draw_Terrain_Raster(tile,0,drawer,surface,10,20,Rect(0,0,80,80),2,1000,true,false,false,false,0,3,translator.data());
		Check(HDAsset::Fallbacks == 1 && pixels[row] == 0x1234,"invalid terrain canvas did not diagnose and fall back");
		Check(pixels[overlap] == 0xf800,"classic terrain fallback lost its separate extra-image pass");
		HDAsset::TestPack.reset();
		tile.Data.Record.IsHasExtraData=false;
		ARing.Pixels.assign(ARing.Pixels.size(),127);
		std::array<unsigned char,4> shroud{0,254,63,127};
		Draw_Alpha_Raster(shroud.data(),2,0,0,5,5,7,7,false);
		for (int ry=0;ry<density;++ry) for (int rx=0;rx<density;++rx) {
			Check(ARing.Pixels[(5*density+ry)*80*density+5*density+rx] == 0,"shroud leaves exposed physical samples");
			Check(ARing.Pixels[(5*density+ry)*80*density+6*density+rx] == 127,"transparent shroud overwrites lighting");
		}
		Draw_Alpha_Raster(shroud.data(),2,0,0,5,5,7,7,true);
		Check(ARing.Pixels[6*density*80*density+5*density] == 0,"fog accumulation differs across density");
	}
}

int main()
{
	try { Check_Facing(); Check_Terrain(); Check_Cache(); Check_Voxel_Projection(); Check_Terrain_Composition(); std::cout << "Validated terrain composition, spans, facing/key agreement, physical projection and bounded voxel caches\n"; return 0; }
	catch (std::exception const & e) { std::cerr << e.what() << '\n'; return 1; }
}
