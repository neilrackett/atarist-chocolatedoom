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

//
// Columns: vanilla's 16.16 texture step in four instructions a pixel.
// pos keeps the fraction in the high word and the 7-bit texel row in the
// low word, which indexes the column directly. addx.l steps both halves
// at once: the fraction's carry out of bit 31 goes to X, and the next
// pixel's addx adds it to the row. Nothing else in the loop touches X,
// and X starts as the first step's carry with the fraction already one
// step on, so every row is vanilla's (frac >> 16) & 127 exactly. The row
// is kept to 7 bits (and.w), with the row step taken mod 128, so the low
// word never carries into the fraction. Two pixels a loop; an odd count
// enters at the second, a line above the first pixel.
//
static inline __attribute__((always_inline)) void DrawColumn(const int low)
{
    int count;
    short n, pairs;
    unsigned step, frac, pos, texel;
    pixel_t *dest;
    const void *cmap;

    count = dc_yh - dc_yl;

    // Zero length, column does not exceed a pixel.
    if (count < 0)
	return;

#ifdef RANGECHECK
    if ((unsigned)dc_x >= SCREENWIDTH
	|| dc_yl < 0
	|| dc_yh >= SCREENHEIGHT)
	I_Error ("R_DrawColumn: %i to %i at %i", dc_yl, dc_yh, dc_x);
#endif

    n = count + 1;
    step = dc_iscale;
    frac = dc_texturemid + (dc_yl - centery) * step;

    // Blocky mode, need to multiply by 2.
    dest = ylookup[dc_yl] + columnofs[low ? dc_x << 1 : dc_x]
         - (n & 1 ? SCREENWIDTH : 0);
    cmap = low ? (const void *) WordColormap(dc_colormap)
               : (const void *) dc_colormap;
    pos = ((frac + step) << 16) | ((frac >> 16) & 127);
    // The CCR to start with, X the first step's carry and Z an even count,
    // and then each texel: below 32, so its high bits are clear for the
    // texel's use as an index
    texel = (((frac & 0xffff) + (step & 0xffff)) >> 16) << 4
          | (n & 1 ? 0 : 4);
    step = (step << 16) | ((step >> 16) & 127);
    pairs = (n >> 1) - !(n & 1);

    __asm__ __volatile__ (
        "move.w	%[texel],%%ccr			\n\t"
        "bne.s	1f				\n\t" // odd: the second
        "0:					\n\t"
        "move.b	(%[src],%[pos].w),%[texel]	\n\t"
        "addx.l	%[step],%[pos]			\n\t"
        "and.w	%[mask],%[pos]			\n\t"
        ".if %c[low]				\n\t"
        "move.w	(%[cmap],%[texel].w*2),(%[dest])	\n\t"
        ".else					\n\t"
        "move.b	(%[cmap],%[texel].w),(%[dest])	\n\t"
        ".endif					\n\t"
        "1:					\n\t"
        "move.b	(%[src],%[pos].w),%[texel]	\n\t"
        "addx.l	%[step],%[pos]			\n\t"
        "and.w	%[mask],%[pos]			\n\t"
        ".if %c[low]				\n\t"
        "move.w	(%[cmap],%[texel].w*2),%c[width](%[dest])	\n\t"
        ".else					\n\t"
        "move.b	(%[cmap],%[texel].w),%c[width](%[dest])	\n\t"
        ".endif					\n\t"
        "lea	(2*%c[width])(%[dest]),%[dest]	\n\t"
        "dbra	%[pairs],0b			\n\t"
        : [pos] "+d" (pos), [texel] "+d" (texel), [dest] "+a" (dest),
          [pairs] "+d" (pairs)
        : [src] "a" (dc_source), [cmap] "a" (cmap),
          [step] "d" (step), [mask] "d" (127),
          [width] "i" (SCREENWIDTH), [low] "i" (low)
        : "cc", "memory");
}

void R_DrawColumn (void)
{
    DrawColumn(0);
}

void R_DrawColumnLow (void)
{
    DrawColumn(1);
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

//
// Spans: vanilla's packed position, exactly: x in the high word and y in
// the low, each 6 bits of texel and 10 of fraction, stepped by one add,
// with y's carry into x's fraction that implies. The texel is
// (position >> 4 & 0xfc0) | position >> 26, the second half as a
// rotate. Two pixels a loop; an odd count enters at the second.
//
#define SPAN_PIXEL(store) \
        "move.l	%[pos],%[spot]			\n\t" \
        "lsr.l	#4,%[spot]			\n\t" \
        "and.w	#0x0fc0,%[spot]			\n\t" \
        "move.l	%[pos],%[texel]			\n\t" \
        "rol.l	#6,%[texel]			\n\t" \
        "and.w	#63,%[texel]			\n\t" \
        "or.w	%[texel],%[spot]		\n\t" \
        "move.b	(%[src],%[spot].w),%[texel]	\n\t" \
        store \
        "add.l	%[step],%[pos]			\n\t"

static inline __attribute__((always_inline)) void DrawSpan(const int low)
{
    unsigned position, step, spot, texel;
    short count;
    pixel_t *dest;
    const void *cmap;

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

    position = ((ds_xfrac << 10) & 0xffff0000)
             | ((ds_yfrac >> 6)  & 0x0000ffff);
    step = ((ds_xstep << 10) & 0xffff0000)
         | ((ds_ystep >> 6)  & 0x0000ffff);

    // The pixels, which the loop counts in pairs: even counts loop
    // count / 2 times, odd ones enter at the second pixel.
    count = ds_x2 - ds_x1 + 1;

    if (low)
    {
        // Blocky mode, need to multiply by 2 (as r_draw.c does, to the
        // globals).
        ds_x1 <<= 1;
        ds_x2 <<= 1;
        cmap = WordColormap(ds_colormap);
    }
    else
    {
        cmap = ds_colormap;
    }

    dest = ylookup[ds_y] + columnofs[ds_x1];

    __asm__ __volatile__ (
        "lsr.w	#1,%[count]			\n\t"
        "bcs.s	1f				\n\t"
        "subq.w	#1,%[count]			\n\t"
        "0:					\n\t"
        ".if %c[low]				\n\t"
        SPAN_PIXEL("move.w	(%[cmap],%[texel].w*2),(%[dest])+	\n\t")
        ".else					\n\t"
        SPAN_PIXEL("move.b	(%[cmap],%[texel].w),(%[dest])+	\n\t")
        ".endif					\n\t"
        "1:					\n\t"
        ".if %c[low]				\n\t"
        SPAN_PIXEL("move.w	(%[cmap],%[texel].w*2),(%[dest])+	\n\t")
        ".else					\n\t"
        SPAN_PIXEL("move.b	(%[cmap],%[texel].w),(%[dest])+	\n\t")
        ".endif					\n\t"
        "dbra	%[count],0b			\n\t"
        : [pos] "+d" (position), [count] "+d" (count), [dest] "+a" (dest),
          [spot] "=&d" (spot), [texel] "=&d" (texel)
        : [src] "a" (ds_source), [cmap] "a" (cmap), [step] "d" (step),
          [low] "i" (low)
        : "cc", "memory");
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
