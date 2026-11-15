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
//     Atari system support.
//

#ifndef __I_ATARI__
#define __I_ATARI__

#include "doomtype.h"

typedef enum
{
    ATARI_ST,
    ATARI_STE,
    ATARI_MEGASTE,
    ATARI_TT,
    ATARI_FALCON
} atari_machine_t;

// The machine, from the _MCH cookie
atari_machine_t I_AtariMachine(void);

// The zone, from the free memory (-mb <MiB> fixes its size)
byte *I_AtariZoneBase(int *size, int mb);

#endif
