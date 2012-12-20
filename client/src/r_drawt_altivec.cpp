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

#ifdef __ALTIVEC__

#include <stdio.h>
#include <stdlib.h>

// Compile on g++-4.0.1 with -faltivec, not -maltivec
#include <altivec.h>

#define ALTIVEC_ALIGNED(x) x __attribute__((aligned(16)))

#include "doomtype.h"
#include "doomdef.h"
#include "i_system.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_things.h"
#include "v_video.h"

// Useful vector shorthand typedefs:
typedef vector signed char vs8;
typedef vector unsigned char vu8;
typedef vector unsigned short vu16;
typedef vector unsigned int vu32;

// Blend 4 colors against 1 color with alpha
#define blend4vs1_altivec(input,blendMult,blendInvAlpha,upper8mask) \
	((vu8)vec_packsu( \
		(vu16)vec_sr( \
			(vu16)vec_mladd( \
				(vu16)vec_and((vu8)vec_unpackh((vs8)input), (vu8)upper8mask), \
				(vu16)blendInvAlpha, \
				(vu16)blendMult \
			), \
			(vu16)vec_splat_u16(8) \
		), \
		(vu16)vec_sr( \
			(vu16)vec_mladd( \
				(vu16)vec_and((vu8)vec_unpackl((vs8)input), (vu8)upper8mask), \
				(vu16)blendInvAlpha, \
				(vu16)blendMult \
			), \
			(vu16)vec_splat_u16(8) \
		) \
	))

// Direct rendering (32-bit) functions for ALTIVEC optimization:

void rt_copy4colsD_ALTIVEC (int sx, int yl, int yh)
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

void rt_map4colsD_ALTIVEC (int sx, int yl, int yh)
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

	// AltiVec temporaries:
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};

	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 blendColor = {0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	do {
#if 0
		dest[0] = dc_colormap.shade(source[0]);
		dest[1] = dc_colormap.shade(source[1]);
		dest[2] = dc_colormap.shade(source[2]);
		dest[3] = dc_colormap.shade(source[3]);
		dest += pitch;

		dest[0] = dc_colormap.shade(source[4]);
		dest[1] = dc_colormap.shade(source[5]);
		dest[2] = dc_colormap.shade(source[6]);
		dest[3] = dc_colormap.shade(source[7]);
		dest += pitch;
#else
		const vu32 shaded1 = {
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1]),
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		};
		const vu32 finalColors1 = (vu32)blend4vs1_altivec(shaded1, blendMult, blendInvAlpha, upper8mask);
        vec_ste(finalColors1, 0, dest);
        vec_ste(finalColors1, 4, dest);
        vec_ste(finalColors1, 8, dest);
        vec_ste(finalColors1, 12, dest);
		dest += pitch;

		const vu32 shaded2 = {
			dc_colormap.shadenoblend(source[4]),
			dc_colormap.shadenoblend(source[5]),
			dc_colormap.shadenoblend(source[6]),
			dc_colormap.shadenoblend(source[7])
		};
		const vu32 finalColors2 = (vu32)blend4vs1_altivec(shaded2, blendMult, blendInvAlpha, upper8mask);
        vec_ste(finalColors2, 0, dest);
        vec_ste(finalColors2, 4, dest);
        vec_ste(finalColors2, 8, dest);
        vec_ste(finalColors2, 12, dest);
		dest += pitch;
#endif

		source += 8;
	} while (--count);
}

void rt_tlate4colsD_ALTIVEC (int sx, int yl, int yh)
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
	
	// AltiVec temporaries:
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};

	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 blendColor = {0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	do {
#if 0
		dest[0] = dc_colormap.shade(translation[source[0]]);
		dest[1] = dc_colormap.shade(translation[source[1]]);
		dest[2] = dc_colormap.shade(translation[source[2]]);
		dest[3] = dc_colormap.shade(translation[source[3]]);
#else
		const vu32 shaded1 = {
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]]),
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		};
		const vu32 finalColors1 = (vu32)blend4vs1_altivec(shaded1, blendMult, blendInvAlpha, upper8mask);
        vec_ste(finalColors1, 0, dest);
        vec_ste(finalColors1, 4, dest);
        vec_ste(finalColors1, 8, dest);
        vec_ste(finalColors1, 12, dest);
#endif

		source += 4;
		dest += pitch;
	} while (--count);
}

void rt_lucent4colsD_ALTIVEC (int sx, int yl, int yh)
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

	// AltiVec temporaries:
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};

	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 blendColor = {0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	do {
#if 0
		DWORD bg, fg;

		fg = dc_colormap.shade(source[0]);
		bg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(source[1]);
		bg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(source[2]);
		bg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(source[3]);
		bg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#else
		DWORD bg, fg;

		const vu32 shaded1 = {
			dc_colormap.shadenoblend(source[0]),
			dc_colormap.shadenoblend(source[1]),
			dc_colormap.shadenoblend(source[2]),
			dc_colormap.shadenoblend(source[3])
		};
		const vu32 finalColors1 = (vu32)blend4vs1_altivec(shaded1, blendMult, blendInvAlpha, upper8mask);
		bg = dest[0];
        vec_ste(finalColors1, 0, dest);
		fg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		bg = dest[1];
        vec_ste(finalColors1, 4, dest);
		fg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		bg = dest[2];
		vec_ste(finalColors1, 8, dest);
		fg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		bg = dest[3];
		vec_ste(finalColors1, 12, dest);
		fg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#endif

		source += 4;
		dest += pitch;
	} while (--count);
}

