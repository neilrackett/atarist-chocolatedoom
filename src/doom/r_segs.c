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
//	All the clipping: columns, horizontal spans, sky columns.
//






#include <stdio.h>
#include <stdlib.h>

#include "i_system.h"

#include "doomdef.h"
#include "doomstat.h"

#include "r_local.h"
#include "r_sky.h"
#include "w_wad.h"


// OPTIMIZE: closed two sided lines as single sided

// True if any of the segs textures might be visible.
boolean		segtextured;	

// False if the back side is the same plane.
boolean		markfloor;	
boolean		markceiling;

boolean		maskedtexture;
int		toptexture;
int		bottomtexture;
int		midtexture;


angle_t		rw_normalangle;
// angle to line origin
int		rw_angle1;	

//
// regular wall
//
int		rw_x;
int		rw_stopx;
angle_t		rw_centerangle;
fixed_t		rw_offset;
fixed_t		rw_distance;
fixed_t		rw_scale;
fixed_t		rw_scalestep;
fixed_t		rw_midtexturemid;
fixed_t		rw_toptexturemid;
fixed_t		rw_bottomtexturemid;

int		worldtop;
int		worldbottom;
int		worldhigh;
int		worldlow;

fixed_t		pixhigh;
fixed_t		pixlow;
fixed_t		pixhighstep;
fixed_t		pixlowstep;

fixed_t		topfrac;
fixed_t		topstep;

fixed_t		bottomfrac;
fixed_t		bottomstep;


lighttable_t**	walllights;

short*		maskedtexturecol;



//
// R_RenderMaskedSegRange
//
void
R_RenderMaskedSegRange
( drawseg_t*	ds,
  int		x1,
  int		x2 )
{
    unsigned	index;
    column_t*	col;
    int		lightnum;
    int		texnum;
    
    // Calculate light table.
    // Use different light tables
    //   for horizontal / vertical / diagonal. Diagonal?
    // OPTIMIZE: get rid of LIGHTSEGSHIFT globally
    curline = ds->curline;
    frontsector = curline->frontsector;
    backsector = curline->backsector;
    texnum = texturetranslation[curline->sidedef->midtexture];
	
    lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT)+extralight;

    if (curline->v1->y == curline->v2->y)
	lightnum--;
    else if (curline->v1->x == curline->v2->x)
	lightnum++;

    if (lightnum < 0)		
	walllights = scalelight[0];
    else if (lightnum >= LIGHTLEVELS)
	walllights = scalelight[LIGHTLEVELS-1];
    else
	walllights = scalelight[lightnum];

    maskedtexturecol = ds->maskedtexturecol;

    rw_scalestep = ds->scalestep;		
    spryscale = ds->scale1 + (x1 - ds->x1)*rw_scalestep;
    mfloorclip = ds->sprbottomclip;
    mceilingclip = ds->sprtopclip;
    
    // find positioning
    if (curline->linedef->flags & ML_DONTPEGBOTTOM)
    {
	dc_texturemid = frontsector->floorheight > backsector->floorheight
	    ? frontsector->floorheight : backsector->floorheight;
	dc_texturemid = dc_texturemid + textureheight[texnum] - viewz;
    }
    else
    {
	dc_texturemid =frontsector->ceilingheight<backsector->ceilingheight
	    ? frontsector->ceilingheight : backsector->ceilingheight;
	dc_texturemid = dc_texturemid - viewz;
    }
    dc_texturemid += curline->sidedef->rowoffset;
			
    if (fixedcolormap)
	dc_colormap = fixedcolormap;
    
    // draw the columns
    for (dc_x = x1 ; dc_x <= x2 ; dc_x++)
    {
	// calculate lighting
	if (maskedtexturecol[dc_x] != SHRT_MAX)
	{
	    if (!fixedcolormap)
	    {
		index = spryscale>>LIGHTSCALESHIFT;

		if (index >=  MAXLIGHTSCALE )
		    index = MAXLIGHTSCALE-1;

		dc_colormap = walllights[index];
	    }
			
	    sprtopscreen = centeryfrac - FixedMul(dc_texturemid, spryscale);
	    dc_iscale = 0xffffffffu / (unsigned)spryscale;
	    
	    // draw the texture
	    col = (column_t *)( 
		(byte *)R_GetColumn(texnum,maskedtexturecol[dc_x]) -3);
			
	    R_DrawMaskedColumn (col);
	    maskedtexturecol[dc_x] = SHRT_MAX;
	}
	spryscale += rw_scalestep;
    }
	
}




