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
//	LineOfSight/Visibility checks, uses REJECT Lookup Table.
//


#include "doomdef.h"
#include "doomstat.h"

#include "i_system.h"
#include "z_zone.h"
#include "p_local.h"

// State.
#include "r_state.h"

//
// P_CheckSight
//
fixed_t		sightzstart;		// eye z of looker
fixed_t		topslope;
fixed_t		bottomslope;		// slopes to top and bottom of target

divline_t	strace;			// from t1 to t2
fixed_t		t2x;
fixed_t		t2y;

int		sightcounts[2];


// PTR_SightTraverse() for Doom 1.2 sight calculations
// taken from prboom-plus/src/p_sight.c:69-102
boolean PTR_SightTraverse(intercept_t *in)
{
    line_t *li;
    fixed_t slope;

    li = in->d.line;

    //
    // crosses a two sided line
    //
    P_LineOpening(li);

    if (openbottom >= opentop) // quick test for totally closed doors
        return false;          // stop

    if (li->frontsector->floorheight != li->backsector->floorheight)
    {
        slope = FixedDiv(openbottom - sightzstart, in->frac);
        if (slope > bottomslope)
            bottomslope = slope;
    }

    if (li->frontsector->ceilingheight != li->backsector->ceilingheight)
    {
        slope = FixedDiv(opentop - sightzstart, in->frac);
        if (slope < topslope)
            topslope = slope;
    }

    if (topslope <= bottomslope)
        return false; // stop

    return true; // keep going
}


//
// P_DivlineSide
// Returns side 0 (front), 1 (back), or 2 (on).
//
#ifdef ATARI
// Inline: the sight check's BSP walk calls it at every node and twice for
// every line it meets (after TTDOOM)
static inline
#endif
int
P_DivlineSide
( fixed_t	x,
  fixed_t	y,
  divline_t*	node )
{
    fixed_t	dx;
    fixed_t	dy;
    fixed_t	left;
    fixed_t	right;

    if (!node->dx)
    {
	if (x==node->x)
	    return 2;
	
	if (x <= node->x)
	    return node->dy > 0;

	return node->dy < 0;
    }
    
    if (!node->dy)
    {
	if (x==node->y)
	    return 2;

	if (y <= node->y)
	    return node->dx < 0;

	return node->dx > 0;
    }
	
    dx = (x - node->x);
    dy = (y - node->y);

    left =  (node->dy>>FRACBITS) * (dx>>FRACBITS);
    right = (dy>>FRACBITS) * (node->dx>>FRACBITS);
	
    if (right < left)
	return 0;	// front side
    
    if (left == right)
	return 2;
    return 1;		// back side
}


//
// P_InterceptVector2
// Returns the fractional intercept point
// along the first divline.
// This is only called by the addthings and addlines traversers.
//
fixed_t
P_InterceptVector2
( divline_t*	v2,
  divline_t*	v1 )
{
    fixed_t	frac;
    fixed_t	num;
    fixed_t	den;
	
    den = FixedMul (v1->dy>>8,v2->dx) - FixedMul(v1->dx>>8,v2->dy);

    if (den == 0)
	return 0;
    //	I_Error ("P_InterceptVector: parallel");
    
    num = FixedMul ( (v1->x - v2->x)>>8 ,v1->dy) + 
	FixedMul ( (v2->y - v1->y)>>8 , v1->dx);
    frac = FixedDiv (num , den);

    return frac;
}

#ifdef ATARI
//
// Compact records for the sight check (after TTDOOM), made at each
// level's load beside id's own: a node's line in whole map units and its
// children, 12 bytes where a node_t is 52, and the line of each seg, its
// ends in map units and the line itself, where id's loop read a seg, its
// line and two vertices. A 68030's data cache holds 16 lines of 16
// bytes. Map coordinates are whole units, so the side tests below give
// what P_DivlineSide gives on id's values, with its arithmetic.
//
// Laid out as a node (its line's first end and direction), so one side
// test serves both in the 68020 trace below
typedef struct
{
    short		v1x, v1y, dx, dy;
    short		v2x, v2y;
    int*		valid;		// the line's validcount
} sightseg_t;

