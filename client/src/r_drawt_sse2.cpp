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

void rt_copy4colsD_SSE2 (int sx, int yl, int yh)
{
	DWORD *source;
	DWORD *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	shaderef_t pal = shaderef_t(&realcolormaps, 0);
	dest = (DWORD *)(ylookup[yl] + columnofs[sx]);
	source = (DWORD *)(&dc_temp[yl*4]);
	pitch = dc_pitch/sizeof(DWORD);

	if (count & 1) {
		DWORD sc = source[0];
		dest[0] = pal.shade((sc >> 24) & 0xff);
		dest[1] = pal.shade((sc >> 16) & 0xff);
		dest[2] = pal.shade((sc >>  8) & 0xff);
		dest[3] = pal.shade((sc      ) & 0xff);

		source += 4/sizeof(DWORD);
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	do {
		DWORD sc = source[0];
		dest[0] = pal.shade((sc >> 24) & 0xff);
		dest[1] = pal.shade((sc >> 16) & 0xff);
		dest[2] = pal.shade((sc >>  8) & 0xff);
		dest[3] = pal.shade((sc      ) & 0xff);

		sc = source[4/sizeof(DWORD)];
		dest[pitch+0] = pal.shade((sc >> 24) & 0xff);
		dest[pitch+1] = pal.shade((sc >> 16) & 0xff);
		dest[pitch+2] = pal.shade((sc >>  8) & 0xff);
		dest[pitch+3] = pal.shade((sc      ) & 0xff);

		source += 8/sizeof(DWORD);
		dest += pitch*2;
	} while (--count);
}

void rt_map4colsD_SSE2 (int sx, int yl, int yh)
{
	byte *source;
	DWORD *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = (DWORD *)(ylookup[yl] + columnofs[sx]);
	source = &dc_temp[yl*4];
	pitch = dc_pitch / sizeof(DWORD);
	
	if (count & 1) {
		dest[0] = dc_colormap.shade(source[0]);
		dest[1] = dc_colormap.shade(source[1]);
		dest[2] = dc_colormap.shade(source[2]);
		dest[3] = dc_colormap.shade(source[3]);
		source += 4;
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);

	const __m128i blendColor = _mm_set_epi16(0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB);
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	do {
		const __m128i shaded1 = _mm_setr_epi32(
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1]),
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		);
		const __m128i finalColors1 = blend4vs1_sse2(shaded1, blendMult, blendInvAlpha, upper8mask);
		_mm_storeu_si128((__m128i *)dest, finalColors1);
		dest += pitch;

		const __m128i shaded2 = _mm_setr_epi32(
			dc_colormap.shadenoblend(source[4]),
			dc_colormap.shadenoblend(source[5]),
			dc_colormap.shadenoblend(source[6]),
			dc_colormap.shadenoblend(source[7])
		);
		const __m128i finalColors2 = blend4vs1_sse2(shaded2, blendMult, blendInvAlpha, upper8mask);
		_mm_storeu_si128((__m128i *)dest, finalColors2);
		dest += pitch;

		source += 8;
	} while (--count);
}

void rt_tlate4colsD_SSE2 (int sx, int yl, int yh)
{
	byte *translation;
	byte *source;
	DWORD *dest;
	int count;
	int pitch;

	translation = dc_translation;
	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = (DWORD *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4];
	pitch = dc_pitch / sizeof(DWORD);

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);

	const __m128i blendColor = _mm_set_epi16(0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB);
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	do {
		const __m128i shaded = _mm_setr_epi32(
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]]),
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		);
		const __m128i finalColors = blend4vs1_sse2(shaded, blendMult, blendInvAlpha, upper8mask);
		_mm_storeu_si128((__m128i *)dest, finalColors);

		source += 4;
		dest += pitch;
	} while (--count);
}

void rt_lucent4colsD_SSE2 (int sx, int yl, int yh)
{
	byte  *source;
	DWORD *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	int fga, bga;
	{
		fixed_t fglevel, bglevel;

		fglevel = dc_translevel & ~0x3ff;
		bglevel = FRACUNIT - fglevel;

		// alphas should be in [0 .. 255]
		fga = (fglevel >> 8) * 256 / 255;
		bga = 256 - fga;
	}

	dest = (DWORD *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4];
	pitch = dc_pitch / sizeof(DWORD);

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);

	const __m128i destAlpha = _mm_set_epi16(0, fga, fga, fga, 0, fga, fga, fga);
	const __m128i destInvAlpha = _mm_set_epi16(0, bga, bga, bga, 0, bga, bga, bga);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);

	const __m128i blendColor = _mm_set_epi16(0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB);
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	do {
		const __m128i shaded = _mm_setr_epi32(
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1]),
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		);
		const __m128i blendedColors = blend4vs1_sse2(shaded, blendMult, blendInvAlpha, upper8mask);

		// Blend the status-blended colors now with the destination colors:
#if 0
		// NOTE(jsd): This SSE2 code does not work correctly.
		const __m128i bgColor = _mm_setr_epi32(dest[0], dest[1], dest[2], dest[3]);
		const __m128i finalColors = blend4vs4_sse2(bgColor, blendedColors, destAlpha, destInvAlpha, upper8mask);
		_mm_storeu_si128((__m128i *)dest, finalColors);
