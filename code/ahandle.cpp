/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Electronic Arts Inc.
 * Copyright 2026 OpenTS contributors
 *
 * Contains material derived from Electronic Arts source code.
 * Modified by OpenTS contributors, 2026.
 * EA's GPLv3 Section 7 additional terms and supplemental warranty
 * disclaimers apply; see LICENSE.md.
 ******************************************************************************/

#include "always.h"

#include "ahandle.h"

#include "dsaudio.h"
#include "dbgprint.h"
#include "gametime.h"
#include "vqaplayp.h"

#include <cassert>
#include <atomic>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace {
bool Audio_Trace_Enabled()
{
	static bool const enabled = [] {
		char const * value = std::getenv("OPENTS_VQA_AUDIO_TRACE");
		return value != nullptr && std::strcmp(value, "1") == 0;
	}();
	return enabled;
}

bool Audio_Trace_Operation()
{
	static std::atomic<unsigned> count{0};
	if (!Audio_Trace_Enabled() || count.load(std::memory_order_relaxed) >= 24) return false;
	return count.fetch_add(1, std::memory_order_relaxed) < 24;
}

void Trace_Audio_Result(char const * operation, HRESULT result, Ahandle const * audio, unsigned offset = 0, unsigned bytes = 0)
{
	if (!Audio_Trace_Operation()) return;
	DebugString("VQA audio trace %s: hr=%08lX rate=%u channels=%u bits=%u offset=%u bytes=%u\n", operation, static_cast<unsigned long>(result),
		static_cast<unsigned>(audio->SampleRate), static_cast<unsigned>(audio->Channels), static_cast<unsigned>(audio->BitsPerSample), offset, bytes);
}

void Trace_Audio_PCM(bool submitted, Ahandle const * audio, void const * buffer, unsigned bytes)
{
	if (!Audio_Trace_Enabled() || buffer == nullptr) return;
	static std::atomic<unsigned> decoded_count{0};
	static std::atomic<unsigned> submitted_count{0};
	static std::atomic<unsigned long long> decoded_fills{0};
	static std::atomic<unsigned long long> submitted_fills{0};
	static std::atomic<bool> decoded_nonzero{false};
	static std::atomic<bool> submitted_nonzero{false};
	auto & count = submitted ? submitted_count : decoded_count;
	auto & fills = submitted ? submitted_fills : decoded_fills;
	auto & seen_nonzero = submitted ? submitted_nonzero : decoded_nonzero;
	if (count.load(std::memory_order_relaxed) >= 8) return;
	unsigned long long fill = fills.fetch_add(1, std::memory_order_relaxed);
	unsigned scan_bytes = bytes < 16384 ? bytes : 16384;
	unsigned sample_bytes = audio->BitsPerSample == 16 ? 2 : audio->BitsPerSample == 8 ? 1 : 0;
	unsigned samples = sample_bytes ? scan_bytes / sample_bytes : 0;
	auto const * source = static_cast<unsigned char const *>(buffer);
	int minimum = samples ? INT_MAX : 0;
	int maximum = samples ? INT_MIN : 0;
	unsigned nonzero = 0;
	for (unsigned sample = 0; sample < samples; ++sample) {
		int value;
		if (sample_bytes == 2) {
			value = source[sample * 2] | (source[sample * 2 + 1] << 8);
			if (value >= 32768) value -= 65536;
		} else {
			value = static_cast<int>(source[sample]) - 128;
		}
		if (value < minimum) minimum = value;
		if (value > maximum) maximum = value;
		if (value != 0) ++nonzero;
	}
	// Reserve the last record for actual signal when the opening buffers are silent.
	if (count.load(std::memory_order_relaxed) >= 7 && nonzero == 0 && !seen_nonzero.load(std::memory_order_relaxed)) return;
	unsigned index = count.fetch_add(1, std::memory_order_relaxed);
	if (index >= 8) return;
	if (nonzero != 0) seen_nonzero.store(true, std::memory_order_relaxed);
	DebugString("VQA audio trace %s PCM[%u]: fill=%llu bytes=%u scanned=%u samples=%u bits=%u channels=%u min=%d max=%d nonzero=%u\n",
		submitted ? "submitted" : "decoded", index, fill, bytes, samples * sample_bytes, samples, static_cast<unsigned>(audio->BitsPerSample),
		static_cast<unsigned>(audio->Channels), minimum, maximum, nonzero);
}
}

