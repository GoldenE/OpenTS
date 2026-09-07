/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 */
#include "always.h"
#include "rastercache.hh"
#include "rendercontext.hh"
#include "bsurface.h"
#include "blit.h"
#include "rlerle.h"
#include <stdexcept>

namespace {
void Check_Cache(bool condition, char const * description)
{
	if (!condition) throw std::runtime_error(description);
}
}

void Run_Render_Cache_Tests()
{
	std::vector<std::uint8_t> encoded{6, 0, 1, 0, 1, 2, 6, 0, 0, 1, 3, 4};
	RasterAssetKey key;
	key.Generation = 1; key.Digest[0] = 9; key.Frame = 3; key.Width = 3; key.Height = 2; key.Density = 2; key.RLE = true;
	RasterAssetCache cache(4096);
	cache.Configure(4096, 1);
	auto first = cache.Fetch(key, encoded);
	Check_Cache(first && first->Width == 6 && first->SourceRows == 2, "raster cache dimensions");
	Check_Cache(first->Pixels == std::vector<std::uint8_t>({1, 1, 0, 0, 2, 2, 0, 0, 3, 3, 4, 4}), "cached scaled row pixels");
	auto repeated = cache.Fetch(key, encoded);
	Check_Cache(first == repeated && cache.Hits() == 1 && cache.Builds() == 1, "immutable frame was decoded again");
	Check_Cache(cache.Bytes() <= 4096, "raster cache exceeded budget");
	encoded[2] = 7;
	++key.Generation;
	auto replaced = cache.Fetch(key, encoded);
	Check_Cache(replaced && replaced->Pixels[0] == 7 && first->Pixels[0] == 1, "source replacement reused stale pixels or borrowed storage");
	cache.Configure(4096, 2);
	Check_Cache(!cache.Fetch(key, encoded), "invalidated pinned entry was reused");
	replaced.reset();
	auto renewed = cache.Fetch(key, encoded);
	Check_Cache(renewed && renewed->Pixels[0] == 7, "invalidated source did not rebuild");
	first.reset(); repeated.reset(); renewed.reset();
	cache.Configure(1, 3);
	Check_Cache(cache.Bytes() == 0 && !cache.Fetch(key, encoded), "cache budget reduction was ignored");
	cache.Configure(4096, 3);
	auto malformed = encoded; malformed[0] = 255;
	Check_Cache(!cache.Fetch(key, malformed), "truncated RLE row entered cache");
	auto huge = key; huge.Width = 4097;
	Check_Cache(!cache.Fetch(huge, encoded), "oversize cache input accepted");
	RasterAssetCache pinned(4096);
	auto pin = pinned.Fetch(key, encoded);
	Check_Cache(pin != nullptr, "pinned cache setup");
	std::size_t budget = pinned.Bytes();
	pinned.Configure(budget, 0);
	auto other = key; ++other.Generation;
	Check_Cache(!pinned.Fetch(other, encoded) && pinned.Bytes() == budget, "pinned frame was evicted or budget exceeded");
	pin.reset();
	Check_Cache(pinned.Fetch(other, encoded) != nullptr, "released frame could not be evicted");
	RasterAssetCache matrix(8192);
	for (int source_scale = 1; source_scale <= 4; ++source_scale) for (int density = 1; density <= 4; ++density) {
		RasterAssetKey sample_key = key;
		sample_key.Generation = 1000 + source_scale * 10 + density;
		sample_key.SourceDensity = source_scale; sample_key.Density = density;
		int source_width = sample_key.Width * source_scale;
		int rows = sample_key.Height * source_scale;
		std::vector<std::uint8_t> input_rows;
		for (int y = 0; y < rows; ++y) {
			std::size_t start = input_rows.size(); input_rows.insert(input_rows.end(), {0, 0});
			for (int x = 0; x < source_width; ++x) { auto value = std::uint8_t((x + y) % 5); input_rows.push_back(value); if (!value) input_rows.push_back(1); }
			input_rows[start] = static_cast<std::uint8_t>(input_rows.size() - start);
		}
		auto frame = matrix.Fetch(sample_key, input_rows);
		Check_Cache(frame != nullptr, "density matrix frame rejected");
		for (int y = 0; y < rows; ++y) for (int x = 0; x < sample_key.Width * density; ++x) Check_Cache(frame->Pixels[y * frame->Width + x] == (x * source_scale / density + y) % 5, "source/target density changed indexed samples");
		Check_Cache(matrix.Bytes() <= 8192, "density matrix cache exceeded budget");
	}
	RasterAssetCache limited(8u * 1024u * 1024u);
	RasterAssetKey tiny = key; tiny.Width = tiny.Height = tiny.Density = 1;
	std::array<std::uint8_t, 3> tiny_data{3, 0, 1};
	for (int i = 0; i < 4100; ++i) { tiny.Generation = i + 1; Check_Cache(limited.Fetch(tiny, tiny_data) != nullptr, "tiny cache frame rejected"); }
	Check_Cache(limited.Entries() == 4096 && limited.Bytes() <= 8u * 1024u * 1024u, "cache entry-count limit ignored");

	unsigned short palette[256]; for (int i = 0; i < 256; ++i) palette[i] = static_cast<unsigned short>(i * 17);
	BSurface input(3, 2, 1, encoded.data()), output(6, 4, 2, nullptr, 2), reference(6, 4, 2, nullptr, 2);
	HDAsset::Source origin; origin.Generation = 500; origin.Content[0] = 27;
	RLEBlitTransXlat<unsigned short> blitter(palette);
	output.Fill(0xaaaa); reference.Fill(0xaaaa);
	RLE_Blit(reference, Rect(-1, 1, 3, 2), input, input.Get_Rect(), blitter);
	std::uint64_t hits = Raster_Asset_Cache_Hits();
	{
		ScopedRasterAsset scope(&origin, 9, encoded.data(), encoded.size(), 77);
		RLE_Blit(output, Rect(-1, 1, 3, 2), input, input.Get_Rect(), blitter);
		RLE_Blit(output, Rect(-1, 1, 3, 2), input, input.Get_Rect(), blitter);
	}
	Check_Cache(Raster_Asset_Cache_Hits() > hits, "shape dispatch did not reuse registered source");
	RasterSurfaceView actual(output), expected(reference);
	for (int y = 0; y < actual.Get_Height(); ++y) for (int x = 0; x < actual.Get_Width(); ++x) Check_Cache(actual.Get_Pixel(Point2D(x, y)) == expected.Get_Pixel(Point2D(x, y)), "cached clipping or transparency diverged from uncached raster");
	encoded[2] = 11;
	RLE_Blit(output, Rect(1, 1, 3, 2), input, input.Get_Rect(), blitter);
	Check_Cache(actual.Get_Pixel(Point2D(2, 2)) == 11 * 17, "mutable source retained cached pixels outside owner scope");
	++origin.Generation;
	{
		ScopedRasterAsset scope(&origin, 9, encoded.data(), encoded.size(), 77);
		RLE_Blit(output, Rect(1, 1, 3, 2), input, input.Get_Rect(), blitter);
	}
	Check_Cache(actual.Get_Pixel(Point2D(2, 2)) == 11 * 17, "new source generation did not reach draw");
	unsigned char remap[256]; for (int i = 0; i < 256; ++i) remap[i] = static_cast<unsigned char>(i);
	{
		ScopedRasterAsset scope(&origin, 9, encoded.data(), encoded.size(), 77);
		RLEBlitTransRemapXlat<unsigned short> remapped(remap, palette);
		remap[11] = 12;
		RLE_Blit(output, Rect(1, 1, 3, 2), input, input.Get_Rect(), remapped);
		Check_Cache(actual.Get_Pixel(Point2D(2, 2)) == 12 * 17, "cache captured translated house colors");
		remap[11] = 13;
		RLE_Blit(output, Rect(1, 1, 3, 2), input, input.Get_Rect(), remapped);
		Check_Cache(actual.Get_Pixel(Point2D(2, 2)) == 13 * 17, "cached pixels ignored changing remap table");
	}
	{
		RasterWorkLease outer; outer.Buffers().Scan.assign(8, 0x42);
		{ RasterWorkLease inner; inner.Buffers().Scan.assign(8, 0x11); }
		Check_Cache(outer.Buffers().Scan[0] == 0x42, "nested raster scratch overwrote active draw");
	}
	BSurface * saved;
	{ RasterSurfaceLease first_surface(7, 5, 2, 2); saved = &first_surface.Get(); }
	{
		RasterSurfaceLease reused(7, 5, 2, 2);
		Check_Cache(&reused.Get() == saved, "UI scratch surface allocation was not reused");
		RasterSurfaceLease nested(7, 5, 2, 2);
		Check_Cache(&nested.Get() != saved, "nested UI draw borrowed active scratch");
	}
	Check_Cache(Raster_Surface_Scratch_Bytes() <= std::min<std::size_t>(16u * 1024u * 1024u, Get_Render_Settings().CacheBudgetBytes / 16), "UI scratch pool exceeded retained limit");
}
