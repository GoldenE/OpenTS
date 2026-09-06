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

/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// Frozen from 56a6f99 for Win32 output comparison; never used by the engine.
#include <cstdint>
#include <cstring>
#include <cassert>
extern "C" unsigned short * HicolorTable;
namespace FrozenVQA {
static inline void memset32(void * destination, unsigned int value, unsigned int count) { auto * out = static_cast<unsigned int *>(destination); while (count--) *out++ = value; }
void __cdecl UnVQ2_C1_4x4(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t block_row_stride = bufwidth * 4u;

	uint8_t * data_end = (uint8_t *)buffer + ((uint32_t)numrows * bufwidth * 4u);
	uint8_t * dst = (uint8_t *)buffer;

	uint8_t * row_base = (uint8_t *)buffer;
	uint8_t * row_end = row_base + ((uint32_t)blocksperrow * 8u);

	uint16_t * src = (uint16_t *)pointers;

	while (dst < data_end) {

		uint16_t command = *src;
		int tag = command & 0xF000;
		uint32_t count = command;
		count &= 0xFFFF0FFFu;
		src += 1;

		switch (tag) {

		case 0x0000u: {
			uint32_t n = 2u * count; /* dwords per 4x4 block run per row */
			/* fill count blocks with a single 16-bit color */
			uint32_t w = *src;
			uint32_t v = (w << 16) | w;

			src += 1;

			{
				unsigned int * p = (unsigned int *)(dst);
				unsigned int left = n;
				while (left > 0) {
					*p++ = v;
					left--;
				}
			}
			{
				unsigned int * p = (unsigned int *)(dst + bufwidth);
				unsigned int left = n;
				while (left > 0) {
					*p++ = v;
					left--;
				}
			}
			{
				unsigned int * p = (unsigned int *)(dst + 2u * bufwidth);
				unsigned int left = n;
				while (left > 0) {
					*p++ = v;
					left--;
				}
			}
			{
				unsigned int * p = (unsigned int *)(dst + 3u * bufwidth);
				unsigned int left = n;
				while (left > 0) {
					*p++ = v;
					left--;
				}
			}

			dst += 8u * count; /* advance horizontally by count blocks */
		} break;

		case 0x1000u: {
			/* skip count blocks */
			dst += 8u * count;
		} break;

		case 0x2000u: {
			/* cb = codebook + 32 * *(uint16_t*)pointers; pointers += 2; */
			const uint8_t * cb = codebook + ((uint32_t)*src << 5);
			src += 1;

			for (uint32_t r = 0; r < 4; r++) {

				/* lo = assembled from two 16-bit loads */
				uint32_t lo = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;

				/* write left dword of each block across the row (stride 8) */
				uint32_t i;
				{
					uint8_t * p = dst;

					for (i = 0; i < count; i++) {
						*(uint32_t *)p = lo;
						p += 8;
					}
				}

				/* hi = assembled from two 16-bit loads */
				uint32_t hi = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;

				/* write right dword (dst + 4) */
				{
					uint8_t * p = dst + 4;

					for (i = 0; i < count; i++) {
						*(uint32_t *)p = hi;
						p += 8;
					}
				}

				dst += bufwidth;
			}

			/* final pointer correction: dst += count*8 - bufwidth*4 */
			dst += (count * 8u) - block_row_stride;
		} break;

		case 0x3000u: {
			/* 4x4 from codebook, but each 16-bit entry can be "masked" by high bit */
			uint16_t cb_index = *src;
			const uint8_t * cb = codebook + 32u * (uint32_t)cb_index;
			uint32_t r;

			src += 1;

			for (r = 0; r < 4; ++r) {
				uint8_t * rowp = dst;
				uint32_t c;

				for (c = 0; c < 4; ++c) {
					uint16_t px = *(const uint16_t *)cb;

					if ((px & 0x8000u) == 0) {
						uint8_t * p = rowp;
						uint32_t i;

						for (i = 0; i < count; ++i) {
							*(uint16_t *)p = px;
							p += 8;
						}
					}

					rowp += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u * count - block_row_stride;
		} break;


		case 0x5000u: {
			/* 0x5000: draw `count` solid 4x4 blocks,
			   each block uses the next 16-bit color from the stream. */
			uint32_t i;

			/* the row step is deliberately rounded down to a multiple of 4 */
			uint32_t step = 4u * (bufwidth >> 2);
			for (i = 0; i < count; i++) {

				uint32_t j;
				uint32_t v;
				uint32_t r = 4;
				uint8_t * p;

				/* load 16-bit color and replicate to 32-bit */
				{
					uint32_t w = *src;
					v = (w << 16) | w;
					src += 1;
				}

				p = dst;

				/* write 4 rows of the 4x4 solid block */
				for (j = 0; j < r; j++) {
					*(uint32_t *)(p + 0) = v;
					*(uint32_t *)(p + 4) = v;
					p += step;
				}

				dst += 8;
			}
		} break;


		case 0x6000u: {

			/* 0x6000: draw `count` consecutive 4x4 blocks, each block has its own
			   codebook index in the pointer stream (no horizontal replication of one index). */

			uint32_t i;

			/* the row step is deliberately rounded down to a multiple of 4 */
			uint32_t step = 4u * (bufwidth >> 2);
			for (i = 0; i < count; i++) {

				/* cb = codebook + 32 * *(uint16_t*)pointers; pointers += 2; */
				const uint8_t * cb = codebook + ((uint32_t)*src << 5);
				src += 1;

				uint8_t * p = dst;
				for (uint32_t r = 0; r < 4; ++r) {
					uint32_t lo;
					uint32_t hi;

					/* lo = assembled from two 16-bit loads */
					lo = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
					cb += 4;
					*(uint32_t *)(p + 0) = lo;

					/* hi = assembled from two 16-bit loads */
					hi = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
					cb += 4;
					*(uint32_t *)(p + 4) = hi;

					p += step;
				}

				dst += 8;
			}
		} break;

		case 0x7000u: {

			uint32_t i;

			for (i = 0; i < count; ++i) {
				uint16_t cb_index = *src;
				const uint8_t * cb = codebook + 32u * (uint32_t)cb_index;
				src += 1;

				uint8_t * p = dst;
				for (uint32_t r = 0; r < 4; ++r) {
					uint8_t * q = p;

					for (uint32_t c = 0; c < 4; ++c) {
						uint16_t px = *(const uint16_t *)cb;

						if ((px & 0x8000u) == 0) {
							*(uint16_t *)q = px;
						}

						q += 2;
						cb += 2;
					}

					p += 2u * (bufwidth >> 1);
				}

				dst += 8;
			}
		} break;
		}


		/* wrap to next 4-row macro-row after blocksperrow blocks */
		if (dst == row_end) {
			row_base += block_row_stride;
			dst = row_base;
			row_end = row_base + (8u * (uint32_t)blocksperrow);
		}
	}
}

void __cdecl UnVQ1_C4_4x4(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t block_row_stride = bufwidth * 4u;

	uint8_t * end = (uint8_t *)buffer + ((uint32_t)numrows * bufwidth * 4u);
	uint8_t * dst = (uint8_t *)buffer;

	uint8_t * row_base = (uint8_t *)buffer;
	uint8_t * row_end = row_base + ((uint32_t)blocksperrow * 8u);

	uint16_t * src = (uint16_t *)pointers;

	while (dst < end) {

		uint32_t command = (*src & 0xE000);
		uint16_t cb_index = (*src & 0x1FFF);

		src += 1;

		switch (command) {

		/* ------------------------------------------------------------ */
		/* 0x0000 - unmasked 4x4 from codebook                          */
		/* ------------------------------------------------------------ */
		case 0x0000u: {
			const uint8_t * cb = (const uint8_t *)codebook + (cb_index << 5);

			for (uint32_t r = 0; r < 4; r++) {
				uint32_t lo;
				uint32_t hi;

				lo = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 0) = lo;

				hi = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 4) = hi;

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x2000 - masked 4x4 from codebook (0x8000 = transparent)     */
		/* ------------------------------------------------------------ */
		case 0x2000u: {
			const uint8_t * cb = (const uint8_t *)codebook + (cb_index << 5);

			for (uint32_t r = 0; r < 4; r++) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; c++) {
					uint16_t v = *(const uint16_t *)cb;

					if (v != 0x8000u) {
						*(uint16_t *)p = v;
					}

					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x4000 - horizontal skip of one 4x4 block                    */
		/* ------------------------------------------------------------ */
		case 0x4000u: {
			dst += 8;
		} break;

		/* ------------------------------------------------------------ */
		/* default - fall-through                                       */
		/* ------------------------------------------------------------ */
		default:
			break;
		}

		/* macro-row wrap */
		if (dst == row_end) {
			row_base += block_row_stride;
			dst = row_base;
			row_end = row_base + ((uint32_t)blocksperrow * 8u);
		}
	}
}

void __cdecl UnVQ2_C4_4x4(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t block_row_stride = bufwidth * 4u;

	uint8_t * end = (uint8_t *)buffer + ((uint32_t)numrows * bufwidth * 4u);
	uint8_t * dst = (uint8_t *)buffer;

	uint8_t * row_base = (uint8_t *)buffer;
	uint8_t * row_end = row_base + ((uint32_t)blocksperrow * 8u);

	uint8_t * src = pointers;

	uint32_t height;
	uint32_t code;

	while (dst < end) {

		uint32_t command = (*(uint16_t *)src & 0xE000);
		uint16_t cb_index = (*(uint16_t *)src & 0x1FFF);
		src += 2;

		switch (command) {

		/* ------------------------------------------------------------ */
		/* 0x4000 - patterned column replicate                          */
		/* ------------------------------------------------------------ */
		case 0x4000u: {
			const uint8_t * cb = codebook + (*(src - 2) << 5);
			uint8_t * rowp = dst;
			uint32_t repeat = ((cb_index >> 7) & 0x3Eu) + 2u;
			uint32_t step = 4u * (bufwidth >> 2);

			for (uint32_t r = 0; r < 4; ++r) {
				uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(rowp + 0) = lo;

				uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(rowp + 4) = hi;

				rowp += step;
			}

			uint8_t * cursor = src;
			dst += 8;

			for (uint32_t i = 0; i < repeat; i++) {
				const uint8_t * cb2 = codebook + (*cursor << 5);
				cursor += 1;

				uint8_t * p = dst;
				for (uint32_t r = 0; r < 4; ++r) {
					uint32_t lo = ((uint32_t)*(uint16_t *)(cb2 + 2) << 16) | *(uint16_t *)(cb2 + 0);
					cb2 += 4;
					*(uint32_t *)(p + 0) = lo;

					uint32_t hi = ((uint32_t)*(uint16_t *)(cb2 + 2) << 16) | *(uint16_t *)(cb2 + 0);
					cb2 += 4;
					*(uint32_t *)(p + 4) = hi;

					p += step;
				}
				dst += 8;
			}

			src = cursor;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x2000 - vertical run fill (0xA000 shares the body)          */
		/* ------------------------------------------------------------ */
		case 0x2000u: {
			code = cb_index;
			code &= 0xFFFFu;
			height = ((code >> 7) & 0x3Eu) + 2u;

			if (height != 0u) {
				goto label_masked_index;
			}

		label_run_length:
			cb_index &= 0xFFFFu;
			height = *src;
			src += 1;
			goto label_vertical_fill;
		}

		/* ------------------------------------------------------------ */
		/* 0x0000 - skip N blocks horizontally                          */
		/* ------------------------------------------------------------ */
		case 0x0000u: {
			dst += 8u * (uint8_t)cb_index;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x6000 - unmasked solid 4x4 (no pointer read)                */
		/* ------------------------------------------------------------ */
		case 0x6000u: {
			const uint8_t * cb = codebook + ((cb_index & 0xFFFFu) << 5);

			for (uint32_t r = 0; r < 4; ++r) {
				uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 0) = lo;

				uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 4) = hi;

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0xC000 - masked vertical run                                 */
		/* ------------------------------------------------------------ */
		case 0xC000u: {
			height = *src;
			src += 1;

			const uint8_t * cb = codebook + (32u * (uint16_t)cb_index);

			for (uint32_t r = 0; r < 4; ++r) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; ++c) {
					uint16_t v = *(uint16_t *)cb;

					if (v != 0x8000u) {
						uint8_t * q = p;
						uint32_t i;
						for (i = 0; i < height; i++) {
							*(uint16_t *)q = v;
							q += 8;
						}
					}

					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u * height - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0xA000 - masked vertical, shares the 0x2000 run-length head  */
		/* ------------------------------------------------------------ */
		case 0xA000u:
			goto label_run_length;

		/* ------------------------------------------------------------ */
		/* shared 0x2000/0xA000 vertical run fill                       */
		/* ------------------------------------------------------------ */
		label_masked_index:
			cb_index = (uint8_t)code;

		label_vertical_fill:
			{
				const uint8_t * cb = codebook + (32u * cb_index);

				for (uint32_t r = 0; r < 4; ++r) {

					uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
					cb += 4;

					{
						uint8_t * p = dst;
						uint32_t i;
						for (i = 0; i < height; i++) {
							*(uint32_t *)p = lo;
							p += 8;
						}
					}

					uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
					cb += 4;

					{
						uint8_t * p = dst + 4;
						uint32_t i;
						for (i = 0; i < height; i++) {
							*(uint32_t *)p = hi;
							p += 8;
						}
					}

					dst += bufwidth;
				}

				dst += 8u * height - block_row_stride;
			}
			break;

		/* ------------------------------------------------------------ */
		/* 0x8000 - masked 4x4 (0x8000 = transparent)                   */
		/* ------------------------------------------------------------ */
		case 0x8000u: {
			const uint8_t * cb = codebook + (32u * (uint16_t)cb_index);

			for (uint32_t r = 0; r < 4; ++r) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; ++c) {
					if (*(uint16_t *)cb != 0x8000u) {
						*(uint16_t *)p = *(uint16_t *)cb;
					}
					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;
		}

		/* ------------------------------------------------------------ */
		/* macro-row wrap                                               */
		/* ------------------------------------------------------------ */
		if (dst == row_end) {
			row_base += block_row_stride;
			dst = row_base;
			row_end = row_base + ((uint32_t)blocksperrow * 8u);
		}
	}
}

void __cdecl UnVQ1_C4_4x2(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t block_row_stride = bufwidth * 2u;

	uint8_t * end = (uint8_t *)buffer + ((uint32_t)numrows * bufwidth * 2u);
	uint8_t * dst = (uint8_t *)buffer;

	uint8_t * row_base = (uint8_t *)buffer;
	uint8_t * row_end = row_base + ((uint32_t)blocksperrow * 8u);

	uint16_t * src = (uint16_t *)pointers;

	while (dst < end) {

		uint32_t command = (*src & 0xE000);
		uint16_t cb_index = (*src & 0x1FFF);

		src += 1;

		switch (command) {

		/* ------------------------------------------------------------ */
		/* 0x0000 - unmasked 4x4 from codebook                          */
		/* ------------------------------------------------------------ */
		case 0x0000u: {
			const uint8_t * cb = (const uint8_t *)codebook + (cb_index << 4);

			for (uint32_t r = 0; r < 2; r++) {
				uint32_t lo;
				uint32_t hi;

				lo = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 0) = lo;

				hi = ((uint32_t)*(const uint16_t *)(cb + 2) << 16) | *(const uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 4) = hi;

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x2000 - masked 4x4 from codebook (0x8000 = transparent)     */
		/* ------------------------------------------------------------ */
		case 0x2000u: {
			const uint8_t * cb = (const uint8_t *)codebook + (cb_index << 4);

			for (uint32_t r = 0; r < 2; r++) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; c++) {
					uint16_t v = *(const uint16_t *)cb;

					if (v != 0x8000u) {
						*(uint16_t *)p = v;
					}

					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x4000 - horizontal skip of one 4x4 block                    */
		/* ------------------------------------------------------------ */
		case 0x4000u: {
			dst += 8;
		} break;

		/* ------------------------------------------------------------ */
		/* default - fall-through                                       */
		/* ------------------------------------------------------------ */
		default:
			break;
		}

		/* macro-row wrap */
		if (dst == row_end) {
			row_base += block_row_stride;
			dst = row_base;
			row_end = row_base + ((uint32_t)blocksperrow * 8u);
		}
	}
}

void __cdecl UnVQ2_C4_4x2(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	bufwidth *= 2u;
	uint32_t block_row_stride = bufwidth * 2u;

	uint8_t * end = (uint8_t *)buffer + ((uint32_t)numrows * bufwidth * 2u);
	uint8_t * dst = (uint8_t *)buffer;

	uint8_t * row_base = (uint8_t *)buffer;
	uint8_t * row_end = row_base + ((uint32_t)blocksperrow * 8u);

	uint8_t * src = pointers;

	uint32_t height;
	uint32_t code;

	while (dst < end) {

		uint32_t command = (*(uint16_t *)src & 0xE000);
		uint32_t cb_index = (*(uint16_t *)src & 0x1FFF);
		src += 2;

		switch (command) {

		/* ------------------------------------------------------------ */
		/* 0x4000 - patterned column replicate                          */
		/* ------------------------------------------------------------ */
		case 0x4000u: {
			const uint8_t * cb = codebook + (*(src - 2) << 4);
			uint8_t * rowp = dst;
			uint32_t repeat = ((cb_index >> 7) & 0x3Eu) + 2u;
			uint32_t step = 4u * (bufwidth >> 2);

			for (uint32_t r = 0; r < 2; ++r) {
				uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(rowp + 0) = lo;

				uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(rowp + 4) = hi;

				rowp += step;
			}

			uint8_t * cursor = src;
			dst += 8;

			for (int32_t i = 0; i < repeat; i++) {
				uint8_t * p = dst;
				const uint8_t * cb2 = codebook + (*cursor << 4);
				cursor += 1;

				for (uint32_t r = 0; r < 2; ++r) {
					uint32_t lo = ((uint32_t)*(uint16_t *)(cb2 + 2) << 16) | *(uint16_t *)(cb2 + 0);
					cb2 += 4;
					*(uint32_t *)(p + 0) = lo;

					uint32_t hi = ((uint32_t)*(uint16_t *)(cb2 + 2) << 16) | *(uint16_t *)(cb2 + 0);
					cb2 += 4;
					*(uint32_t *)(p + 4) = hi;

					p += step;
				}

				dst += 8;
			}

			src = cursor;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x2000 - vertical run fill (0xA000 shares the body)          */
		/* ------------------------------------------------------------ */
		case 0x2000u: {
			code = cb_index;
			code &= 0xFFFFu;
			height = ((code >> 7) & 0x3Eu) + 2u;

			if (height == 0u) {
			label_run_length:
				cb_index &= 0xFFFFu;
				height = *src;
				src += 1;
				goto label_vertical_fill;
			} else {
				goto label_masked_index;
			}

		}

		/* ------------------------------------------------------------ */
		/* 0x0000 - skip N blocks horizontally                          */
		/* ------------------------------------------------------------ */
		case 0x0000u: {
			dst += 8u * (uint8_t)cb_index;
		} break;

		/* ------------------------------------------------------------ */
		/* 0x6000 - unmasked solid 4x2 (no pointer read)                */
		/* ------------------------------------------------------------ */
		case 0x6000u: {
			const uint8_t * cb = codebook + ((cb_index & 0xFFFFu) << 4);

			for (uint32_t r = 0; r < 2; ++r) {
				uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 0) = lo;

				uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
				cb += 4;
				*(uint32_t *)(dst + 4) = hi;

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0xC000 - masked vertical run                                 */
		/* ------------------------------------------------------------ */
		case 0xC000u: {
			height = *src;
			src += 1;

			const uint8_t * cb = codebook + (16u * (uint16_t)cb_index);

			for (uint32_t r = 0; r < 2; ++r) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; ++c) {
					uint16_t v = *(uint16_t *)cb;

					if (v != 0x8000u) {
						uint8_t * q = p;
						for (uint32_t i = 0; i < height; i++) {
							*(uint16_t *)q = v;
							q += 8;
						}
					}

					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u * height - block_row_stride;
		} break;

		/* ------------------------------------------------------------ */
		/* 0xA000 - masked vertical, shares the 0x2000 run-length head  */
		/* ------------------------------------------------------------ */
		case 0xA000u:
			goto label_run_length;

		/* ------------------------------------------------------------ */
		/* shared 0x2000/0xA000 vertical run fill                       */
		/* ------------------------------------------------------------ */
		label_masked_index:
			cb_index = (uint8_t)code;

		label_vertical_fill:
			{
				const uint8_t * cb = codebook + (16u * cb_index);

				for (uint32_t r = 0; r < 2; ++r) {

					uint32_t lo = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
					cb += 4;

					{
						uint8_t * p = dst;
						uint32_t i;
						for (i = 0; i < height; i++) {
							*(uint32_t *)p = lo;
							p += 8;
						}
					}

					uint32_t hi = ((uint32_t)*(uint16_t *)(cb + 2) << 16) | *(uint16_t *)(cb + 0);
					cb += 4;

					{
						uint8_t * p = dst + 4;
						uint32_t i;
						for (i = 0; i < height; i++) {
							*(uint32_t *)p = hi;
							p += 8;
						}
					}

					dst += bufwidth;
				}

				dst += 8u * height - block_row_stride;
			}
			break;

		/* ------------------------------------------------------------ */
		/* 0x8000 - masked 4x2 (0x8000 = transparent)                   */
		/* ------------------------------------------------------------ */
		case 0x8000u: {
			const uint8_t * cb = codebook + (16u * (uint16_t)cb_index);

			for (uint32_t r = 0; r < 2; ++r) {
				uint8_t * p = dst;

				for (uint32_t c = 0; c < 4; ++c) {
					if (*(uint16_t *)cb != 0x8000u) {
						*(uint16_t *)p = *(uint16_t *)cb;
					}
					p += 2;
					cb += 2;
				}

				dst += bufwidth;
			}

			dst += 8u - block_row_stride;
		} break;
		}

		/* ------------------------------------------------------------ */
		/* macro-row wrap                                               */
		/* ------------------------------------------------------------ */
		if (dst == row_end) {
			row_base += block_row_stride;
			dst = row_base;
			row_end = row_base + ((uint32_t)blocksperrow * 8u);
		}
	}
}

void __cdecl UnVQ2_C0_4x4_TRANS(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	uint8_t * dst = (uint8_t *)buffer;
	uint8_t * row_base = (uint8_t *)buffer;
	register uint16_t * src = (uint16_t *)pointers;
	uint32_t blocks = 0;
	uint32_t total = blocksperrow * numrows;

	if (total > 0u) {

		while (1) {

			uint16_t word = *src;

			if ((word & 0x8000u) != 0) {

				int32_t op = word & 0xF000u;

				if (op <= 0xC000) {

					if (op != 0xC000) {

						switch (op) {

						case 0xB000: {
							/* 0xB000 - solid color fill, count in bits 8-11 */
							uint32_t count = ((uint32_t)word >> 8) & 0xFu;
							int r;

							if ((uint8_t)word) {
								uint32_t n = 4u * count;

								for (r = 0; r < 4; ++r) {
									for (uint32_t k = 0; k < n; ++k) {
										*dst++ = (uint8_t)word;
									}
									dst -= n;
									dst += bufwidth;
								}

								dst -= 4u * bufwidth;
							}

							dst += 4u * count;
							blocks += count;
							src += 1;
						} break;

						case 0xA000: {
							/* 0xA000 - read a second word, select sub-mode */
							uint16_t word2 = src[1];
							src += 1;
							uint32_t count = word & 0xFFFu;
							int r;

							if ((word2 & 0x8000u) != 0) {
								int sub = word2 & 0xC000;

								switch (sub) {

								case 0xC000: {
									/* transparent codebook run */
									const uint8_t * cb = codebook + ((uint32_t)(word2 & 0xFFFu) << 4);

									for (r = 0; r < 4; ++r) {
										uint32_t nn;
										for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 4; ++c) {
												if (*cb) {
													*dst = *cb;
												}
												++dst;
												++cb;
											}
											cb -= 4;
										}
										cb += 4;
										dst -= 4u * count;
										dst += bufwidth;
									}

									dst += 4u * count;

									dst -= 4u * bufwidth;
									blocks += count;
									src += 1;
								} break;

								case 0x8000: {
									/* solid color run, color = low byte of word2 */
									if ((uint8_t)word2) {
										uint32_t n = 4u * count;

										for (r = 0; r < 4; ++r) {
											for (uint32_t k = 0; k < n; ++k) {
												*dst++ = (uint8_t)word2;
											}
											dst -= n;
											dst += bufwidth;
										}

										dst -= 4u * bufwidth;
									}

									dst += 4u * count;
									blocks += count;
									src += 1;

								} break;
								}

							} else {
								/* opaque codebook run */
								const uint8_t * cb = codebook + ((uint32_t)word2 << 4);

								for (r = 0; r < 4; ++r) {
									uint32_t nn;
									for (nn = 0; nn < count; nn++) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = *cb++;
										}
										cb -= 4;
									}
									cb += 4;
									dst -= 4u * count;
									dst += bufwidth;
								}

								src += 1;
								dst += 4u * count;
								dst -= 4u * bufwidth;
								blocks += count;
							}

						} break;
						}

					} else {
						/* 0xC000 - paired solid colors, two blocks per stream word */
						uint32_t count = word & 0xFFFu;
						int r;
						src += 1;

						uint32_t pairs = count >> 1;

						if (pairs != 0u) {
							do {
								uint16_t word2 = *src;
								uint8_t hi = (uint8_t)(word2 >> 8);

								if (hi != 0u) {
									for (r = 0; r < 4; ++r) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = hi;
										}
										dst -= 4u;
										dst += bufwidth;
									}
									dst -= 4u * bufwidth;
								}
								dst += 4;

								if ((uint8_t)word2) {
									for (r = 0; r < 4; ++r) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = (uint8_t)word2;
										}
										dst -= 4u;
										dst += bufwidth;
									}
									dst -= 4u * bufwidth;
								}
								dst += 4;

								src += 1;
								pairs--;
							} while (pairs);
						}

						blocks += count;
					}

				} else {

					switch (op) {

					case 0xF000: {
						/* 0xF000 - solid single block, color = low byte */
						int r;
						if ((uint8_t)word) {
							for (r = 0; r < 4; ++r) {
								for (int c = 0; c < 4; ++c) {
									*dst++ = (uint8_t)word;
								}
								dst -= 4u;
								dst += bufwidth;
							}
							dst -= 4u * bufwidth;
						}

						dst += 4;
						src += 1;
						blocks += 1;
					} break;

					case 0xE000: {
						/* 0xE000 - transparent single block from codebook */
						const uint8_t * cb = codebook + ((uint32_t)(word & 0xFFFu) << 4);
						int r;

						for (r = 0; r < 4; ++r) {
							for (int c = 0; c < 4; ++c) {
								if (*cb) {
									*dst = *cb;
								}
								++dst;
								++cb;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 4u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;

					} break;
					}
				}

			} else {
				/* high bit clear - opaque single block from codebook */
				const uint8_t * cb = codebook + ((uint32_t)*src << 4);
				int r;

				for (r = 0; r < 4; ++r) {
					for (int c = 0; c < 4; ++c) {
						*dst++ = *cb++;
					}
					dst -= 4u;
					dst += bufwidth;
				}

				dst -= 4u * bufwidth;

				dst += 4u;
				src += 1;
				blocks += 1;
			}

			/* macro-row wrap */
			if (blocks % blocksperrow == 0u) {
				dst = row_base + 4u * bufwidth;
				row_base = dst;
			}

			if (blocks >= total) {
				return;
			}
		}
	}
}

void __cdecl UnVQ2_C0_4x4_KEY(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	uint8_t * dst = (uint8_t *)buffer;
	uint8_t * row_base = (uint8_t *)buffer;
	register uint16_t * src = (uint16_t *)pointers;
	uint32_t blocks = 0;
	uint32_t total = numrows * blocksperrow;

	if (total > 0u) {

		while (1) {

			uint16_t word = *src;

			if ((word & 0x8000u) != 0) {

				int32_t op = word & 0xF000u;

				if (op <= 0xC000) {

					if (op != 0xC000) {

						switch (op) {

						case 0xB000: {
						/* 0xB000 - solid color fill, count in bits 8-11 */
						uint32_t count = ((uint32_t)word >> 8) & 0xFu;
						int r;

						uint32_t n = 4u * count;

						for (r = 0; r < 4; ++r) {
							for (uint32_t k = 0; k < n; ++k) {
								*dst++ = (uint8_t)word;
							}
							dst -= n;
							dst += bufwidth;
						}

						dst += 4u * count;

						dst -= 4u * bufwidth;
						blocks += count;
						src += 1;
						} break;


						case 0xA000: {
						/* 0xA000 - read a second word, select sub-mode */
						int16_t word2 = (int16_t)src[1];
						src += 1;
						uint32_t count = word & 0xFFFu;
						int r;

						if (word2 < 0) {
							int sub = word2 & 0xC000;

							if (sub != 0x8000) {

								if (sub == 0xC000) {
									/* opaque codebook run, masked index */
									const uint8_t * cb = codebook + ((uint32_t)(word2 & 0xFFF) << 4);

									for (r = 0; r < 4; ++r) {
										uint32_t nn;
										for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 4; ++c) {
												*dst++ = *cb++;
											}
											cb -= 4;
										}
										cb += 4;
										dst -= 4u * count;
										dst += bufwidth;
									}

									dst += 4u * count;

									dst -= 4u * bufwidth;
									blocks += count;
									src += 1;
								}

							} else {
								/* solid color run, color = low byte of word2 */
								uint32_t n = 4u * count;

								for (r = 0; r < 4; ++r) {
									for (uint32_t k = 0; k < n; ++k) {
										*dst++ = (uint8_t)word2;
									}
									dst -= n;
									dst += bufwidth;
								}

								dst += 4u * count;

								dst -= 4u * bufwidth;
								blocks += count;
								src += 1;
							}

						} else {
							/* opaque codebook run */
							const uint8_t * cb = codebook + ((uint32_t)*src << 4);

							for (r = 0; r < 4; ++r) {
								uint32_t nn;
								for (nn = 0; nn < count; nn++) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = *cb++;
									}
									cb -= 4;
								}
								cb += 4;
								dst -= 4u * count;
								dst += bufwidth;
							}

							dst += 4u * count;

							dst -= 4u * bufwidth;
							blocks += count;
							src += 1;
						}
						} break;

						}

					} else {

						/* 0xC000 - paired solid colors, two blocks per stream word */
						uint32_t count = word & 0xFFFu;
						int r;

						src += 1;

						if ((count >> 1) != 0u) {
							uint32_t pairs = count >> 1;

							do {
								uint16_t word2 = *src;
								uint8_t hi = (uint8_t)(word2 >> 8);

								for (r = 0; r < 4; ++r) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = hi;
									}
									dst -= 4u;
									dst += bufwidth;
								}
								dst -= 4u * bufwidth;
								dst += 4u;

								for (r = 0; r < 4; ++r) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = (uint8_t)word2;
									}
									dst -= 4u;
									dst += bufwidth;
								}
								dst -= 4u * bufwidth;
								dst += 4u;

								src += 1;
								pairs--;
							} while (pairs);
						}

						blocks += count;
					}

				} else {

					switch (op) {

					case 0xF000: {
						/* 0xF000 - solid single block, color = low byte */
						int r;

						for (r = 0; r < 4; ++r) {
							for (int c = 0; c < 4; ++c) {
								*dst++ = (uint8_t)word;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 4u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;
					} break;

					case 0xE000: {
						/* 0xE000 - opaque single block from codebook */
						const uint8_t * cb = codebook + ((uint32_t)(word & 0xFFFu) << 4);
						int r;

						for (r = 0; r < 4; ++r) {
							for (int c = 0; c < 4; ++c) {
								*dst++ = *cb++;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 4u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;
					} break;
					}

				}

			} else {
				/* high bit clear - opaque single block from codebook */
				const uint8_t * cb = codebook + ((uint32_t)*src << 4);
				int r;

				for (r = 0; r < 4; ++r) {
					for (int c = 0; c < 4; ++c) {
						*dst++ = *cb++;
					}
					dst -= 4u;
					dst += bufwidth;
				}

				dst -= 4u * bufwidth;

				dst += 4u;
				src += 1;
				blocks += 1;
			}

			/* macro-row wrap */
			if (blocks % blocksperrow == 0u) {
				dst = row_base + 4u * bufwidth;
				row_base = dst;
			}

			if (blocks >= total) {
				return;
			}
		}
	}
}

void __cdecl UnVQ2_C0_4x4_TRANS_HALF(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	uint8_t * dst = (uint8_t *)buffer;
	uint8_t * row_base = (uint8_t *)buffer;
	uint16_t * src = (uint16_t *)pointers;
	uint32_t blocks = 0;
	uint32_t total = numrows * blocksperrow;

	if (total > 0u) {

		while (1) {

			uint16_t word = *src;

			if ((word & 0x8000u) != 0) {

				int op = (word & 0xFFFF) & 0xF000;

				if (op <= 0xC000) {

					if (op != 0xC000) {

						switch (op) {

						case 0xB000: {
							/* 0xB000 - solid color fill, count in bits 8-11 */
							uint32_t count = ((uint32_t)word >> 8) & 0xFu;
							int r;

							if ((uint8_t)word) {
								uint32_t n = 2u * count;

								for (r = 0; r < 2; ++r) {
									for (uint32_t k = 0; k < n; ++k) {
										*dst++ = (uint8_t)word;
									}
									dst -= n;
									dst += bufwidth;
								}

								dst -= 2u * bufwidth;
							}

							dst += 2u * count;
							blocks += count;
							src += 1;
						} break;

						case 0xA000: {
							/* 0xA000 - read a second word, select sub-mode */
							uint16_t word2 = src[1];
							src += 1;
							uint32_t count = word & 0xFFFu;
							int r;

							if ((word2 & 0x8000u) != 0) {
								int sub = word2 & 0xC000;

								switch (sub) {

								case 0xC000: {
									/* transparent codebook run */
									const uint8_t * cb = codebook + ((uint32_t)(word2 & 0xFFFu) << 4);

									for (r = 0; r < 2; ++r) {
										uint32_t nn;
										for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 2; ++c) {
												if (*cb) {
													*dst = *cb;
												}
												++dst;
												cb += 2;
											}
											cb -= 4;
										}
										cb += 4;
										dst -= 2u * count;
										dst += bufwidth;
									}

									dst += 2u * count;

									dst -= 2u * bufwidth;
									blocks += count;
									src += 1;
								} break;

								case 0x8000: {
									/* solid color run, color = low byte of word2 */
									if ((uint8_t)word2) {
										uint32_t n = 2u * count;

										for (r = 0; r < 2; ++r) {
											for (uint32_t k = 0; k < n; ++k) {
												*dst++ = (uint8_t)word2;
											}
											dst -= n;
											dst += bufwidth;
										}

										dst -= 2u * bufwidth;
									}

									dst += 2u * count;
									blocks += count;
									src += 1;

								} break;
								}

							} else {
								/* opaque codebook run */
								const uint8_t * cb = codebook + ((uint32_t)word2 << 4);

								for (r = 0; r < 2; ++r) {
									uint32_t nn;
									for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 2; ++c) {
												*dst++ = *cb;
												cb += 2;
											}
											cb -= 4;
									}
									cb += 4;
									dst -= 2u * count;
									dst += bufwidth;
								}

								src += 1;
								dst += 2u * count;
								dst -= 2u * bufwidth;
								blocks += count;
							}

						} break;
						}

					} else {
						/* 0xC000 - paired solid colors, two blocks per stream word */
						uint32_t count = word & 0xFFFu;
						int r;
						src += 1;

						uint32_t pairs = count >> 1;

						if (pairs != 0u) {
							do {
								uint16_t word2 = *src;
								uint8_t hi = (uint8_t)(word2 >> 8);

								if (hi != 0u) {
									for (r = 0; r < 2; ++r) {
										for (int c = 0; c < 2; ++c) {
											*dst++ = hi;
										}
										dst -= 2u;
										dst += bufwidth;
									}
									dst -= 2u * bufwidth;
								}
								dst += 2;

								if ((uint8_t)word2 != 0u) {
									for (r = 0; r < 2; ++r) {
										for (int c = 0; c < 2; ++c) {
											*dst++ = (uint8_t)word2;
										}
										dst -= 2u;
										dst += bufwidth;
									}
									dst -= 2u * bufwidth;
								}
								dst += 2;

								src += 1;
								pairs--;
							} while (pairs);
						}

						blocks += count;
					}

				} else {

					switch (op) {

					case 0xF000: {
						/* 0xF000 - solid single block, color = low byte */
						uint32_t r;
						if ((uint8_t)word) {
							for (r = 0; r < 2; ++r) {
								for (int c = 0; c < 2; ++c) {
									*dst++ = (uint8_t)word;
								}
								dst -= 2u;
								dst += bufwidth;
							}
							dst -= 2u * bufwidth;
						}

						dst += 2;
						src += 1;
						blocks += 1;
					} break;

					case 0xE000: {
						/* 0xE000 - transparent single block from codebook */
						const uint8_t * cb = codebook + ((uint32_t)(word & 0xFFFu) << 4);
						int r;

						for (r = 0; r < 2; ++r) {
							for (int c = 0; c < 2; ++c) {
								if (*cb) {
									*dst = *cb;
								}
								++dst;
								cb += 2;
							}
							dst -= 2u;
							dst += bufwidth;
						}

						dst -= 2u * bufwidth;

						dst += 2u;
						src += 1;
						blocks += 1;

					} break;
					}
				}

			} else {
				/* high bit clear - opaque single block from codebook */
				const uint8_t * cb = codebook + ((uint32_t)*src << 4);
				int r;

				for (r = 0; r < 2; ++r) {
					for (int c = 0; c < 2; ++c) {
						*dst++ = *cb;
						cb += 2;
					}
					dst -= 2u;
					dst += bufwidth;
				}

				dst -= 2u * bufwidth;

				dst += 2u;
				src += 1;
				blocks += 1;
			}