typedef struct
{
    const sightseg_t*	start;
    const sightseg_t*	end;
} sightsub_t;

sightnode_t*		sightnodes;	// p_local.h
static sightseg_t*	sightsegs;
static sightsub_t*	sightsubs;

// The map's extent, vertices and node lines, in map units: within it, a
// difference of two points fits in 16 bits if the extent is under 32767
static int		sight_minx, sight_maxx, sight_miny, sight_maxy;
static boolean		sight_short;

static void SightExtent (fixed_t x, fixed_t y)
{
    x >>= FRACBITS;
    y >>= FRACBITS;
    if (x < sight_minx) sight_minx = x;
    if (x > sight_maxx) sight_maxx = x;
    if (y < sight_miny) sight_miny = y;
    if (y > sight_maxy) sight_maxy = y;
}

void P_InitSightRecords (void)
{
    int			i;

    sightnodes = Z_Malloc(numnodes * sizeof(*sightnodes), PU_LEVEL, NULL);
    for (i = 0; i < numnodes; ++i)
    {
	sightnodes[i].x = nodes[i].x >> FRACBITS;
	sightnodes[i].y = nodes[i].y >> FRACBITS;
	sightnodes[i].dx = nodes[i].dx >> FRACBITS;
	sightnodes[i].dy = nodes[i].dy >> FRACBITS;
	sightnodes[i].children[0] = nodes[i].children[0];
	sightnodes[i].children[1] = nodes[i].children[1];
    }

    sightsegs = Z_Malloc(numsegs * sizeof(*sightsegs), PU_LEVEL, NULL);
    for (i = 0; i < numsegs; ++i)
    {
	line_t *line = segs[i].linedef;

	sightsegs[i].v1x = line->v1->x >> FRACBITS;
	sightsegs[i].v1y = line->v1->y >> FRACBITS;
	sightsegs[i].dx = (line->v2->x - line->v1->x) >> FRACBITS;
	sightsegs[i].dy = (line->v2->y - line->v1->y) >> FRACBITS;
	sightsegs[i].v2x = line->v2->x >> FRACBITS;
	sightsegs[i].v2y = line->v2->y >> FRACBITS;
	sightsegs[i].valid = &line->validcount;
    }

    sightsubs = Z_Malloc(numsubsectors * sizeof(*sightsubs), PU_LEVEL, NULL);
    for (i = 0; i < numsubsectors; ++i)
    {
	sightsubs[i].start = &sightsegs[subsectors[i].firstline];
	sightsubs[i].end = sightsubs[i].start + subsectors[i].numlines;
    }

    sight_minx = sight_miny = 0x7fffffff;
    sight_maxx = sight_maxy = -0x7fffffff;
    for (i = 0; i < numvertexes; ++i)
    {
	SightExtent (vertexes[i].x, vertexes[i].y);
    }
    for (i = 0; i < numnodes; ++i)
    {
	SightExtent (nodes[i].x, nodes[i].y);
	SightExtent (nodes[i].x + nodes[i].dx, nodes[i].y + nodes[i].dy);
    }
    sight_short = sight_maxx - sight_minx < 32767
               && sight_maxy - sight_miny < 32767;
}

// P_DivlineSide for a node, from its record
static inline int
P_NodeSide
( fixed_t	x,
  fixed_t	y,
  const sightnode_t*	node )
{
    fixed_t	dx;
    fixed_t	dy;
    fixed_t	left;
    fixed_t	right;

    if (!node->dx)
    {
	if (x==((fixed_t)node->x << FRACBITS))
	    return 2;

	if (x <= ((fixed_t)node->x << FRACBITS))
	    return node->dy > 0;

	return node->dy < 0;
    }

    if (!node->dy)
    {
	// (id's slip, kept: x against the line's y)
	if (x==((fixed_t)node->y << FRACBITS))
	    return 2;

	if (y <= ((fixed_t)node->y << FRACBITS))
	    return node->dx < 0;

	return node->dx > 0;
    }

    dx = (x - ((fixed_t)node->x << FRACBITS));
    dy = (y - ((fixed_t)node->y << FRACBITS));

    left =  node->dy * (dx>>FRACBITS);
    right = (dy>>FRACBITS) * node->dx;

    if (right < left)
	return 0;	// front side

    if (left == right)
	return 2;
    return 1;		// back side
}

