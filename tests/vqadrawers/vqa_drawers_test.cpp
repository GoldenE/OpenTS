// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 OpenTS contributors
// See LICENSE.md for applicable additional terms and warranty disclaimers.

#include "unvq.h"
#include "unvqtblc.h"
#include "vqaplayp.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" { unsigned short * HicolorTable; }
void VQA_UnVQFrame(VQAHandleP *, VQAFrameNode *);
long VQA_Set_DrawBuffer(VQAHandle *, unsigned char *, unsigned long, unsigned long, long, long);

#define DECLARE_DRAWER(name) void __cdecl name(unsigned char *, unsigned char *, unsigned char *, unsigned long, unsigned long, unsigned long)
namespace FrozenVQA {
DECLARE_DRAWER(UnVQ2_C1_4x4);
DECLARE_DRAWER(UnVQ1_C4_4x4);
DECLARE_DRAWER(UnVQ2_C4_4x4);
DECLARE_DRAWER(UnVQ1_C4_4x2);
DECLARE_DRAWER(UnVQ2_C4_4x2);
DECLARE_DRAWER(UnVQ2_C0_4x4_TRANS);
DECLARE_DRAWER(UnVQ2_C0_4x4_KEY);
DECLARE_DRAWER(UnVQ2_C0_4x4_TRANS_HALF);
DECLARE_DRAWER(UnVQ2_C0_4x2_TRANS);
DECLARE_DRAWER(UnVQ2_C0_4x2_KEY);
DECLARE_DRAWER(UnVQ2_4x4_Table);
DECLARE_DRAWER(UnVQ2_4x2_Table);
DECLARE_DRAWER(UnVQ1_4x4_Table);
DECLARE_DRAWER(UnVQ1_4x2_Table);
}
#undef DECLARE_DRAWER

namespace {
using Bytes = std::vector<unsigned char>;
constexpr unsigned BLOCKS = 6;
constexpr unsigned ROWS = 2;
constexpr unsigned PITCH = 32;
constexpr unsigned GUARD = 64;

enum class Commands { C1, C4Key, C4Delta, C4AlternateDelta, C0 };
struct Case {
	char const * Name;
	UNVQ_FUNC Draw;
	UNVQ_FUNC Frozen;
	Commands Format;
	unsigned SourceHeight;
	unsigned OutputHeight;
	bool Hicolor;
	bool Table;
	bool Alternate;
	bool Half;
	bool Transparent;
};

void Word(Bytes & bytes, unsigned value)
{
	bytes.push_back(static_cast<unsigned char>(value));
	bytes.push_back(static_cast<unsigned char>(value >> 8));
}

void Compare(Bytes const & actual, Bytes const & expected, std::string const & label)
{
	for (std::size_t index = 0; index < expected.size(); ++index) {
		if (actual[index] != expected[index]) {
			throw std::runtime_error(label + " byte " + std::to_string(index) + ": actual=" + std::to_string(actual[index]) + " expected=" + std::to_string(expected[index]));
		}
	}
}

struct Fixture {
	Case const & Spec;
	Bytes Codebook;
	Bytes Pointers;
	Bytes Expected;
	unsigned Block = 0;

	explicit Fixture(Case const & spec) : Spec(spec), Expected(GUARD + ROWS * spec.OutputHeight * PITCH * (spec.Hicolor ? 2 : 1) + 256, 0xa7)
	{
		for (unsigned entry = 0; entry < 8; ++entry) {
			for (unsigned y = 0; y < spec.SourceHeight; ++y) {
				for (unsigned x = 0; x < 4; ++x) {
					unsigned value = spec.Hicolor ? 0x1000 + entry * 37 + y * 7 + x : 1 + entry * 19 + y * 4 + x;
					if ((entry & 1) && x == 2 && y == 1) value = spec.Hicolor ? 0x8000 : 0;
					if (spec.Hicolor) Word(Codebook, value); else Codebook.push_back(static_cast<unsigned char>(value));
				}
			}
		}
	}