			/* macro-row wrap */
			if (blocks % blocksperrow == 0u) {
				dst = row_base + 2u * bufwidth;
				row_base = dst;
			}

			if (blocks >= total) {
				return;
			}
		}
	}
}

void __cdecl UnVQ2_C0_4x2_TRANS(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	uint8_t * dst = (uint8_t *)buffer;
	uint16_t * src = (uint16_t *)pointers;
	uint32_t blocks = 0;
	uint8_t * row_base = (uint8_t *)buffer;
	uint32_t total = numrows * blocksperrow;

	if (total > 0u) {

		while (1) {

			uint16_t word = *src;

			if ((word & 0x8000u) != 0) {

				int32_t op = word & 0xF000u;

				if (op <= 0xC000) {

					if (op != 0xC000) {

						switch (op) {

						case 0xB000: {
							/* 0xB000 - solid color fill, count in bits 8-11 */
							uint32_t count = ((uint32_t)word >> 8) & 0xFu;
							int r;

							if ((uint8_t)word) {
								uint32_t n = 4u * count;

								for (r = 0; r < 2; ++r) {
									for (uint32_t k = 0; k < n; ++k) {
										*dst++ = (uint8_t)word;
									}
									dst -= n;
									dst += bufwidth;
								}

								dst -= 2u * bufwidth;
							}

							dst += 4u * count;
							blocks += count;
							src += 1;
						} break;

						case 0xA000: {
							/* 0xA000 - read a second word, select sub-mode */
							src += 1;
							uint16_t word2 = *src;
							uint32_t count = word & 0xFFFu;
							int r;

							if ((word2 & 0x8000u) != 0) {
								int sub = word2 & 0xC000;

								switch (sub) {

								case 0xC000: {
									/* transparent codebook run */
									const uint8_t * cb = codebook + ((uint32_t)(word2 & 0xFFFu) << 3);

									for (r = 0; r < 2; ++r) {
										uint32_t nn;
										for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 4; ++c) {
												if (*cb) {
													*dst = *cb;
												}
												++dst;
												++cb;
											}
											cb -= 4;
										}
										cb += 4;
										dst -= 4u * count;
										dst += bufwidth;
									}

									src += 1;
									dst += 4u * count;
									dst -= 2u * bufwidth;
									blocks += count;

								} break;

								case 0x8000: {
									/* solid color run, color = low byte of word2 */
									if ((uint8_t)word2) {
										uint32_t n = 4u * count;

										for (r = 0; r < 2; ++r) {
											for (uint32_t k = 0; k < n; ++k) {
												*dst++ = (uint8_t)word2;
											}
											dst -= n;
											dst += bufwidth;
										}

										dst -= 2u * bufwidth;
									}

									blocks += count;
									dst += 4u * count;
									src += 1;

								} break;
								}

							} else {
								/* opaque codebook run */
								const uint8_t * cb = codebook + ((uint32_t)word2 << 3);

								for (r = 0; r < 2; ++r) {
									uint32_t nn;
									for (nn = 0; nn < count; nn++) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = *cb++;
										}
										cb -= 4;
									}
									cb += 4;
									dst -= 4u * count;
									dst += bufwidth;
								}

								src += 1;
								dst += 4u * count;
								dst -= 2u * bufwidth;
								blocks += count;
							}

						} break;
						}

					} else {
						/* 0xC000 - paired solid colors, two blocks per stream word */
						uint32_t count = word & 0xFFFu;
						int r;
						src += 1;

						uint32_t pairs = count >> 1;

						if (pairs != 0u) {
							do {
								uint16_t word2 = *src;
								uint8_t hi = (uint8_t)(word2 >> 8);

								if (hi != 0u) {
									for (r = 0; r < 2; ++r) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = hi;
										}
										dst -= 4u;
										dst += bufwidth;
									}
									dst -= 2u * bufwidth;
								}
								dst += 4;

								uint8_t lo = (uint8_t)word2 & 0xFF;

								if (lo != 0u) {
									for (r = 0; r < 2; ++r) {
										for (int c = 0; c < 4; ++c) {
											*dst++ = lo;
										}
										dst -= 4u;
										dst += bufwidth;
									}
									dst -= 2u * bufwidth;
								}
								dst += 4;

								src += 1;
								pairs--;
							} while (pairs);
						}

						blocks += count;
					}

				} else {

					switch (op) {

					case 0xF000: {
						/* 0xF000 - solid single block, color = low byte */
						int r;
						if ((uint8_t)word) {
							for (r = 0; r < 2; ++r) {
								for (int c = 0; c < 4; ++c) {
									*dst++ = (uint8_t)word;
								}
								dst -= 4u;
								dst += bufwidth;
							}
							dst -= 2u * bufwidth;
						}

						dst += 4;
						src += 1;
						blocks += 1;
					} break;

					case 0xE000: {
						/* 0xE000 - transparent single block from codebook */
						const uint8_t * cb = codebook + ((uint32_t)(word & 0xFFFu) << 3);
						int r;

						for (r = 0; r < 2; ++r) {
							for (int c = 0; c < 4; ++c) {
								if (*cb) {
									*dst = *cb;
								}
								++dst;
								++cb;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 2u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;

					} break;
					}
				}

			} else {
				/* high bit clear - opaque single block from codebook */
				const uint8_t * cb = codebook + ((uint32_t)*src << 3);
				int r;

				for (r = 0; r < 2; ++r) {
					for (int c = 0; c < 4; ++c) {
						*dst++ = *cb++;
					}
					dst -= 4u;
					dst += bufwidth;
				}

				dst -= 2u * bufwidth;

				dst += 4u;
				src += 1;
				blocks += 1;
			}

			/* macro-row wrap */
			if (blocks % blocksperrow == 0u) {
				dst = row_base + 2u * bufwidth;
				row_base = dst;
			}

			if (blocks >= total) {
				return;
			}
		}
	}
}

