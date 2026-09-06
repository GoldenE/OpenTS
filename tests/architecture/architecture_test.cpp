/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "architecture.hh"
#include "radio.hh"
#include "version.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

using WCHAR = wchar_t;
static constexpr int RecordingTagSize = 8;
unsigned int ExpectedGameVersion = OPENTS_STATE_VERSION;
unsigned int InputSaveVersion;
bool ReadSaveInfo = true;

void DebugString(char const *, ...) {}

struct SaveVersionInfo {
	unsigned int Get_Internal_Version() const { return(InputSaveVersion); }
};

bool Get_Savefile_Info(char const *, SaveVersionInfo *) { return(ReadSaveInfo); }

struct CCFileClass {
	std::string Bytes;
	std::size_t Offset = 0;
	int Read(void * destination, int size)
	{
		int count = static_cast<int>(std::min(Bytes.size() - Offset, static_cast<std::size_t>(size)));
		std::memcpy(destination, Bytes.data() + Offset, count);
		Offset += count;
		return(count);
	}
	char const * File_Name() const { return("synthetic recording"); }
};

#include "architecture_functions.inc"

int main()
{
	int failures = 0;
	auto check = [&failures](bool result, char const * name) {
		std::printf("%s: %s\n", result ? "PASS" : "FAIL", name);
		if (!result) failures++;
	};
	unsigned int opposite = OPENTS_STATE_VERSION ^ 0x01000000u;
	check(Build_Number() == OPENTS_STATE_VERSION, "WOL build identity includes the architecture stamp");
	check(Build_Number() != opposite, "WOL build identity rejects an opposite-architecture host before joining");
	VersionClass version;
	version.Init_Clipping();
	check(version.Clip_Version(OPENTS_STATE_VERSION, OPENTS_STATE_VERSION) == OPENTS_STATE_VERSION, "Matching architecture negotiates its state version");
	unsigned int rejection = version.Clip_Version(opposite, opposite);
	check(rejection == 0 || rejection == 0xffffffffu, "Opposite architecture reaches the existing network rejection result");
	check(version.Clip_Version(OPENTS_STATE_VERSION, OPENTS_STATE_VERSION) == OPENTS_STATE_VERSION, "Rejected peer does not change the accepted version range");
	InputSaveVersion = opposite;
	check(!Load_Game("synthetic save"), "Opposite architecture save is rejected before session state loading");
	InputSaveVersion = OPENTS_STATE_VERSION;
	check(Load_Game("synthetic save"), "Matching save passes the early identity guard");
	ReadSaveInfo = false;
	check(!Load_Game("synthetic save"), "Unreadable save header fails before session state loading");
	CCFileClass compatible{std::string(OPENTS_RECORDING_TAG, 8) + "sentinel"};
	check(Load_Recording_Values(compatible) && compatible.Offset == 8, "Matching recording leaves payload untouched at the early identity guard");
	#if defined(_DEBUG)
	char const * opposite_architecture = sizeof(void *) == 8 ? "OTSREC1" : "OTSREC2";
	char const * opposite_configuration = sizeof(void *) == 8 ? "OTSREL2" : "OTSREL1";
	#else
	char const * opposite_architecture = sizeof(void *) == 8 ? "OTSREL1" : "OTSREL2";
	char const * opposite_configuration = sizeof(void *) == 8 ? "OTSREC2" : "OTSREC1";
	#endif
	CCFileClass incompatible{std::string(opposite_architecture, 8) + "sentinel"};
	check(!Load_Recording_Values(incompatible) && incompatible.Offset == 8, "Opposite architecture recording fails before reading session fields");
	CCFileClass other_configuration{std::string(opposite_configuration, 8) + "sentinel"};
	check(!Load_Recording_Values(other_configuration) && other_configuration.Offset == 8, "Opposite build configuration recording fails before reading session fields");
	CCFileClass truncated{"OTSREC"};
	check(!Load_Recording_Values(truncated), "Truncated recording header is rejected");
	std::uintptr_t bits = sizeof(void *) == 8 ? 0x0000123456789000ull : 0xf6789000u;
	void * pointer = reinterpret_cast<void *>(bits);
	RadioParameter parameter = reinterpret_cast<RadioParameter>(pointer);
	check(reinterpret_cast<void *>(parameter) == pointer, "Radio channel preserves high pointer bits without dereferencing synthetic addresses");
	return(failures == 0 ? 0 : 1);
}
