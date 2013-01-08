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


#include <stdio.h>
#include <stdlib.h>

#include "doomtype.h"
#include "doomdef.h"
#include "r_defs.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_things.h"
#include "v_video.h"


// Palettized functions:


byte dc_temp[MAXHEIGHT * 4]; // denis - todo - security, overflow
unsigned int dc_tspans[4][256];
unsigned int *dc_ctspan[4];
unsigned int *horizspan[4];

// Stretches a column into a temporary buffer which is later
// drawn to the screen along with up to three other columns.
void R_DrawColumnHorizP (void)
{
	int count = dc_yh - dc_yl;
	byte *dest;
	fixed_t fracstep;
	fixed_t frac;

	if (count++ < 0)
		return;

	count++;
	{
		int x = dc_x & 3;
		unsigned int **span = &dc_ctspan[x];

		(*span)[0] = dc_yl;
		(*span)[1] = dc_yh;
		*span += 2;
		dest = &dc_temp[x + 4*dc_yl];
	}
	fracstep = dc_iscale;
	frac = dc_texturefrac;

	{
		int texheight = dc_textureheight;
		int mask = (texheight >> FRACBITS) - 1;
		
		byte *source = dc_source;

		// [SL] Properly tile textures whose heights are not a power-of-2,
		// avoiding a tutti-frutti effect.  From Eternity Engine.
		if (texheight & (texheight - 1))
		{
			// texture height is not a power-of-2
			if (frac < 0)
				while((frac += texheight) < 0);
			else
				while(frac >= texheight)
					frac -= texheight;

			do
			{
				*dest = source[frac>>FRACBITS];
				dest += 4;
				if ((frac += fracstep) >= texheight)
					frac -= texheight;
			} while (--count);	
		}
		else
		{
			// texture height is a power-of-2
			if (count & 1) {
				*dest = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest += 4;				
			}
			if (count & 2) {
				dest[0] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[4] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest += 8;
			}
			if (count & 4) {
				dest[0] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[4] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[8] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[12] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest += 16;
			}
			count >>= 3;
			if (!count)
				return;

			do
			{
				dest[0] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[4] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[8] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[12] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[16] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[20] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[24] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest[28] = source[(frac>>FRACBITS) & mask];
				frac += fracstep;
				dest += 32;
			} while (--count);
		}
	}
}

// [RH] Just fills a column with a given color
void R_FillColumnHorizP (void)
{
	int count = dc_yh - dc_yl;
	byte color = dc_color;
	byte *dest;

	if (count++ < 0)
		return;

	count++;
	{
		int x = dc_x & 3;
		unsigned int **span = &dc_ctspan[x];

		(*span)[0] = dc_yl;
		(*span)[1] = dc_yh;
		*span += 2;
		dest = &dc_temp[x + 4*dc_yl];
	}

	if (count & 1) {
		*dest = color;
		dest += 4;
	}
	if (!(count >>= 1))
		return;
	do {
		dest[0] = color;
		dest[4] = color;
		dest += 8;
	} while (--count);
}



typedef byte palindex_t;
typedef DWORD argb_t;

// rt_rawcolor does no color mapping and only uses the default palette.
template<typename pixel_t>
static forceinline pixel_t rt_rawcolor(const shaderef_t &pal, const byte c);

// rt_mapcolor does color mapping.
template<typename pixel_t>
static forceinline pixel_t rt_mapcolor(const shaderef_t &pal, const byte c);

// rt_tlatecolor does color mapping and translation.
template<typename pixel_t>
static forceinline pixel_t rt_tlatecolor(const shaderef_t &pal, const translationref_t &translation, const byte c);

// rt_blend2 does alpha blending between two colors.
template<typename pixel_t>
static forceinline pixel_t rt_blend2(const pixel_t bg, const int bga, const pixel_t fg, const int fga);


template<>
static forceinline palindex_t rt_rawcolor<palindex_t>(const shaderef_t &pal, const byte c)
{
	// NOTE(jsd): For rawcolor we do no index.
	return (c);
}

template<>
static forceinline argb_t rt_rawcolor<argb_t>(const shaderef_t &pal, const byte c)
{
	return pal.shade(c);
}


template<>
static forceinline palindex_t rt_mapcolor<palindex_t>(const shaderef_t &pal, const byte c)
{
	return pal.index(c);
}

