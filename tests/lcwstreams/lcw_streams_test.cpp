// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors
// See LICENSE.md for applicable additional terms and warranty disclaimers.

#include "lcwpipe.h"
#include "lcwstraw.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t GUARD_SIZE = 256;
struct alignas(std::max_align_t) Allocation {
	std::size_t Size;
};
using Bytes = std::vector<unsigned char>;

void Require(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

class OutputPipe : public Pipe {
public:
	Bytes Output;
	int Put(void const * source, int length) override
	{
		auto const * bytes = static_cast<unsigned char const *>(source);
		Output.insert(Output.end(), bytes, bytes + length);
		return length;
	}
};

class InputStraw : public Straw {
public:
	explicit InputStraw(Bytes const & bytes) : Input(bytes) {}
	int Get(void * buffer, int length) override
	{
		auto count = std::min(static_cast<std::size_t>(length), Input.size() - Position);
		std::memcpy(buffer, Input.data() + Position, count);
		Position += count;
		return static_cast<int>(count);
	}
private:
	Bytes const & Input;
	std::size_t Position = 0;
};

Bytes Literal_Input(std::size_t length)
{
	Bytes input;
	std::vector<bool> seen(1 << 24);
	std::uint32_t state = 0x7a239b61;
	while (input.size() < length) {
		state = state * 1664525u + 1013904223u;
		auto byte = static_cast<unsigned char>(state >> 24);
		if (input.size() >= 2) {
			unsigned triple = (input[input.size() - 2] << 16) | (input.back() << 8) | byte;
			if (seen[triple]) continue;
			seen[triple] = true;
		}
		input.push_back(byte);
	}
	return input;
}

void Append_Word(Bytes & bytes, unsigned value)
{
	bytes.push_back(static_cast<unsigned char>(value));
	bytes.push_back(static_cast<unsigned char>(value >> 8));
}

Bytes Literal_Stream(Bytes const & input, unsigned blocksize)
{
	Bytes encoded;
	for (std::size_t block = 0; block < input.size(); block += blocksize) {
		unsigned length = static_cast<unsigned>(std::min(input.size() - block, static_cast<std::size_t>(blocksize)));
		Append_Word(encoded, length + (length + 62) / 63 + 1);
		Append_Word(encoded, length);
		for (unsigned position = 0; position < length; position += 63) {
			unsigned count = std::min(63u, length - position);
			encoded.push_back(static_cast<unsigned char>(0x80 | count));
			encoded.insert(encoded.end(), input.begin() + block + position, input.begin() + block + position + count);
		}
		encoded.push_back(0x80);
	}
	return encoded;
}

Bytes Push(Bytes const & input, LCWPipe::CompControl mode, int blocksize)
{
	OutputPipe output;
	{
		LCWPipe codec(mode, blocksize);
		codec.Put_To(&output);
		constexpr unsigned CHUNKS[] = {1, 2, 7, 257, 8192};
		std::size_t position = 0;
		unsigned chunk = 0;
		while (position < input.size()) {
			unsigned count = static_cast<unsigned>(std::min(input.size() - position, static_cast<std::size_t>(CHUNKS[chunk++ % 5])));
			codec.Put(input.data() + position, count);
			position += count;
		}
		codec.Flush();
	}
	return output.Output;
}

Bytes Pull(Bytes const & input, LCWStraw::CompControl mode, int blocksize)
{
	InputStraw source(input);
	Bytes output;
	{
		LCWStraw codec(mode, blocksize);
		codec.Get_From(&source);
		std::array<unsigned char, 257> buffer;
		for (unsigned chunk = 0;; ++chunk) {
			int count = codec.Get(buffer.data(), chunk % 3 == 0 ? 1 : static_cast<int>(buffer.size()));
			if (!count) break;
			output.insert(output.end(), buffer.begin(), buffer.begin() + count);
		}
	}
	return output;
}
}

void * operator new[](std::size_t size)
{
	auto * allocation = static_cast<Allocation *>(std::malloc(sizeof(Allocation) + 2 * GUARD_SIZE + size));
	if (!allocation) throw std::bad_alloc();
	allocation->Size = size;
	auto * prefix = reinterpret_cast<unsigned char *>(allocation + 1);
	std::memset(prefix, 0xa6, GUARD_SIZE);
	std::memset(prefix + GUARD_SIZE + size, 0xa6, GUARD_SIZE);
	return prefix + GUARD_SIZE;
}

