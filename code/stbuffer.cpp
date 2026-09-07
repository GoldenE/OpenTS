/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "always.h"

#include "stbuffer.h"

#include "rle.h"
#include "surface.h"

#include <cstring>
#include <vector>


/// <summary>
/// Creates a static buffer of the capacity requested.
/// This routine will allocate the whole block up front. Space is only ever handed out from
/// the front of it, since individual entries are never released -- Reset is what reclaims
/// the buffer for reuse.
/// </summary>
/// <param name="size">The capacity of the buffer, in bytes.</param>
StaticBufferClass::StaticBufferClass(int size)
{
	Size = size;
	Buffer = new unsigned char[size];
	Cursor = Buffer;
}


/// <summary>
/// Frees the memory held by the static buffer.
/// Every entry handed out by Add points into this one block, so none of them outlive the
/// buffer they were cached in.
/// </summary>
StaticBufferClass::~StaticBufferClass(void)
{
	if (Buffer != NULL) {
		delete [] Buffer;
	}
}



/// <summary>
/// Adds a compressed copy of a surface region to the buffer.
/// This routine is used by the voxel drawing cache to stash a freshly rendered voxel so
/// that later frames can blit the image back instead of rendering it again. The image is
/// run length compressed as it is copied out of the surface.
/// </summary>
/// <param name="surface">The surface to capture the image from.</param>
/// <param name="region">The area of the surface to capture, along with the draw offset to
/// remember with it.</param>
/// <returns>Returns with a pointer to the entry describing the cached image. Otherwise,
/// NULL is returned if the buffer has no room left.</returns>
StaticBufferClass::Entry * StaticBufferClass::Add(Surface & surface, SurfaceRegion const & region)
{
	return Add(surface, region.Bounds, region.Point.X, region.Point.Y);
}


/// <summary>
/// Adds a compressed copy of a clipped surface rectangle to the buffer.
/// This routine serves the same purpose as the region flavor above, except that the caller
/// states the draw offset to remember with the image rather than taking it from a surface
/// region.
/// </summary>
/// <param name="surface">The surface to capture the image from.</param>
/// <param name="cliprect">The area of the surface to capture.</param>
/// <param name="x">The horizontal draw offset to remember with the cached image.</param>
/// <param name="y">The vertical draw offset to remember with the cached image.</param>
/// <returns>Returns with a pointer to the entry describing the cached image. Otherwise,
/// NULL is returned if the buffer has no room left.</returns>
StaticBufferClass::Entry * StaticBufferClass::Add(Surface & surface, Rect const & cliprect, short x, short y)
{
	if (surface.Bytes_Per_Pixel() != 1 || cliprect.Width <= 0 || cliprect.Height <= 0 || cliprect.X < 0 || cliprect.Y < 0 ||
		cliprect.X + cliprect.Width > surface.Get_Width() || cliprect.Y + cliprect.Height > surface.Get_Height()) return nullptr;
	unsigned char * const previous = Cursor;
	std::size_t const padding = (alignof(Entry) - reinterpret_cast<std::uintptr_t>(Cursor) % alignof(Entry)) % alignof(Entry);
	if (!Reserve(static_cast<int>(padding))) return nullptr;
	Entry * header = (Entry *)Reserve(sizeof(Entry));
	if (header == NULL) {
		Cursor = previous;
		return(NULL);
	}

	header->X = x;
	header->Y = y;
	header->Width = cliprect.Width;
	header->Height = cliprect.Height;
	header->Density = surface.Get_Raster_Scale();
	header->Data = Cursor;

	int const density = header->Density;
	int const width = cliprect.Width * density;
	int const height = cliprect.Height * density;
	std::vector<unsigned char> compression(static_cast<std::size_t>(width) * 2 + 16);
	unsigned char * data = (unsigned char *)surface.Lock(Point2D(cliprect.X * density, cliprect.Y * density));
	if (!data) { Cursor = previous; return nullptr; }

	int line = 0;
	while (line < height) {
		int comp_size = 2;
		for (int column = 0; column < width;) {
			if (data[column]) {
				compression[comp_size++] = data[column++];
			} else {
				int run = 0;
				while (column + run < width && run < 255 && data[column + run] == 0) ++run;
				compression[comp_size++] = 0;
				compression[comp_size++] = static_cast<unsigned char>(run);
				column += run;
			}
		}
		compression[0] = static_cast<unsigned char>(comp_size);
		compression[1] = static_cast<unsigned char>(comp_size >> 8);
		unsigned char * buffer = Reserve(comp_size);
		if (buffer == NULL) {
			surface.Unlock();
			Cursor = previous;
			return(NULL);
		}

		memcpy(buffer, compression.data(), comp_size);
		line++;
		data += surface.Stride();
	}

	surface.Unlock();
	return(header);
}