//
// R_RenderSegLoop
// Draws zero, one, or two textures (and possibly a masked
//  texture) for walls.
// Can draw or mark the starting pixel of floor and ceiling
//  textures.
// CALLED: CORE LOOPING ROUTINE.
//
#define HEIGHTBITS		12
#define HEIGHTUNIT		(1<<HEIGHTBITS)

#ifdef R_DRAW_M68K
// The seg loop in passes (after TTDOOM), each a loop of its own that
// mostly fits the 68030's 256-byte instruction cache, with the seg's state
// in locals (as one loop, with the column fetch and drawer in turn, it
// missed the cache a third of the time):
// 1. R_SegMarkSolid / R_SegMarkTwoSided: each column's clipping, its
//    marks in the floor and ceiling planes, and the tiers it finds.
// 2. R_SegTierColumns: the texture column, light and scale of the columns
//    that have a tier, once a column.
// 3. R_DrawSegTiers: each tier's texture column fetched and set up as a
//    column record, and then drawn together.
// The same pixels as id's loop: a column's scale is the seg's first plus
// its step times the columns since, which is what adding the step each
// column comes to, in unsigned arithmetic.

extern short**		texturecolumnlump;
extern unsigned short**	texturecolumnofs;
extern byte**		texturecomposite;
extern int*		texturewidthmask;

// A wall tier as the seg loop finds it, set up as a column afterwards
typedef struct
{
    short		x;
    short		yl;
    short		yh;
    short		tex;
    fixed_t		texturecolumn;
    fixed_t		texturemid;
    lighttable_t*	colormap;
    fixed_t		iscale;
} segtier_t;

static segtier_t	segtiers[3*SCREENWIDTH];
static colrec_t		segcols[3*SCREENWIDTH];

// R_GetColumn when the column is in memory already (a cached patch or a
// composite), without W_CacheLumpNum: NULL if it isn't. (W_CacheLumpNum
// would also set a cached patch's zone tag to PU_CACHE, which matters
// only to what the zone purges, not to what is drawn.)
static inline const byte* R_GetCachedColumn (int tex, int col)
{
    int		lump;
    int		ofs;

    col &= texturewidthmask[tex];
    lump = texturecolumnlump[tex][col];
    ofs = texturecolumnofs[tex][col];

    if (lump > 0)
    {
	byte *cache = lumpinfo[lump]->cache;

	return cache ? cache + ofs : NULL;
    }

    return texturecomposite[tex] ? texturecomposite[tex] + ofs : NULL;
}

// The seg's tiers t to end as columns, then drawn. A column points into a
// cached lump, which fetching one that isn't cached can purge
// (W_CacheLumpNum, Z_Malloc): the columns set up before such a fetch are
// drawn first.
static inline __attribute__((always_inline)) void
R_DrawSegTiers (const segtier_t* t, const segtier_t* end, const int low)
{
    colrec_t*	c = segcols;
    const byte*	source;
    // the last tier's texture's tables (a seg has at most three textures)
    int			tex = -1;
    int			mask = 0;
    const short*	collump = NULL;
    const unsigned short* colofs = NULL;
    const byte*		composite = NULL;
    int			col;
    int			lump;

    for ( ; t < end; t++)
    {
	if (t->tex != tex)
	{
	    tex = t->tex;
	    mask = texturewidthmask[tex];
	    collump = texturecolumnlump[tex];
	    colofs = texturecolumnofs[tex];
	}
	// R_GetCachedColumn, with them
	col = t->texturecolumn & mask;
	lump = collump[col];
	if (lump > 0)
	{
	    byte *cache = lumpinfo[lump]->cache;

	    source = cache ? cache + colofs[col] : NULL;
	}
	else
	{
	    composite = texturecomposite[tex];
	    source = composite ? composite + colofs[col] : NULL;
	}
	if (!source)
	{
	    if (low)
		R_DrawColumnsLow (segcols, c);
	    else
		R_DrawColumns (segcols, c);
	    c = segcols;
	    source = R_GetColumn (t->tex, t->texturecolumn);
	}
	R_SetupColumn (c++, t->x, t->yl, t->yh, source, t->colormap,
		       t->iscale, t->texturemid, low);
    }
    if (low)
	R_DrawColumnsLow (segcols, c);
    else
	R_DrawColumns (segcols, c);
}

