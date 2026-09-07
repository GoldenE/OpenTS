/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#include <algorithm>
#include <array>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Point2D {
	int X, Y;
	Point2D(int x = 0, int y = 0) : X(x), Y(y) {}
};
struct Rect {
	int X, Y, Width, Height;
	Rect(int x = 0, int y = 0, int width = 0, int height = 0) : X(x), Y(y), Width(width), Height(height) {}
};

class Surface {
public:
	Surface(int width, int height, int bpp) : Width(width), Height(height), Pixels(width * height + 2, 0xcafe)
	{
		if (bpp != 2) throw std::runtime_error("Unexpected ring buffer pixel format");
	}
	virtual ~Surface() = default;
	void * Lock(Point2D point = {}) { return Pixels.data() + 1 + point.Y * Width + point.X; }
	void Unlock() {}
	int Stride() const { return Width * 2; }
	bool Fill_Rect(Rect rect, unsigned short color)
	{
		for (int y = std::max(0, rect.Y); y < std::min(Height, rect.Y + rect.Height); ++y) {
			for (int x = std::max(0, rect.X); x < std::min(Width, rect.X + rect.Width); ++x) Pixels[1 + y * Width + x] = color;
		}
		return true;
	}
	void Check_Guards() const
	{
		if (Pixels.front() != 0xcafe || Pixels.back() != 0xcafe) throw std::runtime_error("Ring buffer write escaped allocation");
	}
private:
	int Width, Height;
	std::vector<unsigned short> Pixels;
};
class BSurface : public Surface { public: using Surface::Surface; };

#include "abuffer_production.inc"
#include "zbuffer_production.inc"

ABuffer * AlphaBuffer = nullptr;
ZBuffer * DepthBuffer = nullptr;