/// use of this is a bug..
#ifndef DSBCAPS_GETCURRENTPOSITION2
#define DSBCAPS_GETCURRENTPOSITION2	0x00010000
#endif

Ahandle _handles[Ahandle::MAX_HANDLES];

bool _restore_primary = false;
WAVEFORMATEX _restore_format;

AHANDLE_CALLBACK_1 _AHandleCallbackFunc1;
AHANDLE_CALLBACK_2 _AHandleCallbackFunc2;


/// private, call handler instead

long __cdecl Open_Audio_Handler(VQAHandleP *vqap, AhandleInitParams *params, long);
long __cdecl Close_Audio_Handler(VQAHandleP *vqap);
long __cdecl Start_Audio_Handler(VQAHandleP *vqap);
long __cdecl Stop_Audio_Handler(VQAHandleP *vqap);
long __cdecl Play_Audio_Handler(VQAHandleP *vqap);
long __cdecl Pause_Audio_Handler(VQAHandleP *vqap);
long __cdecl Resume_Audio_Handler(VQAHandleP *vqap);
long __cdecl Load_Audio_Handler(VQAHandleP *vqap, void *buffer, long nbytes);
void CALLBACK AudioCallback(UINT uTimerID, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR);
_STATIC unsigned long Get_Playback_Position(VQAHandle *vqa, Ahandle *handle, VQAConfig *config);

_STATIC BOOL Move_HMI_Audio_Block_To_Direct_Sound_Buffer(VQAHandleP *vqap);

unsigned long __cdecl Timer_Callback_Audio_Handler(VQAHandle *vqa)
{
	VQAHandleP    *vqap = (VQAHandleP *)vqa;
	VQAConfig     *config = &vqap->Config;
	Ahandle       *handle = &_handles[vqap->AudioHandleIndex];
	unsigned long d;
	unsigned long t;

	if (handle->Flags & AHANDLEF_IS_PAUSED) {
#ifdef _DEBUG
		DebugString("Ahandle: Paused %ld\n", handle->TickCount);
#endif
		return(handle->TickCount);
	}

	t = Simple_Timer_Callback_Audio_Handler(NULL) - handle->PauseAdjust;

	d = Get_Playback_Position(vqa, handle, config);

	if (d > 0 && d <= handle->LastPlaybackPosition) {
		if (t > handle->LastTimerTick) {
			d = (t - handle->LastTimerTick);
			handle->LastTimerTick = t;
			handle->TickCount += d;
		}
	} else {
		handle->LastPlaybackPosition = d;
		handle->LastTimerTick = t;
		handle->TickCount = (VQA_TIMETICKS * (d / (vqap->Channels * (vqap->BitsPerSample >> 3))) / vqap->SampleRate);

		if (handle->TickCount >= config->LatencyAdjustment) {
			handle->TickCount -= config->LatencyAdjustment;
		} else {
			handle->TickCount = 0;
		}
	}
	return(handle->TickCount);
}