#define SEG_NOTE(tier_yl, tier_yh, tier_tex, tier_mid) \
    { \
	t->x = x; \
	t->yl = tier_yl; \
	t->yh = tier_yh; \
	t->tex = tier_tex; \
	t->texturemid = tier_mid; \
	t++; \
    }

// A column's clipping and plane marks, the same for both kinds of line
#define SEG_MARK \
	cc = ceilingclip[x]; \
	fc = floorclip[x]; \
 \
	/* mark floor / ceiling areas */ \
	yl = (tfrac+HEIGHTUNIT-1)>>HEIGHTBITS; \
 \
	/* no space above wall? */ \
	if (yl < cc+1) \
	    yl = cc+1; \
 \
	if (markc) \
	{ \
	    top = cc+1; \
	    bottom = yl-1; \
 \
	    if (bottom >= fc) \
		bottom = fc-1; \
 \
	    if (top <= bottom) \
	    { \
		ctop[x] = top; \
		cbottom[x] = bottom; \
	    } \
	} \
 \
	yh = bfrac>>HEIGHTBITS; \
 \
	if (yh >= fc) \
	    yh = fc-1; \
 \
	if (markf) \
	{ \
	    top = yh+1; \
	    bottom = fc-1; \
	    if (top <= cc) \
		top = cc+1; \
	    if (top <= bottom) \
	    { \
		ftop[x] = top; \
		fbottom[x] = bottom; \
	    } \
	}

#define SEG_MARK_LOCALS \
    int			x = rw_x; \
    const int		stopx = rw_stopx; \
    fixed_t		tfrac = topfrac; \
    fixed_t		bfrac = bottomfrac; \
    const fixed_t	tstep = topstep; \
    const fixed_t	bstep = bottomstep; \
    const int		markc = markceiling; \
    const int		markf = markfloor; \
    byte* const		ctop = markc ? ceilingplane->top : NULL; \
    byte* const		cbottom = markc ? ceilingplane->bottom : NULL; \
    byte* const		ftop = markf ? floorplane->top : NULL; \
    byte* const		fbottom = markf ? floorplane->bottom : NULL; \
    int			cc; \
    int			fc; \
    int			yl; \
    int			yh; \
    int			top; \
    int			bottom

// A single sided line's columns: its tiers from t on. Returns their end.
static __attribute__((noinline)) segtier_t* R_SegMarkSolid (segtier_t* t)
{
    SEG_MARK_LOCALS;
    const short		tex = midtexture;
    const fixed_t	texmid = rw_midtexturemid;
    const short		vh = viewheight;

    for ( ; x < stopx ; x++)
    {
	SEG_MARK

	if (yl <= yh)
	    SEG_NOTE (yl, yh, tex, texmid);
	ceilingclip[x] = vh;
	floorclip[x] = -1;

	tfrac += tstep;
	bfrac += bstep;
    }
    topfrac = tfrac;
    bottomfrac = bfrac;
    return t;
}