// The rest of id's P_CrossSubsector loop for a line whose ends are on
// both sides of the sight line: false stops the check.
static boolean P_SightCrossedLine (seg_t* seg, divline_t* divl);

static boolean P_SightCrossLine (seg_t* seg)
{
    line_t*		line;
    int			s1;
    int			s2;
    divline_t		divl;
    vertex_t*		v1;
    vertex_t*		v2;

    line = seg->linedef;
    v1 = line->v1;
    v2 = line->v2;

    divl.x = v1->x;
    divl.y = v1->y;
    divl.dx = v2->x - v1->x;
    divl.dy = v2->y - v1->y;
    s1 = P_DivlineSide (strace.x, strace.y, &divl);
    s2 = P_DivlineSide (t2x, t2y, &divl);

    // line isn't crossed?
    if (s1 == s2)
	return true;

    return P_SightCrossedLine (seg, &divl);
}

// The sight line crosses seg's line, divl: the rest of id's loop for it
static boolean P_SightCrossedLine (seg_t* seg, divline_t* divl)
{
    line_t*		line = seg->linedef;
    sector_t*		front;
    sector_t*		back;
    fixed_t		opentop;
    fixed_t		openbottom;
    fixed_t		frac;
    fixed_t		slope;

    // Backsector may be NULL if this is an "impassible
    // glass" hack line.

    if (line->backsector == NULL)
    {
	return false;
    }

    // stop because it is not two sided anyway
    // might do this after updating validcount?
    if ( !(line->flags & ML_TWOSIDED) )
	return false;

    // crosses a two sided line
    front = seg->frontsector;
    back = seg->backsector;

    // no wall to block sight with?
    if (front->floorheight == back->floorheight
	&& front->ceilingheight == back->ceilingheight)
	return true;

    // possible occluder
    // because of ceiling height differences
    if (front->ceilingheight < back->ceilingheight)
	opentop = front->ceilingheight;
    else
	opentop = back->ceilingheight;

    // because of ceiling height differences
    if (front->floorheight > back->floorheight)
	openbottom = front->floorheight;
    else
	openbottom = back->floorheight;

    // quick test for totally closed doors
    if (openbottom >= opentop)
	return false;		// stop

    frac = P_InterceptVector2 (&strace, divl);

    if (front->floorheight != back->floorheight)
    {
	slope = FixedDiv (openbottom - sightzstart , frac);
	if (slope > bottomslope)
	    bottomslope = slope;
    }

    if (front->ceilingheight != back->ceilingheight)
    {
	slope = FixedDiv (opentop - sightzstart , frac);
	if (slope < topslope)
	    topslope = slope;
    }

    if (topslope <= bottomslope)
	return false;		// stop

    return true;
}

static boolean P_CrossSubsectorId (int num);

