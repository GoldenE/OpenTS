/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rendercontext.hh"
#include "renderstage.hh"
#include "renderworld.hh"
#include "bsurface.h"
#include "dsurface.h"
#include "blit.h"
#include "blitblit.h"
#include "rlerle.h"
#include "face.h"
#include "globals.h"
#include "hdasset.hh"
#include "vector3.h"
#include "renderportable.h"
#include "draw.hh"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

int VideoModeWidth = 96, VideoModeHeight = 80;
void Video_Mark_Dirty() {}
const double RAD_60 = 60 * 0.017453292519943295;
const double CELL_LEPTON_DIAG = std::sqrt(256.0 * 256.0 * 2);
const int ISO_TILE_PIXEL_W = 48;
Rect TacticalRect(0,8,96,72);

// Only decoded metadata and asset lookup are adapted; every pixel writer is production code.
class ConvertClass {
public:
	virtual ~ConvertClass() = default;
	int IntensityLevels = 33;
	std::vector<unsigned short> Table = std::vector<unsigned short>(256 * 33);
	void * IntensityTranslator = Table.data();
	int Bytes_Per_Pixel() const { return 2; }
	int Get_Intensity_Levels() const { return IntensityLevels; }
	void const * Get_Intensity_Table() const { return Table.data(); }
};
class LightConvertClass : public ConvertClass {
public:
	bool UseIonLighting = false;
	int NormalRedTint=1000,NormalGreenTint=1000,NormalBlueTint=1000,IonRedTint=1000,IonGreenTint=1000,IonBlueTint=1000;
};
class ShapeSet {
public:
	std::array<unsigned char,64> Pixels{};
	int Get_Count() const { return 1; }
	int Get_Width() const { return 8; }
	int Get_Height() const { return 8; }
	Rect Get_Rect(int) const { return Rect(0,0,8,8); }
	bool Is_RLE_Compressed(int) const { return false; }
	void const * Get_Data(int) const { return Pixels.data(); }
};
struct IsoTileRecord {
	int X=0,Y=0,ExtraX=0,ExtraY=0,ExtraWidth=0,ExtraHeight=0,ExtraOffset=0,ExtraZOffset=0;
	bool IsHasZData=true,IsHasExtraData=false;
	int ZDataOffset=sizeof(IsoTileRecord)+576;
};
class IsoTileSet {
public:
	int Width=48,Height=24;
	struct Payload { IsoTileRecord Record; std::array<unsigned char,576> Color,Depth; } Data;
	int Pixel_Width() const { return Width; }
	int Pixel_Height() const { return Height; }
	int Tile_Count() const { return 1; }
	IsoTileRecord const * Fetch_Record_Pointer(int) const { return &Data.Record; }
};
class IsometricTileTypeClass {
public:
	IsoTileSet Set;
	int NumTileTypesInSet=1;
	IsometricTileTypeClass const * NextTileTypeInSet=this;
	void const * Get_Image_Data() const { return &Set; }
	void Draw_Tile(LightConvertClass *,int,Surface &,int,int,Rect,int,int,bool,int,bool,bool,bool,int) const;
};
enum {ISO_WIDTH=48,ISO_HEIGHT=24,ISO_DRAW_WIDTH=48,ISO_DRAW_HEIGHT=23};
struct SceneRules { bool IsBlendedFog=true; } SceneRule;
SceneRules * Rule=&SceneRule;
namespace VoxelDrawSystem { bool EnableZBuffer=false; }
namespace HDAsset {
	std::map<void const *,std::shared_ptr<Pack const>> SceneAssets;
	std::shared_ptr<Pack const> Fetch(void const * owner) { auto i=SceneAssets.find(owner); return i==SceneAssets.end()?nullptr:i->second; }
	void Report_Fallback(char const *,char const * reason) { throw std::runtime_error(reason); }
	void Forget(void const *) {}
}
#include "world_production.inc"
#include "dense_production.inc"
#include "alpha_production.inc"
#include "scene_terrain_production.inc"
#include "hdshape_under_test.inc"
#include "spotlight_production.inc"
#include "particle_production.inc"

static void Check(bool condition,char const * message) { if(!condition) throw std::runtime_error(message); }
using Image = std::vector<unsigned short>;
std::array<std::array<int,3>,256> Palette{};