template<>
static forceinline argb_t rt_mapcolor<argb_t>(const shaderef_t &pal, const byte c)
{
	return pal.shade(c);
}


template<>
static forceinline palindex_t rt_tlatecolor<palindex_t>(const shaderef_t &pal, const translationref_t &translation, const byte c)
{
	return translation.tlate(c);
}

template<>
static forceinline argb_t rt_tlatecolor<argb_t>(const shaderef_t &pal, const translationref_t &translation, const byte c)
{
	return pal.tlate(translation, c);
}


template<>
static forceinline palindex_t rt_blend2(const palindex_t bg, const int bga, const palindex_t fg, const int fga)
{
	// Crazy 8bpp alpha-blending using lookup tables and bit twiddling magic
	argb_t bgARGB = Col2RGB8[bga >> 2][bg];
	argb_t fgARGB = Col2RGB8[fga >> 2][fg];

	argb_t mix = (fgARGB + bgARGB) | 0x1f07c1f;
	return RGB32k[0][0][mix & (mix >> 15)];
}

template<>
static forceinline argb_t rt_blend2(const argb_t bg, const int bga, const argb_t fg, const int fga)
{
	return alphablend2a(bg, bga, fg, fga);
}


template<typename pixel_t, int columns>
static forceinline void rt_copycols(int hx, int sx, int yl, int yh);

template<typename pixel_t, int columns>
static forceinline void rt_mapcols(int hx, int sx, int yl, int yh);

template<typename pixel_t, int columns>
static forceinline void rt_tlatecols(int hx, int sx, int yl, int yh);

template<typename pixel_t, int columns>
static forceinline void rt_lucentcols(int hx, int sx, int yl, int yh);

template<typename pixel_t, int columns>
static forceinline void rt_tlatelucentcols(int hx, int sx, int yl, int yh);


template<typename pixel_t, int columns>
static forceinline void rt_copycols(int hx, int sx, int yl, int yh)
{
	byte *source;
	pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	shaderef_t pal = shaderef_t(&realcolormaps, 0);
	dest = (pixel_t *)(ylookup[yl] + columnofs[sx]);
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch / sizeof(pixel_t);

	if (count & 1)
	{
		for (int i = 0; i < columns; ++i)
			dest[pitch*0+i] = rt_rawcolor<pixel_t>(pal, source[0+i]);
		source += 4;
		dest += pitch;
	}
	if (count & 2)
	{
		for (int i = 0; i < columns; ++i)
			dest[pitch*0+i] = rt_rawcolor<pixel_t>(pal, source[0+i]);
		for (int i = 0; i < columns; ++i)
			dest[pitch*1+i] = rt_rawcolor<pixel_t>(pal, source[4+i]);
		source += 8;
		dest += pitch*2;
	}
	if (!(count >>= 1))
		return;

	do
	{
		for (int i = 0; i < columns; ++i)
			dest[pitch*0+i] = rt_rawcolor<pixel_t>(pal, source[0+i]);
		for (int i = 0; i < columns; ++i)
			dest[pitch*1+i] = rt_rawcolor<pixel_t>(pal, source[4+i]);
		source += 8;
		dest += pitch*2;
	} while (--count);
}

template<typename pixel_t, int columns>
static forceinline void rt_mapcols(int hx, int sx, int yl, int yh)
{
	byte *source;
	pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = (pixel_t *)(ylookup[yl] + columnofs[sx]);
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch / sizeof(pixel_t);

	if (count & 1)
	{
		for (int i = 0; i < columns; ++i)
			dest[pitch*0+i] = rt_mapcolor<pixel_t>(dc_colormap, source[0+i]);
		source += 4;
		dest += pitch;
	}
	if (!(count >>= 1))
		return;

	do
	{
		for (int i = 0; i < columns; ++i)
			dest[pitch*0+i] = rt_mapcolor<pixel_t>(dc_colormap, source[0+i]);
		for (int i = 0; i < columns; ++i)
			dest[pitch*1+i] = rt_mapcolor<pixel_t>(dc_colormap, source[4+i]);
		source += 8;
		dest += pitch*2;
	} while (--count);
}