void __cdecl UnVQ2_C0_4x2_KEY(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	uint8_t * dst = (uint8_t *)buffer;
	uint8_t * row_base = (uint8_t *)buffer;
	register uint16_t * src = (uint16_t *)pointers;
	uint32_t blocks = 0;
	uint32_t total = numrows * blocksperrow;

	if (total > 0u) {

		while (1) {

			uint16_t word = *src;

			if ((word & 0x8000u) != 0) {

				int32_t op = word & 0xF000u;

				if (op <= 0xC000) {

					if (op != 0xC000) {

						switch (op) {

						case 0xB000: {
						/* 0xB000 - solid color fill, count in bits 8-11 */
						uint32_t count = ((uint32_t)word >> 8) & 0xFu;
						int r;

						uint32_t n = 4u * count;

						for (r = 0; r < 2; ++r) {
							for (uint32_t k = 0; k < n; ++k) {
								*dst++ = (uint8_t)word;
							}
							dst -= n;
							dst += bufwidth;
						}

						dst += 4u * count;

						dst -= 2u * bufwidth;
						blocks += count;
						src += 1;
						} break;


						case 0xA000: {
						/* 0xA000 - read a second word, select sub-mode */
						int16_t word2 = (int16_t)src[1];
						src += 1;
						uint32_t count = word & 0xFFFu;
						int r;

						if (word2 != 0) {
							int sub = word2 & 0xC000;

							if (sub != 0x8000) {

								if (sub == 0xC000) {
									/* opaque codebook run, masked index */
									const uint8_t * cb = codebook + ((uint32_t)(word2 & 0xFFF) << 3);

									for (r = 0; r < 2; ++r) {
										uint32_t nn;
										for (nn = 0; nn < count; nn++) {
											for (int c = 0; c < 4; ++c) {
												*dst++ = *cb++;
											}
											cb -= 4;
										}
										cb += 4;
										dst -= 4u * count;
										dst += bufwidth;
									}

									dst += 4u * count;

									dst -= 2u * bufwidth;
									blocks += count;
									src += 1;
								}

							} else {
								/* solid color run, color = low byte of word2 */
								uint32_t n = 4u * count;

								for (r = 0; r < 2; ++r) {
									for (uint32_t k = 0; k < n; ++k) {
										*dst++ = (uint8_t)word2;
									}
									dst -= n;
									dst += bufwidth;
								}

								dst += 4u * count;

								dst -= 2u * bufwidth;
								blocks += count;
								src += 1;
							}

						} else {
							/* opaque codebook run */
							const uint8_t * cb = codebook + ((uint32_t)*src << 3);

							for (r = 0; r < 2; ++r) {
								uint32_t nn;
								for (nn = 0; nn < count; nn++) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = *cb++;
									}
									cb -= 4;
								}
								cb += 4;
								dst -= 4u * count;
								dst += bufwidth;
							}

							dst += 4u * count;

							dst -= 2u * bufwidth;
							blocks += count;
							src += 1;
						}
						} break;

						}

					} else {

						/* 0xC000 - paired solid colors, two blocks per stream word */
						uint32_t count = word & 0xFFFu;
						int r;

						src += 1;

						uint32_t pairs = count >> 1;

						if (pairs != 0u) {
							do {
								uint16_t word2 = *src;
								uint8_t hi = (uint8_t)(word2 >> 8);

								for (r = 0; r < 2; ++r) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = hi;
									}
									dst -= 4u;
									dst += bufwidth;
								}
								dst -= 2u * bufwidth;
								dst += 4u;

								for (r = 0; r < 2; ++r) {
									for (int c = 0; c < 4; ++c) {
										*dst++ = (uint8_t)word2;
									}
									dst -= 4u;
									dst += bufwidth;
								}
								dst -= 2u * bufwidth;
								dst += 4u;

								src += 1;
								pairs--;
							} while (pairs);
						}

						blocks += count;
					}

				} else {

					switch (op) {

					case 0xF000: {
						/* 0xF000 - solid single block, color = low byte */
						int r;

						for (r = 0; r < 2; ++r) {
							for (int c = 0; c < 4; ++c) {
								*dst++ = (uint8_t)word;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 2u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;
					} break;

					case 0xE000: {
						/* 0xE000 - opaque single block from codebook */
						const uint8_t * cb = codebook + ((uint32_t)(word & 0xFFFu) << 3);
						int r;

						for (r = 0; r < 2; ++r) {
							for (int c = 0; c < 4; ++c) {
								*dst++ = *cb++;
							}
							dst -= 4u;
							dst += bufwidth;
						}

						dst -= 2u * bufwidth;

						dst += 4u;
						src += 1;
						blocks += 1;
					} break;
					}

				}

			} else {
				/* high bit clear - opaque single block from codebook */
				const uint8_t * cb = codebook + ((uint32_t)*src << 3);
				int r;

				for (r = 0; r < 2; ++r) {
					for (int c = 0; c < 4; ++c) {
						*dst++ = *cb++;
					}
					dst -= 4u;
					dst += bufwidth;
				}

				dst -= 2u * bufwidth;

				dst += 4u;
				src += 1;
				blocks += 1;
			}

			/* macro-row wrap */
			if (blocks % blocksperrow == 0u) {
				dst = row_base + 2u * bufwidth;
				row_base = dst;
			}

			if (blocks >= total) {
				return;
			}
		}
	}
}