static std::shared_ptr<HDAsset::Pack> Shape_Pack(ShapeSet const & shape,int density,bool ui)
{
	auto pack=std::make_shared<HDAsset::Pack>(); pack->Name=ui?"SCENEUI.SHP":"SCENEBODY.SHP"; pack->Type=ui?HDAsset::Kind::UI:HDAsset::Kind::SHAPE;
	HDAsset::Variant v; v.Scale=density; v.LogicalWidth=v.LogicalHeight=8; v.LogicalFrames=1;
	HDAsset::Frame f; f.Width=f.Height=8*density; f.Color.resize(f.Width*f.Height*4); f.Remap.resize(f.Width*f.Height); f.Shadow.resize(f.Width*f.Height);
	for(unsigned y=0;y<f.Height;++y) for(unsigned x=0;x<f.Width;++x) {
		unsigned p=y*f.Width+x,index=shape.Pixels[(y/density)*8+x/density];
		for(unsigned c=0;c<3;++c) f.Color[p*4+c]=Palette[index][c];
		f.Color[p*4+3]=index?255:0;
		if(index==16) f.Remap[p]=1;
		if(index && density>1 && (x%density)) f.Color[p*4+2]=208;
		if(!ui && index && y>=4*density) {f.Color[p*4+3]=128; f.Shadow[p]=128;}
	}
	v.Frames.push_back(std::move(f)); pack->Variants.push_back(std::move(v)); return pack;
}

static void Draw_Scene_Shape(Surface & surface,LightConvertClass & converter,ShapeSet const & shape,Point2D point,int z,bool ui)
{
	ShapeFlags_Type flags=ui?SHAPE_NORMAL:ShapeFlags_Type(SHAPE_ALPHA|SHAPE_ZWRITE);
	if(HD_Try_Draw_Shape(surface,converter,&shape,0,point,surface.Get_Rect(),flags,nullptr,z,ZGRAD_GROUND,1000,nullptr,0,Point2D())) return;
	BSurface source(8,8,1,const_cast<unsigned char *>(shape.Pixels.data()));
	if(ui) Bit_Blit(surface,Rect(point,8,8),source,source.Get_Rect(),BlitTransXlat<unsigned short>(converter.Table.data()+16*256));
	else Bit_Blit(surface,Rect(point,8,8),source,source.Get_Rect(),BlitTransXlatAlphaZReadWrite<unsigned short>(converter.Table.data(),33),z,ZGRAD_GROUND,1000);
}