	void Paint(unsigned entry, bool masked, unsigned count = 1, bool solid = false)
	{
		unsigned bytes = Spec.Hicolor ? 2 : 1;
		unsigned width = Spec.Half ? 2 : 4;
		for (unsigned repeat = 0; repeat < count; ++repeat, ++Block) {
			for (unsigned y = 0; y < Spec.OutputHeight; y += Spec.Alternate ? 2 : 1) {
				for (unsigned x = 0; x < width; ++x) {
					unsigned source_x = Spec.Half ? x * 2 : x;
					unsigned offset = (entry * Spec.SourceHeight * 4 + y * 4 + source_x) * bytes;
					unsigned value = solid ? entry : Codebook[offset] | (bytes == 2 ? Codebook[offset + 1] << 8 : 0);
					if (masked && (Spec.Hicolor ? (value & 0x8000) != 0 : value == 0)) continue;
					if (solid && Spec.Table) value = HicolorTable[value];
					unsigned destination = GUARD + ((Block / BLOCKS * Spec.OutputHeight + y) * PITCH + (Block % BLOCKS * width + x)) * bytes;
					Expected[destination] = static_cast<unsigned char>(value);
					if (bytes == 2) Expected[destination + 1] = static_cast<unsigned char>(value >> 8);
				}
			}
		}
	}

	void Build()
	{
		for (unsigned row = 0; row < ROWS; ++row) {
			switch (Spec.Format) {
			case Commands::C1:
				Word(Pointers, 0x2002); Word(Pointers, row * 2); Paint(row * 2, false, 2);
				Word(Pointers, 0x3001); Word(Pointers, 1); Paint(1, true);
				Word(Pointers, 0x1001); ++Block;
				Word(Pointers, 0x0001); Word(Pointers, 0x1234 + row); Paint(0x1234 + row, false, 1, true);
				Word(Pointers, 0x6001); Word(Pointers, 3); Paint(3, false);
				break;
			case Commands::C4Key:
				for (unsigned column = 0; column < BLOCKS; ++column) {
					if (column == 2) { Word(Pointers, 0x4000); ++Block; continue; }
					unsigned entry = (column + row) % 8;
					bool masked = (column & 1) != 0;
					Word(Pointers, entry | (masked ? 0x2000 : 0)); Paint(entry, masked);
				}
				break;
			case Commands::C4Delta:
				if (row == 0) {
					Word(Pointers, 0x6000); Paint(0, false);
					Word(Pointers, 0x8001); Paint(1, true);
					Word(Pointers, 0x2002); Paint(2, false, 2);
					Word(Pointers, 0x0002); Block += 2;
				} else {
					Word(Pointers, 0xa003); Pointers.push_back(2); Paint(3, false, 2);
					Word(Pointers, 0xc001); Pointers.push_back(1); Paint(1, true);
					Word(Pointers, 0x4005); Pointers.push_back(6); Pointers.push_back(7);
					Paint(5, false); Paint(6, false); Paint(7, false);
				}
				break;
			case Commands::C4AlternateDelta:
				Word(Pointers, 0x2002); Paint(2, false, 2);
				Word(Pointers, 0xc001); Pointers.push_back(1); Paint(1, true);
				Word(Pointers, 0x0001); ++Block;
				Word(Pointers, 0xa003); Pointers.push_back(2); Paint(3, false, 2);
				break;
			case Commands::C0:
				Word(Pointers, row * 2); Paint(row * 2, false);
				Word(Pointers, 0xe001); Paint(1, Spec.Transparent);
				Word(Pointers, 0xf005); Paint(5, false, 1, true);
				Word(Pointers, 0xf000); if (Spec.Transparent) ++Block; else Paint(0, false, 1, true);
				Word(Pointers, 0xb20a); Paint(10, false, 2, true);
				break;
			}
		}
		if (Block != BLOCKS * ROWS) throw std::runtime_error("Fixture block accounting failed");
	}
};

unsigned char * LockBuffer;
int Unlocks;
std::intptr_t __cdecl Event(VQAHandle * handle, long action, void *, long)
{
	if (action == VQAEVENT_LOCK) {
		if (VQA_Set_DrawBuffer(handle, nullptr, PITCH, 64, 0, 0) != VQAERR_NONE) throw std::runtime_error("Surface geometry setup failed");
		return reinterpret_cast<std::intptr_t>(LockBuffer);
	}
	if (action == VQAEVENT_UNLOCK) ++Unlocks;
	return 0;
}

void __cdecl WrongDecoder(unsigned char *, unsigned char *, unsigned char *, unsigned long, unsigned long, unsigned long)
{
	throw std::runtime_error("VQA frame chose the wrong key/delta decoder");
}

void Run(Case const & test)
{
	Fixture fixture(test);
	fixture.Build();
	Bytes actual(fixture.Expected.size(), 0xa7);
	VQAHandleP player{};
	VQAFrameNode frame{};
	VQACBNode codebook{};
	frame.Codebook = &codebook;
	frame.Pointers = fixture.Pointers.data();
	codebook.Buffer = fixture.Codebook.data();
	bool delta = test.Format != Commands::C4Key;
	frame.Flags = delta ? VQAFRMF_RSDCOMP : 0;
	player.UnVQ1 = delta ? WrongDecoder : test.Draw;
	player.UnVQ2 = delta ? test.Draw : WrongDecoder;
	player.Header.ColorMode = test.Hicolor ? 1 : 0;
	player.Drawer.BlocksPerRow = BLOCKS;
	player.Drawer.NumRows = ROWS;
	player.Config.EventHandler = Event;
	LockBuffer = actual.data() + GUARD;
	Unlocks = 0;
	VQA_UnVQFrame(&player, &frame);
	Compare(actual, fixture.Expected, std::string(test.Name) + " expected raster");
	if (Unlocks != 1) throw std::runtime_error("VQA frame failed to unlock its surface exactly once");
#if OPENTS_VQA_FROZEN
	Bytes baseline(fixture.Expected.size(), 0xa7);
	test.Frozen(fixture.Codebook.data(), fixture.Pointers.data(), baseline.data() + GUARD, BLOCKS, ROWS, PITCH);
	Compare(actual, baseline, std::string(test.Name) + " frozen Win32");
#endif
	std::printf("PASS %s: complete multi-command rows, padding, masks and surface callback\n", test.Name);
}
}