template<typename pixel_t, int columns>
static forceinline void rt_tlatecols(int hx, int sx, int yl, int yh)
{
	byte *source;
	pixel_t *dest;
	int count;
	int pitch;

	count = yh-yl;
	if (count < 0)
		return;
	count++;

	dest = (pixel_t *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch / sizeof(pixel_t);

	do
	{
		for (int i = 0; i < columns; ++i)
			dest[i] = rt_tlatecolor<pixel_t>(dc_colormap, dc_translation, source[i]);
		source += 4;
		dest += pitch;
	} while (--count);
}

template<typename pixel_t, int columns>
static forceinline void rt_lucentcols(int hx, int sx, int yl, int yh)
{
	byte  *source;
	pixel_t *dest;
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

		// alphas should be in [0 .. 256]
		fga = (fglevel >> 8) * 256 / 255;
		bga = 256 - fga;
	}

	dest = (pixel_t *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch / sizeof(pixel_t);

	do
	{
		for (int i = 0; i < columns; ++i)
		{
			const pixel_t fg = rt_mapcolor<pixel_t>(dc_colormap, source[i]);
			const pixel_t bg = dest[i];

			dest[i] = rt_blend2<pixel_t>(bg, bga, fg, fga);
		}

		source += 4;
		dest += pitch;
	} while (--count);
}

template<typename pixel_t, int columns>
static forceinline void rt_tlatelucentcols(int hx, int sx, int yl, int yh)
{
	byte  *source;
	pixel_t *dest;
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

		// alphas should be in [0 .. 256]
		fga = (fglevel >> 8) * 256 / 255;
		bga = 256 - fga;
	}

	dest = (pixel_t *)( ylookup[yl] + columnofs[sx] );
	source = &dc_temp[yl*4 + hx];
	pitch = dc_pitch / sizeof(pixel_t);

	do
	{
		for (int i = 0; i < columns; ++i)
		{
			const pixel_t fg = rt_tlatecolor<pixel_t>(dc_colormap, dc_translation, source[i]);
			const pixel_t bg = dest[i];

			dest[i] = rt_blend2<pixel_t>(bg, bga, fg, fga);
		}

		source += 4;
		dest += pitch;
	} while (--count);
}


// Copies one span at hx to the screen at sx.
void rt_copy1colP (int hx, int sx, int yl, int yh)
{
	rt_copycols<byte, 1>(hx, sx, yl, yh);
}

// Copies two spans at hx and hx+1 to the screen at sx and sx+1.
void rt_copy2colsP (int hx, int sx, int yl, int yh)
{
	rt_copycols<byte, 2>(hx, sx, yl, yh);
}

// Copies all four spans to the screen starting at sx.
void rt_copy4colsP (int sx, int yl, int yh)
{
	rt_copycols<byte, 4>(0, sx, yl, yh);
}

// Maps one span at hx to the screen at sx.
void rt_map1colP (int hx, int sx, int yl, int yh)
{
	rt_mapcols<byte, 1>(hx, sx, yl, yh);
}

// Maps two spans at hx and hx+1 to the screen at sx and sx+1.
void rt_map2colsP (int hx, int sx, int yl, int yh)
{
	rt_mapcols<byte, 2>(hx, sx, yl, yh);
}

// Maps all four spans to the screen starting at sx.
void rt_map4colsP (int sx, int yl, int yh)
{
	rt_mapcols<byte, 4>(0, sx, yl, yh);
}

// Translates one span at hx to the screen at sx.
void rt_tlate1colP (int hx, int sx, int yl, int yh)
{
	rt_tlatecols<byte, 1>(hx, sx, yl, yh);
}

// Translates two spans at hx and hx+1 to the screen at sx and sx+1.
void rt_tlate2colsP (int hx, int sx, int yl, int yh)
{
	rt_tlatecols<byte, 2>(hx, sx, yl, yh);
}

// Translates all four spans to the screen starting at sx.
void rt_tlate4colsP (int sx, int yl, int yh)
{
	rt_tlatecols<byte, 4>(0, sx, yl, yh);
}

// Mixes one span at hx to the screen at sx.
void rt_lucent1colP (int hx, int sx, int yl, int yh)
{
	rt_lucentcols<byte, 1>(hx, sx, yl, yh);
}

// Mixes two spans at hx and hx+1 to the screen at sx and sx+1.
void rt_lucent2colsP (int hx, int sx, int yl, int yh)
{
	rt_lucentcols<byte, 2>(hx, sx, yl, yh);
}

// Mixes all four spans to the screen starting at sx.
void rt_lucent4colsP (int sx, int yl, int yh)
{
	rt_lucentcols<byte, 4>(0, sx, yl, yh);
}

