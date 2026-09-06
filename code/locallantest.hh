/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Addresses and ports passed to these helpers are in host byte order.
struct LoopbackLANTestConfig {
	LoopbackLANTestConfig();
	bool Enabled() const;
	bool Is_Self(std::uint32_t address, std::uint16_t port) const;
	bool Is_Peer(std::uint32_t address, std::uint16_t port) const;
	std::string Mutex_Name(std::string_view base) const;
	static std::uint32_t Address();

	std::uint16_t LocalPort;
	std::uint16_t PeerPort;
};

bool Parse_Loopback_LAN_Test(char const * value, LoopbackLANTestConfig & config);

#ifdef _DEBUG
extern LoopbackLANTestConfig LoopbackLANTest;
#endif
