#include "vqaplayp.h"
#include "ahandle.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <type_traits>

#include "memory_handlers.inc"

static bool SimulatedAudioAvailable = true;
static Ahandle _handles[Ahandle::MAX_HANDLES]{};
static long ReceivedAudioInitSize = 0;

bool Audio_Available() { return SimulatedAudioAvailable; }
void DebugString(char const *, ...) {}
long __cdecl VQA_AudioFillCallback(VQAHandleP *) { return VQAERR_NONE; }
long __cdecl VQA_AudioDoneCallback(VQAHandle *, void *) { return VQAERR_NONE; }

#include "audio_open.inc"

long __cdecl Check_Audio_Open_Handler(VQAHandle * player, long action, void * parameters, long bytes)
{
	if (action != VQAAUDIO_OPEN) return VQAERR_AUDIO;
	ReceivedAudioInitSize = bytes;
	return Open_Audio_Handler(reinterpret_cast<VQAHandleP *>(player), static_cast<AhandleInitParams *>(parameters), bytes);
}

long __cdecl Memory_VQA_Stream_Handler(VQAHandle *vqa, long action, void *buffer, long nbytes);

static_assert(std::is_same_v<std::invoke_result_t<VQA_POINTER_H_FUNC, VQAHandle *, long, void *, long>, std::intptr_t>);
static_assert(sizeof(VQALoopCache::FileOffset) == 4);

namespace {

void Require(bool condition, char const * message)
{
	if (!condition) throw std::runtime_error(message);
}


void Check_Allocator(VQA_POINTER_H_FUNC handler)
{
	auto result = handler(nullptr, VQAMEM_ALLOC, nullptr, 64);
	Require(result != 0, "VQA allocation failed");
	void * pointer = reinterpret_cast<void *>(result);
	std::memset(pointer, 0x5a, 64);
	Require(handler(nullptr, VQAMEM_LOCK, pointer, 64) == result, "VQA lock truncated allocation pointer");
	Require(handler(nullptr, VQAMEM_UNLOCK, pointer, 64) == result, "VQA unlock truncated allocation pointer");
	Require(handler(nullptr, VQAMEM_FREE, pointer, 0) == 0, "VQA free returned failure");
	std::uintptr_t const address = sizeof(void *) == 8 ? UINT64_C(0x12345678abcdef00) : UINT64_C(0xfedcba00);
	void * token = reinterpret_cast<void *>(address);
	Require(static_cast<std::uintptr_t>(handler(nullptr, VQAMEM_LOCK, token, 0)) == address, "VQA lock lost high pointer bits");
	Require(static_cast<std::uintptr_t>(handler(nullptr, VQAMEM_UNLOCK, token, 0)) == address, "VQA unlock lost high pointer bits");
}


void Check_Stream()
{
	VQAHandleP player{};
	VQAHandle * handle = reinterpret_cast<VQAHandle *>(&player);
	std::array<char, 8> data{1, 2, 3, 4, 5, 6, 7, 8};
	player.LoopCache.Ptr = data.data();
	player.LoopCache.FileOffset = 100000;
	player.LoopCache.Bytes = static_cast<int>(data.size());
	char output[3]{};
	Require(Memory_VQA_Stream_Handler(handle, VQACMD_SEEK, nullptr, 100002) == 0, "VQA absolute cached seek failed");
	Require(player.LoopCache.Offset == 2, "VQA absolute seek confused file and pointer offsets");
	Require(Memory_VQA_Stream_Handler(handle, VQACMD_READ, output, 3) == 0, "VQA cached read failed");
	Require(std::memcmp(output, data.data() + 2, 3) == 0, "VQA cached read differs");
	Require(Memory_VQA_Stream_Handler(handle, VQACMD_SEEK, reinterpret_cast<void *>(1), -2) == 0, "VQA relative cached seek failed");
	Require(player.LoopCache.Offset == 3, "VQA relative seek offset differs");
	Require(Memory_VQA_Stream_Handler(handle, VQACMD_READ, output, 6) != 0, "VQA oversized cached read succeeded");
	Require(Memory_VQA_Stream_Handler(handle, VQACMD_SEEK, nullptr, 99999) != 0, "VQA seek before cached file origin succeeded");
}


void Check_Audio_Init()
{
	VQAHandleP player{};
	unsigned char buffer = 0;
	player.Audio.Buffer = &buffer;
	player.SampleRate = 22050;
	player.Channels = 2;
	player.BitsPerSample = 16;
	player.Config.AudioHandler = Check_Audio_Open_Handler;
	Require(VQA_OpenAudio(&player) == VQAERR_NONE, "Production audio caller was rejected by native-size handler guard");
	Require(ReceivedAudioInitSize == sizeof(AhandleInitParams), "Audio caller did not send the native parameter size");
	Require(sizeof(AhandleInitParams) == 8 + 2 * sizeof(void *), "Unexpected audio initialization structure layout");
	AhandleInitParams parameters{};
	for (long size : {0L, static_cast<long>(sizeof(parameters) - 1), static_cast<long>(sizeof(parameters) + 1)}) {
		Require(Open_Audio_Handler(&player, &parameters, size) == VQAERR_AUDIO, "Malformed audio parameter size was accepted");
	}
	if (sizeof(void *) == 8) Require(Open_Audio_Handler(&player, &parameters, 16) == VQAERR_AUDIO, "Legacy 16-byte audio parameters were accepted on x64");
	Require(Open_Audio_Handler(&player, nullptr, sizeof(parameters)) == VQAERR_AUDIO, "Null audio initialization parameters were accepted");
	_handles[0].Used = true;
	Require(VQA_OpenAudio(&player) == VQAERR_AUDIO, "Exhausted audio handle table was accepted");
	_handles[0].Used = false;
	SimulatedAudioAvailable = false;
	Require(VQA_OpenAudio(&player) == VQAERR_AUDIO, "Unavailable audio was accepted");
	SimulatedAudioAvailable = true;
}

}


int main()
{
	try {
		Check_Allocator(VQA_Memory_Handler);
		Check_Allocator(VQAMemoryHandler);
		Check_Stream();
		Check_Audio_Init();
		std::cout << "VQA pointer callback, cached file-offset and native audio-init guard checks passed\n";
		return 0;
	} catch (std::exception const & error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