// Most lines a sight check meets it has checked already, or have both
// ends on one side of the sight line: that much from the records, with
// P_DivlineSide's arithmetic for a sight line along neither axis (one
// along x or y takes id's loop, below).
boolean P_CrossSubsector (int num)
{
    const sightseg_t*	ss;
    seg_t*		seg;
    line_t*		line;
    int			count;
    fixed_t		sdx, sdy, left, right;
    int			s1, s2;

    if (!strace.dx || !strace.dy)
	return P_CrossSubsectorId (num);

#ifdef RANGECHECK
    if (num>=numsubsectors)
	I_Error ("P_CrossSubsector: ss %i with numss = %i",
		 num,
		 numsubsectors);
#endif

    count = subsectors[num].numlines;
    seg = &segs[subsectors[num].firstline];
    ss = &sightsegs[subsectors[num].firstline];
    sdx = strace.dx >> FRACBITS;
    sdy = strace.dy >> FRACBITS;

    for ( ; count ; seg++, ss++, count--)
    {
	// allready checked other side?
	if (*ss->valid == validcount)
	    continue;

	*ss->valid = validcount;

	left = sdy * ((((fixed_t)ss->v1x << FRACBITS) - strace.x) >> FRACBITS);
	right = ((((fixed_t)ss->v1y << FRACBITS) - strace.y) >> FRACBITS) * sdx;
	s1 = right < left ? 0 : left == right ? 2 : 1;
	left = sdy * ((((fixed_t)ss->v2x << FRACBITS) - strace.x) >> FRACBITS);
	right = ((((fixed_t)ss->v2y << FRACBITS) - strace.y) >> FRACBITS) * sdx;
	s2 = right < left ? 0 : left == right ? 2 : 1;

	// line isn't crossed?
	if (s1 == s2)
	    continue;

	if (!P_SightCrossLine (seg))
	    return false;
    }

    // passed the subsector ok
    return true;
}

#define P_CrossSubsector P_CrossSubsectorId
static
#endif

//
// P_CrossSubsector
// Returns true
//  if strace crosses the given subsector successfully.
//
boolean P_CrossSubsector (int num)
{
    seg_t*		seg;
    line_t*		line;
    int			s1;
    int			s2;
    int			count;
    subsector_t*	sub;
    sector_t*		front;
    sector_t*		back;
    fixed_t		opentop;
    fixed_t		openbottom;
    divline_t		divl;
    vertex_t*		v1;
    vertex_t*		v2;
    fixed_t		frac;
    fixed_t		slope;
	
#ifdef RANGECHECK
    if (num>=numsubsectors)
	I_Error ("P_CrossSubsector: ss %i with numss = %i",
		 num,
		 numsubsectors);
#endif

    sub = &subsectors[num];
    
    // check lines
    count = sub->numlines;
    seg = &segs[sub->firstline];

    for ( ; count ; seg++, count--)
    {
	line = seg->linedef;

	// allready checked other side?
	if (line->validcount == validcount)
	    continue;
	
	line->validcount = validcount;

	v1 = line->v1;
	v2 = line->v2;
	s1 = P_DivlineSide (v1->x,v1->y, &strace);
	s2 = P_DivlineSide (v2->x, v2->y, &strace);

	// line isn't crossed?
	if (s1 == s2)
	    continue;
	
	divl.x = v1->x;
	divl.y = v1->y;
	divl.dx = v2->x - v1->x;
	divl.dy = v2->y - v1->y;
	s1 = P_DivlineSide (strace.x, strace.y, &divl);
	s2 = P_DivlineSide (t2x, t2y, &divl);

	// line isn't crossed?
	if (s1 == s2)
	    continue;	

        // Backsector may be NULL if this is an "impassible
        // glass" hack line.

        if (line->backsector == NULL)
        {
            return false;
        }

	// stop because it is not two sided anyway
	// might do this after updating validcount?
	if ( !(line->flags & ML_TWOSIDED) )
	    return false;
	
	// crosses a two sided line
	front = seg->frontsector;
	back = seg->backsector;

	// no wall to block sight with?
	if (front->floorheight == back->floorheight
	    && front->ceilingheight == back->ceilingheight)
	    continue;	

	// possible occluder
	// because of ceiling height differences
	if (front->ceilingheight < back->ceilingheight)
	    opentop = front->ceilingheight;
	else
	    opentop = back->ceilingheight;

	// because of ceiling height differences
	if (front->floorheight > back->floorheight)
	    openbottom = front->floorheight;
	else
	    openbottom = back->floorheight;
		
	// quick test for totally closed doors
	if (openbottom >= opentop)	
	    return false;		// stop
	
	frac = P_InterceptVector2 (&strace, &divl);
		
	if (front->floorheight != back->floorheight)
	{
	    slope = FixedDiv (openbottom - sightzstart , frac);
	    if (slope > bottomslope)
		bottomslope = slope;
	}
		
	if (front->ceilingheight != back->ceilingheight)
	{
	    slope = FixedDiv (opentop - sightzstart , frac);
	    if (slope < topslope)
		topslope = slope;
	}
		
	if (topslope <= bottomslope)
	    return false;		// stop				
    }
    // passed the subsector ok
    return true;		
}