unsigned long Get_Playback_Position(VQAHandle *vqa, Ahandle *audio, VQAConfig *config)
{
	VQAHandleP *vqap = (VQAHandleP *)vqa;

	assert(vqap != 0);
	assert(audio != 0);
	assert(config != 0);

	unsigned long s = audio->SecondaryBufferSize;
	assert(s > 0);

	unsigned long dma_diff;
	unsigned long totalbytes;
	DWORD				play_cursor;		//Position that direct sound is reading from
	DWORD				write_cursor;		//Position in buffer that we can write to

	EnterCriticalSection(&audio->CriticalSection);

	long r = vqap->RepeatedBuffers;
	long l = audio->LastChunkPosition;
	long m = audio->ChunksMovedToAudioBuffer;

	totalbytes = config->HMIBufSize * m;

	if (audio->SecondaryBufferPtr &&
		audio->SecondaryBufferPtr->GetCurrentPosition (&play_cursor, &write_cursor) == S_OK) {
		if (l) {
			totalbytes += play_cursor;
		} else {
			if (play_cursor >= (s / 2)) {
				totalbytes += play_cursor - (s / 2);
			} else {
				if (totalbytes > 0) {
					totalbytes += play_cursor + (s / 2);
				}
			}
		}
	}

	LeaveCriticalSection(&audio->CriticalSection);

	dma_diff = totalbytes - config->HMIBufSize * r;
	if (dma_diff > totalbytes) {
		dma_diff = 0;
	}
	return(dma_diff);
}


long __cdecl Stream_Audio_Handler(VQAHandle *vqa, long action, void *buffer, long nbytes)
{
	VQAHandleP *vqap = (VQAHandleP *)vqa;
	VQAConfig *config;

	long error = VQAERR_NONE;

	switch (action) {

		case VQAAUDIO_INIT:
			config = &vqap->Config;
			config->TimerCallback = Simple_Timer_Callback_Audio_Handler;
			config->RefreshRate = VQA_TIMETICKS;
			break;

		case VQAAUDIO_OPEN:
			error = Open_Audio_Handler((VQAHandleP *)vqa, (AhandleInitParams *)buffer, nbytes);
			break;

		case VQAAUDIO_CLOSE:
			error = Close_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_START:
			error = Start_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_LOAD:
			error = Load_Audio_Handler((VQAHandleP *)vqa, buffer, nbytes);
			break;

		case VQAAUDIO_PAUSE:
			error = Pause_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_PLAY:
			error = Play_Audio_Handler((VQAHandleP *)vqa);
			break;

		case VQAAUDIO_STOP:
			error = Stop_Audio_Handler((VQAHandleP *)vqa);
			break;
	}

	return(error);
}


