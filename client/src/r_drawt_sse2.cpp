// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id$
//
// Copyright (C) 1998-2006 by Randy Heit (ZDoom).
// Copyright (C) 2006-2012 by The Odamex Team.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	Functions for drawing columns into a temporary buffer and then
//	copying them to the screen. On machines with a decent cache, this
//	is faster than drawing them directly to the screen. Will I be able
//	to even understand any of this if I come back to it later? Let's
//	hope so. :-)
//
//-----------------------------------------------------------------------------

#include "SDL_cpuinfo.h"
#include "r_intrin.h"

#ifdef __SSE2__

#include <stdio.h>
#include <stdlib.h>
#include <emmintrin.h>

#ifdef _MSC_VER
#define SSE2_ALIGNED(x) _CRT_ALIGN(16) x
#else
#define SSE2_ALIGNED(x) x __attribute__((aligned(16)))
#endif

#include "doomtype.h"
#include "doomdef.h"
#include "i_system.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_things.h"
#include "v_video.h"

// SSE2 alpha-blending functions.
// NOTE(jsd): We can only blend two colors per 128-bit register because we need 16-bit resolution for multiplication.

#if 0
// Blend 4 colors against 4 other colors using SSE2 intrinsics.
// NOTE(jsd): This function produces darker colors somehow; it is disabled for now.
#define blend4vs4_sse2(dcba,hgfe,alpha,invAlpha,upper8mask) \
	(_mm_packus_epi16( \
		_mm_add_epi16( \
			_mm_srli_epi16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpacklo_epi8(dcba, dcba), upper8mask), \
					invAlpha \
				), \
				8 \
			), \
			_mm_srli_epi16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpacklo_epi8(hgfe, hgfe), upper8mask), \
					alpha \
				), \
				8 \
			) \
		), \
		_mm_add_epi16( \
			_mm_srli_epi16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpackhi_epi8(dcba, dcba), upper8mask), \
					invAlpha \
				), \
				8 \
			), \
			_mm_srli_epi16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpackhi_epi8(hgfe, hgfe), upper8mask), \
					alpha \
				), \
				8 \
			) \
		) \
	))
#endif

// Blend 4 colors against 1 color using SSE2:
#define blend4vs1_sse2(input,blendMult,blendInvAlpha,upper8mask) \
	(_mm_packus_epi16( \
		_mm_srli_epi16( \
			_mm_adds_epu16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpacklo_epi8(input, input), upper8mask), \
					blendInvAlpha \
				), \
				blendMult \
			), \
			8 \
		), \
		_mm_srli_epi16( \
			_mm_adds_epu16( \
				_mm_mullo_epi16( \
					_mm_and_si128(_mm_unpackhi_epi8(input, input), upper8mask), \
					blendInvAlpha \
				), \
				blendMult \
			), \
			8 \
		) \
	))

// Direct rendering (32-bit) functions for SSE2 optimization:

void r_dimpatchD_SSE2(const DCanvas *const cvs, DWORD color, int alpha, int x1, int y1, int w, int h)
{
	int x, y, i;
	DWORD *line;
	int invAlpha = 256 - alpha;

	int dpitch = cvs->pitch / sizeof(DWORD);
	line = (DWORD *)cvs->buffer + y1 * dpitch;

	int batches = w / 4;
	int remainder = w & 3;

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);
	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);
	const __m128i blendColor = _mm_set_epi16(0, RPART(color), GPART(color), BPART(color), 0, RPART(color), GPART(color), BPART(color));
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	for (y = y1; y < y1 + h; y++)
	{
		// SSE2 optimize the bulk in batches of 4 colors:
		for (i = 0, x = x1; i < batches; ++i, x += 4)
		{
			const __m128i input = _mm_setr_epi32(line[x + 0], line[x + 1], line[x + 2], line[x + 3]);
			_mm_storeu_si128((__m128i *)&line[x], blend4vs1_sse2(input, blendMult, blendInvAlpha, upper8mask));
		}

		if (remainder)
		{
			// Pick up the remainder:
			for (; x < x1 + w; x++)
			{
				line[x] = alphablend1a(line[x], color, alpha);
			}
		}

		line += dpitch;
	}
}

VERSION_CONTROL (r_drawt_sse2_cpp, "$Id$")

#endif