#ifdef ATARI
#undef P_CrossSubsector
#endif



//
// P_CrossBSPNode
// Returns true
//  if strace crosses the given node successfully.
//
#ifdef ATARI
// The BSP walk as a loop with its own stack of the nodes whose far side is
// still to come, rather than a recursive call per node: the same nodes and
// subsectors, in the same order, as id's recursion (after TTDOOM).
#define SIGHTSTACK	128

boolean P_CrossBSPNode (int bspnum)
{
    const sightnode_t*	stack[SIGHTSTACK];
    char	sides[SIGHTSTACK];
    int		sp = 0;
    const sightnode_t*	bsp;
    int		side;

    for ( ; ; )
    {
	// Down the starting sides to a subsector
	while (!(bspnum & NF_SUBSECTOR))
	{
	    bsp = &sightnodes[bspnum];
	    // decide which side the start point is on
	    side = P_NodeSide (strace.x, strace.y, bsp);
	    if (side == 2)
		side = 0;	// an "on" should cross both sides
	    if (sp == SIGHTSTACK)
		I_Error ("P_CrossBSPNode: BSP deeper than %i", SIGHTSTACK);
	    stack[sp] = bsp;
	    sides[sp++] = side;
	    // cross the starting side
	    bspnum = bsp->children[side];
	}
	if (!P_CrossSubsector (bspnum == -1 ? 0 : bspnum&(~NF_SUBSECTOR)))
	    return false;

	// Crossed: up to a node whose ending side is still to cross
	for ( ; ; )
	{
	    if (!sp)
		return true;
	    bsp = stack[--sp];
	    side = sides[sp];
	    // the partition plane is crossed here
	    if (side != P_NodeSide (t2x, t2y, bsp))
		break;
	    // the line doesn't touch the other side: this node is crossed
	}
	// cross the ending side
	bspnum = bsp->children[side^1];
    }
}
#else
boolean P_CrossBSPNode (int bspnum)
{
    node_t*	bsp;
    int		side;

    if (bspnum & NF_SUBSECTOR)
    {
	if (bspnum == -1)
	    return P_CrossSubsector (0);
	else
	    return P_CrossSubsector (bspnum&(~NF_SUBSECTOR));
    }
		
    bsp = &nodes[bspnum];
    
    // decide which side the start point is on
    side = P_DivlineSide (strace.x, strace.y, (divline_t *)bsp);
    if (side == 2)
	side = 0;	// an "on" should cross both sides

    // cross the starting side
    if (!P_CrossBSPNode (bsp->children[side]) )
	return false;
	
    // the partition plane is crossed here
    if (side == P_DivlineSide (t2x, t2y,(divline_t *)bsp))
    {
	// the line doesn't touch the other side
	return true;
    }
    
    // cross the ending side		
    return P_CrossBSPNode (bsp->children[side^1]);
}
#endif



#if defined(ATARI) && defined(__M68020__)
//
// P_SightTrace (TTDOOM's, on these records)
// P_CrossBSPNode and P_CrossSubsector's loop as one routine small enough
// to stay in a 68030's 256-byte instruction cache, for a sight line along
// neither x nor y: the same nodes, subsectors and lines in the same
// order, and the same tests. The C versions walk the tree in one loop and
// the lines in another, each pushing the other out of the cache.
//
// Its arithmetic is in 16 bits: (x - node x) >> 16 as x's high word less
// node x, and muls.w. That is P_DivlineSide's 32-bit arithmetic exactly
// when no difference of two points reaches 32767 map units: only for a
// map smaller than that, and both ends of the sight line inside it
// (P_CheckSight).
//
// The nodes still to cross on their far side are on the stack (node,
// side), a null node under them. A sight record starts as a node does, so
// one routine gives the side of a node's line and of a seg's line. A line
// the sight line crosses goes to P_SightCrossedLineAt, id's code for it.
//
typedef struct
{
    fixed_t		x;		// 0: strace.x, .y
    fixed_t		y;
    fixed_t		t2x;		// 8
    fixed_t		t2y;
    const sightnode_t*	nodes;		// 16
    const sightsub_t*	sightsubs;	// 20
    int			pad;		// 24
    short		sdx;		// 28: strace.dx, .dy >> FRACBITS
    short		sdy;
    short		nsx;		// 32: -strace.x, .y >> FRACBITS
    short		nsy;
    int			validcount;	// 36
} sighttrace_t;

