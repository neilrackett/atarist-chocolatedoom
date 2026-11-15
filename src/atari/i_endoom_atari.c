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
//     The ENDOOM screen on an Atari: its text on the desktop's console,
//     once the game's screen has gone, if the console has 80 columns.
//     There is no textscreen library here, and the PC's colours and box
//     characters are not the Atari's: those print as spaces.
//

#include <stdio.h>
#include <mint/osbind.h>

#include "SDL.h"

#include "doomtype.h"
#include "i_endoom.h"
#include "i_system.h"

#define ENDOOM_W 80
#define ENDOOM_H 25

void I_Endoom(byte *endoom_data)
{
    char line[ENDOOM_W + 3];
    int x, y, len;
    byte c;

    // The desktop's screen and keyboard back first: STDL holds the IKBD
    // until it quits, and TOS would never see the key waited for. D_Endoom
    // is the last of the exit functions to run.
    SDL_Quit();

    // ST Low's console has 40 columns; ST Medium, ST High and a TT's or
    // a Falcon's desktop modes have 80.
    if (Getrez() == 0)
    {
        return;
    }

    Cconws("\033E");        // clear the screen, cursor home

    for (y = 0; y < ENDOOM_H; ++y)
    {
        len = 0;
        for (x = 0; x < ENDOOM_W; ++x)
        {
            c = endoom_data[(y * ENDOOM_W + x) * 2];
            line[x] = (c >= 32 && c < 127) ? (char) c : ' ';
            if (line[x] != ' ')
            {
                len = x + 1;
            }
        }

        // The last line, without its line feed: an 80-column console
        // scrolls on a 26th.
        line[len] = '\0';
        Cconws(line);
        if (y < ENDOOM_H - 1)
        {
            Cconws("\r\n");
        }
    }

    I_AtariWaitKey();
}