#if OPENTS_VQA_FROZEN
#define DRAW(name) #name, name, FrozenVQA::name
#else
#define DRAW(name) #name, name, nullptr
#endif

int main()
{
	try {
		std::array<unsigned short, 65536> table;
		for (unsigned index = 0; index < table.size(); ++index) table[index] = static_cast<unsigned short>(index ^ 0x2468);
		HicolorTable = table.data();
		Case tests[] = {
			{DRAW(UnVQ2_C1_4x4), Commands::C1, 4, 4, true, false, false, false, false},
			{DRAW(UnVQ1_C4_4x4), Commands::C4Key, 4, 4, true, false, false, false, false},
			{DRAW(UnVQ2_C4_4x4), Commands::C4Delta, 4, 4, true, false, false, false, false},
			{DRAW(UnVQ1_C4_4x2), Commands::C4Key, 2, 2, true, false, false, false, false},
			{DRAW(UnVQ2_C4_4x2), Commands::C4Delta, 2, 2, true, false, false, false, false},
			{DRAW(UnVQ2_C0_4x4_TRANS), Commands::C0, 4, 4, false, false, false, false, true},
			{DRAW(UnVQ2_C0_4x4_KEY), Commands::C0, 4, 4, false, false, false, false, false},
			{DRAW(UnVQ2_C0_4x4_TRANS_HALF), Commands::C0, 4, 2, false, false, false, true, true},
			{DRAW(UnVQ2_C0_4x2_TRANS), Commands::C0, 2, 2, false, false, false, false, true},
			{DRAW(UnVQ2_C0_4x2_KEY), Commands::C0, 2, 2, false, false, false, false, false},
			{DRAW(UnVQ2_4x4_Table), Commands::C1, 4, 4, true, true, false, false, false},
			{DRAW(UnVQ2_4x2_Table), Commands::C1, 4, 4, true, true, true, false, false},
			{DRAW(UnVQ1_4x4_Table), Commands::C4Key, 4, 4, true, false, true, false, false},
			{DRAW(UnVQ1_4x2_Table), Commands::C4AlternateDelta, 4, 4, true, false, true, false, false},
		};
		for (Case const & test : tests) Run(test);
		return 0;
	} catch (std::exception const & error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