// Translates and mixes one span at hx to the screen at sx.
void rt_tlatelucent1colP (int hx, int sx, int yl, int yh)
{
	rt_tlatelucentcols<byte, 1>(hx, sx, yl, yh);
}

// Translates and mixes two spans at hx and hx+1 to the screen at sx and sx+1.
void rt_tlatelucent2colsP (int hx, int sx, int yl, int yh)
{
	rt_tlatelucentcols<byte, 2>(hx, sx, yl, yh);
}

// Translates and mixes all four spans to the screen starting at sx.
void rt_tlatelucent4colsP (int sx, int yl, int yh)
{
	rt_tlatelucentcols<byte, 4>(0, sx, yl, yh);
}


// Direct rendering (32-bit) functions:


void rt_copy1colD_c (int hx, int sx, int yl, int yh)
{
	rt_copycols<DWORD, 1>(hx, sx, yl, yh);
}

void rt_copy2colsD_c (int hx, int sx, int yl, int yh)
{
	rt_copycols<DWORD, 2>(hx, sx, yl, yh);
}

void rt_copy4colsD_c (int sx, int yl, int yh)
{
	rt_copycols<DWORD, 4>(0, sx, yl, yh);
}

void rt_map1colD_c (int hx, int sx, int yl, int yh)
{
	rt_mapcols<DWORD, 1>(hx, sx, yl, yh);
}

void rt_map2colsD_c (int hx, int sx, int yl, int yh)
{
	rt_mapcols<DWORD, 2>(hx, sx, yl, yh);
}

void rt_map4colsD_c (int sx, int yl, int yh)
{
	rt_mapcols<DWORD, 4>(0, sx, yl, yh);
}

void rt_tlate1colD_c (int hx, int sx, int yl, int yh)
{
	rt_tlatecols<DWORD, 1>(hx, sx, yl, yh);
}

void rt_tlate2colsD_c (int hx, int sx, int yl, int yh)
{
	rt_tlatecols<DWORD, 2>(hx, sx, yl, yh);
}

void rt_tlate4colsD_c (int sx, int yl, int yh)
{
	rt_tlatecols<DWORD, 4>(0, sx, yl, yh);
}

void rt_lucent1colD_c (int hx, int sx, int yl, int yh)
{
	rt_lucentcols<DWORD, 1>(hx, sx, yl, yh);
}

void rt_lucent2colsD_c (int hx, int sx, int yl, int yh)
{
	rt_lucentcols<DWORD, 2>(hx, sx, yl, yh);
}

void rt_lucent4colsD_c (int sx, int yl, int yh)
{
	rt_lucentcols<DWORD, 4>(0, sx, yl, yh);
}

void rt_tlatelucent1colD_c (int hx, int sx, int yl, int yh)
{
	rt_tlatelucentcols<DWORD, 1>(hx, sx, yl, yh);
}

void rt_tlatelucent2colsD_c (int hx, int sx, int yl, int yh)
{
	rt_tlatelucentcols<DWORD, 2>(hx, sx, yl, yh);
}

void rt_tlatelucent4colsD_c (int sx, int yl, int yh)
{
	rt_tlatelucentcols<DWORD, 4>(0, sx, yl, yh);
}


// Functions for v_video.cpp support

void r_dimpatchD_c(const DCanvas *const cvs, DWORD color, int alpha, int x1, int y1, int w, int h)
{
	int x, y;
	DWORD *line;
	int invAlpha = 256 - alpha;

	int dpitch = cvs->pitch / sizeof(DWORD);
	line = (DWORD *)cvs->buffer + y1 * dpitch;

	for (y = y1; y < y1 + h; y++)
	{
		for (x = x1; x < x1 + w; x++)
		{
			line[x] = alphablend1a(line[x], color, alpha);
		}
		line += dpitch;
	}
}


// Generic drawing functions which call either D(irect) or P(alettized) functions above:


// Draws all spans at hx to the screen at sx.
void rt_draw1col (int hx, int sx)
{
	while (horizspan[hx] < dc_ctspan[hx]) {
		hcolfunc_post1 (hx, sx, horizspan[hx][0], horizspan[hx][1]);
		horizspan[hx] += 2;
	}
}

