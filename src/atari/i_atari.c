//
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
//     Atari system support: the machine, the zone's size, a key wait.
//

#include <stdio.h>
#include <stdlib.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

#include "doomtype.h"
#include "i_atari.h"
#include "i_system.h"

// What the zone leaves for everything else, besides the zone.
//
// The SDL2 add-on's pages are in ST-RAM and only allocated when the
// window opens, after the zone: three 320x480 pages on a TT (460K), up
// to three 384x240 on a Falcon (276K), three 32K pages and a 77K chunky
// model on an ST. Then Chocolate Doom's own mallocs: the WAD directory,
// sound effects, music, stdio.
#define RESERVE_ST_RAM_TT   (640 * 1024L)
#define RESERVE_ST_RAM_ST   (320 * 1024L)
#define RESERVE_MALLOC      (320 * 1024L)

// With the zone in TT-RAM, what malloc can't have there comes from the
// ST-RAM beside the pages (over a megabyte on a TT with 2 MB of it): the
// zone leaves TT-RAM only a little.
#define RESERVE_MALLOC_TT   (128 * 1024L)

// Vanilla's -mb default was 6 MiB, Chocolate Doom's 16: an Atari seldom
// has that much, and the zone stops at 8 MiB.
#define ZONE_MAX            (8 * 1024 * 1024L)
#define ZONE_MIN            (1024 * 1024L)

static long machine = -1;

atari_machine_t I_AtariMachine(void)
{
    long mch;

    if (machine < 0)
    {
        if (Getcookie(C__MCH, &mch) != C_FOUND)
        {
            mch = 0;
        }

        switch (mch >> 16)
        {
            case 0:  machine = ATARI_ST;  break;
            case 1:  machine = (mch & 0xffff) == 0x10 ? ATARI_MEGASTE
                                                      : ATARI_STE;
                     break;
            case 2:  machine = ATARI_TT;  break;
            case 3:  machine = ATARI_FALCON; break;
            default: machine = ATARI_FALCON; break;  // a clone: 8-bit video
        }
    }

    return (atari_machine_t) machine;
}

// The largest free block of a kind of RAM (Mxalloc mode 0: ST-RAM,
// 1: TT-RAM); TOS before 2.0 has no Mxalloc, and all of its RAM is
// ST-RAM.
static long LargestBlock(int mode)
{
    long n = (long) Mxalloc(-1, mode);

    if (n < 0)
    {
        return mode == 0 ? (long) Malloc(-1) : 0;
    }

    return n;
}

byte *I_AtariZoneBase(int *size, int mb)
{
    long st_free, tt_free, want, avail;
    int in_tt_ram;
    byte *zone;

    st_free = LargestBlock(0);
    tt_free = LargestBlock(1);

    // TT-RAM is the zone's if there is enough of it; ST-RAM then keeps
    // the pages and the DMA sound to itself.
    in_tt_ram = tt_free - RESERVE_MALLOC_TT >= 2 * ZONE_MIN;

    if (in_tt_ram)
    {
        avail = tt_free - RESERVE_MALLOC_TT;
    }
    else
    {
        avail = st_free - RESERVE_MALLOC
              - (I_AtariMachine() >= ATARI_TT ? RESERVE_ST_RAM_TT
                                              : RESERVE_ST_RAM_ST);
    }

    want = mb > 0 ? mb * 1024L * 1024L : ZONE_MAX;

    if (want > avail)
    {
        if (mb > 0)
        {
            I_Error("Unable to allocate %i MiB of RAM for zone: %li KiB "
                    "free", mb, avail / 1024);
        }
        want = avail;
    }

    if (want < ZONE_MIN)
    {
        I_Error("Not enough memory: the zone has %li KiB, and needs at "
                "least %li", want / 1024, ZONE_MIN / 1024);
    }

    want &= ~15L;
    zone = (byte *) Mxalloc(want, in_tt_ram ? 1 : 0);
    if ((long) zone < 0 || zone == NULL)
    {
        zone = (byte *) Malloc(want);
    }
    if (zone == NULL)
    {
        I_Error("Unable to allocate %li KiB of RAM for zone", want / 1024);
    }

    // GEMDOS keeps blocks only even: on 16 bytes, no long the zone holds
    // straddles two (a 68030 reads such a long in two bus cycles).
    {
        unsigned long base = ((unsigned long) zone + 15) & ~15UL;

        want -= base - (unsigned long) zone;
        want &= ~15L;
        zone = (byte *) base;
    }

    printf("zone: %li KiB in %s-RAM\n", want / 1024, in_tt_ram ? "TT" : "ST");
    *size = (int) want;
    return zone;
}

void I_AtariWaitKey(void)
{
    // The keyboard buffer may hold keys pressed during play
    while (Cconis())
    {
        Cconin();
    }
    Cconws("Press any key.\r\n");
    Cconin();
}
