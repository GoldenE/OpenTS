#include "locallantest.hh"

#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

}


int main()
{
	try {
		LoopbackLANTestConfig first;
		Require(Parse_Loopback_LAN_Test(nullptr, first) && !first.Enabled(), "Absent environment must preserve default mode");
		Require(first.Mutex_Name("original-guid") == "original-guid", "Default mutex name changed");
		Require(!first.Is_Self(0x7f000001, 45001) && !first.Is_Peer(0x7f000001, 45002), "Disabled mode must not classify test endpoints");

		Require(Parse_Loopback_LAN_Test("45001,45002", first) && first.Enabled(), "Valid port pair rejected");
		LoopbackLANTestConfig second;
		Require(Parse_Loopback_LAN_Test("45002,45001", second), "Reverse port pair rejected");
		Require(first.LocalPort == 45001 && first.PeerPort == 45002, "Port order reversed");
		Require(first.Mutex_Name("app") == "app.loopback.45001", "Instance mutex suffix is not stable");
		Require(first.Mutex_Name("app") != second.Mutex_Name("app"), "Peer instances share a mutex");
		Require(first.Mutex_Name("app") != first.Mutex_Name("autoplay"), "App and autoplay mutexes collide");
		Require(first.Is_Self(0x7f000001, 45001), "Own endpoint not recognized");
		Require(!first.Is_Self(0x7f000001, 45002), "Peer on same IP mistaken for self");
		Require(first.Is_Peer(0x7f000001, 45002) && second.Is_Peer(0x7f000001, 45001), "Configured peers not accepted");
		for (std::uint32_t address : {0u, 0x7f000000u, 0x7f000002u, 0xc0a84465u, 0xffffffffu}) {
			Require(!first.Is_Self(address, 45001) && !first.Is_Peer(address, 45002), "Unconfigured address accepted");
		}
		for (std::uint16_t port : {0, 1234, 45001, 45003, 65535}) {
			Require(!first.Is_Peer(0x7f000001, port), "Unconfigured peer port accepted");
		}
		for (char const * input : {"", "45001", ",45002", "45001,", "45001,45002,45003", "45001,45001", "0,45002", "1023,45002", "45001,1023", "65536,45002", "45001,65536", "-45001,45002", "+45001,45002", " 45001,45002", "45001, 45002", "45001,45002 ", "45001,45002\n", "45001,45002x", "45001;45002", "999999999999999,45002"}) {
			Require(!Parse_Loopback_LAN_Test(input, first), "Malformed environment accepted");
			Require(!first.Enabled() && first.LocalPort == 0 && first.PeerPort == 0, "Invalid input retained previous configuration");
		}
		Require(Parse_Loopback_LAN_Test("1024,65535", first), "Port boundary pair rejected");
		Require(Parse_Loopback_LAN_Test(nullptr, first) && !first.Enabled(), "Default mode did not reset a previous test configuration");
		std::cout << "Loopback LAN opt-in, mutex, self and peer endpoint checks passed\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