long __cdecl Open_Audio_Handler(VQAHandleP *vqap, AhandleInitParams *params, long b)
{
	DSCAPS dscaps;

	DebugString("Opening VQ audio handler\n");
	DebugString("Current thread ID is %08x\n", GetCurrentThreadId());

	if (!Audio_Available()) {
		return(VQAERR_AUDIO);
	}

	int index = 0;
	while (index < Ahandle::MAX_HANDLES) {
		if (!_handles[index].Used) {
			break;
		}
		index++;
	}

	if (index < Ahandle::MAX_HANDLES && params != NULL && b == static_cast<long>(sizeof(AhandleInitParams))) {

		VQAConfig *config = &vqap->Config;

		vqap->AudioHandleIndex = index;

		Ahandle *handle = &_handles[index];
		memset(handle, 0, sizeof(*handle));
		handle->Used = TRUE;
		handle->Volume = config->Volume;

		InitializeCriticalSection(&handle->CriticalSection);

		if (config->AudioRate != -1) {
			handle->SampleRate = config->AudioRate;
		} else {
			if (config->FrameRate != vqap->FrameRate) {
				handle->SampleRate = params->SampleRate * (unsigned)config->FrameRate / vqap->FrameRate;
			} else {
				handle->SampleRate = params->SampleRate;
			}
		}

		memset(&dscaps, 0, sizeof(dscaps));
		dscaps.dwSize = sizeof(DSCAPS);

		DebugString("Audio.Lock_Mutex\n");
		Audio.Lock_Mutex();

		Direct_Sound_Object()->GetCaps(&dscaps);

		if (dscaps.dwFlags & DSCAPS_EMULDRIVER) {
			DebugString("Ahandle detected emulated sound driver. LatencyAdjustment = %ld ticks\n", config->LatencyAdjustment);
		} else {
			config->LatencyAdjustment = 0;
		}

		DebugString("Audio.Get_Primary_Buffer()\n");

		memset(&_restore_format, 0, sizeof(_restore_format));
		WAVEFORMATEX format;
		memset(&format, 0, sizeof(format));

		DebugString("Getting current primary buffer format\n");
		Direct_Sound_Primary_Buffer()->GetFormat(&_restore_format, sizeof(_restore_format), NULL);

		_restore_primary = false;
		if (params->Channels != _restore_format.nChannels || params->SampleRate != _restore_format.nSamplesPerSec || params->BitsPerSample != _restore_format.wBitsPerSample) {
			DebugString("Changing primary buffer format\n");
			Audio.Stop_Primary_Sound_Buffer();
			DebugString("Primary buffer stopped\n");
			_restore_primary = true;

			format.wFormatTag		= WAVE_FORMAT_PCM;
			format.nSamplesPerSec	= params->SampleRate;
			format.nChannels		= params->Channels;
			format.wBitsPerSample	= (short) params->BitsPerSample;
			format.nBlockAlign		= (unsigned short)( (params->BitsPerSample/8) * params->Channels);
			format.nAvgBytesPerSec	= params->SampleRate * format.nBlockAlign;

			Direct_Sound_Primary_Buffer()->SetFormat(&format);
			DebugString("Primary buffer format changed\n");
			Audio.Start_Primary_Sound_Buffer(false);
		}

		DebugString("Audio.Unlock_Mutex\n");
		Audio.Unlock_Mutex();

		handle->Channels = params->Channels;
		handle->BitsPerSample = params->BitsPerSample;
		handle->InitFlags = params->Flags;
		_AHandleCallbackFunc1 = (AHANDLE_CALLBACK_1)params->Callback1;
		_AHandleCallbackFunc2 = (AHANDLE_CALLBACK_2)params->Callback2;

		DebugString("Calling timeBeginPeriod\n");
		if (timeBeginPeriod(1000/VQA_TIMETICKS) != TIMERR_NOCANDO) {
			DebugString("Creating VQ audio timer thread\n");
			// Set orf 60hz timer
			handle->TimerHandle = timeSetEvent ( 1000/VQA_TIMETICKS , 1 , AudioCallback , reinterpret_cast<DWORD_PTR>(vqap) , TIME_PERIODIC);
			DebugString("VQ audio handler opened OK\n");
			if (handle->TimerHandle != 0) {
				return(VQAERR_NONE);
			}
			return(VQAERR_AUDIO);
		}
	}
	return(VQAERR_AUDIO);
}


long __cdecl Close_Audio_Handler(VQAHandleP *vqap)
{
	DebugString("Closing VQ audio handler\n");

	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (handle->Used == 1) {

		if (handle->SecondaryBufferPtr != NULL) {
			DebugString("Stop_Audio_Handler\n");
			Stop_Audio_Handler(vqap);
		}

		DebugString("Calling timeKillEvent\n");
		timeKillEvent(handle->TimerHandle);
		handle->TimerHandle = 0;

		DebugString("Calling timeEndPeriod\n");
		timeEndPeriod(1000/VQA_TIMETICKS);

		handle->Used = false;

		if (_restore_primary) {
			DebugString("Changing primary buffer format back to original\n");
			_restore_primary = false;

			DebugString("Audio.Lock_Mutex()\n");
			Audio.Lock_Mutex();

			DebugString("Stopping primary buffer\n");
			Audio.Stop_Primary_Sound_Buffer();

			DebugString("Calling SetFormat\n");
			Direct_Sound_Primary_Buffer()->SetFormat(&_restore_format);

			Audio.Start_Primary_Sound_Buffer(false);

			DebugString("Audio.Unlock_Mutex()\n");
			Audio.Unlock_Mutex();
		}
		DebugString("Deleting the critical section object\n");
		DeleteCriticalSection(&handle->CriticalSection);
	}

	DebugString("VQ audio handler closed OK\n");
	return(VQAERR_NONE);
}


