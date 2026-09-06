/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#ifndef OPENTS_LOCAL_LAN_STANDALONE
#include "always.h"
#endif
#include "locallantest.hh"

#include <charconv>

#ifdef _DEBUG
LoopbackLANTestConfig LoopbackLANTest;
#endif

LoopbackLANTestConfig::LoopbackLANTestConfig() : LocalPort(0), PeerPort(0)
{}


bool LoopbackLANTestConfig::Enabled() const
{
	return LocalPort != 0 && PeerPort != 0;
}


std::uint32_t LoopbackLANTestConfig::Address()
{
	return 0x7f000001u;
}


bool LoopbackLANTestConfig::Is_Self(std::uint32_t address, std::uint16_t port) const
{
	return Enabled() && address == Address() && port == LocalPort;
}


bool LoopbackLANTestConfig::Is_Peer(std::uint32_t address, std::uint16_t port) const
{
	return Enabled() && address == Address() && port == PeerPort;
}


std::string LoopbackLANTestConfig::Mutex_Name(std::string_view base) const
{
	std::string result(base);
	if (Enabled()) result += ".loopback." + std::to_string(LocalPort);
	return result;
}


bool Parse_Loopback_LAN_Test(char const * value, LoopbackLANTestConfig & config)
{
	config = LoopbackLANTestConfig();
	if (value == nullptr) return true;
	std::string_view text(value);
	auto separator = text.find(',');
	if (separator == std::string_view::npos || text.find(',', separator + 1) != std::string_view::npos) return false;
	auto port = [](std::string_view input, std::uint16_t & result) {
		if (input.empty() || input.size() > 5 || input.find_first_not_of("0123456789") != std::string_view::npos) return false;
		auto parsed = std::from_chars(input.data(), input.data() + input.size(), result);
		return parsed.ec == std::errc() && parsed.ptr == input.data() + input.size() && result >= 1024;
	};
	LoopbackLANTestConfig candidate;
	if (!port(text.substr(0, separator), candidate.LocalPort) || !port(text.substr(separator + 1), candidate.PeerPort)
		|| candidate.LocalPort == candidate.PeerPort) return false;
	config = candidate;
	return true;
}