void __cdecl UnVQ2_4x4_Table(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	assert(HicolorTable != 0);
	assert(codebook != 0);
	assert(pointers != 0);
	assert(buffer != 0);
	assert(blocksperrow != 0);
	assert(numrows != 0);
	assert(bufwidth != 0);

	bufwidth = 2 * bufwidth;
	unsigned int last_pos = 8 * blocksperrow;
	unsigned char * data_end = &buffer[4 * numrows * bufwidth];
	unsigned char * last_buf_pos = &buffer[8 * blocksperrow];
	unsigned int blocks_per_rowa = 4 * bufwidth;
	unsigned short * ptrs = (unsigned short *)pointers;
	unsigned char * row_ptr = buffer;

	while (buffer < data_end) {
		unsigned short valw = *ptrs++;
		unsigned int val = valw;
		unsigned int count = val & 0xFFFF0FFF;
		int code = val & 0xF000;
		if (code <= 0x3000) {
			if (code != 0x3000) {
				if (code) {
					if (code != 0x1000) {
						if (code == 0x2000) {
							unsigned int len = 4;
							unsigned short * cbptr = (unsigned short *)&codebook[32 * *ptrs++];
							while (len > 0) {
								unsigned int w0 = cbptr[0];
								unsigned int w1 = cbptr[1];
								int pixels = w0 | (w1 << 16);
								cbptr += 2;
								unsigned char * dptr = buffer;
								unsigned int rows = count;
								while (rows > 0) {
									*(unsigned int *)dptr = pixels;
									dptr += 8;
									--rows;
								}
								w0 = cbptr[0];
								w1 = cbptr[1];
								int pixels2 = w0 | (w1 << 16);
								cbptr += 2;
								unsigned char * dptr2 = buffer + 4;
								unsigned int c2 = count;
								while (c2 > 0) {
									*(unsigned int *)dptr2 = pixels2;
									dptr2 += 8;
									--c2;
								}
								buffer += bufwidth;
								--len;
							}
							buffer += 8 * count - blocks_per_rowa;
						}
					} else {
						buffer += 8 * count;
					}
				} else {
					int len = 2 * count;
					unsigned short pixel = HicolorTable[*ptrs++];
					int value = pixel | (pixel << 16);
					if (2 * count) {
						memset32(buffer, value, len);
					}
					if (len) {
						memset32(&buffer[bufwidth], value, len);
					}
					if (len) {
						memset32(&buffer[2 * bufwidth], value, len);
					}
					if (len) {
						memset32(&buffer[2 * bufwidth + bufwidth], value, len);
					}
					buffer += 8 * count;
				}
			} else {
				unsigned int len = 4;
				unsigned short * cbsrc = (unsigned short *)&codebook[32 * *ptrs++];
				while (len > 0) {
					unsigned short * dstw = (unsigned short *)buffer;
					unsigned int c2 = 4;
					while (c2 > 0) {
						unsigned short cbpixel = *cbsrc;
						if ((cbpixel & 0x8000) == 0) {
							unsigned short * dstwalk = dstw;
							unsigned int c3 = count;
							while (c3 > 0) {
								*dstwalk = cbpixel;
								dstwalk += 4;
								--c3;
							}
						}
						++dstw;
						++cbsrc;
						--c2;
					}
					buffer += bufwidth;
					--len;
				}

				buffer += 8 * count - blocks_per_rowa;
			}
		} else {
			switch (code) {
			case 0x5000:
				if (count > 0) {
					int fillwidth = 4 * (bufwidth >> 2);
					while (count > 0) {
						unsigned int c2 = 4;
						int fillpix = HicolorTable[*ptrs] | (HicolorTable[*ptrs] << 16);
						++ptrs;
						unsigned char * fillptr = buffer;
						while (c2 > 0) {
							*(unsigned int *)fillptr = fillpix;
							*((unsigned int *)fillptr + 1) = fillpix;
							fillptr += fillwidth;
							--c2;
						}
						buffer += 8;
						--count;
					};
				}
				break;
			case 0x6000:
				if (count > 0) {
					int rowwidth = 4 * (bufwidth >> 2);
					while (count > 0) {
						unsigned char * rowptr = buffer;
						unsigned short * cbrow = (unsigned short *)&codebook[32 * *ptrs++];
						unsigned int c2 = 4;
						while (c2 > 0) {
							unsigned short hi0 = *((unsigned short *)cbrow + 1);
							unsigned short lo0 = *(unsigned short *)cbrow;
							unsigned short * cbnext = cbrow + 2;
							*(unsigned int *)rowptr = lo0 | (hi0 << 16);
							unsigned short hi1 = cbnext[1];
							unsigned short lo1 = *cbnext;
							cbrow = (cbnext + 2);
							*((unsigned int *)rowptr + 1) = lo1 | (hi1 << 16);
							rowptr += rowwidth;
							--c2;
						}
						buffer += 8;
						--count;
					};
				}
				break;
			case 0x7000:
				if (count > 0) {
					while (count > 0) {
						int c2 = 4;
						unsigned short * cbline = (unsigned short *)&codebook[32 * *ptrs++];
						unsigned char * lineptr = buffer;
						while (c2) {
							unsigned short * linew = (unsigned short *)lineptr;
							unsigned int c3 = 4;
							while (c3 > 0) {
								if ((*cbline & 0x8000) == 0) {
									*linew = *cbline;
								}
								++linew;
								++cbline;
								--c3;
							}
							--c2;
							lineptr += 2 * (bufwidth >> 1);
						}
						buffer += 8;
						--count;
					}
				}
				break;
			}
		}
		if (buffer == last_buf_pos) {
			buffer = &row_ptr[blocks_per_rowa];
			row_ptr = buffer;
			last_buf_pos = &buffer[last_pos];
		}
	}
}