// A two sided line's
static __attribute__((noinline)) segtier_t* R_SegMarkTwoSided (segtier_t* t)
{
    SEG_MARK_LOCALS;
    int			mid;
    const short		toptex = toptexture;
    const short		bottomtex = bottomtexture;
    const fixed_t	topmid = rw_toptexturemid;
    const fixed_t	bottommid = rw_bottomtexturemid;
    fixed_t		phigh = pixhigh;
    fixed_t		plow = pixlow;
    const fixed_t	phstep = pixhighstep;
    const fixed_t	plstep = pixlowstep;

    for ( ; x < stopx ; x++)
    {
	SEG_MARK

	if (toptex)
	{
	    // top wall
	    mid = phigh>>HEIGHTBITS;
	    phigh += phstep;

	    if (mid >= fc)
		mid = fc-1;

	    if (mid >= yl)
	    {
		SEG_NOTE (yl, mid, toptex, topmid);
		cc = mid;
	    }
	    else
		cc = yl-1;
	    ceilingclip[x] = cc;
	}
	else
	{
	    // no top wall
	    if (markc)
	    {
		cc = yl-1;
		ceilingclip[x] = cc;
	    }
	}

	if (bottomtex)
	{
	    // bottom wall
	    mid = (plow+HEIGHTUNIT-1)>>HEIGHTBITS;
	    plow += plstep;

	    // no space above wall?
	    if (mid <= cc)
		mid = cc+1;

	    if (mid <= yh)
	    {
		SEG_NOTE (mid, yh, bottomtex, bottommid);
		floorclip[x] = mid;
	    }
	    else
		floorclip[x] = yh+1;
	}
	else
	{
	    // no bottom wall
	    if (markf)
		floorclip[x] = yh+1;
	}

	tfrac += tstep;
	bfrac += bstep;
    }
    topfrac = tfrac;
    bottomfrac = bfrac;
    pixhigh = phigh;
    pixlow = plow;
    return t;
}

// A column's place across the wall's texture
#define SEG_TEXTURECOLUMN(x) \
    ((offset - FixedMul (finetangent[(centerangle + xtoviewangle[x]) \
				     >>ANGLETOFINESHIFT], distance)) \
     >> FRACBITS)

// The texture column, light and scale of the tiers t to end, a seg's from
// column x0, where its scale is scale0: worked out once a column.
static __attribute__((noinline)) void
R_SegTierColumns (segtier_t* t, const segtier_t* end, int x0, fixed_t scale0)
{
    const fixed_t	step = rw_scalestep;
    const angle_t	centerangle = rw_centerangle;
    const fixed_t	offset = rw_offset;
    const fixed_t	distance = rw_distance;
    lighttable_t** const lights = walllights;
    int			x = x0 - 1;
    int			d;
    unsigned		scale = scale0 - step;	// column x's
    unsigned		index;
    fixed_t		texturecolumn = 0;
    lighttable_t*	colormap = NULL;
    fixed_t		iscale = 0;

    for ( ; t < end ; t++)
    {
	d = t->x - x;
	if (d)
	{
	    // on to this tier's column: usually the next
	    x += d;
	    if (d == 1)
		scale += step;
	    else
		scale += (unsigned)d * (unsigned)step;
	    // calculate texture offset
	    texturecolumn = SEG_TEXTURECOLUMN (x);
	    // calculate lighting
	    index = (fixed_t)scale>>LIGHTSCALESHIFT;
	    if (index >= MAXLIGHTSCALE)
		index = MAXLIGHTSCALE-1;
	    colormap = lights[index];
	    iscale = 0xffffffffu / scale;
	}
	t->texturecolumn = texturecolumn;
	t->colormap = colormap;
	t->iscale = iscale;
    }
}

// save texturecol
//  for backdrawing of masked mid texture
static __attribute__((noinline)) void R_SegMaskedColumns (int x, int stopx)
{
    const angle_t	centerangle = rw_centerangle;
    const fixed_t	offset = rw_offset;
    const fixed_t	distance = rw_distance;
    short* const	col = maskedtexturecol;

    for ( ; x < stopx ; x++)
	col[x] = SEG_TEXTURECOLUMN (x);
}

static inline __attribute__((always_inline)) void R_RenderSegLoopAt (const int low)
{
    const int		x0 = rw_x;
    const int		stopx = rw_stopx;
    const fixed_t	scale0 = rw_scale;
    segtier_t*		t;

    if (x0 < stopx)
    {
	if (midtexture)
	    t = R_SegMarkSolid (segtiers);
	else
	{
	    t = R_SegMarkTwoSided (segtiers);
	    if (maskedtexture)
		R_SegMaskedColumns (x0, stopx);
	}
	if (t != segtiers)
	    R_SegTierColumns (segtiers, t, x0, scale0);
	rw_x = stopx;
	rw_scale = (unsigned)scale0
	    + (unsigned)(stopx - x0) * (unsigned)rw_scalestep;
	R_DrawSegTiers (segtiers, t, low);
    }
}

