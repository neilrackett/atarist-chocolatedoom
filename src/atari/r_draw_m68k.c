//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 Neil Rackett
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
//     The column and span drawers for a 68020 or better (the Atari TT
//     and Falcon build, R_DRAW_M68K), after TTDOOM's: the same pixels
//     as r_draw.c's, which they replace.
//
//     Low detail draws each texel to two pixels with one word store,
//     from the word colormaps: every light table again, with each colour
//     in both bytes of a word.
//

#include <stdlib.h>

#include "doomdef.h"
#include "i_system.h"
#include "r_local.h"
#include "w_wad.h"
#include "z_zone.h"

#ifdef R_DRAW_M68K

// r_draw.c's
extern pixel_t *ylookup[];
extern int columnofs[];

unsigned short *colormaps_w;

// r_draw.c's fuzz table, and here as words, repeated so that a column
// starting anywhere in it never wraps
#define FUZZTABLE 50
extern int fuzzoffset[FUZZTABLE];
extern int fuzzpos;
static short fuzzoffset_w[FUZZTABLE + SCREENHEIGHT];

void R_InitColormapsW(int lump)
{
    int i, n = W_LumpLength(lump);

    colormaps_w = Z_Malloc(n * 2, PU_STATIC, NULL);
    for (i = 0; i < n; ++i)
    {
        colormaps_w[i] = colormaps[i] * 0x0101;
    }

    for (i = 0; i < FUZZTABLE + SCREENHEIGHT; ++i)
    {
        fuzzoffset_w[i] = fuzzoffset[i % FUZZTABLE];
    }
}

// The word colormap for a byte colormap in colormaps
static inline const unsigned short *WordColormap(const lighttable_t *cmap)
{
    return colormaps_w + (cmap - colormaps);
}

static inline __attribute__((always_inline)) void DrawColumn(const int low)
{
    colrec_t c;

    // Zero length, column does not exceed a pixel.
    if (dc_yh < dc_yl)
	return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    R_SetupColumn(&c, dc_x, dc_yl, dc_yh, dc_source, dc_colormap,
                  dc_iscale, dc_texturemid, low);
    DrawColumnRec(&c, low);
}

void R_DrawColumn (void)
{
    DrawColumn(0);
}

void R_DrawColumnLow (void)
{
    DrawColumn(1);
}

void R_DrawColumns (const colrec_t* c, const colrec_t* end)
{
    for ( ; c < end; c++)
        DrawColumnRec(c, 0);
}

void R_DrawColumnsLow (const colrec_t* c, const colrec_t* end)
{
    for ( ; c < end; c++)
        DrawColumnRec(c, 1);
}

//
// Fuzz: each pixel the one above or below it (fuzzoffset) through the
// light table 6, top to bottom as r_draw.c's, which reads pixels it has
// just written. Low detail does the column's two pixels in turn.
//
static inline __attribute__((always_inline)) void DrawFuzzColumn(const int low)
{
    int count;
    short n;
    pixel_t *dest;
    const short *fuzz;
    unsigned texel, offset;

    // Adjust borders. Low...
    if (!dc_yl)
	dc_yl = 1;

    // .. and high.
    if (dc_yh == viewheight-1)
	dc_yh = viewheight - 2;

    count = dc_yh - dc_yl;

    // Zero length.
    if (count < 0)
	return;

#ifdef RANGECHECK
    if ((unsigned)(low ? dc_x << 1 : dc_x) >= SCREENWIDTH
	|| dc_yl < 0 || dc_yh >= SCREENHEIGHT)
    {
	I_Error ("R_DrawFuzzColumn: %i to %i at %i",
		 dc_yl, dc_yh, dc_x);
    }
#endif

    dest = ylookup[dc_yl] + columnofs[low ? dc_x << 1 : dc_x];
    fuzz = fuzzoffset_w + fuzzpos;
    n = count;
    texel = 0;

    __asm__ __volatile__ (
        "0:					\n\t"
        "move.w	(%[fuzz])+,%[offset]		\n\t"
        "move.b	(%[dest],%[offset].w),%[texel]	\n\t"
        "move.b	(%[cmap],%[texel].w),(%[dest])	\n\t"
        ".if %c[low]				\n\t"
        "move.b	1(%[dest],%[offset].w),%[texel]	\n\t"
        "move.b	(%[cmap],%[texel].w),1(%[dest])	\n\t"
        ".endif					\n\t"
        "lea	%c[width](%[dest]),%[dest]	\n\t"
        "dbra	%[n],0b				\n\t"
        : [n] "+d" (n), [dest] "+a" (dest), [fuzz] "+a" (fuzz),
          [texel] "+d" (texel), [offset] "=&d" (offset)
        : [cmap] "a" (colormaps + 6 * 256), [width] "i" (SCREENWIDTH),
          [low] "i" (low)
        : "cc", "memory");

    fuzzpos = (fuzzpos + count + 1) % FUZZTABLE;
}

void R_DrawFuzzColumn (void)
{
    DrawFuzzColumn(0);
}

void R_DrawFuzzColumnLow (void)
{
    DrawFuzzColumn(1);
}

static inline __attribute__((always_inline)) void DrawSpan(const int low)
{
    spanrec_t	r;

#ifdef RANGECHECK
    if (ds_x2 < ds_x1
	|| ds_x1<0
	|| ds_x2>=SCREENWIDTH
	|| (unsigned)ds_y>SCREENHEIGHT)
    {
	I_Error( "R_DrawSpan: %i to %i at %i",
		 ds_x1,ds_x2,ds_y);
    }
#endif

    R_SetupSpan (&r, ds_y, ds_x1, ds_x2, ds_colormap,
		 ds_xfrac, ds_yfrac, ds_xstep, ds_ystep, low);
    if (low)
    {
        // r_draw.c's doubles the globals
        ds_x1 <<= 1;
        ds_x2 <<= 1;
    }
    DrawSpanRec (&r, ds_source, low);
}

void R_DrawSpans (const spanrec_t* r, const spanrec_t* end, const byte* source)
{
    for ( ; r < end; r++)
	DrawSpanRec (r, source, 0);
}

void R_DrawSpansLow (const spanrec_t* r, const spanrec_t* end, const byte* source)
{
    for ( ; r < end; r++)
	DrawSpanRec (r, source, 1);
}

void R_DrawSpan (void)
{
    DrawSpan(0);
}

void R_DrawSpanLow (void)
{
    DrawSpan(1);
}

#endif
