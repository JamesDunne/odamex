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

#ifdef __MMX__

#include <stdio.h>
#include <stdlib.h>
#include <mmintrin.h>

#ifdef _MSC_VER
#define MMX_ALIGNED(x) _CRT_ALIGN(8) x
#else
#define MMX_ALIGNED(x) x __attribute__((aligned(8)))
#endif

#include "doomtype.h"
#include "doomdef.h"
#include "i_system.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_things.h"
#include "v_video.h"

// With MMX we can process 4 16-bit words at a time.

// Blend 2 colors against 1 color using MMX:
#define blend2vs1_mmx(input, blendMult, blendInvAlpha, upper8mask) \
	(_mm_packs_pu16( \
		_mm_srli_pi16( \
			_mm_add_pi16( \
				_mm_mullo_pi16( \
					_mm_and_si64(_mm_unpacklo_pi8(input, input), upper8mask), \
					blendInvAlpha \
				), \
				blendMult \
			), \
			8 \
		), \
		_mm_srli_pi16( \
			_mm_add_pi16( \
				_mm_mullo_pi16( \
					_mm_and_si64(_mm_unpackhi_pi8(input, input), upper8mask), \
					blendInvAlpha \
				), \
				blendMult \
			), \
			8 \
		) \
	))

// Direct rendering (32-bit) functions for MMX optimization:

void rt_copy4colsD_MMX (int sx, int yl, int yh)
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

void rt_map4colsD_MMX (int sx, int yl, int yh)
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

	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);

	const __m64 blendColor = _mm_set_pi16(0, BlendR, BlendG, BlendB);
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	do {
		const __m64 shaded1a = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1])
		);
		const __m64 final1a = blend2vs1_mmx(shaded1a, blendMult, blendInvAlpha, upper8mask);
		const __m64 shaded1b = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		);
		const __m64 final1b = blend2vs1_mmx(shaded1b, blendMult, blendInvAlpha, upper8mask);
		dest[0] = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*0));
		dest[1] = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*1));
		dest[2] = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*0));
		dest[3] = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*1));
		dest += pitch;

		const __m64 shaded2a = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[4]),
			dc_colormap.shadenoblend(source[5])
		);
		const __m64 final2a = blend2vs1_mmx(shaded2a, blendMult, blendInvAlpha, upper8mask);
		const __m64 shaded2b = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[6]),
			dc_colormap.shadenoblend(source[7])
		);
		const __m64 final2b = blend2vs1_mmx(shaded2b, blendMult, blendInvAlpha, upper8mask);
		dest[0] = _mm_cvtsi64_si32(_mm_srli_si64(final2a, 32*0));
		dest[1] = _mm_cvtsi64_si32(_mm_srli_si64(final2a, 32*1));
		dest[2] = _mm_cvtsi64_si32(_mm_srli_si64(final2b, 32*0));
		dest[3] = _mm_cvtsi64_si32(_mm_srli_si64(final2b, 32*1));
		dest += pitch;

		source += 8;
	} while (--count);

	// Required to reset FP:
	_mm_empty();
}

void rt_tlate4colsD_MMX (int sx, int yl, int yh)
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

	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);

	const __m64 blendColor = _mm_set_pi16(0, BlendR, BlendG, BlendB);
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	do {
		const __m64 shaded1a = _mm_setr_pi32(
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]])
		);
		const __m64 final1a = blend2vs1_mmx(shaded1a, blendMult, blendInvAlpha, upper8mask);
		const __m64 shaded1b = _mm_setr_pi32(
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		);
		const __m64 final1b = blend2vs1_mmx(shaded1b, blendMult, blendInvAlpha, upper8mask);
		dest[0] = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*0));
		dest[1] = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*1));
		dest[2] = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*0));
		dest[3] = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*1));
		dest += pitch;

		source += 4;
	} while (--count);

	// Required to reset FP:
	_mm_empty();
}

void rt_lucent4colsD_MMX (int sx, int yl, int yh)
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

	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);

	const __m64 blendColor = _mm_set_pi16(0, BlendR, BlendG, BlendB);
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	do {
		const __m64 shaded1a = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1])
		);
		const __m64 final1a = blend2vs1_mmx(shaded1a, blendMult, blendInvAlpha, upper8mask);
		const __m64 shaded1b = _mm_setr_pi32(
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		);
		const __m64 final1b = blend2vs1_mmx(shaded1b, blendMult, blendInvAlpha, upper8mask);

		DWORD bg = dest[0];
		DWORD fg = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*0));
		dest[0] = alphablend2a(bg, bga, fg, fga);

		bg = dest[1];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*1));
		dest[1] = alphablend2a(bg, bga, fg, fga);

		bg = dest[2];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*0));
		dest[2] = alphablend2a(bg, bga, fg, fga);

		bg = dest[3];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*1));
		dest[3] = alphablend2a(bg, bga, fg, fga);
		dest += pitch;

		source += 4;
	} while (--count);

	// Required to reset FP:
	_mm_empty();
}