void R_RenderSegLoop (void)
{
    if (detailshift)
	R_RenderSegLoopAt (1);
    else
	R_RenderSegLoopAt (0);
}

#else

void R_RenderSegLoop (void)
{
    angle_t		angle;
    unsigned		index;
    int			yl;
    int			yh;
    int			mid;
    fixed_t		texturecolumn;
    int			top;
    int			bottom;

    for ( ; rw_x < rw_stopx ; rw_x++)
    {
	// mark floor / ceiling areas
	yl = (topfrac+HEIGHTUNIT-1)>>HEIGHTBITS;

	// no space above wall?
	if (yl < ceilingclip[rw_x]+1)
	    yl = ceilingclip[rw_x]+1;
	
	if (markceiling)
	{
	    top = ceilingclip[rw_x]+1;
	    bottom = yl-1;

	    if (bottom >= floorclip[rw_x])
		bottom = floorclip[rw_x]-1;

	    if (top <= bottom)
	    {
		ceilingplane->top[rw_x] = top;
		ceilingplane->bottom[rw_x] = bottom;
	    }
	}
		
	yh = bottomfrac>>HEIGHTBITS;

	if (yh >= floorclip[rw_x])
	    yh = floorclip[rw_x]-1;

	if (markfloor)
	{
	    top = yh+1;
	    bottom = floorclip[rw_x]-1;
	    if (top <= ceilingclip[rw_x])
		top = ceilingclip[rw_x]+1;
	    if (top <= bottom)
	    {
		floorplane->top[rw_x] = top;
		floorplane->bottom[rw_x] = bottom;
	    }
	}
	
	// texturecolumn and lighting are independent of wall tiers
	if (segtextured)
	{
	    // calculate texture offset
	    angle = (rw_centerangle + xtoviewangle[rw_x])>>ANGLETOFINESHIFT;
	    texturecolumn = rw_offset-FixedMul(finetangent[angle],rw_distance);
	    texturecolumn >>= FRACBITS;
	    // calculate lighting
	    index = rw_scale>>LIGHTSCALESHIFT;

	    if (index >=  MAXLIGHTSCALE )
		index = MAXLIGHTSCALE-1;

	    dc_colormap = walllights[index];
	    dc_x = rw_x;
	    dc_iscale = 0xffffffffu / (unsigned)rw_scale;
	}
        else
        {
            // purely to shut up the compiler

            texturecolumn = 0;
        }
	
	// draw the wall tiers
	if (midtexture)
	{
	    // single sided line
	    dc_yl = yl;
	    dc_yh = yh;
	    dc_texturemid = rw_midtexturemid;
	    dc_source = R_GetColumn(midtexture,texturecolumn);
	    colfunc ();
	    ceilingclip[rw_x] = viewheight;
	    floorclip[rw_x] = -1;
	}
	else
	{
	    // two sided line
	    if (toptexture)
	    {
		// top wall
		mid = pixhigh>>HEIGHTBITS;
		pixhigh += pixhighstep;

		if (mid >= floorclip[rw_x])
		    mid = floorclip[rw_x]-1;

		if (mid >= yl)
		{
		    dc_yl = yl;
		    dc_yh = mid;
		    dc_texturemid = rw_toptexturemid;
		    dc_source = R_GetColumn(toptexture,texturecolumn);
		    colfunc ();
		    ceilingclip[rw_x] = mid;
		}
		else
		    ceilingclip[rw_x] = yl-1;
	    }
	    else
	    {
		// no top wall
		if (markceiling)
		    ceilingclip[rw_x] = yl-1;
	    }
			
	    if (bottomtexture)
	    {
		// bottom wall
		mid = (pixlow+HEIGHTUNIT-1)>>HEIGHTBITS;
		pixlow += pixlowstep;

		// no space above wall?
		if (mid <= ceilingclip[rw_x])
		    mid = ceilingclip[rw_x]+1;
		
		if (mid <= yh)
		{
		    dc_yl = mid;
		    dc_yh = yh;
		    dc_texturemid = rw_bottomtexturemid;
		    dc_source = R_GetColumn(bottomtexture,
					    texturecolumn);
		    colfunc ();
		    floorclip[rw_x] = mid;
		}
		else
		    floorclip[rw_x] = yh+1;
	    }
	    else
	    {
		// no bottom wall
		if (markfloor)
		    floorclip[rw_x] = yh+1;
	    }
			
	    if (maskedtexture)
	    {
		// save texturecol
		//  for backdrawing of masked mid texture
		maskedtexturecol[rw_x] = texturecolumn;
	    }
	}
		
	rw_scale += rw_scalestep;
	topfrac += topstep;
	bottomfrac += bottomstep;
    }
}
#endif // R_DRAW_M68K




