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
//     Hexen's 640x480 16-colour start-up screen (i_videohr.c on a PC), for
//     an Atari, where the add-on's screen is 320x240 in 256 colours: the
//     mode never opens, so Hexen prints its start-up as lines, and nothing
//     else here is reached.
//

#include "doomtype.h"
#include "i_videohr.h"

boolean I_SetVideoModeHR(void)
{
    return false;
}

void I_UnsetVideoModeHR(void)
{
}

void I_SetWindowTitleHR(const char *title)
{
}

void I_ClearScreenHR(void)
{
}

void I_SlamBlockHR(int x, int y, int w, int h, const byte *src)
{
}

void I_SlamHR(const byte *buffer)
{
}

void I_InitPaletteHR(void)
{
}

void I_SetPaletteHR(const byte *palette)
{
}

void I_FadeToPaletteHR(const byte *palette)
{
}

void I_BlackPaletteHR(void)
{
}

boolean I_CheckAbortHR(void)
{
    return false;
}