void rt_tlatelucent4colsD_ALTIVEC (int sx, int yl, int yh)
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

	// AltiVec temporaries:
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};

	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 blendColor = {0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	do {
#if 0
		DWORD bg, fg;

		fg = dc_colormap.shade(translation[source[0]]);
		bg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(translation[source[1]]);
		bg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(translation[source[2]]);
		bg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		fg = dc_colormap.shade(translation[source[3]]);
		bg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#else
		DWORD bg, fg;

		const vu32 shaded1 = {
			dc_colormap.shadenoblend(translation[source[0]]),
			dc_colormap.shadenoblend(translation[source[1]]),
			dc_colormap.shadenoblend(translation[source[2]]),
			dc_colormap.shadenoblend(translation[source[3]])
		};
		const vu32 finalColors1 = (vu32)blend4vs1_altivec(shaded1, blendMult, blendInvAlpha, upper8mask);
		bg = dest[0];
        vec_ste(finalColors1, 0, dest);
		fg = dest[0];
		dest[0] = alphablend2a(bg, bga, fg, fga);

		bg = dest[1];
        vec_ste(finalColors1, 4, dest);
		fg = dest[1];
		dest[1] = alphablend2a(bg, bga, fg, fga);

		bg = dest[2];
		vec_ste(finalColors1, 8, dest);
		fg = dest[2];
		dest[2] = alphablend2a(bg, bga, fg, fga);

		bg = dest[3];
		vec_ste(finalColors1, 12, dest);
		fg = dest[3];
		dest[3] = alphablend2a(bg, bga, fg, fga);
#endif

		source += 4;
		dest += pitch;
	} while (--count);
}

void R_DrawSpanD_ALTIVEC (void)
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

	// AltiVec temporaries:
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};

	int alpha = BlendA;
	if (alpha > 256) alpha = 256;
	int invAlpha = 256 - alpha;

	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};

	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 blendColor = {0, BlendR, BlendG, BlendB, 0, BlendR, BlendG, BlendB};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	int batches = count / 4;
	int remainder = count & 3;

	for (int i = 0; i < batches; ++i, count -= 4)
	{
		// Align this temporary to a 16-byte boundary so we can do _mm_load_si128:
		ALTIVEC_ALIGNED(DWORD tmp[4]);

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

		const vu32 shaded1 = vec_ld(0, (vu32 *)&tmp);
		const vu32 finalColors1 = (vu32)blend4vs1_altivec(shaded1, blendMult, blendInvAlpha, upper8mask);

		// NOTE(jsd): This ignores ds_colsize which is really only used for low/high detail mode.
		vec_ste(finalColors1, 0, dest);
		vec_ste(finalColors1, 4, dest);
		vec_ste(finalColors1, 8, dest);
		vec_ste(finalColors1, 12, dest);
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

void r_dimpatchD_ALTIVEC(const DCanvas *const cvs, DWORD color, int alpha, int x1, int y1, int w, int h)
{
	int x, y, i;
	DWORD *line;
	int invAlpha = 256 - alpha;

	int dpitch = cvs->pitch / sizeof(DWORD);
	line = (DWORD *)cvs->buffer + y1 * dpitch;

	int batches = w / 4;
	int remainder = w & 3;

	// AltiVec temporaries:
	const vu16 zero = {0, 0, 0, 0, 0, 0, 0, 0};
	const vu16 upper8mask = {0, 0xff, 0xff, 0xff, 0, 0xff, 0xff, 0xff};
	const vu16 blendAlpha = {0, alpha, alpha, alpha, 0, alpha, alpha, alpha};
	const vu16 blendInvAlpha = {0, invAlpha, invAlpha, invAlpha, 0, invAlpha, invAlpha, invAlpha};
	const vu16 blendColor = {0, RPART(color), GPART(color), BPART(color), 0, RPART(color), GPART(color), BPART(color)};
	const vu16 blendMult = vec_mladd(blendColor, blendAlpha, zero);

	for (y = y1; y < y1 + h; y++)
	{
		// AltiVec optimize the bulk in batches of 4 colors:
		for (i = 0, x = x1; i < batches; ++i, x += 4)
		{
			const vu32 input = {line[x + 0], line[x + 1], line[x + 2], line[x + 3]};
			const vu32 output = (vu32)blend4vs1_altivec(input, blendMult, blendInvAlpha, upper8mask);
			vec_ste(output, 0, &line[x]);
			vec_ste(output, 4, &line[x]);
			vec_ste(output, 8, &line[x]);
			vec_ste(output, 12, &line[x]);
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

VERSION_CONTROL (r_drawt_altivec_cpp, "$Id$")

#endif