long __cdecl Start_Audio_Handler(VQAHandleP *vqap)
{
	/* Dereference commonly used data members for quicker access. */
	VQAConfig *config = &vqap->Config;
	Ahandle *audio = &_handles[vqap->AudioHandleIndex];

	if (audio->Flags & AHANDLEF_IS_PAUSED) {
		return(Play_Audio_Handler(vqap));
	}

	EnterCriticalSection(&audio->CriticalSection);

	assert(audio->SecondaryBufferPtr == NULL);

	/*
	**	If we already have a direct sound secondary buffer then get rid of it
	*/
	if (audio->SecondaryBufferPtr != NULL){
		audio->SecondaryBufferPtr->Stop();
		audio->SecondaryBufferPtr->Release();
		audio->SecondaryBufferPtr = NULL;
	}

	/*
	**	Make it big enough for 2 blocks of HMI data
	*/
	audio->SecondaryBufferSize = config->HMIBufSize*2;

	/*
	**	Define the format for the secondary sound buffer
	*/
	memset (&audio->BufferDesc , 0 , sizeof(DSBUFFERDESC));
	audio->BufferDesc.dwSize				= sizeof(DSBUFFERDESC);
	audio->BufferDesc.dwFlags				= DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2;
	audio->BufferDesc.dwBufferBytes		= audio->SecondaryBufferSize;
	audio->BufferDesc.lpwfxFormat 		= (LPWAVEFORMATEX) &audio->DsBuffFormat;
	memset (&audio->DsBuffFormat , 0 , sizeof(WAVEFORMATEX));
	audio->DsBuffFormat.wFormatTag		= WAVE_FORMAT_PCM;
	audio->DsBuffFormat.nSamplesPerSec	= audio->SampleRate;
	audio->DsBuffFormat.nChannels			= audio->Channels;
	audio->DsBuffFormat.wBitsPerSample	= audio->BitsPerSample;
	audio->DsBuffFormat.nBlockAlign		= (short) ((audio->DsBuffFormat.wBitsPerSample/8) * audio->DsBuffFormat.nChannels);
	audio->DsBuffFormat.nAvgBytesPerSec	= audio->DsBuffFormat.nSamplesPerSec * audio->DsBuffFormat.nBlockAlign;

	/*
	**	Create the secondary sound buffer object
	*/
	Audio.Lock_Mutex();
	HRESULT create_result = Direct_Sound_Object()->CreateSoundBuffer (&audio->BufferDesc , &audio->SecondaryBufferPtr , NULL);
	Audio.Unlock_Mutex();
	Trace_Audio_Result("CreateSoundBuffer", create_result, audio, 0, audio->SecondaryBufferSize);

	if (audio->SecondaryBufferPtr == NULL) {
		LeaveCriticalSection(&audio->CriticalSection);
		return(VQAERR_AUDIO);
	}

	/* Start playback */
	_AHandleCallbackFunc1((VQAHandle *)vqap);
	_AHandleCallbackFunc1((VQAHandle *)vqap);
	audio->AudioBufWriteIndex = 0;
	audio->EndLastAudioChunk = 0;
	audio->ChunksMovedToAudioBuffer = 0;
	audio->SecondaryBufferPtr->SetCurrentPosition (0);

	/*
	**	Set the volume
	*/
	LONG requested_volume = Convert_HMI_To_Direct_Sound_Volume(audio->Volume & 255);
	HRESULT volume_result = audio->SecondaryBufferPtr->SetVolume(requested_volume);
	if (Audio_Trace_Operation()) {
		LONG actual_volume = 0;
		HRESULT volume_read_result = audio->SecondaryBufferPtr->GetVolume(&actual_volume);
		DebugString("VQA audio trace volume: input=%u requested=%ld set_hr=%08lX actual=%ld get_hr=%08lX\n", audio->Volume & 255,
			requested_volume, static_cast<unsigned long>(volume_result), actual_volume, static_cast<unsigned long>(volume_read_result));
	}

	HRESULT return_code = audio->SecondaryBufferPtr->Play(0, 0, DSBPLAY_LOOPING);
	Trace_Audio_Result("Play", return_code, audio);
	LeaveCriticalSection(&audio->CriticalSection);
	return(return_code == DS_OK ? VQAERR_NONE : VQAERR_AUDIO);
}


