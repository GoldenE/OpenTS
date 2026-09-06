/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/
#include "always.h"
#include "getcpu.h"
#include "mpu.h"
#include <intrin.h>
#include <cstring>

extern "C" {
char UseCMOV = 0;
char HasCMOV = 0;
char UseMMX = 0;
char CPUType = 0;
char VendorID[20] = "Not available";

bool __cdecl Detect_MMX_Availability(void)
{
	int registers[4];
	__cpuid(registers, 0);
	int const maximum_leaf = registers[0];
	std::memcpy(VendorID, &registers[1], 4);
	std::memcpy(VendorID + 4, &registers[3], 4);
	std::memcpy(VendorID + 8, &registers[2], 4);
	VendorID[12] = ' ';
	VendorID[13] = 0;
	CPUType = 4;
	UseMMX = 0;
	if (maximum_leaf >= 1) {
		__cpuid(registers, 1);
		CPUType = static_cast<char>((registers[0] >> 8) & 15);
		UseMMX = CPUType >= 5 && (registers[3] & (1 << 23)) != 0;
	}
	return UseMMX != 0;
}

bool __cdecl Detect_CMOV_Availability(void)
{
	int registers[4];
	__cpuid(registers, 1);
	HasCMOV = CPUType >= 5 && (registers[3] & (1 << 15)) != 0;
	UseCMOV = HasCMOV && CPUType > 5;
	return HasCMOV != 0;
}

unsigned int __cdecl Get_CPU_Clock(unsigned int & high)
{
	unsigned __int64 const clock = __rdtsc();
	high = static_cast<unsigned int>(clock >> 32);
	return static_cast<unsigned int>(clock);
}

unsigned short __cdecl Processor(void)
{
	// Both supported architectures require SSE2 and therefore support CPUID.
	return PROC_PENTIUM;
}
}