void rt_tlatelucent4colsD_MMX (int sx, int yl, int yh)
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

	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);

	const __m64 blendColor = _mm_set_pi16(0, BlendR, BlendG, BlendB);
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	do {
		const __m64 shaded1a = _mm_setr_pi32(
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]])
		);
		const __m64 final1a = blend2vs1_mmx(shaded1a, blendMult, blendInvAlpha, upper8mask);
		const __m64 shaded1b = _mm_setr_pi32(
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		);
		const __m64 final1b = blend2vs1_mmx(shaded1b, blendMult, blendInvAlpha, upper8mask);

		DWORD bg = dest[0];
		DWORD fg = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*0));
		dest[0] = alphablend2a(bg, bga, fg, fga);

		bg = dest[1];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1a, 32*1));
		dest[1] = alphablend2a(bg, bga, fg, fga);

		bg = dest[2];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*0));
		dest[2] = alphablend2a(bg, bga, fg, fga);

		bg = dest[3];
		fg = _mm_cvtsi64_si32(_mm_srli_si64(final1b, 32*1));
		dest[3] = alphablend2a(bg, bga, fg, fga);
		dest += pitch;

		source += 4;
	} while (--count);

	// Required to reset FP:
	_mm_empty();
}

void R_DrawSpanD_MMX (void)
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

	// MMX temporaries:
	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);

	const __m64 blendColor = _mm_set_pi16(0, BlendR, BlendG, BlendB);
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	int batches = count / 2;
	int remainder = count & 1;
	
	for (int i = 0; i < batches; ++i, count -= 2)
	{
		// Align this temporary to a 16-byte boundary so we can do _mm_load_si128:
		MMX_ALIGNED(DWORD tmp[2]);

		// Generate 4 colors for vectorized alpha-blending:
		for (int j = 0; j < 2; ++j)
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

		const __m64 tmpColors = *((__m64 *)&tmp);
		const __m64 finalColors = blend2vs1_mmx(tmpColors, blendMult, blendInvAlpha, upper8mask);

		*dest = _mm_cvtsi64_si32(_mm_srli_si64(finalColors, 32 * (0)));
		dest += ds_colsize;
		*dest = _mm_cvtsi64_si32(_mm_srli_si64(finalColors, 32 * (1)));
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

	// Required to reset FP:
	_mm_empty();
}

// Functions for v_video.cpp support

void r_dimpatchD_MMX(const DCanvas *const cvs, DWORD color, int alpha, int x1, int y1, int w, int h)
{
	int x, y, i;
	DWORD *line;
	int invAlpha = 256 - alpha;

	int dpitch = cvs->pitch / sizeof(DWORD);
	line = (DWORD *)cvs->buffer + y1 * dpitch;

	int batches = w / 2;
	int remainder = w & 1;

	// MMX temporaries:
	const __m64 upper8mask = _mm_set_pi16(0, 0xff, 0xff, 0xff);
	const __m64 blendAlpha = _mm_set_pi16(0, alpha, alpha, alpha);
	const __m64 blendInvAlpha = _mm_set_pi16(0, invAlpha, invAlpha, invAlpha);
	const __m64 blendColor = _mm_set_pi16(0, RPART(color), GPART(color), BPART(color));
	const __m64 blendMult = _mm_mullo_pi16(blendColor, blendAlpha);

	for (y = y1; y < y1 + h; y++)
	{
		// MMX optimize the bulk in batches of 2 colors:
		for (i = 0, x = x1; i < batches; ++i, x += 2)
		{
			const __m64 input = _mm_setr_pi32(line[x + 0], line[x + 1]);
			const __m64 output = blend2vs1_mmx(input, blendMult, blendInvAlpha, upper8mask);
			line[x+0] = _mm_cvtsi64_si32(_mm_srli_si64(output, 32*0));
			line[x+1] = _mm_cvtsi64_si32(_mm_srli_si64(output, 32*1));
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

	// Required to reset FP:
	_mm_empty();
}

VERSION_CONTROL (r_drawt_mmx_cpp, "$Id$")

#endif
