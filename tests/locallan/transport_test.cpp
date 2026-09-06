#include "locallantest.hh"

#include <winsock2.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int Sent;
int LastError;
sockaddr_in ReceivedSource;
sockaddr_in SentDestination;
sockaddr_in BoundEndpoint;
int BroadcastOptions;

int Mock_Send_To(SOCKET, char const *, int size, int, sockaddr const * destination, int)
{
	++Sent;
	SentDestination = *reinterpret_cast<sockaddr_in const *>(destination);
	return size;
}


int Mock_Receive_From(SOCKET, char * buffer, int size, int, sockaddr * source, int *)
{
	*reinterpret_cast<sockaddr_in *>(source) = ReceivedSource;
	if (size < 4) return SOCKET_ERROR;
	std::memcpy(buffer, "test", 4);
	return 4;
}


void Mock_Set_Error(int error)
{
	LastError = error;
}


SOCKET Mock_Socket(int, int, int)
{
	return 1;
}


int Mock_Bind(SOCKET, sockaddr const * address, int)
{
	BoundEndpoint = *reinterpret_cast<sockaddr_in const *>(address);
	return 0;
}


int Mock_Set_Socket_Option(SOCKET, int, int option, char const *, int)
{
	if (option == SO_BROADCAST) ++BroadcastOptions;
	return 0;
}


void DebugString(char const *, ...)
{}


sockaddr_in Endpoint(std::uint32_t address, std::uint16_t port)
{
	sockaddr_in result{};
	result.sin_family = AF_INET;
	result.sin_addr.s_addr = htonl(address);
	result.sin_port = htons(port);
	return result;
}


void Require(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}

}

struct EmptyAddresses {
	int Count() const { return 0; }
	unsigned char * operator[](int) { return nullptr; }
	void Delete_Index(int) {}
};

struct IPXAddressClass {
	IPXAddressClass(std::uint32_t address, std::uint16_t port) : IP(address), Port(port) {}
	std::uint32_t IP;
	std::uint16_t Port;
};

struct SocketDefaults {
	bool Set_Socket_Options() { return true; }
};

class UDPInterfaceClass : public SocketDefaults {
public:
	using BASECLASS = SocketDefaults;
	int Send_To(char const * buffer, int buffer_len, sockaddr_in * destination);
	int Receive_From(char * buffer, int buffer_len, sockaddr_in * source);
	bool Open_Socket(SOCKET);
	bool Init() { return true; }
	void Close_Socket() {}
	void Register_Local_Addresses() {}
	void Set_Local_Port(unsigned short port) { LocalPort = port; LocalPortSet = true; }
	void Set_Destination_Port(unsigned short port) { DestinationPort = port; }
	void Enable_Broadcast(bool enabled) { UseBroadcast = enabled; }
	void Set_Broadcast_Address(IPXAddressClass const & address) { BroadcastAddresses.push_back(address); }
	EmptyAddresses LocalAddresses;
	std::vector<IPXAddressClass> BroadcastAddresses;
	bool WinsockInitialised = true;
	bool LocalPortSet = false;
	bool UseBroadcast = false;
	unsigned short LocalPort = 0;
	unsigned short DestinationPort = 0;
	SOCKET Socket = 0;
	unsigned short TunnelID = 0;
	unsigned long TunnelIP = 0;
	unsigned short TunnelPort = 0;
};

UDPInterfaceClass * PacketTransport;
int WestwoodOnline_PortNumber = 1234;

class IPXManagerClass {
public:
	enum { TRANSPORT_NONE, TRANSPORT_LAN };
	int TransportMode = TRANSPORT_NONE;
	void Shutdown() { delete PacketTransport; PacketTransport = nullptr; TransportMode = TRANSPORT_NONE; }
	void Configure_LAN(unsigned short port);
};

#define TUNNEL_HEADER_SIZE 4
#define WS_RECEIVE_BUFFER_LEN 1024
#define sendto Mock_Send_To
#define recvfrom Mock_Receive_From
#define WSASetLastError Mock_Set_Error
#define socket Mock_Socket
#define bind Mock_Bind
#define setsockopt Mock_Set_Socket_Option
#define LAST_ERROR LastError
#include "transport.inc"
#undef LAST_ERROR
#undef setsockopt
#undef bind
#undef socket
#undef WSASetLastError
#undef recvfrom
#undef sendto