long __cdecl Load_Audio_Handler(VQAHandleP *vqap, void *buffer, long nbytes)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (buffer != NULL && nbytes != 0) {
		EnterCriticalSection(&handle->CriticalSection);
		if (handle->AudioBufInUse[handle->AudioBufReadIndex] == TRUE) {
			LeaveCriticalSection(&handle->CriticalSection);
			return(VQAERR_AUDIO);
		}
		int readindex = handle->AudioBufReadIndex;
		if (nbytes > 0) Trace_Audio_PCM(false, handle, buffer, static_cast<unsigned>(nbytes));
		handle->AudioBuf[readindex] = buffer;
		handle->AudioBufSize[readindex] = nbytes;
		handle->AudioBufInUse[readindex] = TRUE;
		Move_HMI_Audio_Block_To_Direct_Sound_Buffer(vqap);
		int index = readindex + 1;
		if (index >= Ahandle::MAX_BUFFERS) {
			index = 0;
		}
		handle->AudioBufReadIndex = index;
		LeaveCriticalSection(&handle->CriticalSection);
		return(VQAERR_NONE);
	}
	return(VQAERR_AUDIO);
}


long __cdecl Pause_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	EnterCriticalSection(&handle->CriticalSection);

	if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {
		handle->SecondaryBufferPtr->Stop();
	}
	handle->Flags |= AHANDLEF_IS_PAUSED;

	LeaveCriticalSection(&handle->CriticalSection);

	return(VQAERR_NONE);
}


long __cdecl Play_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	long rc;
	if (!handle->Used || handle->SecondaryBufferPtr == NULL) {
		return(VQAERR_AUDIO);
	}

	EnterCriticalSection(&handle->CriticalSection);

	rc = handle->SecondaryBufferPtr->Play(0, 0, DSBPLAY_LOOPING);
	Trace_Audio_Result("Resume", rc, handle);
	if (rc == S_OK) {
		handle->PauseAdjust = Simple_Timer_Callback_Audio_Handler(NULL) - handle->LastTimerTick;
		DebugString("Ahandle: PauseAdjust %ld\n", handle->PauseAdjust);
		handle->Flags &= ~AHANDLEF_IS_PAUSED;
		rc = VQAERR_NONE;
	} else {
		rc = VQAERR_AUDIO;
	}
	LeaveCriticalSection(&handle->CriticalSection);

	return(rc);
}


long __cdecl Stop_Audio_Handler(VQAHandleP *vqap)
{
	Ahandle *handle = &_handles[vqap->AudioHandleIndex];

	if (handle->SecondaryBufferPtr != NULL) {

		EnterCriticalSection(&handle->CriticalSection);
		handle->SecondaryBufferPtr->Stop();
		handle->SecondaryBufferPtr->Release();
		handle->SecondaryBufferPtr = NULL;
		handle->AudioBufReadIndex = 0;
		handle->AudioBufWriteIndex = 0;
		for (int i = 0; i < Ahandle::MAX_BUFFERS; i++) {
			handle->AudioBufInUse[i] = false;
		}

		LeaveCriticalSection(&handle->CriticalSection);
	}
	return(VQAERR_NONE);
}