static Image Render_Scene(int density,bool mixed,bool panned=false)
{
	Check(Set_Render_Settings({density==1 && !mixed?RenderMode::Classic:RenderMode::HD,density,density,density,64u*1024u*1024u}),"scene settings");
	BSurface surface(96,80,2,nullptr,density,RenderDomain::World); surface.Fill(0x0841);
	ZBuffer depth(TacticalRect,density); ABuffer alpha(TacticalRect,density); DepthBuffer=&depth; AlphaBuffer=&alpha;
	if(panned){depth.Pan(3,-2,65535);alpha.Pan(3,-2,127);}
	LightConvertClass converter;
	for(int level=0;level<33;++level) for(int color=0;color<256;++color) {
		auto c=Palette[color]; converter.Table[level*256+color]=DSurface::Build_Hicolor_Pixel(std::min(255,c[0]*level/16),std::min(255,c[1]*level/16),std::min(255,c[2]*level/16));
	}
	std::array<unsigned char,96*80> mask; mask.fill(127);
	for(int y=0;y<80;++y) for(int x=0;x<96;++x) if(x<8) mask[y*96+x]=0; else if(x>=72) mask[y*96+x]=63;
	Draw_Alpha_Raster(mask.data(),96,0,8,0,8,96,80,false);
	IsometricTileTypeClass tile; tile.Set.Data.Color.fill(7); tile.Set.Data.Depth.fill(8);
	HDAsset::SceneAssets.clear();
	if(mixed) {
		auto pack=std::make_shared<HDAsset::Pack>(); pack->Name="SCENE.TMP";pack->Type=HDAsset::Kind::TERRAIN;
		HDAsset::Variant v;v.Scale=density;v.LogicalWidth=48;v.LogicalHeight=24;v.LogicalFrames=1;
		HDAsset::Frame f;f.Width=48*density;f.Height=24*density;f.Color.resize(f.Width*f.Height*4);f.Depth.assign(f.Width*f.Height,-8);
		for(unsigned p=0;p<f.Width*f.Height;++p) {f.Color[p*4]=64;f.Color[p*4+1]=(p%f.Width)%density?112:96;f.Color[p*4+2]=128;f.Color[p*4+3]=255;}
		v.Frames.push_back(std::move(f));pack->Variants.push_back(std::move(v));HDAsset::SceneAssets[&tile.Set]=pack;
	}
	for(Point2D p : {Point2D(-12,32),Point2D(12,20),Point2D(36,32),Point2D(60,20),Point2D(12,44),Point2D(36,56)}) tile.Draw_Tile(&converter,0,surface,p.X,p.Y,TacticalRect,0,1000,true,0,false,false,false,0);
	ShapeSet body,behind,ui,legacy;
	for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
		body.Pixels[y*8+x]=(x==0||x==7||y==0||y==7)?0:(x<4?16:5);
		behind.Pixels[y*8+x]=3;ui.Pixels[y*8+x]=(x+y)%2?2:5;legacy.Pixels[y*8+x]=4;
	}
	if(mixed) {HDAsset::SceneAssets[&body]=Shape_Pack(body,density,false);HDAsset::SceneAssets[&ui]=Shape_Pack(ui,density,true);}
	Draw_Scene_Shape(surface,converter,behind,Point2D(34,33),24,false);
	Draw_Scene_Shape(surface,converter,body,Point2D(32,30),-48,false);
	Draw_Scene_Shape(surface,converter,legacy,Point2D(52,43),-48,false);
	BSurface voxel(16,16,1,nullptr,density);voxel.Fill(0);std::vector<unsigned char> voxel_depth(16*16*density*density,0);
	DenseVoxelRaster raster{density,16*density,static_cast<unsigned char *>(voxel.Lock()),voxel_depth.data()};
	Stamp_Dense_Voxel(raster,Vector3(mixed?4.5f:4.0f,4,32),4,2);Stamp_Dense_Voxel(raster,Vector3(6,5,32),5,2);voxel.Unlock();
	Bit_Blit(surface,Rect(58,36,16,16),voxel,voxel.Get_Rect(),BlitTransXlatAlphaZReadWrite<unsigned short>(converter.Table.data(),33),-64,ZGRAD_GROUND,1000);
	std::array<unsigned char,24> spotlight_mask{0,32,64,64,32,0,32,64,128,128,64,32,32,64,128,128,64,32,0,32,64,64,32,0};
	BSurface spotlight(6,4,1,spotlight_mask.data());
	Draw_Spotlight_Raster(surface,spotlight,Rect(44,29,6,4),TacticalRect,nullptr);
	std::vector<int> spotlight_lookup(65536);
	for(unsigned pixel=0;pixel<65536;++pixel) {auto c=DSurface::Deconstruct_Hicolor_Pixel(pixel);spotlight_lookup[pixel]=(c.Get_Red()<<16)|(c.Get_Green()<<8)|c.Get_Blue();}
	Draw_Spotlight_Raster(surface,spotlight,Rect(50,29,6,4),TacticalRect,spotlight_lookup.data());
	BSurface interface(96,80,2,nullptr,density,RenderDomain::UI);interface.Fill(0);
	{
		ScopedScreenSpace scope;interface.Fill_Rect(Rect(0,0,96,8),0x2104);interface.Draw_Rect(Rect(1,1,94,6),0xffff);
		Draw_Scene_Shape(interface,converter,ui,Point2D(84,0),0,true);
	}
	Check(surface.Blit_From(interface,true),"scene UI composition");
	RasterSurfaceView view(surface); Image image(96*80*density*density);
	for(int y=0;y<80*density;++y) for(int x=0;x<96*density;++x) image[y*96*density+x]=static_cast<unsigned short>(view.Get_Pixel(Point2D(x,y)));
	Check(image[2*density*96*density+2*density]==0x2104,"UI layout changed with density");
	Check(image[31*density*96*density+34*density]!=0xf800,"rear sprite bypassed terrain depth");
	Check(image[44*density*96*density+54*density]==converter.Table[16*256+4],"legacy fallback sprite misplaced in mixed scene");
	Check(image[40*density*96*density+63*density]==converter.Table[16*256+4],"voxel projection or composition displaced from world");
	Check(image[38*density*96*density+46*density]==0x6498,"scalar spotlight did not brighten the physical ground pool");
	Check(image[38*density*96*density+52*density]==0x6498,"lookup spotlight did not brighten the physical ground pool");
	DepthBuffer=nullptr;AlphaBuffer=nullptr;HDAsset::SceneAssets.clear();return image;
}

static std::uint64_t Hash(Image const & image)
{
	std::uint64_t hash=14695981039346656037ull;
	for(auto pixel:image) for(unsigned shift:{0u,8u}) {hash^=(pixel>>shift)&255;hash*=1099511628211ull;}return hash;
}

