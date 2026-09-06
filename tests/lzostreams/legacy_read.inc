/*******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Frozen reader body from the pre-migration CStream implementation.
HRESULT CStreamClass::Read(void *pv, ULONG cb, ULONG *pcbRead)
{
	int read_size;
	int left;

	read_size = cb;
	left = cb;

	if (pv == NULL) {
		return(E_POINTER);
	}

	if (read_size < 0) {
		return(E_INVALIDARG);
	}

	if (StreamPtr == NULL) {
		return(E_FAIL);
	}

	if (IsWriting) {
		return(E_FAIL);
	}

	IsReading = true;

	if (pcbRead != NULL) {
		*pcbRead = 0;
	}

	while (left > 0) {

		int offset = CurOffset;
		if (offset > 0) {
			int len = left;
			if (left >= offset) {
				len = CurOffset;
			}
			memmove(pv, (char *)DataBuffer + BlockHead.UncompSize - offset, len);
			pv = (char *)pv + len;
			left -= len;
			CurOffset -= len;
		}

		if (left == 0) {
			break;
		}

		ULONG read = 0;

		HRESULT hr = StreamPtr->Read(&BlockHead, sizeof(BlockHead), &read);
		if (FAILED(hr)) {
			return(hr);
		}

		if (read != sizeof(BlockHead)) {
			return(E_FAIL);
		}

		hr = StreamPtr->Read(StreamBuffer, BlockHead.CompSize, &read);
		if (FAILED(hr)) {
			return(hr);
		}

		unsigned int inlen = BlockHead.CompSize;
		if (read != inlen) {
			return(E_FAIL);
		}
		lzo_byte *out = (lzo_byte *)DataBuffer;
		lzo_byte *in = (lzo_byte *)StreamBuffer;
		unsigned int out_len = BUFFER_SIZE;
		lzo1x_decompress(in, inlen, out, &out_len, 0);
		CurOffset = BlockHead.UncompSize;
	}

	if (pcbRead != NULL) {
		*pcbRead = read_size;
	}

	return(S_OK);
}