void operator delete[](void * pointer) noexcept
{
	if (!pointer) return;
	auto * prefix = static_cast<unsigned char *>(pointer) - GUARD_SIZE;
	auto * allocation = reinterpret_cast<Allocation *>(prefix) - 1;
	for (std::size_t index = 0; index < GUARD_SIZE; ++index) {
		if (prefix[index] != 0xa6 || prefix[GUARD_SIZE + allocation->Size + index] != 0xa6) {
			std::fprintf(stderr, "LCW stream allocation guard overwritten (allocation size %zu, guard offset %zu)\n", allocation->Size, index);
			std::_Exit(EXIT_FAILURE);
		}
	}
	std::free(allocation);
}

int main()
{
	try {
		for (unsigned length : {0u, 1u, 63u, 64u, 8192u, 8193u, 8329u, 64510u}) {
			unsigned blocksize = length == 64510 ? 64510 : 8192;
			auto input = Literal_Input(length);
			auto encoded = Literal_Stream(input, blocksize);
			Require(Push(input, LCWPipe::COMPRESS, blocksize) == encoded, "Pipe literal stream bytes changed");
			Require(Pull(input, LCWStraw::COMPRESS, blocksize) == encoded, "Straw literal stream bytes changed");
			Require(Push(encoded, LCWPipe::DECOMPRESS, blocksize) == input, "Pipe literal stream round trip failed");
			Require(Pull(encoded, LCWStraw::DECOMPRESS, blocksize) == input, "Straw literal stream round trip failed");
			std::printf("PASS literal-only %u-byte input, %u-byte decoded blocks\n", length, blocksize);
		}
		for (unsigned blocksize : {1u, 2u, 63u}) {
			auto input = Literal_Input(147);
			auto encoded = Literal_Stream(input, blocksize);
			Require(Push(input, LCWPipe::COMPRESS, blocksize) == encoded, "Small pipe blocks changed");
			Require(Pull(input, LCWStraw::COMPRESS, blocksize) == encoded, "Small straw blocks changed");
			Require(Push(encoded, LCWPipe::DECOMPRESS, blocksize) == input, "Small pipe blocks failed to decode");
			Require(Pull(encoded, LCWStraw::DECOMPRESS, blocksize) == input, "Small straw blocks failed to decode");
		}
		Bytes longest_fill = {5, 0, 255, 255, 0xfe, 255, 255, 77, 0x80};
		Require(Push(longest_fill, LCWPipe::DECOMPRESS, 65535) == Bytes(65535, 77), "Maximum decoded pipe block failed");
		Require(Pull(longest_fill, LCWStraw::DECOMPRESS, 65535) == Bytes(65535, 77), "Maximum decoded straw block failed");
		for (int size : {INT_MIN, -1, 0, 64511, 65535, 65536, INT_MAX}) {
			bool rejected = false;
			try { LCWPipe codec(LCWPipe::COMPRESS, size); } catch (std::invalid_argument const &) { rejected = true; }
			Require(rejected, "Invalid pipe compression size accepted");
			rejected = false;
			try { LCWStraw codec(LCWStraw::COMPRESS, size); } catch (std::invalid_argument const &) { rejected = true; }
			Require(rejected, "Invalid straw compression size accepted");
		}
		for (int size : {INT_MIN, -1, 0, 65536, INT_MAX}) {
			bool rejected = false;
			try { LCWPipe codec(LCWPipe::DECOMPRESS, size); } catch (std::invalid_argument const &) { rejected = true; }
			Require(rejected, "Invalid pipe decompression size accepted");
			rejected = false;
			try { LCWStraw codec(LCWStraw::DECOMPRESS, size); } catch (std::invalid_argument const &) { rejected = true; }
			Require(rejected, "Invalid straw decompression size accepted");
		}
		for (auto invalid : {Bytes{255, 255, 0, 32}, Bytes{1, 0, 1, 32, 0x80}}) {
			Require(Push(invalid, LCWPipe::DECOMPRESS, 8192).empty(), "Oversized pipe header was accepted");
			Require(Pull(invalid, LCWStraw::DECOMPRESS, 8192).empty(), "Oversized straw header was accepted");
		}
		std::puts("Passed LCW stream bounds, framing, and allocation guards");
		return 0;
	} catch (std::exception const & error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