void Check(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

template<class Buffer> void Exercise(unsigned short neutral)
{
	Buffer buffer(Rect(0, 0, 8, 6));
	auto * surface = buffer.Get_Surface();
	auto * storage = static_cast<unsigned short *>(surface->Lock());
	std::uintptr_t start = reinterpret_cast<std::uintptr_t>(storage);
	surface->Unlock();
	if constexpr (sizeof(void *) == 8) Check(start > UINT32_MAX, "x64 test requires an allocated surface above 4 GiB");
	Check(buffer.Get_Buffer_End() == reinterpret_cast<std::uintptr_t>(storage + 48), "end address was truncated");
	Check(buffer.Get_Buffer_Offset(Point2D(7, 5)) == reinterpret_cast<std::uintptr_t>(storage + 47), "initial last pixel address");
	Check(buffer.Wrap_Overflow(start + 96) == start, "overflow at exact end");
	Check(buffer.Wrap_Overflow(start + 100) == start + 4, "overflow past end");
	Check(buffer.Wrap_Underflow(start - 2) == start + 94, "underflow one pixel");
	Check(buffer.Wrap_Underflow(start - 96) == start, "underflow one full buffer");
	Check(buffer.Wrap_Overflow(start + 94) == start + 94, "last pixel remains unchanged");
	Check(buffer.Wrap_Underflow(start) == start, "first pixel remains unchanged");
	for (int index = 0; index < 48; ++index) Check(storage[index] == neutral, "constructor neutral fill");

	for (int index = 0; index < 48; ++index) storage[index] = static_cast<unsigned short>(1000 + index);
	buffer.Pan(2, 0, 77);
	Check(buffer.Get_Buffer_Offset(Point2D()) == start + 4, "positive horizontal pan");
	Check(storage[0] == 77 && storage[1] == 77 && storage[2] == 1002 && storage[41] == 77, "horizontal exposed columns");
	buffer.Pan(-3, 0, 88);
	Check(buffer.Get_Buffer_Offset(Point2D()) == start + 94, "negative horizontal pan underflow");
	Check(buffer.Get_Buffer_Offset(Point2D(1, 0)) == start, "pixel lookup wraps after horizontal pan");
	buffer.Pan(0, 1, 99);
	Check(buffer.Get_Buffer_Offset(Point2D()) == start + 14, "positive vertical pan overflow");
	Check(buffer.Get_Scroll() == 32767, "positive vertical scroll bias");
	Check(storage[0] == 99 && storage[6] == 99, "vertical exposed rows across end");
	Check(storage[47] == 88, "legacy aligned single-pixel Set leaves value unchanged");
	buffer.Pan(0, -2, 111);
	Check(buffer.Get_Buffer_Offset(Point2D()) == start + 78, "negative vertical pan underflow");
	Check(buffer.Get_Scroll() == 32769, "negative vertical scroll bias");

	BSurface copy(8, 6, 2);
	buffer.Copy_To(&copy, Rect(0, 0, 8, 6));
	auto * copied = static_cast<unsigned short *>(copy.Lock());
	for (int index = 0; index < 9; ++index) Check(copied[index] == storage[39 + index], "copy tail before ring wrap");
	for (int index = 9; index < 48; ++index) Check(copied[index] == storage[index - 9], "copy head after ring wrap");
	copy.Unlock();
	copy.Check_Guards();
	buffer.Pan(9, 0, 222);
	Check(buffer.Get_Buffer_Offset(Point2D()) == start + 78, "large pan retains ring origin");
	Check(buffer.Get_Scroll() == 32768, "large pan resets scroll bias");
	for (int index = 0; index < 48; ++index) Check(storage[index] == 222, "large pan clears complete buffer");
	surface->Check_Guards();
	std::cout << "Validated native ring addresses at " << std::hex << start << std::dec << '\n';
}

template<class Buffer> void Exercise_Density(unsigned short neutral)
{
	for (Rect invalid : {Rect(0,0,INT_MAX,INT_MAX),Rect(0,0,INT_MAX,1),Rect(0,0,1,INT_MAX),Rect(0,0,4097,1)}) {
		bool rejected = false;
		try { Buffer oversized(invalid,4); } catch (std::invalid_argument const &) { rejected = true; }
		Check(rejected,"oversized raster ring was not rejected before dimension multiplication");
	}
	for (int density : {2, 3, 4}) {
		Buffer buffer(Rect(0, 0, 8, 6), density);
		int const width = 8 * density;
		int const count = width * 6 * density;
		auto * surface = buffer.Get_Surface();
		auto * data = static_cast<unsigned short *>(surface->Lock());
		std::uintptr_t const start = reinterpret_cast<std::uintptr_t>(data);
		surface->Unlock();
		Check(buffer.Get_Buffer_End() == start + count * 2, "dense ring extent");
		Check(buffer.Get_Buffer_Offset(Point2D(7, 5)) == start + (5 * density * width + 7 * density) * 2, "logical addressing scales both axes");
		for (int n = 0; n < count; ++n) Check(data[n] == neutral, "dense constructor fill");
		buffer.Pan(2, 1, 19);
		int origin = (2 * density + width * density) % count;
		Check(buffer.Get_Raster_Offset(Point2D()) == start + origin * 2, "dense pan offset");
		Check(buffer.Get_Scroll() == 32767, "dense pan preserves logical depth bias");
		buffer.Pan(-3, -2, 29);
		origin = (origin - 3 * density - 2 * density * width + count) % count;
		Check(buffer.Get_Raster_Offset(Point2D()) == start + origin * 2, "dense negative pan wraps");
		Check(buffer.Get_Scroll() == 32769, "dense negative bias remains logical");
		for (int n = 0; n < count; ++n) data[n] = static_cast<unsigned short>(n + 1);
		BSurface copy(width, 6 * density, 2);
		buffer.Copy_To(&copy, Rect(0, 0, 8, 6));
		auto const * copied = static_cast<unsigned short const *>(copy.Lock());
		for (int n = 0; n < count; ++n) Check(copied[n] == data[(origin + n) % count], "dense unwrapped copy");
		copy.Unlock();
		copy.Check_Guards();
		surface->Check_Guards();
	}
}

int main()
{
	try {
		Exercise<ABuffer>(0x007f);
		Exercise<ZBuffer>(0xffff);
		Exercise_Density<ABuffer>(0x007f);
		Exercise_Density<ZBuffer>(0xffff);
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
