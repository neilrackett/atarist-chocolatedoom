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
//     and Falcon builds), after TTDOOM's: their records, set-up and
//     loops, for every game's renderer (Doom's r_draw_m68k.c, Heretic's
//     and Hexen's r_draw.c). Included after the game's renderer types
//     (fixed_t, lighttable_t, SCREENWIDTH).
//
//     Low detail draws each texel to two pixels with one word store,
//     from the word colormaps: every light table again, with each colour
//     in both bytes of a word (colormaps_w, which the game builds).
//

#ifndef __R_DRAW_M68K__
#define __R_DRAW_M68K__

#if defined(ATARI) && defined(__M68020__)
#define R_DRAW_M68K

// A column to draw, set up from what R_DrawColumn takes by R_SetupColumn
// and drawn by R_DrawColumns: the walls set up a seg's columns and then
// draw them together, each loop small enough for a 68030's 256-byte
// instruction cache (after TTDOOM).
typedef struct
{
    pixel_t*		dest;	// an odd count: a line above the first pixel
    const byte*		source;
    const void*		cmap;	// a word colormap in low detail
    unsigned		pos;	// fraction (a step on) : texel row
    unsigned		step;	// fraction step : row step mod 128
    short		ccr;	// X: the first step's carry; Z: an even count
    short		pairs;	// a loop each, less one (dbra)
} colrec_t;

extern pixel_t*		ylookup[];
extern int		columnofs[];
extern int		centery;
extern unsigned short*	colormaps_w;
extern lighttable_t*	colormaps;

// Pixels yl to yh (at least one) of view column x (a low detail column is
// two screen columns). The texture step is vanilla's 16.16: frac is kept
// with the fraction in the high word and the 7-bit texel row in the low,
// stepped by addx.l with X carrying the fraction into the next row.
static inline void
R_SetupColumn
( colrec_t*		c,
  int			x,
  int			yl,
  int			yh,
  const byte*		source,
  const lighttable_t*	colormap,
  fixed_t		iscale,
  fixed_t		texturemid,
  const int		low )
{
    short	n = yh - yl + 1;
    unsigned	step = iscale;
    unsigned	frac = texturemid + (yl - centery) * step;

    c->dest = ylookup[yl] + columnofs[low ? x << 1 : x]
	- (n & 1 ? SCREENWIDTH : 0);
    c->source = source;
    c->cmap = low ? (const void *)(colormaps_w + (colormap - colormaps))
	: (const void *)colormap;
    c->pos = ((frac + step) << 16) | ((frac >> 16) & 127);
    c->step = (step << 16) | ((step >> 16) & 127);
    c->ccr = (((frac & 0xffff) + (step & 0xffff)) >> 16) << 4
	| (n & 1 ? 0 : 4);
    c->pairs = (n >> 1) - !(n & 1);
}


// A span to draw, set up from what R_DrawSpan takes by R_SetupSpan and
// drawn by R_DrawSpans (after TTDOOM, and Aki Laukkanen's span for ADoom).
// The 16.16 positions are split across two registers with the fractions
// crossed over, so one X flag carries between them (r_draw_m68k.c).
// Vanilla packs 10 fraction bits a coordinate, this keeps 16: floors can
// differ from vanilla's by sub-texel amounts.
typedef struct
{
    pixel_t*		dest;
    const void*		cmap;	// a word colormap in low detail
    unsigned		u;	// y fraction (a step on) : x texel
    unsigned		v;	// x fraction : y texel * 64
    unsigned		ustep;	// y fraction step : x step mod 64
    unsigned		vstep;	// x fraction step : (y step mod 64) * 64
    short		ccr;	// X: y's first carry; Z: an even count
    short		pairs;	// pairs a loop, less one (dbra)
} spanrec_t;

// Texels x1 to x2 of view row y (a low detail texel is two pixels), from
// 16.16 xfrac/yfrac stepping by xstep/ystep, as vanilla's
static inline void
R_SetupSpan
( spanrec_t*		r,
  int			y,
  int			x1,
  int			x2,
  const lighttable_t*	colormap,
  unsigned		xf,
  unsigned		yf,
  unsigned		xs,
  unsigned		ys,
  const int		low )
{
    unsigned short	n = x2 - x1 + 1;

    r->dest = ylookup[y] + columnofs[low ? x1 << 1 : x1];
    r->cmap = low ? (const void *)(colormaps_w + (colormap - colormaps))
	: (const void *)colormap;
    r->u = ((yf + ys) << 16) | ((xf >> 16) & 63);
    r->v = (xf << 16) | ((yf >> 10) & (63 << 6));
    r->ustep = (ys << 16) | ((xs >> 16) & 63);
    r->vstep = (xs << 16) | ((ys >> 10) & (63 << 6));
    r->ccr = (((yf & 0xffff) + (ys & 0xffff)) >> 16) << 4 | (n & 1 ? 0 : 4);
    r->pairs = (n >> 1) - 1;
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

#endif

#endif