// Adjusts two columns so that they both start on the same row.
// Returns false if it succeeded, true if a column ran out.
static BOOL rt_nudgecols (int hx, int sx)
{
	if (horizspan[hx][0] < horizspan[hx+1][0]) {
spaghetti1:
		// first column starts before the second; it might also end before it
		if (horizspan[hx][1] < horizspan[hx+1][0]){
			while (horizspan[hx] < dc_ctspan[hx] && horizspan[hx][1] < horizspan[hx+1][0]) {
				hcolfunc_post1 (hx, sx, horizspan[hx][0], horizspan[hx][1]);
				horizspan[hx] += 2;
			}
			if (horizspan[hx] >= dc_ctspan[hx]) {
				// the first column ran out of spans
				rt_draw1col (hx+1, sx+1);
				return true;
			}
			if (horizspan[hx][0] > horizspan[hx+1][0])
				goto spaghetti2;	// second starts before first now
			else if (horizspan[hx][0] == horizspan[hx+1][0])
				return false;
		}
		hcolfunc_post1 (hx, sx, horizspan[hx][0], horizspan[hx+1][0] - 1);
		horizspan[hx][0] = horizspan[hx+1][0];
	}
	if (horizspan[hx][0] > horizspan[hx+1][0]) {
spaghetti2:
		// second column starts before the first; it might also end before it
		if (horizspan[hx+1][1] < horizspan[hx][0]) {
			while (horizspan[hx+1] < dc_ctspan[hx+1] && horizspan[hx+1][1] < horizspan[hx][0]) {
				hcolfunc_post1 (hx+1, sx+1, horizspan[hx+1][0], horizspan[hx+1][1]);
				horizspan[hx+1] += 2;
			}
			if (horizspan[hx+1] >= dc_ctspan[hx+1]) {
				// the second column ran out of spans
				rt_draw1col (hx, sx);
				return true;
			}
			if (horizspan[hx][0] < horizspan[hx+1][0])
				goto spaghetti1;	// first starts before second now
			else if (horizspan[hx][0] == horizspan[hx+1][0])
				return false;
		}
		hcolfunc_post1 (hx+1, sx+1, horizspan[hx+1][0], horizspan[hx][0] - 1);
		horizspan[hx+1][0] = horizspan[hx][0];
	}
	return false;
}

// Copies all spans at hx and hx+1 to the screen at sx and sx+1.
// hx and sx should be word-aligned.
void rt_draw2cols (int hx, int sx)
{
    while(1) // keep going until all columns have no more spans
    {
        if (horizspan[hx] >= dc_ctspan[hx]) {
            // no first column, do the second (if any)
            rt_draw1col (hx+1, sx+1);
            break;
        }
        if (horizspan[hx+1] >= dc_ctspan[hx+1]) {
            // no second column, do the first
            rt_draw1col (hx, sx);
            break;
        }

        // both columns have spans, align their tops
        if (rt_nudgecols (hx, sx))
            break;

        // now draw as much as possible as a series of words
        if (horizspan[hx][1] < horizspan[hx+1][1]) {
            // first column ends first, so draw down to its bottom
            hcolfunc_post2 (hx, sx, horizspan[hx][0], horizspan[hx][1]);
            horizspan[hx+1][0] = horizspan[hx][1] + 1;
            horizspan[hx] += 2;
        } else {
            // second column ends first, or they end at the same spot
            hcolfunc_post2 (hx, sx, horizspan[hx+1][0], horizspan[hx+1][1]);
            if (horizspan[hx][1] == horizspan[hx+1][1]) {
                horizspan[hx] += 2;
                horizspan[hx+1] += 2;
            } else {
                horizspan[hx][0] = horizspan[hx+1][1] + 1;
                horizspan[hx+1] += 2;
            }
        }
    }
}

