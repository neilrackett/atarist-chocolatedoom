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
//     The textscreen calls Heretic's and Strife's start-up screens make
//     (DOS text screens with progress bars), for an Atari, where there is
//     no textscreen library. TXT_Init fails, so each prints its start-up
//     as lines, as Doom does, and makes none of the others.
//

#include <stddef.h>

#include "txt_main.h"
#include "txt_io.h"

int TXT_Init(void)
{
    return 0;
}

void TXT_Shutdown(void)
{
}

unsigned char *TXT_GetScreenData(void)
{
    return NULL;
}

void TXT_UpdateScreen(void)
{
}

int TXT_GetChar(void)
{
    return -1;
}

void TXT_SetWindowTitle(const char *title)
{
}

void TXT_PutChar(int c)
{
}

void TXT_Puts(const char *s)
{
}

void TXT_GotoXY(int x, int y)
{
}

void TXT_FGColor(txt_color_t color)
{
}

void TXT_BGColor(int color, int blinking)
{
}

void TXT_GetXY(int *x, int *y)
{
    *x = *y = 0;
}