int main()
{
	try {
		IPXManagerClass manager;
		manager.Configure_LAN(0);
		Require(manager.TransportMode == IPXManagerClass::TRANSPORT_LAN, "Default LAN transport mode changed");
		Require(PacketTransport->LocalPort == 1234 && PacketTransport->DestinationPort == 1234 && PacketTransport->UseBroadcast, "Default LAN ports or broadcast changed");
		Require(PacketTransport->Open_Socket(0), "Default socket setup failed");
		Require(ntohl(BoundEndpoint.sin_addr.s_addr) == INADDR_ANY && ntohs(BoundEndpoint.sin_port) == 1234 && BroadcastOptions == 1, "Default socket binding changed");
		UDPInterfaceClass transport;
		auto remote = Endpoint(0xc0a84401, 1234);
		char buffer[32]{};
		sockaddr_in source{};
		Require(transport.Send_To("data", 4, &remote) == 4 && Sent == 1, "Default send behavior changed");
		ReceivedSource = remote;
		Require(transport.Receive_From(buffer, sizeof(buffer), &source) == 4, "Default receive behavior changed");
		Require(std::memcmp(buffer, "test", 4) == 0, "Default payload changed");
#ifdef _DEBUG
		Require(Parse_Loopback_LAN_Test("45001,45002", LoopbackLANTest), "Test configuration rejected");
		manager.Configure_LAN(55555);
		Require(manager.TransportMode == IPXManagerClass::TRANSPORT_LAN, "Test mode bypassed the LAN protocol");
		Require(PacketTransport->LocalPort == 45001 && PacketTransport->DestinationPort == 45002 && !PacketTransport->UseBroadcast, "Test LAN ports or broadcast configuration incorrect");
		Require(PacketTransport->BroadcastAddresses.size() == 1 && ntohl(PacketTransport->BroadcastAddresses[0].IP) == 0x7f000001 && ntohs(PacketTransport->BroadcastAddresses[0].Port) == 45002, "Discovery escaped the configured peer");
		Require(PacketTransport->Open_Socket(0), "Test socket setup failed");
		Require(ntohl(BoundEndpoint.sin_addr.s_addr) == 0x7f000001 && ntohs(BoundEndpoint.sin_port) == 45001 && BroadcastOptions == 1, "Test socket escaped loopback or enabled network broadcasts");
		auto peer = Endpoint(0x7f000001, 45002);
		Require(transport.Send_To("data", 4, &peer) == 4 && Sent == 2, "Configured peer send failed");
		Require(SentDestination.sin_addr.s_addr == peer.sin_addr.s_addr && SentDestination.sin_port == peer.sin_port, "Peer destination changed");
		ReceivedSource = peer;
		Require(transport.Receive_From(buffer, sizeof(buffer), &source) == 4, "Configured peer receive failed");
		for (auto invalid : {remote, Endpoint(0x7f000001, 45001), Endpoint(0x7f000001, 45003), Endpoint(0x7f000002, 45002), Endpoint(0xffffffff, 45002)}) {
			Require(transport.Send_To("data", 4, &invalid) == SOCKET_ERROR && Sent == 2 && LastError == WSAEACCES, "Unconfigured destination reached sendto");
			ReceivedSource = invalid;
			Require(transport.Receive_From(buffer, sizeof(buffer), &source) == SOCKET_ERROR && LastError == WSAEACCES, "Unconfigured source accepted");
		}
		transport.TunnelPort = htons(50000);
		transport.TunnelIP = htonl(0xc0a84401);
		Require(transport.Send_To("data", 4, &peer) == SOCKET_ERROR && Sent == 2, "Tunnel escaped loopback restrictions");
		ReceivedSource = peer;
		Require(transport.Receive_From(buffer, sizeof(buffer), &source) == SOCKET_ERROR, "Tunnel payload accepted in loopback mode");
		LoopbackLANTest = LoopbackLANTestConfig();
		transport.TunnelPort = 0;
		Require(transport.Send_To("data", 4, &remote) == 4 && Sent == 3, "Disabling test mode did not restore default behavior");
#else
		Require(transport.Send_To("data", 4, &remote) == 4 && Sent == 2, "Release unexpectedly restricts network endpoints");
#endif
		manager.Shutdown();
		std::cout << "Production UDP endpoint restrictions and default behavior passed\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
