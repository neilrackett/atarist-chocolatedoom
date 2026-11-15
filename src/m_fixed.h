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
//	Fixed point arithemtics, implementation.
//


#ifndef __M_FIXED__
#define __M_FIXED__




//
// Fixed point, 32bit as 16.16.
//
#define FRACBITS		16
#define FRACUNIT		(1<<FRACBITS)

typedef int fixed_t;

#if defined(ATARI) && defined(__M68020__)

// The Atari TT and Falcon build (after TTDOOM): on a 68020 to 68040
// FixedMul is one 64-bit muls.l and FixedDiv one 64/32 divs.l, inline.
// Both give exactly what the C below gives (the product's bits 47-16;
// the quotient truncated toward zero, which the guard keeps inside 31
// bits). gcc makes the C's 64-bit product and quotient library calls.
// A 68060 has neither instruction (it traps them to software), so it
// keeps the C: M_InitFixed sets fixedmul_muls64 from the _CPU cookie.

#define FIXEDMUL_MULS64
extern int fixedmul_muls64;
fixed_t FixedMulSoft	(fixed_t a, fixed_t b);
fixed_t FixedDivSoft	(fixed_t a, fixed_t b);

static inline fixed_t FixedMul64(fixed_t a, fixed_t b)
{
    fixed_t hi;

    // The 64-bit product in hi:a; bits 47..16 are the result
    __asm__ ("muls.l	%2,%1:%0\n\t"
             "move.w	%1,%0\n\t"
             "swap	%0"
             : "=d" (a), "=&d" (hi)
             : "d" (b), "0" (a)
             : "cc");
    return a;
}

static inline fixed_t FixedMul(fixed_t a, fixed_t b)
{
    if (fixedmul_muls64)
    {
        return FixedMul64(a, b);
    }
    return FixedMulSoft(a, b);
}

static inline fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    // INT_MIN's abs() is itself, which the C's guard and divide take as
    // they come: the C does that.
    if (fixedmul_muls64 && a != (fixed_t) 0x80000000)
    {
        unsigned int ua = a < 0 ? 0u - (unsigned int) a : (unsigned int) a;
        unsigned int ub = b < 0 ? 0u - (unsigned int) b : (unsigned int) b;
        fixed_t hi;

        if ((int) (ua >> 14) >= (int) ub)
        {
            return (a ^ b) < 0 ? (fixed_t) 0x80000000 : 0x7fffffff;
        }
        hi = a >> 16;
        a = (fixed_t) ((unsigned int) a << 16);
        // hi:a / b, the quotient in a
        __asm__ ("divs.l	%2,%1:%0"
                 : "+d" (a), "+d" (hi)
                 : "d" (b)
                 : "cc");
        return a;
    }
    return FixedDivSoft(a, b);
}

#else

fixed_t FixedMul	(fixed_t a, fixed_t b);

fixed_t FixedDiv	(fixed_t a, fixed_t b);

#endif

#ifdef ATARI
// At startup: picks the fastest FixedMul and FixedDiv this CPU can run
void M_InitFixed(void);
#endif


#endif