boolean P_SightTrace (int bspnum, const sighttrace_t* t);

// The routine has found the sight line's ends on both sides of the line,
// with P_DivlineSide's arithmetic: id's code from there
__attribute__((used, noinline)) boolean
P_SightCrossedLineAt (const sightseg_t* ss)
{
    seg_t*		seg = &segs[ss - sightsegs];
    const line_t*	line = seg->linedef;
    divline_t		divl;

    divl.x = line->v1->x;
    divl.y = line->v1->y;
    divl.dx = line->v2->x - line->v1->x;
    divl.dy = line->v2->y - line->v1->y;
    return P_SightCrossedLine (seg, &divl);
}

__asm__ (
"	.text\n"
"	.even\n"
"	.globl	_P_SightTrace\n"
"_P_SightTrace:\n"
"	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n"
"	move.l	48(%sp),%d0\n"			// bspnum
"	move.l	52(%sp),%a6\n"			// the trace
"	movem.w	28(%a6),%d2-%d5\n"		// sdx, sdy, nsx, nsy
"	move.l	16(%a6),%a4\n"			// nodes
"	move.l	36(%a6),%a5\n"			// validcount
"	clr.l	-(%sp)\n"			// the node stack's end
"	jbra	.Lsight_down\n"

// What runs least first: the cache's first and last lines are shared
// A line's ends are on both sides of the sight line: are the sight line's
// on both sides of it?
".Lsight_cross:\n"
"	move.l	%a3,%a0\n"
"	lea	(%a6),%a1\n"			// its start
"	jbsr	.Lsight_side\n"
"	move.w	%d0,%d7\n"
"	addq.l	#8,%a1\n"			// its end
"	jbsr	.Lsight_side\n"
"	cmp.w	%d0,%d7\n"
"	jbeq	.Lsight_next\n"
"	jbra	.Lsight_crossed\n"
".Lsight_on:\n"
"	moveq	#2,%d0\n"
"	rts\n"
".Lsight_side2:\n"
"	move.w	(%a1),%d0\n"			// (x - node x) >> 16
"	sub.w	(%a0),%d0\n"
"	muls.w	6(%a0),%d0\n"			// left = dy * that
"	move.w	4(%a1),%d1\n"
"	sub.w	2(%a0),%d1\n"
"	muls.w	4(%a0),%d1\n"			// right = (y - node y) >> 16 * dx
"	cmp.l	%d0,%d1\n"
"	jbeq	.Lsight_on\n"
"	sgt	%d0\n"				// front (0) if right < left
".Lsight_bool:\n"
"	and.w	#1,%d0\n"
"	rts\n"

// P_DivlineSide: a1 = the point (x, y), a0 = node or sight record; side
// 0, 1 or 2 in d0.w. Uses d1.
".Lsight_side:\n"
"	tst.w	4(%a0)\n"			// dx
"	jbne	.Lsight_side1\n"
"	move.w	(%a0),%d1\n"			// the line is x = node x
"	swap	%d1\n"
"	clr.w	%d1\n"
"	move.l	(%a1),%d0\n"
"	cmp.l	%d1,%d0\n"
"	jbeq	.Lsight_on\n"
"	jbgt	.Lsight_side0\n"
"	tst.w	6(%a0)\n"			// x <= node x: dy > 0
"	sgt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side0:\n"
"	tst.w	6(%a0)\n"			// dy < 0
"	slt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side1:\n"
"	tst.w	6(%a0)\n"			// dy
"	jbne	.Lsight_side2\n"
"	move.w	2(%a0),%d1\n"			// the line is y = node y
"	swap	%d1\n"
"	clr.w	%d1\n"
"	cmp.l	(%a1),%d1\n"			// (id's slip: x against it)
"	jbeq	.Lsight_on\n"
"	cmp.l	4(%a1),%d1\n"
"	jblt	.Lsight_side3\n"
"	tst.w	4(%a0)\n"			// y <= node y: dx < 0
"	slt	%d0\n"
"	jbra	.Lsight_bool\n"
".Lsight_side3:\n"
"	tst.w	4(%a0)\n"			// dx > 0
"	sgt	%d0\n"
"	jbra	.Lsight_bool\n"

// Down the starting sides to a subsector: d0.w = node or subsector
".Lsight_node:\n"
"	lea	(%a4,%d0.w*4),%a0\n"		// nodes are 12 bytes
"	lea	(%a0,%d0.w*8),%a0\n"
"	lea	(%a6),%a1\n"
"	jbsr	.Lsight_side\n"
"	and.w	#1,%d0\n"			// on the line (2): both sides, front first
"	move.w	%d0,-(%sp)\n"
"	move.l	%a0,-(%sp)\n"
"	move.w	8(%a0,%d0.w*2),%d0\n"		// cross the starting side
".Lsight_down:\n"
"	tst.w	%d0\n"
"	jbpl	.Lsight_node\n"

// A subsector's lines: a3 = sight record, d6 = the end of them
"	and.w	#0x7fff,%d0\n"
"	move.l	20(%a6),%a0\n"			// sightsubs
"	lea	(%a0,%d0.w*8),%a0\n"
"	move.l	(%a0)+,%a3\n"
"	move.l	(%a0),%d6\n"
"	cmp.l	%d6,%a3\n"
"	jbeq	.Lsight_pop\n"
".Lsight_seg:\n"
"	move.l	12(%a3),%a0\n"			// the line's validcount:
"	cmp.l	(%a0),%a5\n"			// already checked?
"	jbeq	.Lsight_next\n"
"	move.l	%a5,(%a0)\n"
"	move.w	(%a3),%d0\n"			// its first end against the sight
"	add.w	%d4,%d0\n"			// line: left = sdy * (v.x - x >> 16)
"	muls.w	%d3,%d0\n"
"	move.w	2(%a3),%d1\n"
"	add.w	%d5,%d1\n"			// right = (v.y - y >> 16) * sdx
"	muls.w	%d2,%d1\n"
"	sub.l	%d0,%d1\n"			// side 0 below 0, 2 at 0, 1 above
"	move.w	8(%a3),%d0\n"			// its second end
"	add.w	%d4,%d0\n"
"	muls.w	%d3,%d0\n"
"	move.w	10(%a3),%d7\n"
"	add.w	%d5,%d7\n"
"	muls.w	%d2,%d7\n"
"	sub.l	%d0,%d7\n"
"	jbeq	.Lsight_on2\n"
"	tst.l	%d1\n"
"	jbeq	.Lsight_cross\n"
"	eor.l	%d1,%d7\n"
"	jbmi	.Lsight_cross\n"
".Lsight_next:\n"
"	lea	16(%a3),%a3\n"
"	cmp.l	%d6,%a3\n"
"	jbne	.Lsight_seg\n"

// The subsector is crossed: up to a node whose ending side is still to cross
".Lsight_pop:\n"
"	move.l	(%sp)+,%d0\n"
"	jbeq	.Lsight_true\n"
"	move.l	%d0,%a0\n"
"	move.w	(%sp)+,%d7\n"			// the side crossed first
"	lea	8(%a6),%a1\n"
"	jbsr	.Lsight_side\n"
"	cmp.w	%d0,%d7\n"			// the sight line ends on it: this
"	jbeq	.Lsight_pop\n"			// node is crossed
"	eor.w	#1,%d7\n"
"	move.w	8(%a0,%d7.w*2),%d0\n"		// cross the ending side
"	jbra	.Lsight_down\n"

".Lsight_on2:\n"
"	tst.l	%d1\n"				// both ends on the sight line
"	jbeq	.Lsight_next\n"
"	jbra	.Lsight_cross\n"
".Lsight_crossed:\n"
"	move.l	%a3,-(%sp)\n"
"	jsr	_P_SightCrossedLineAt\n"
"	addq.l	#4,%sp\n"
"	tst.l	%d0\n"
"	jbne	.Lsight_next\n"
".Lsight_false:\n"				// blocked: drop the node stack
"	move.l	(%sp)+,%d0\n"
"	jbeq	.Lsight_ret\n"
"	addq.l	#2,%sp\n"
"	jbra	.Lsight_false\n"
".Lsight_true:\n"
"	moveq	#1,%d0\n"
".Lsight_ret:\n"
"	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n"
"	rts\n"
);
#endif

