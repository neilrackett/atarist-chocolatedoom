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
//      Timer functions.
//

#include "SDL.h"

#include "i_timer.h"
#include "m_argv.h"
#include <stdlib.h>
#include "doomtype.h"

//
// I_GetTime
// returns time in 1/35th second tics
//

static Uint32 basetime = 0;

#if defined(ATARI) || defined(CHOC_BENCH)
// -benchpace <percent>: the game's clock that much faster, to play at a
// slower machine's pace in an emulator that runs faster than it (Hatari's
// TT runs about 1.9 times a real TT's speed: 190)
static Uint32 pace = 100;
#define PACE(t) ((t) * pace / 100)
#else
#define PACE(t) (t)
#endif

int  I_GetTime (void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    ticks = PACE(ticks - basetime);

    return (ticks * TICRATE) / 1000;    
}

//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    return PACE(ticks - basetime);
}

// Sleep for a specified number of ms

void I_Sleep(int ms)
{
    SDL_Delay(ms);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}


void I_InitTimer(void)
{
    // initialize timer

#ifndef ATARI
    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");
#endif

    SDL_Init(SDL_INIT_TIMER);

#if defined(ATARI) || defined(CHOC_BENCH)
    {
        //!
        // @category obscure
        // @arg <percent>
        //
        // Run the game's clock this much faster, to time real play at a
        // slower machine's pace in a faster emulator.
        //

        int p = M_CheckParmWithArgs("-benchpace", 1);

        if (p > 0)
        {
            pace = atoi(myargv[p + 1]);
        }
    }
#endif
}