// Copies all spans in all four columns to the screen starting at sx.
// sx should be longword-aligned.
void rt_draw4cols (int sx)
{
loop:
	if (horizspan[0] >= dc_ctspan[0]) {
		// no first column, do the second (if any)
		rt_draw1col (1, sx+1);
		rt_draw2cols (2, sx+2);
		return;
	}
	if (horizspan[1] >= dc_ctspan[1]) {
		// no second column, we already know there is a first one
		rt_draw1col (0, sx);
		rt_draw2cols (2, sx+2);
		return;
	}
	if (horizspan[2] >= dc_ctspan[2]) {
		// no third column, do the fourth (if any)
		rt_draw2cols (0, sx);
		rt_draw1col (3, sx+3);
		return;
	}
	if (horizspan[3] >= dc_ctspan[3]) {
		// no fourth column, but there is a third
		rt_draw2cols (0, sx);
		rt_draw1col (2, sx+2);
		return;
	}

	// if we get here, then we know all four columns have something,
	// make sure they all align at the top
	if (rt_nudgecols (0, sx)) {
		rt_draw2cols (2, sx+2);
		return;
	}
	if (rt_nudgecols (2, sx+2)) {
		rt_draw2cols (0, sx);
		return;
	}

	// first column is now aligned with second at top, and third is aligned
	// with fourth at top. now make sure both halves align at the top and
	// also have some shared space to their bottoms.
	{
		unsigned int bot1 = horizspan[0][1] < horizspan[1][1] ? horizspan[0][1] : horizspan[1][1];
		unsigned int bot2 = horizspan[2][1] < horizspan[3][1] ? horizspan[2][1] : horizspan[3][1];

		if (horizspan[0][0] < horizspan[2][0]) {
			// first half starts before second half
			if (bot1 >= horizspan[2][0]) {
				// first half ends after second begins
				hcolfunc_post2 (0, sx, horizspan[0][0], horizspan[2][0] - 1);
				horizspan[0][0] = horizspan[1][0] = horizspan[2][0];
			} else {
				// first half ends before second begins
				hcolfunc_post2 (0, sx, horizspan[0][0], bot1);
				if (horizspan[0][1] == bot1)
					horizspan[0] += 2;
				else
					horizspan[0][0] = bot1 + 1;
				if (horizspan[1][1] == bot1)
					horizspan[1] += 2;
				else
					horizspan[1][0] = bot1 + 1;
				goto loop;	// start over
			}
		} else if (horizspan[0][0] > horizspan[2][0]) {
			// second half starts before the first
			if (bot2 >= horizspan[0][0]) {
				// second half ends after first begins
				hcolfunc_post2 (2, sx+2, horizspan[2][0], horizspan[0][0] - 1);
				horizspan[2][0] = horizspan[3][0] = horizspan[0][0];
			} else {
				// second half ends before first begins
				hcolfunc_post2 (2, sx+2, horizspan[2][0], bot2);
				if (horizspan[2][1] == bot2)
					horizspan[2] += 2;
				else
					horizspan[2][0] = bot2 + 1;
				if (horizspan[3][1] == bot2)
					horizspan[3] += 2;
				else
					horizspan[3][0] = bot2 + 1;
				goto loop;	// start over
			}
		}

		// all four columns are now aligned at the top; draw all of them
		// until one ends.
		bot1 = bot1 < bot2 ? bot1 : bot2;

		hcolfunc_post4 (sx, horizspan[0][0], bot1);

		{
			int x;

			for (x = 3; x >= 0; x--)
				if (horizspan[x][1] == bot1)
					horizspan[x] += 2;
				else
					horizspan[x][0] = bot1 + 1;
		}
	}

	goto loop;	// keep going until all columns have no more spans
}

// Before each pass through a rendering loop that uses these routines,
// call this function to set up the span pointers.
void rt_initcols (void)
{
	int y;

	for (y = 3; y >= 0; y--)
		horizspan[y] = dc_ctspan[y] = &dc_tspans[y][0];
}

// Same as R_DrawMaskedColumn() except that it always uses
// R_DrawColumnHoriz().
void R_DrawMaskedColumnHoriz (tallpost_t *post)
{
	dc_texturefrac = 0;

	while (!post->end())
	{
		// calculate unclipped screen coordinates for post
		int topscreen = sprtopscreen + spryscale * post->topdelta + 1;

		dc_yl = (topscreen + FRACUNIT) >> FRACBITS;
		dc_yh = (topscreen + spryscale * post->length) >> FRACBITS;
				
		if (dc_yh >= mfloorclip[dc_x])
			dc_yh = mfloorclip[dc_x] - 1;
		if (dc_yl <= mceilingclip[dc_x])
		{
			int oldyl = dc_yl;
			dc_yl = mceilingclip[dc_x] + 1;
			dc_texturefrac = (dc_yl - oldyl) * dc_iscale;
		}
		else
			dc_texturefrac = 0;

		if (dc_yl <= dc_yh)
		{
			dc_source = post->data();
			hcolfunc_pre ();
		}

		post = post->next();
	}
}

VERSION_CONTROL (r_drawt_cpp, "$Id$")