//
// R_StoreWallRange
// A wall segment will be drawn
//  between start and stop pixels (inclusive).
//
void
R_StoreWallRange
( int	start,
  int	stop )
{
    fixed_t		hyp;
    fixed_t		sineval;
    angle_t		distangle, offsetangle;
    fixed_t		vtop;
    int			lightnum;

    // don't overflow and crash
    if (ds_p == &drawsegs[MAXDRAWSEGS])
	return;		
		
#ifdef RANGECHECK
    if (start >=viewwidth || start > stop)
	I_Error ("Bad R_RenderWallRange: %i to %i", start , stop);
#endif
    
    sidedef = curline->sidedef;
    linedef = curline->linedef;

    // mark the segment as visible for auto map
    linedef->flags |= ML_MAPPED;
    
    // calculate rw_distance for scale calculation
    rw_normalangle = curline->angle + ANG90;
    offsetangle = abs((int)rw_normalangle-(int)rw_angle1);
    
    if (offsetangle > ANG90)
	offsetangle = ANG90;

    distangle = ANG90 - offsetangle;
    hyp = R_PointToDist (curline->v1->x, curline->v1->y);
    sineval = finesine[distangle>>ANGLETOFINESHIFT];
    rw_distance = FixedMul (hyp, sineval);
		
	
    ds_p->x1 = rw_x = start;
    ds_p->x2 = stop;
    ds_p->curline = curline;
    rw_stopx = stop+1;
    
    // calculate scale at both ends and step
    ds_p->scale1 = rw_scale = 
	R_ScaleFromGlobalAngle (viewangle + xtoviewangle[start]);
    
    if (stop > start )
    {
	ds_p->scale2 = R_ScaleFromGlobalAngle (viewangle + xtoviewangle[stop]);
	ds_p->scalestep = rw_scalestep = 
	    (ds_p->scale2 - rw_scale) / (stop-start);
    }
    else
    {
	// UNUSED: try to fix the stretched line bug
#if 0
	if (rw_distance < FRACUNIT/2)
	{
	    fixed_t		trx,try;
	    fixed_t		gxt,gyt;

	    trx = curline->v1->x - viewx;
	    try = curline->v1->y - viewy;
			
	    gxt = FixedMul(trx,viewcos); 
	    gyt = -FixedMul(try,viewsin); 
	    ds_p->scale1 = FixedDiv(projection, gxt-gyt)<<detailshift;
	}
#endif
	ds_p->scale2 = ds_p->scale1;
    }
    
    // calculate texture boundaries
    //  and decide if floor / ceiling marks are needed
    worldtop = frontsector->ceilingheight - viewz;
    worldbottom = frontsector->floorheight - viewz;
	
    midtexture = toptexture = bottomtexture = maskedtexture = 0;
    ds_p->maskedtexturecol = NULL;
	
    if (!backsector)
    {
	// single sided line
	midtexture = texturetranslation[sidedef->midtexture];
	// a single sided line is terminal, so it must mark ends
	markfloor = markceiling = true;
	if (linedef->flags & ML_DONTPEGBOTTOM)
	{
	    vtop = frontsector->floorheight +
		textureheight[sidedef->midtexture];
	    // bottom of texture at bottom
	    rw_midtexturemid = vtop - viewz;	
	}
	else
	{
	    // top of texture at top
	    rw_midtexturemid = worldtop;
	}
	rw_midtexturemid += sidedef->rowoffset;

	ds_p->silhouette = SIL_BOTH;
	ds_p->sprtopclip = screenheightarray;
	ds_p->sprbottomclip = negonearray;
	ds_p->bsilheight = INT_MAX;
	ds_p->tsilheight = INT_MIN;
    }
    else
    {
	// two sided line
	ds_p->sprtopclip = ds_p->sprbottomclip = NULL;
	ds_p->silhouette = 0;
	
	if (frontsector->floorheight > backsector->floorheight)
	{
	    ds_p->silhouette = SIL_BOTTOM;
	    ds_p->bsilheight = frontsector->floorheight;
	}
	else if (backsector->floorheight > viewz)
	{
	    ds_p->silhouette = SIL_BOTTOM;
	    ds_p->bsilheight = INT_MAX;
	    // ds_p->sprbottomclip = negonearray;
	}
	
	if (frontsector->ceilingheight < backsector->ceilingheight)
	{
	    ds_p->silhouette |= SIL_TOP;
	    ds_p->tsilheight = frontsector->ceilingheight;
	}
	else if (backsector->ceilingheight < viewz)
	{
	    ds_p->silhouette |= SIL_TOP;
	    ds_p->tsilheight = INT_MIN;
	    // ds_p->sprtopclip = screenheightarray;
	}
		
	if (backsector->ceilingheight <= frontsector->floorheight)
	{
	    ds_p->sprbottomclip = negonearray;
	    ds_p->bsilheight = INT_MAX;
	    ds_p->silhouette |= SIL_BOTTOM;
	}
	
	if (backsector->floorheight >= frontsector->ceilingheight)
	{
	    ds_p->sprtopclip = screenheightarray;
	    ds_p->tsilheight = INT_MIN;
	    ds_p->silhouette |= SIL_TOP;
	}
	
	worldhigh = backsector->ceilingheight - viewz;
	worldlow = backsector->floorheight - viewz;
		
	// hack to allow height changes in outdoor areas
	if (frontsector->ceilingpic == skyflatnum 
	    && backsector->ceilingpic == skyflatnum)
	{
	    worldtop = worldhigh;
	}
	
			
	if (worldlow != worldbottom 
	    || backsector->floorpic != frontsector->floorpic
	    || backsector->lightlevel != frontsector->lightlevel)
	{
	    markfloor = true;
	}
	else
	{
	    // same plane on both sides
	    markfloor = false;
	}
	
			
	if (worldhigh != worldtop 
	    || backsector->ceilingpic != frontsector->ceilingpic
	    || backsector->lightlevel != frontsector->lightlevel)
	{
	    markceiling = true;
	}
	else
	{
	    // same plane on both sides
	    markceiling = false;
	}
	
	if (backsector->ceilingheight <= frontsector->floorheight
	    || backsector->floorheight >= frontsector->ceilingheight)
	{
	    // closed door
	    markceiling = markfloor = true;
	}
	

	if (worldhigh < worldtop)
	{
	    // top texture
	    toptexture = texturetranslation[sidedef->toptexture];
	    if (linedef->flags & ML_DONTPEGTOP)
	    {
		// top of texture at top
		rw_toptexturemid = worldtop;
	    }
	    else
	    {
		vtop =
		    backsector->ceilingheight
		    + textureheight[sidedef->toptexture];
		
		// bottom of texture
		rw_toptexturemid = vtop - viewz;	
	    }
	}
	if (worldlow > worldbottom)
	{
	    // bottom texture
	    bottomtexture = texturetranslation[sidedef->bottomtexture];

	    if (linedef->flags & ML_DONTPEGBOTTOM )
	    {
		// bottom of texture at bottom
		// top of texture at top
		rw_bottomtexturemid = worldtop;
	    }
	    else	// top of texture at top
		rw_bottomtexturemid = worldlow;
	}
	rw_toptexturemid += sidedef->rowoffset;
	rw_bottomtexturemid += sidedef->rowoffset;
	
	// allocate space for masked texture tables
	if (sidedef->midtexture)
	{
	    // masked midtexture
	    maskedtexture = true;
	    ds_p->maskedtexturecol = maskedtexturecol = lastopening - rw_x;
	    lastopening += rw_stopx - rw_x;
	}
    }
    
    // calculate rw_offset (only needed for textured lines)
    segtextured = midtexture | toptexture | bottomtexture | maskedtexture;

    if (segtextured)
    {
	offsetangle = rw_normalangle-rw_angle1;
	
	if (offsetangle > ANG180)
	    offsetangle = -offsetangle;

	if (offsetangle > ANG90)
	    offsetangle = ANG90;

	sineval = finesine[offsetangle >>ANGLETOFINESHIFT];
	rw_offset = FixedMul (hyp, sineval);

	if (rw_normalangle-rw_angle1 < ANG180)
	    rw_offset = -rw_offset;

	rw_offset += sidedef->textureoffset + curline->offset;
	rw_centerangle = ANG90 + viewangle - rw_normalangle;
	
	// calculate light table
	//  use different light tables
	//  for horizontal / vertical / diagonal
	// OPTIMIZE: get rid of LIGHTSEGSHIFT globally
	if (!fixedcolormap)
	{
	    lightnum = (frontsector->lightlevel >> LIGHTSEGSHIFT)+extralight;

	    if (curline->v1->y == curline->v2->y)
		lightnum--;
	    else if (curline->v1->x == curline->v2->x)
		lightnum++;

	    if (lightnum < 0)		
		walllights = scalelight[0];
	    else if (lightnum >= LIGHTLEVELS)
		walllights = scalelight[LIGHTLEVELS-1];
	    else
		walllights = scalelight[lightnum];
	}
    }
    
    // if a floor / ceiling plane is on the wrong side
    //  of the view plane, it is definitely invisible
    //  and doesn't need to be marked.
    
  
    if (frontsector->floorheight >= viewz)
    {
	// above view plane
	markfloor = false;
    }
    
    if (frontsector->ceilingheight <= viewz 
	&& frontsector->ceilingpic != skyflatnum)
    {
	// below view plane
	markceiling = false;
    }

    
    // calculate incremental stepping values for texture edges
    worldtop >>= 4;
    worldbottom >>= 4;
	
    topstep = -FixedMul (rw_scalestep, worldtop);
    topfrac = (centeryfrac>>4) - FixedMul (worldtop, rw_scale);

    bottomstep = -FixedMul (rw_scalestep,worldbottom);
    bottomfrac = (centeryfrac>>4) - FixedMul (worldbottom, rw_scale);
	
    if (backsector)
    {	
	worldhigh >>= 4;
	worldlow >>= 4;

	if (worldhigh < worldtop)
	{
	    pixhigh = (centeryfrac>>4) - FixedMul (worldhigh, rw_scale);
	    pixhighstep = -FixedMul (rw_scalestep,worldhigh);
	}
	
	if (worldlow > worldbottom)
	{
	    pixlow = (centeryfrac>>4) - FixedMul (worldlow, rw_scale);
	    pixlowstep = -FixedMul (rw_scalestep,worldlow);
	}
    }
    
    // render it
    if (markceiling)
	ceilingplane = R_CheckPlane (ceilingplane, rw_x, rw_stopx-1);
    
    if (markfloor)
	floorplane = R_CheckPlane (floorplane, rw_x, rw_stopx-1);

    R_RenderSegLoop ();

    
    // save sprite clipping info
    if ( ((ds_p->silhouette & SIL_TOP) || maskedtexture)
	 && !ds_p->sprtopclip)
    {
	memcpy (lastopening, ceilingclip+start, sizeof(*lastopening)*(rw_stopx-start));
	ds_p->sprtopclip = lastopening - start;
	lastopening += rw_stopx - start;
    }
    
    if ( ((ds_p->silhouette & SIL_BOTTOM) || maskedtexture)
	 && !ds_p->sprbottomclip)
    {
	memcpy (lastopening, floorclip+start, sizeof(*lastopening)*(rw_stopx-start));
	ds_p->sprbottomclip = lastopening - start;
	lastopening += rw_stopx - start;	
    }

    if (maskedtexture && !(ds_p->silhouette&SIL_TOP))
    {
	ds_p->silhouette |= SIL_TOP;
	ds_p->tsilheight = INT_MIN;
    }
    if (maskedtexture && !(ds_p->silhouette&SIL_BOTTOM))
    {
	ds_p->silhouette |= SIL_BOTTOM;
	ds_p->bsilheight = INT_MAX;
    }
    ds_p++;
}