void __cdecl UnVQ2_4x2_Table(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	assert(HicolorTable != 0);
	assert(codebook != 0);
	assert(pointers != 0);
	assert(buffer != 0);
	assert(blocksperrow != 0);
	assert(numrows != 0);
	assert(bufwidth != 0);

	bufwidth = 2 * bufwidth;
	unsigned char * data_end = &buffer[4 * numrows * bufwidth];
	unsigned char * last_buf_pos = &buffer[8 * blocksperrow];
	unsigned short * ptrs = (unsigned short *)pointers;
	unsigned char * row_ptr = buffer;

	while (buffer < data_end) {
		unsigned short valw = *ptrs++;
		unsigned int val = valw;
		unsigned int count = val & 0xFFFF0FFF;
		int code = val & 0xF000;
		if (code <= 0x3000) {
			if (code != 0x3000) {
				if (code) {
					if (code != 0x1000) {
						if (code == 0x2000) {
							unsigned int len = 2;
							unsigned short * cbptr = (unsigned short *)&codebook[32 * *ptrs++];
							while (len > 0) {
								unsigned int w0 = cbptr[0];
								unsigned int w1 = cbptr[1];
								unsigned int value = w0 | (w1 << 16);
								cbptr += 2;
								unsigned char * dptr = buffer;
								unsigned int rows = count;
								while (rows > 0) {
									*(unsigned int *)dptr = value;
									dptr += 8;
									--rows;
								}
								w0 = cbptr[0];
								w1 = cbptr[1];
								value = w0 | (w1 << 16);
								cbptr += 6;
								unsigned char * dptr2 = buffer + 4;
								unsigned int c2 = count;
								while (c2 > 0) {
									*(unsigned int *)dptr2 = value;
									dptr2 += 8;
									--c2;
								}
								buffer += 2 * bufwidth;
								--len;
							}
							buffer += 8 * count - (int)(bufwidth << 2);
						}
					} else {
						buffer += 8 * count;
					}
				} else {
					int len = 2 * count;
					unsigned short pixel = HicolorTable[*ptrs++];
					int value = pixel | (pixel << 16);
					if (2 * count) {
						memset32(buffer, value, len);
					}
					if (len) {
						memset32(&buffer[2 * bufwidth], value, len);
					}
					buffer += 8 * count;
				}
			} else {
				unsigned int len = 2;
				unsigned short * cbsrc = (unsigned short *)&codebook[32 * *ptrs++];
				while (len > 0) {
					unsigned short * dstw = (unsigned short *)buffer;
					unsigned int c2 = 4;
					while (c2 > 0) {
						unsigned short cbpixel = *cbsrc;
						if ((cbpixel & 0x8000) == 0) {
							unsigned short * dstwalk = dstw;
							unsigned int c3 = count;
							while (c3 > 0) {
								*dstwalk = cbpixel;
								dstwalk += 4;
								--c3;
							}
						}
						++dstw;
						++cbsrc;
						--c2;
					}
					cbsrc += 4;
					buffer += 2 * bufwidth;
					--len;
				}

				buffer += 8 * count - (int)(bufwidth << 2);
			}
		} else {
			switch (code) {
			case 0x5000:
				if (count > 0) {
					int fillwidth = 4 * (bufwidth >> 1);
					while (count > 0) {
						unsigned int c2 = 2;
						int fillpix = HicolorTable[*ptrs] | (HicolorTable[*ptrs] << 16);
						++ptrs;
						unsigned char * fillptr = buffer;
						while (c2 > 0) {
							*(unsigned int *)fillptr = fillpix;
							*((unsigned int *)fillptr + 1) = fillpix;
							fillptr += fillwidth;
							--c2;
						}
						buffer += 8;
						--count;
					};
				}
				break;
			case 0x6000:
				if (count > 0) {
					int rowwidth = 4 * (bufwidth >> 1);
					while (count > 0) {
						unsigned char * rowptr = buffer;
						unsigned short * cbrow = (unsigned short *)&codebook[32 * *ptrs++];
						unsigned int c2 = 2;
						while (c2 > 0) {
							unsigned short hi0 = *((unsigned short *)cbrow + 1);
							unsigned short lo0 = *(unsigned short *)cbrow;
							unsigned short * cbnext = cbrow + 2;
							*(unsigned int *)rowptr = lo0 | (hi0 << 16);
							unsigned short hi1 = cbnext[1];
							unsigned short lo1 = *cbnext;
							cbrow = cbnext + 6;
							*((unsigned int *)rowptr + 1) = lo1 | (hi1 << 16);
							rowptr += rowwidth;
							--c2;
						}
						buffer += 8;
						--count;
					};
				}
				break;
			case 0x7000:
				if (count > 0) {
					while (count > 0) {
						int c2 = 2;
						unsigned short * cbline = (unsigned short *)&codebook[32 * *ptrs++];
						unsigned char * lineptr = buffer;
						while (c2) {
							unsigned short * linew = (unsigned short *)lineptr;
							unsigned int c3 = 4;
							while (c3 > 0) {
								if ((*cbline & 0x8000) == 0) {
									*linew = *cbline;
								}
								++linew;
								++cbline;
								--c3;
							}
							--c2;
							lineptr += 2 * bufwidth;
						}
						buffer += 8;
						--count;
					}
				}
				break;
			}
		}
		if (buffer == last_buf_pos) {
			buffer = &row_ptr[bufwidth << 2];
			row_ptr = buffer;
			last_buf_pos = &buffer[8 * blocksperrow];
		}
	}
}