static void Check_Particle_Occlusion()
{
	Set_Render_Settings({RenderMode::HD,2,2,2,64u*1024u*1024u});
	BSurface surface(96,80,2,nullptr,2,RenderDomain::World);surface.Fill(0x1234);
	ZBuffer depth(TacticalRect,2);ABuffer alpha(TacticalRect,2);DepthBuffer=&depth;AlphaBuffer=&alpha;depth.Fill(200);
	*reinterpret_cast<unsigned short *>(depth.Get_Raster_Offset(Point2D(40,24)))=100;
	*reinterpret_cast<unsigned short *>(alpha.Get_Raster_Offset(Point2D(41,24)))=0;
	*reinterpret_cast<unsigned short *>(alpha.Get_Raster_Offset(Point2D(40,25)))=63;
	Draw_Particle_Raster(surface,Point2D(20,20),TacticalRect,100,RGBClass(128,64,32));
	RasterSurfaceView raster(surface);
	Check(raster.Get_Pixel(Point2D(40,40))==0x1234 && raster.Get_Pixel(Point2D(41,40))==0x1234,"particle ignored per-sample depth or zero alpha");
	Check(raster.Get_Pixel(Point2D(40,41))==0x38e1 && raster.Get_Pixel(Point2D(41,41))==0x8204,"particle did not preserve partial physical-block visibility and lighting");
	Check(raster.Get_Pixel(Point2D(42,41))==0x1234,"particle raster escaped its logical footprint");
	Check(*reinterpret_cast<unsigned short *>(depth.Get_Raster_Offset(Point2D(41,25)))==200,"particle changed read-only depth");
	DepthBuffer=nullptr;AlphaBuffer=nullptr;
}

static void Export(std::filesystem::path const & path,Image const & image,int density)
{
	int width=96*density,height=80*density;std::ofstream file(path,std::ios::binary);
	auto word=[&](std::uint32_t value,int n){while(n--){file.put(static_cast<char>(value));value>>=8;}};
	word(0x4d42,2);word(54+width*height*3,4);word(0,4);word(54,4);word(40,4);word(width,4);word(-height,4);word(1,2);word(24,2);word(0,4);word(width*height*3,4);word(0,4);word(0,4);word(0,4);word(0,4);
	for(auto pixel:image){auto c=DSurface::Deconstruct_Hicolor_Pixel(pixel);file.put(c.Get_Blue());file.put(c.Get_Green());file.put(c.Get_Red());}
	Check(static_cast<bool>(file),"scene export failed");
}

int main(int argc,char ** argv)
{
	try {
		std::unique_ptr<DSurface> primary(DSurface::Create_Primary());Check(primary!=nullptr,"pixel format setup");
		Palette[2]={0,192,0};Palette[3]={255,0,0};Palette[4]={224,192,32};Palette[5]={32,160,224};Palette[7]={64,96,128};Palette[16]={224,96,32};
		Image classic=Render_Scene(1,false);
		constexpr std::uint64_t fallback_hashes[]={0xe0e6111cca4aa875ull,0xdeb1e1889e90d365ull,0xd3f405f4d101fbc5ull,0x345f8f7bad518f25ull};
		constexpr std::uint64_t mixed_hashes[]={0x1b2f635f4b693b36ull,0xbdd340ec7519085dull,0x4fbaa74dffde5cbaull,0x0ff685995875ac65ull};
		for(int density=1;density<=4;++density) {
			Image fallback=Render_Scene(density,false),mixed=Render_Scene(density,true);
			Check(Render_Scene(density,false,true)==fallback && Render_Scene(density,true,true)==mixed,"assembled scene changed when alpha/depth rings wrapped after pan");
			if(argc>1){std::filesystem::path output(argv[1]);std::filesystem::create_directories(output);Export(output/("fallback-"+std::to_string(density)+".bmp"),fallback,density);Export(output/("mixed-"+std::to_string(density)+".bmp"),mixed,density);}
			int mismatches=0;
			for(int y=0;y<80*density;++y) for(int x=0;x<96*density;++x) if(fallback[y*96*density+x]!=classic[(y/density)*96+x/density]) {
				if(mismatches++<8) std::cerr<<"fallback mismatch scale="<<density<<" pixel="<<x<<','<<y<<" expected="<<std::hex<<classic[(y/density)*96+x/density]<<" actual="<<fallback[y*96*density+x]<<std::dec<<'\n';
			}
			Check(!mismatches,"assembled Classic/fallback image differs across density");
			Check(Hash(fallback)==fallback_hashes[density-1] && Hash(mixed)==mixed_hashes[density-1],"assembled scene image hash changed");
			Image altered=mixed;altered[altered.size()/2]^=1;Check(Hash(altered)!=mixed_hashes[density-1],"scene hash negative control did not detect a changed sample");
			std::cout<<"scale="<<density<<" fallback="<<std::hex<<Hash(fallback)<<" mixed="<<Hash(mixed)<<std::dec<<'\n';
		}
		Check_Particle_Occlusion();
		return 0;
	}catch(std::exception const & e){std::cerr<<e.what()<<'\n';return 1;}
}
