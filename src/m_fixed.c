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
//	Fixed point implementation.
//



#include "stdlib.h"

#include "doomtype.h"
#include "i_system.h"

#include "m_fixed.h"

#ifdef ATARI
#include <mint/cookie.h>
#endif

#ifdef FIXEDMUL_MULS64

// The Atari TT and Falcon build: the inline muls.l and divs.l in
// m_fixed.h on a 68020 to 68040, the C below on a 68060.

int fixedmul_muls64;

void M_InitFixed(void)
{
    long cpu = 0;

    fixedmul_muls64 = Getcookie(C__CPU, &cpu) == C_FOUND
                   && cpu >= 20 && cpu < 60;
}

#define FixedMul FixedMulSoft
#define FixedDiv FixedDivSoft

#elif defined(ATARI)

void M_InitFixed(void)
{
}

#endif

#if defined(ATARI) && !defined(__M68020__)

// The 68000 (after STDOOM): four 16x16 multiplies, which give exactly
// the 64-bit product's bits 47-16, rounded as an arithmetic shift does
// (checked for 50 million products, edge cases included). gcc makes the
// C's 64-bit product a library call.

static inline uint32_t mulu(uint16_t a, uint16_t b)
{
    uint32_t result = a;

    __asm__ ("mulu.w	%1,%0" : "+d" (result) : "d" (b) : "cc");
    return result;
}

fixed_t FixedMul(fixed_t a, fixed_t b)
{
    // Is the result a negative number?
    boolean neg = 0 != ((a ^ b) & 0x80000000);
    uint16_t alw, ahw, blw, bhw;
    uint32_t hh, hl, lh, ll;
    int32_t result;

    // Only work with unsigned numbers.
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    alw = a;
    ahw = a >> FRACBITS;
    blw = b;
    bhw = b >> FRACBITS;

    hh = ahw && bhw ? mulu(ahw, bhw) << FRACBITS : 0;
    hl = ahw && blw ? mulu(ahw, blw) : 0;
    lh = alw && bhw ? mulu(alw, bhw) : 0;

    // Make sure we round towards -inf
    ll = alw && blw ? (mulu(alw, blw) + (neg ? 0xffff : 0)) >> FRACBITS : 0;

    result = hh + hl + lh + ll;
    if (neg) result = -result;
    return result;
}

// a << 16 / b, a < 2^29 and b < 2^30, both positive: bit by bit, which
// is the 64-bit quotient there (checked for 181 million pairs).
static fixed_t FixedDivBits(uint32_t a, uint32_t b)
{
    uint16_t ibit = 1, cl = 0;
    int16_t ch = 0;
    int i;

    while (b < a)
    {
        b <<= 1;
        ibit <<= 1;
    }

    for (; ibit != 0; ibit >>= 1)
    {
        if (a >= b)
        {
            a -= b;
            ch |= ibit;
        }
        a <<= 1;
    }

    for (i = 15; i >= 0; --i)
    {
        if (a >= b)
        {
            a -= b;
            cl |= 1 << i;
        }
        a <<= 1;
    }

    return (((fixed_t) ch) << FRACBITS) | cl;
}

fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    uint32_t ua, ub;
    fixed_t q;

    if ((abs(a) >> 14) >= abs(b))
    {
	return (a^b) < 0 ? INT_MIN : INT_MAX;
    }

    ua = a < 0 ? 0u - (uint32_t) a : (uint32_t) a;
    ub = b < 0 ? 0u - (uint32_t) b : (uint32_t) b;

    if (ua < (1u << 29) && ub < (1u << 30))
    {
        q = FixedDivBits(ua, ub);
        return (a ^ b) < 0 ? -q : q;
    }

    return (fixed_t) (((int64_t) a << FRACBITS) / b);
}

#else

// Fixme. __USE_C_FIXED__ or something.

fixed_t
FixedMul
( fixed_t	a,
  fixed_t	b )
{
    return ((int64_t) a * (int64_t) b) >> FRACBITS;
}



//
// FixedDiv, C version.
//

fixed_t FixedDiv(fixed_t a, fixed_t b)
{
    if ((abs(a) >> 14) >= abs(b))
    {
	return (a^b) < 0 ? INT_MIN : INT_MAX;
    }
    else
    {
	int64_t result;

	result = ((int64_t) a << FRACBITS) / b;

	return (fixed_t) result;
    }
}

#endif