/****************************************************************************
*
* NAME
*     AudioCallback - Sound system callback.
*
* SYNOPSIS
*     AudioCallback(DriverHandle, Action, SampleID)
*
*     void AudioCallback(WORD, WORD, WORD);
*
* FUNCTION
*     Our custom audio callback routine that services HMI.
*
* INPUTS
*     DriverHandle - HMI driver handle.
*     Action       - Action taken.
*     SampleID     - ID of sample.
*
* RESULT
*     NONE
*
****************************************************************************/
void CALLBACK AudioCallback ( UINT uTimerID, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR )
{
	Ahandle  	*audio;
	DWORD			play_cursor;		//Position that direct sound is reading from
	DWORD			write_cursor;		//Position in buffer that we can write to
	HRESULT		return_code;
	bool			buffer_stopped = false;
	DWORD			status;

	VQAHandle *vqa = (VQAHandle *)dwUser;
	VQAHandleP *vqap = (VQAHandleP *)dwUser;

	audio = &_handles[vqap->AudioHandleIndex];

	if (InterlockedIncrement(&audio->SuspendAudioCallback) != TRUE || (audio->Flags & AHANDLEF_IS_PAUSED)) {
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	EnterCriticalSection(&audio->CriticalSection);

	if (!audio->SecondaryBufferPtr || audio->TimerHandle != uTimerID)  {
		LeaveCriticalSection(&audio->CriticalSection);
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	return_code = audio->SecondaryBufferPtr->GetStatus(&status);
	if (!(status & DSBSTATUS_PLAYING) && !(status & DSBSTATUS_LOOPING)) {
		LeaveCriticalSection(&audio->CriticalSection);
		InterlockedDecrement(&audio->SuspendAudioCallback);
		return;
	}

	/*
	**	See if we are nearing the end of the meaningful data in the direct sound buffer
	*/
	return_code = audio->SecondaryBufferPtr->GetCurrentPosition (&play_cursor , &write_cursor);

	bool write_more = false;

	if (return_code == DSERR_BUFFERLOST) {
		audio->SecondaryBufferPtr->Restore();
		audio->SecondaryBufferPtr->Stop();
		buffer_stopped = true;
		audio->SecondaryBufferPtr->SetCurrentPosition (0);
		audio->LastChunkPosition = 0;
		audio->EndLastAudioChunk = 0;
		write_more = true;
	}


	if (play_cursor < audio->EndLastAudioChunk){
		write_more = true;
	}else{
		if ((play_cursor >= audio->SecondaryBufferSize / 2) && audio->EndLastAudioChunk==0){
			write_more = true;
		}
	}

	/*
	**	See if we need to fill the buffer
	*/
	if (write_more == true) {
		_AHandleCallbackFunc2((VQAHandle *)vqa, audio->AudioBuf[audio->AudioBufWriteIndex]);
		audio->ChunksMovedToAudioBuffer++;
		int index = audio->AudioBufWriteIndex + 1;
		if (index >= Ahandle::MAX_BUFFERS) {
			index = 0;
		}
		audio->AudioBufInUse[audio->AudioBufWriteIndex] = FALSE;
		_AHandleCallbackFunc1((VQAHandle *)vqa);

		if (audio->AudioBufInUse[index]) {
			audio->AudioBufWriteIndex = index;
		}

		/*
		**	Start the buffer playing again if we had to stop it
		*/
		if (buffer_stopped == true) {
			HRESULT play_result = audio->SecondaryBufferPtr->Play(0,0,DSBPLAY_LOOPING);
			Trace_Audio_Result("TimerResume", play_result, audio);
		}
	}
	LeaveCriticalSection(&audio->CriticalSection);
	InterlockedDecrement(&audio->SuspendAudioCallback);
}


/***********************************************************************************************
 * Move_HMI_Audio_Block_To_Direct_Sound_Buffer -- moves an audio block which would have been   *
 *                                                played by HMI into a direct sound            *
 *                                                secondary buffer                             *
 *                                                                                             *
 * INPUT:    Nothing                                                                           *
 *                                                                                             *
 * OUTPUT:   BOOL was block moved                                                              *
 *                                                                                             *
 * WARNINGS: None                                                                              *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *    12/21/95 9:51AM ST : Created                                                             *
 *=============================================================================================*/
BOOL Move_HMI_Audio_Block_To_Direct_Sound_Buffer(VQAHandleP *vqap)
{
	Ahandle  	*audio;

	LPVOID		play_buffer_ptr1;   //Beginning of locked area of buffer
	LPVOID		play_buffer_ptr2;   //Length of locked area in buffer
	DWORD			lock_length1;   //Beginning of second locked area in buffer
	DWORD			lock_length2;   //Length of second locked area in buffer
	unsigned		next_fill_pos;
	HRESULT		return_code;

	audio = &_handles[vqap->AudioHandleIndex];
	VQAConfig *config = &vqap->Config;


	if (audio->SecondaryBufferPtr == NULL) {
		return(0);
	}

	/*************************************************************************
	**
	**	Copy the data from the HMI play position into the direct sound buffer
	**
	*/
	next_fill_pos = audio->EndLastAudioChunk;

	/*
	**	Lock the buffer to get a pointer to it
	*/
	return_code= audio->SecondaryBufferPtr->Lock ((DWORD)next_fill_pos,
																	(DWORD)config->HMIBufSize,
																	&play_buffer_ptr1,
																	&lock_length1,
																	&play_buffer_ptr2,
																	&lock_length2,
																	0 );

	Trace_Audio_Result("Lock", return_code, audio, next_fill_pos, config->HMIBufSize);
	if (return_code!=DS_OK) return(FALSE);

	int index = audio->AudioBufReadIndex;

	/*
	**	Copy the HMI audio buffer to the direct sound buffer
	*/
	if (audio->AudioBufSize[index] > lock_length1) {

		memcpy((char*)play_buffer_ptr1, audio->AudioBuf[index], lock_length1);

		if (audio->AudioBufSize[index] - lock_length1 > lock_length2) {
			memcpy(play_buffer_ptr2, audio->AudioBuf[index], lock_length2);
		} else {
			memcpy(play_buffer_ptr2, audio->AudioBuf[index], audio->AudioBufSize[index] - lock_length1);
		}

	} else {
		memcpy(play_buffer_ptr1, audio->AudioBuf[index], audio->AudioBufSize[index]);
	}


	/*
	**	Unlock the direct sound buffer
	*/
	unsigned copied_first = audio->AudioBufSize[index] < lock_length1 ? audio->AudioBufSize[index] : lock_length1;
	Trace_Audio_PCM(true, audio, play_buffer_ptr1, copied_first);
	audio->SecondaryBufferPtr->Unlock(play_buffer_ptr1,
												lock_length1,
												play_buffer_ptr2,
												lock_length2);

	/*
	**	Update our audio data pointers
	*/
	audio->LastChunkPosition = next_fill_pos;
	audio->EndLastAudioChunk = next_fill_pos + audio->AudioBufSize[index];
	if (audio->EndLastAudioChunk >= audio->SecondaryBufferSize) {
		audio->EndLastAudioChunk = 0;
	}

	return(TRUE);
}


unsigned long __cdecl Simple_Timer_Callback_Audio_Handler(VQAHandle *vqa)
{
	return(Get_Game_Time_50());
}


void Pause_All_Audio_Handler(void)
{
	for (int i = 0; i < Ahandle::MAX_HANDLES; i++) {
		Ahandle *handle = &_handles[i];
		if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {

			EnterCriticalSection(&handle->CriticalSection);

			handle->SecondaryBufferPtr->Stop();
			handle->Flags |= AHANDLEF_IS_PAUSED;

			LeaveCriticalSection(&handle->CriticalSection);

		}
	}
}


void Resume_All_Audio_Handler(void)
{
	for (int i = 0; i < Ahandle::MAX_HANDLES; i++) {
		Ahandle *handle = &_handles[i];
		if (handle->Used == true && handle->SecondaryBufferPtr != NULL) {

			EnterCriticalSection(&handle->CriticalSection);

			handle->SecondaryBufferPtr->Play(0, 0, DSBPLAY_LOOPING);
			handle->Flags &= ~AHANDLEF_IS_PAUSED;

			LeaveCriticalSection(&handle->CriticalSection);

		}
	}
}


long __cdecl Lock_Audio_Handler(void)
{
	/// nothing in win32
	return(1);
}


long __cdecl Unlock_Audio_Handler(void)
{
	/// nothing in win32
	return(1);
}
