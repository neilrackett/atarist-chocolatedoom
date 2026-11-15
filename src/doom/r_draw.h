//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//	System specific interface stuff.
//


#ifndef __R_DRAW__
#define __R_DRAW__




extern lighttable_t*	dc_colormap;
extern int		dc_x;
extern int		dc_yl;
extern int		dc_yh;
extern fixed_t		dc_iscale;
extern fixed_t		dc_texturemid;

// first pixel in a column
extern byte*		dc_source;		


// The span blitting interface.
// Hook in assembler or system specific BLT
//  here.
#if defined(ATARI) && defined(__M68020__)
// The column and span drawers in assembler (src/atari/r_draw_m68k.c)
#define R_DRAW_M68K
void	R_InitColormapsW (int lump);

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

// Draws columns c to end (exclusive)
void	R_DrawColumns (const colrec_t* c, const colrec_t* end);
void	R_DrawColumnsLow (const colrec_t* c, const colrec_t* end);

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

// Draws spans r to end (exclusive) from the 64x64 flat source
void	R_DrawSpans (const spanrec_t* r, const spanrec_t* end, const byte* source);
void	R_DrawSpansLow (const spanrec_t* r, const spanrec_t* end, const byte* source);
#endif

void 	R_DrawColumn (void);
void 	R_DrawColumnLow (void);

// The Spectre/Invisibility effect.
void 	R_DrawFuzzColumn (void);
void 	R_DrawFuzzColumnLow (void);

// Draw with color translation tables,
//  for player sprite rendering,
//  Green/Red/Blue/Indigo shirts.
void	R_DrawTranslatedColumn (void);
void	R_DrawTranslatedColumnLow (void);

void
R_VideoErase
( unsigned	ofs,
  int		count );

extern int		ds_y;
extern int		ds_x1;
extern int		ds_x2;

extern lighttable_t*	ds_colormap;

extern fixed_t		ds_xfrac;
extern fixed_t		ds_yfrac;
extern fixed_t		ds_xstep;
extern fixed_t		ds_ystep;

// start of a 64*64 tile image
extern byte*		ds_source;		

extern byte*		translationtables;
extern byte*		dc_translation;


// Span blitting for rows, floor/ceiling.
// No Sepctre effect needed.
void 	R_DrawSpan (void);

// Low resolution mode, 160x200?
void 	R_DrawSpanLow (void);


void
R_InitBuffer
( int		width,
  int		height );


// Initialize color translation tables,
//  for player rendering etc.
void	R_InitTranslationTables (void);



// Rendering function.
void R_FillBackScreen (void);

// If the view size is not full screen, draws a border around it.
void R_DrawViewBorder (void);



#endif
