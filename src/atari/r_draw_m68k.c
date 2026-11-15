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
static inline __attribute__((always_inline)) void
DrawColumnRec(const colrec_t *c, const int low)
{
    unsigned pos = c->pos;
    // The CCR to start with, and then each texel: below 32, so its high
    // bits are clear for the texel's use as an index
    unsigned texel = (unsigned short) c->ccr;
    short pairs = c->pairs;
    pixel_t *dest = c->dest;

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
        : [src] "a" (c->source), [cmap] "a" (c->cmap),
          [step] "d" (c->step), [mask] "d" (127),
          [width] "i" (SCREENWIDTH), [low] "i" (low)
        : "cc", "memory");
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

//
// Spans: 7.5 instructions a pixel (TTDOOM's, after Aki Laukkanen's span
// for ADoom). x (u) and y (v) are 16.16 as in vanilla, but split across
// two registers with the fractions crossed over (R_SetupSpan, r_draw.h):
//   u = v fraction : u texel (bits 5-0)
//   v = u fraction : v texel * 64 (bits 11-6)
// addx.l steps a register's fraction and texel together; the fraction's
// carry goes to X, and the other register's addx adds it to its own
// texel's low bit. So the two addx.l chain one X flag between u and v,
// nothing else in the loop touches X, and X starts as v's first carry
// (v's fraction is already one step on). or.w sets u's bits 15-6 and v's
// bits 15-12 and 5-0: v's carry ripples through bits 5-0 into its texel,
// and the and.w of the two is $f000 + v*64 + u, a texel of the flat 4096
// bytes on (src is the flat + $1000). A row that wraps carries into the
// other fraction's lowest bit (1/65536 of a texel). Low detail writes each
// texel to two pixels with one word store, from the word colormaps.
//
#define R_DRAWSPAN_PIXEL \
        "or.w    %%d6,%[u]                  \n\t" \
        "or.w    %%d7,%[v]                  \n\t" \
        "and.w   %[v],%[u]                  \n\t" /* $f000 + v*64 + u */ \
        "addx.l  %[vstep],%[v]              \n\t" \
        "move.b  (%[src],%[u].w),%%d5       \n\t" \
        "addx.l  %[ustep],%[u]              \n\t" \
        ".if %c[low]                        \n\t" \
        "move.w  (%[cmap],%%d5.w*2),(%[dest])+ \n\t" \
        ".else                              \n\t" \
        "move.b  (%[cmap],%%d5.w),(%[dest])+ \n\t" \
        ".endif                             \n\t"

static inline __attribute__((always_inline)) void
DrawSpanRec (const spanrec_t* r, const byte* source, const int low)
{
    unsigned	u = r->u;
    unsigned	v = r->v;
    pixel_t*	dest = r->dest;
    short	pairs = r->pairs;
    // The CCR to start with, in d5, which then takes each texel: below 32,
    // so its high bits are already clear for the texel's use as an index
    register long ccr __asm__("d5") = (unsigned short)r->ccr;

    __asm__ __volatile__ (
        "move.w  #0xffc0,%%d6               \n\t"
        "move.w  #0xf03f,%%d7               \n\t"
        "move.w  %%d5,%%ccr                 \n\t"
        "beq.s   1f                         \n\t" // even: pairs only
        R_DRAWSPAN_PIXEL
        "1:                                 \n\t"
        "tst.w   %[pairs]                   \n\t"
        "bmi.s   3f                         \n\t"
        "2:                                 \n\t"
        R_DRAWSPAN_PIXEL
        R_DRAWSPAN_PIXEL
        "dbra    %[pairs],2b                \n\t"
        "3:                                 \n\t"
        : [dest] "+a"(dest), [u] "+d"(u), [v] "+d"(v), [pairs] "+d"(pairs),
          "+d"(ccr)
        : [src] "a"(source + 0x1000), [cmap] "a"(r->cmap),
          [ustep] "d"(r->ustep), [vstep] "d"(r->vstep),
          [low] "i"(low)
        : "d6", "d7", "cc", "memory");
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