void __cdecl UnVQ1_4x4_Table(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	assert(codebook != 0);
	assert(pointers != 0);
	assert(buffer != 0);
	assert(blocksperrow != 0);
	assert(numrows != 0);
	assert(bufwidth != 0);

	bufwidth = 2 * bufwidth;
	unsigned int total = numrows * bufwidth;
	unsigned long step = 8 * blocksperrow;
	unsigned char * data_end = buffer + (total * 4);
	unsigned char * row_end = buffer + step;
	unsigned char * row_ptr = buffer;
	unsigned short * src = (unsigned short *)pointers;

	unsigned long i;
	unsigned long j;
	unsigned short index;
	unsigned long type;

	unsigned long row_step = bufwidth << 2;

	while (buffer < data_end) {
		type = (*src & 0xE000);
		index = (*src & 0x1FFF);
		src++;

		switch (type) {
		case 0: {
			unsigned short * cb = (unsigned short *)&codebook[32 * index];

			unsigned int w0;
			unsigned int w1;
			unsigned int w2;
			unsigned int w3;
			for (i = 0; i != 2; i++) {
				w0 = cb[0];
				w1 = cb[1];
				((unsigned int *)buffer)[0] = w0 | (w1 << 16);
				cb += 2;
				w2 = cb[0];
				w3 = cb[1];
				((unsigned int *)buffer)[1] = w2 | (w3 << 16);
				cb += 2;

				cb += 4;
				buffer += 2 * bufwidth;
			}
			buffer += 8 - (int)row_step;
			break;
		}

		case 0x2000: {
			unsigned short * cb = (unsigned short *)&codebook[32 * index];
			for (i = 0; i != 2; i++) {
				unsigned short * dstw = (unsigned short *)buffer;
				for (j = 0; j != 4; j++) {
					unsigned short cbword = cb[0];
					if ((cbword & 0x8000) == 0) {
						dstw[0] = cbword;
					}
					dstw++;
					cb++;
				}
				cb += 4;
				buffer += (2 * bufwidth);
			}
			buffer += 8 - (int)row_step;
			break;
		}

		case 0x4000:
			buffer += 8;
			break;
		}

		if (buffer == row_end) {
			buffer = row_ptr + row_step;
			row_ptr = buffer;
			row_end = buffer + (blocksperrow << 3);
		}
	}
}

