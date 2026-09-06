/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/
#include <objidl.h>
#include "cstream.h"
#include "lzo.h"
#include "lzopipe.h"
#include "lzostraw.h"
#include "legacy_cstream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <new>
#include <span>
#include <stdexcept>
#include <vector>

ULONG COMRefCount = 0;
extern "C" const IID IID_ILinkStream = __uuidof(ILinkStream);

namespace {
std::map<void *, void *> & Allocations()
{
	static auto * entries = new std::map<void *, void *>;
	return *entries;
}
}

// Place each production array allocation immediately before an inaccessible page.
void * operator new[](std::size_t size)
{
	std::size_t aligned = (size + 15) & ~std::size_t(15);
	std::size_t pages = (aligned + 4095) & ~std::size_t(4095);
	auto * allocation = static_cast<unsigned char *>(VirtualAlloc(nullptr, pages + 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!allocation) throw std::bad_alloc();
	DWORD previous;
	if (!VirtualProtect(allocation + pages, 4096, PAGE_NOACCESS, &previous)) throw std::bad_alloc();
	void * result = allocation + pages - aligned;
	Allocations().emplace(result, allocation);
	return result;
}

void operator delete[](void * pointer) noexcept
{
	if (!pointer) return;
	auto iterator = Allocations().find(pointer);
	if (iterator == Allocations().end()) std::abort();
	VirtualFree(iterator->second, 0, MEM_RELEASE);
	Allocations().erase(iterator);
}
void operator delete[](void * pointer, std::size_t) noexcept { ::operator delete[](pointer); }

void Check(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}
void Rewind(IStream * stream)
{
	Check(SUCCEEDED(stream->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr)), "rewind failed");
}

class Collector : public Pipe {
public:
	int Put(void const * source, int length) override
	{
		auto * bytes = static_cast<unsigned char const *>(source);
		Data.insert(Data.end(), bytes, bytes + length);
		return length;
	}
	std::vector<unsigned char> Data;
};
class Input : public Straw {
public:
	explicit Input(std::span<unsigned char const> data) : Data(data) {}
	int Get(void * output, int requested) override
	{
		std::size_t count = std::min(static_cast<std::size_t>(requested), Data.size());
		std::memcpy(output, Data.data(), count);
		Data = Data.subspan(count);
		return static_cast<int>(count);
	}
private:
	std::span<unsigned char const> Data;
};

void Stream_Roundtrip(std::vector<unsigned char> const & input, bool legacy_header)
{
	IStreamPtr storage;
	Check(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &storage)), "memory stream creation");
	{
		CStreamClass compressor;
		Check(SUCCEEDED(compressor.Link_Stream(storage)), "compression link");
		std::size_t position = 0;
		for (ULONG fragment : {13ul, 4093ul, 65536ul, 7ul, 131072ul}) {
			ULONG count = static_cast<ULONG>(std::min(static_cast<std::size_t>(fragment), input.size() - position));
			if (count == 0) break;
			ULONG written = 0;
			Check(SUCCEEDED(compressor.Write(input.data() + position, count, &written)) && written == count, "fragmented compressed write");
			position += count;
		}
		if (position < input.size()) Check(SUCCEEDED(compressor.Write(input.data() + position, static_cast<ULONG>(input.size() - position), nullptr)), "remaining compressed write");
		Check(SUCCEEDED(compressor.Unlink_Stream(nullptr)), "compression flush");
	}
	if (input.size() < CStreamClass::BUFFER_SIZE) {
		Rewind(storage);
		LegacyCStreamClass legacy;
		Check(SUCCEEDED(legacy.Link_Stream(storage)), "legacy reader link");
		std::vector<unsigned char> old_output(input.size());
		Check(SUCCEEDED(legacy.Read(old_output.data(), static_cast<ULONG>(old_output.size()), nullptr)), "legacy reader accepts actual tail length");
		Check(old_output == input, "new partial blocks remain readable by old reader");
		Check(SUCCEEDED(legacy.Unlink_Stream(nullptr)), "legacy reader unlink");
	}
	if (legacy_header) {
		Rewind(storage);
		std::array<ULONG, 2> header{};
		ULONG read;
		Check(SUCCEEDED(storage->Read(header.data(), sizeof(header), &read)) && read == sizeof(header), "partial block header");
		Check(header[1] == input.size(), "new partial block declares actual length");
		header[1] = CStreamClass::BUFFER_SIZE;
		Rewind(storage);
		Check(SUCCEEDED(storage->Write(header.data(), sizeof(header), nullptr)), "legacy header construction");
	}
	Rewind(storage);
	CStreamClass decompressor;
	Check(SUCCEEDED(decompressor.Link_Stream(storage)), "decompression link");
	std::vector<unsigned char> output(input.size());
	for (std::size_t position = 0; position < output.size();) {
		ULONG count = static_cast<ULONG>(std::min<std::size_t>(997, output.size() - position)), read = 0;
		Check(SUCCEEDED(decompressor.Read(output.data() + position, count, &read)) && read == count, "fragmented decompressed read");
		position += count;
	}
	Check(output == input, "compressed stream roundtrip mismatch");
	Check(SUCCEEDED(decompressor.Unlink_Stream(nullptr)), "decompression unlink");
}

void Pipe_And_Straw(std::vector<unsigned char> const & input)
{
	Collector compressed;
	{
		LZOPipe pipe(LZOPipe::COMPRESS);
		pipe.Put_To(compressed);
		pipe.Put(input.data(), 17);
		pipe.Put(input.data() + 17, static_cast<int>(input.size() - 17));
		pipe.Flush();
	}
	Input source(compressed.Data);
	LZOStraw decompress(LZOStraw::DECOMPRESS);
	decompress.Get_From(source);
	std::vector<unsigned char> output(input.size());
	Check(decompress.Get(output.data(), static_cast<int>(output.size())) == output.size(), "pipe to straw output count");
	Check(output == input, "pipe to straw roundtrip");

	Input original(input);
	LZOStraw compress(LZOStraw::COMPRESS);
	compress.Get_From(original);
	Collector decoded;
	LZOPipe decoder(LZOPipe::DECOMPRESS);
	decoder.Put_To(decoded);
	std::array<unsigned char, 701> fragment;
	for (int count; (count = compress.Get(fragment.data(), static_cast<int>(fragment.size()))) != 0;) decoder.Put(fragment.data(), count);
	decoder.Flush();
	Check(decoded.Data == input, "straw to pipe roundtrip");
}

int main()
{
	try {
		static_assert(LZO1X_MEM_COMPRESS == 16384 * sizeof(unsigned char *));
		std::vector<unsigned char> bytes(65536 * 2 + 123);
		std::uint32_t state = 0x419e2a91;
		for (auto & byte : bytes) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = static_cast<unsigned char>(state); }
		if constexpr (sizeof(void *) == 8) Check(reinterpret_cast<std::uintptr_t>(bytes.data()) > UINT32_MAX, "requires high-address input");
		Stream_Roundtrip(bytes, false);
		Pipe_And_Straw(bytes);
		bytes.resize(327);
		Stream_Roundtrip(bytes, true);
		std::fill(bytes.begin(), bytes.end(), 0);
		Stream_Roundtrip(bytes, false);
		Check(Allocations().empty(), "production array allocation leaked");
		std::cout << "LZO native dictionary, guarded allocations, streams and legacy partial headers passed\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