//
// P_CheckSight
// Returns true
//  if a straight line between t1 and t2 is unobstructed.
// Uses REJECT.
//
boolean
P_CheckSight
( mobj_t*	t1,
  mobj_t*	t2 )
{
    int		s1;
    int		s2;
    int		pnum;
    int		bytenum;
    int		bitnum;
    
    // First check for trivial rejection.

    // Determine subsector entries in REJECT table.
    s1 = (t1->subsector->sector - sectors);
    s2 = (t2->subsector->sector - sectors);
    pnum = s1*numsectors + s2;
    bytenum = pnum>>3;
    bitnum = 1 << (pnum&7);

    // Check in REJECT table.
    if (rejectmatrix[bytenum]&bitnum)
    {
	sightcounts[0]++;

	// can't possibly be connected
	return false;	
    }

    // An unobstructed LOS is possible.
    // Now look from eyes of t1 to any part of t2.
    sightcounts[1]++;

    validcount++;
	
    sightzstart = t1->z + t1->height - (t1->height>>2);
    topslope = (t2->z+t2->height) - sightzstart;
    bottomslope = (t2->z) - sightzstart;
	
    if (gameversion <= exe_doom_1_2)
    {
        return P_PathTraverse(t1->x, t1->y, t2->x, t2->y,
                              PT_EARLYOUT | PT_COMPATADDLINES, PTR_SightTraverse);
    }

    strace.x = t1->x;
    strace.y = t1->y;
    t2x = t2->x;
    t2y = t2->y;
    strace.dx = t2->x - t1->x;
    strace.dy = t2->y - t1->y;

#if defined(ATARI) && defined(__M68020__)
    // The assembler trace where its 16 bits are exact: a map under 32767
    // units across, and both ends inside it
    if (strace.dx && strace.dy && numnodes && sight_short
     && (t1->x >> FRACBITS) >= sight_minx && (t1->x >> FRACBITS) <= sight_maxx
     && (t1->y >> FRACBITS) >= sight_miny && (t1->y >> FRACBITS) <= sight_maxy
     && (t2x >> FRACBITS) >= sight_minx && (t2x >> FRACBITS) <= sight_maxx
     && (t2y >> FRACBITS) >= sight_miny && (t2y >> FRACBITS) <= sight_maxy)
    {
	sighttrace_t	t;

	t.x = strace.x;
	t.y = strace.y;
	t.t2x = t2x;
	t.t2y = t2y;
	t.nodes = sightnodes;
	t.sightsubs = sightsubs;
	t.sdx = strace.dx >> FRACBITS;
	t.sdy = strace.dy >> FRACBITS;
	t.nsx = (fixed_t)(0u - (unsigned)strace.x) >> FRACBITS;
	t.nsy = (fixed_t)(0u - (unsigned)strace.y) >> FRACBITS;
	t.validcount = validcount;
	return P_SightTrace (numnodes-1, &t);
    }
#endif

    // the head node is the last node output
    return P_CrossBSPNode (numnodes-1);	
}