void __cdecl UnVQ1_4x2_Table(unsigned char * codebook, unsigned char * pointers, unsigned char * buffer, unsigned long blocksperrow, unsigned long numrows, unsigned long bufwidth)
{
	assert(codebook != 0);
	assert(pointers != 0);
	assert(buffer != 0);
	assert(blocksperrow != 0);
	assert(numrows != 0);
	assert(bufwidth != 0);

	bufwidth = 2 * bufwidth;
	unsigned char * data_end = buffer + (numrows * bufwidth * 4);
	unsigned char * last_buf_pos = buffer + 8 * blocksperrow;
	unsigned char * row_ptr = buffer;
	unsigned short * src = (unsigned short *)pointers;

	unsigned long i;
	unsigned short index;
	unsigned long type;
	unsigned int cb_idx;
	unsigned int scatter_count;
	unsigned int code;

	while (buffer < data_end) {
		type = (*src & 0xE000);
		index = (*src & 0x1FFF);
		src++;

		if (type <= 0x6000) {
			if (type != 0x6000) {
				if (type) {
					if (type != 0x2000) {
						if (type == 0x4000) {
							unsigned char primary_idx = *((unsigned char *)src - 2);
							unsigned int count = (((unsigned short)index >> 7) & 0x3E) + 2;
							unsigned short * cb = (unsigned short *)&codebook[32 * primary_idx];
							unsigned int stride = bufwidth >> 2;
							unsigned int w0;
							unsigned int w1;
							unsigned int w2;
							unsigned int w3;
							unsigned char * dstb = buffer;
							int n;
							for (n = 0; n < 2; n++) {
								w0 = cb[0];
								w1 = cb[1];
								cb += 2;
								((unsigned int *)dstb)[0] = w0 | (w1 << 16);
								w2 = cb[0];
								w3 = cb[1];
								cb += 6;
								((unsigned int *)dstb)[1] = w2 | (w3 << 16);
								dstb += 4 * stride;
							}
							buffer += 8;
							unsigned char * byte_src = (unsigned char *)src;

							unsigned int full_stride = 8 * stride;
							unsigned int remaining = count;
							do {
								unsigned char * d2 = buffer;
								unsigned char * next_byte = byte_src + 1;
								unsigned short * cb2 = (unsigned short *)&codebook[32 * *byte_src];
								int n2;
								for (n2 = 0; n2 < 2; n2++) {
									w0 = cb2[0];
									w1 = cb2[1];
									cb2 += 2;
									((unsigned int *)d2)[0] = w0 | (w1 << 16);
									w2 = cb2[0];
									w3 = cb2[1];
									cb2 += 6;
									((unsigned int *)d2)[1] = w2 | (w3 << 16);
									d2 += full_stride;
								}
								byte_src = next_byte;
								buffer += 8;
								--remaining;
							} while (remaining);
							src = (unsigned short *)byte_src;
						}
					} else {
						code = index;
						code &= 0xFFFF;
						cb_idx = code & 0xFF;
						scatter_count = ((code >> 7) & 0x3E) + 2;
						if (scatter_count == 0) {
							goto label_extended_count;
						}

					label_scatter_copy: {
						unsigned int len = 2;
						unsigned short * cb = (unsigned short *)&codebook[32 * cb_idx];
						do {
							unsigned int w0 = cb[0];
							unsigned int w1 = cb[1];
							unsigned int value = w0 | (w1 << 16);
							cb += 2;
							unsigned char * dstb = buffer;
							if (scatter_count > 0) {
								unsigned int remain = scatter_count;
								do {
									*(unsigned int *)dstb = value;
									dstb += 8;
									--remain;
								} while (remain);
							}
							unsigned int w2 = cb[0];
							unsigned int w3 = cb[1];
							value = w2 | (w3 << 16);
							cb += 6;
							unsigned char * d2 = buffer + 4;
							if (scatter_count > 0) {
								unsigned int remain = scatter_count;
								do {
									*(unsigned int *)d2 = value;
									d2 += 8;
									--remain;
								} while (remain);
							}
							buffer += 2 * bufwidth;
							--len;
						} while (len);
						buffer += 8 * scatter_count - (int)(bufwidth << 2);
					}
					}
				} else {
					buffer += 8 * (unsigned char)index;
				}
			} else {
				unsigned short * cb = (unsigned short *)&codebook[32 * index];
				unsigned int w0;
				unsigned int w1;
				unsigned int w2;
				unsigned int w3;
				for (i = 0; i != 4; i++) {
					w0 = cb[0];
					w1 = cb[1];
					((unsigned int *)buffer)[0] = w0 | (w1 << 16);
					cb += 2;
					w2 = cb[0];
					w3 = cb[1];
					((unsigned int *)buffer)[1] = w2 | (w3 << 16);
					cb += 6;
					buffer += 2 * bufwidth;
				}
				buffer += 8 - (int)(bufwidth << 2);
			}
		} else {
			switch (type) {
			case 0x8000: {
				unsigned short * cb = (unsigned short *)&codebook[32 * index];
				int len = 4;
				do {
					unsigned short * dstw = (unsigned short *)buffer;
					int c2 = 4;
					do {
						if ((*cb & 0x8000) == 0) {
							*dstw = *cb;
						}
						dstw++;
						cb++;
						--c2;
					} while (c2);
					cb += 4;
					buffer += 2 * bufwidth;
					--len;
				} while (len);
				buffer += 8 - (int)(bufwidth << 2);
				break;
			}

			case 0xA000: {
			label_extended_count:
				scatter_count = *(unsigned char *)src;
				cb_idx = (unsigned short)index;
				src = (unsigned short *)((unsigned char *)src + 1);
				goto label_scatter_copy;
			}

			case 0xC000: {
				unsigned int count = *(unsigned char *)src;
				src = (unsigned short *)((unsigned char *)src + 1);
				unsigned short * cb = (unsigned short *)&codebook[32 * index];
				unsigned int len;
				for (len = 0; len < 2; len++) {
					unsigned short * dstw = (unsigned short *)buffer;
					unsigned int c2;
					for (c2 = 0; c2 < 4; c2++) {
						unsigned short cbval = *cb;
						if ((*cb & 0x8000) == 0) {
							unsigned short * d2 = dstw;
							unsigned int c3;
							for (c3 = 0; c3 < count; c3++) {
								*d2 = cbval;
								d2 += 4;
							}
						}
						dstw++;
						cb++;
					}
					cb += 4;
					buffer += 2 * bufwidth;
				}

				buffer += 8 * count - (int)(bufwidth << 2);
				break;
			}
			}
		}
		if (buffer == last_buf_pos) {
			buffer = row_ptr + (bufwidth << 2);
			row_ptr = buffer;
			last_buf_pos = buffer + 8 * blocksperrow;
		}
	}
}
}