#else
		DWORD fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (0)));
		DWORD bg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (1)));
		bg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (2)));
		bg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (3)));
		bg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#endif

		source += 4;
		dest += pitch;
	} while (--count);
}

void rt_tlatelucent4colsD_SSE2 (int sx, int yl, int yh)
{
	byte  *translation;
	byte  *source;
	DWORD *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	int fga, bga;
	{
		fixed_t fglevel;

		fglevel = dc_translevel & ~0x3ff;

		// alphas should be in [0 .. 255]
		fga = (fglevel >> 8) * 256 / 255;
		bga = 256 - fga;
	}

	translation = dc_translation;
	dest = (DWORD *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4];
	pitch = dc_pitch / sizeof(DWORD);

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);

	const __m128i destAlpha = _mm_set_epi16(0, fga, fga, fga, 0, fga, fga, fga);
	const __m128i destInvAlpha = _mm_set_epi16(0, bga, bga, bga, 0, bga, bga, bga);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);

	const __m128i blendColor = _mm_set_epi16(0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB);
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	do {
		const __m128i shaded = _mm_setr_epi32(
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]]),
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		);
		const __m128i blendedColors = blend4vs1_sse2(shaded, blendMult, blendInvAlpha, upper8mask);

		// Blend the status-blended colors now with the destination colors:
#if 0
		// NOTE(jsd): This SSE2 code does not work correctly.
		const __m128i bgColor = _mm_setr_epi32(dest[0], dest[1], dest[2], dest[3]);
		const __m128i finalColors = blend4vs4_sse2(bgColor, blendedColors, destAlpha, destInvAlpha, upper8mask);
		_mm_storeu_si128((__m128i *)dest, finalColors);
#else
		DWORD fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (0)));
		DWORD bg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (1)));
		bg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (2)));
		bg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		fg = _mm_cvtsi128_si32(_mm_srli_si128(blendedColors, 4 * (3)));
		bg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#endif

		source += 4;
		dest += pitch;
	} while (--count);
}

void R_DrawSpanD_SSE2 (void)
{
	dsfixed_t			xfrac;
	dsfixed_t			yfrac;
	dsfixed_t			xstep;
	dsfixed_t			ystep;
	DWORD*       dest;
	int 				count;
	int 				spot;

#ifdef RANGECHECK
	if (ds_x2 < ds_x1
		|| ds_x1<0
		|| ds_x2>=screen->width
		|| ds_y>screen->height)
	{
		I_Error ("R_DrawSpan: %i to %i at %i",
				 ds_x1,ds_x2,ds_y);
	}
//		dscount++;
#endif

	xfrac = ds_xfrac;
	yfrac = ds_yfrac;

	dest = (DWORD *)(ylookup[ds_y] + columnofs[ds_x1]);

	// We do not check for zero spans here?
	count = ds_x2 - ds_x1 + 1;

	xstep = ds_xstep;
	ystep = ds_ystep;

	// SSE2 temporaries:
	const __m128i upper8mask = _mm_set_epi16(0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m128i blendAlpha = _mm_set_epi16(0, alpha, alpha, alpha, 0, alpha, alpha, alpha);
	const __m128i blendInvAlpha = _mm_set_epi16(0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha);

	const __m128i blendColor = _mm_set_epi16(0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB);
	const __m128i blendMult = _mm_mullo_epi16(blendColor, blendAlpha);

	int batches = count / 4;
	int remainder = count & 3;

	for (int i = 0; i < batches; ++i, count -= 4)
	{
		// Align this temporary to a 16-byte boundary so we can do _mm_load_si128:
		SSE2_ALIGNED(DWORD tmp[4]);

		// Generate 4 colors for vectorized alpha-blending:
		for (int j = 0; j < 4; ++j)
		{
			// Current texture index in u,v.
			spot = ((yfrac>>(32-6-6))&(63*64)) + (xfrac>>(32-6));

			// Lookup pixel from flat texture tile,
			//  re-index using light/colormap.
			tmp[j] = ds_colormap.shadenoblend(ds_source[spot]);

			// Next step in u,v.
			xfrac += xstep;
			yfrac += ystep;
		}

		const __m128i tmpColors = _mm_load_si128((__m128i *)&tmp);
		const __m128i finalColors = blend4vs1_sse2(tmpColors, blendMult, blendInvAlpha, upper8mask);

		*dest = _mm_cvtsi128_si32(_mm_srli_si128(finalColors, 4 * (0)));
		dest += ds_colsize;
		*dest = _mm_cvtsi128_si32(_mm_srli_si128(finalColors, 4 * (1)));
		dest += ds_colsize;
		*dest = _mm_cvtsi128_si32(_mm_srli_si128(finalColors, 4 * (2)));
		dest += ds_colsize;
		*dest = _mm_cvtsi128_si32(_mm_srli_si128(finalColors, 4 * (3)));
		dest += ds_colsize;
	}

	if (remainder)
	{
		do {
			// Current texture index in u,v.
			spot = ((yfrac>>(32-6-6))&(63*64)) + (xfrac>>(32-6));

			// Lookup pixel from flat texture tile,
			//  re-index using light/colormap.
			*dest = ds_colormap.shade(ds_source[spot]);
			dest += ds_colsize;

			// Next step in u,v.
			xfrac += xstep;
			yfrac += ystep;
		} while (--count);
	}
}

// Functions for v_video.cpp support

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
