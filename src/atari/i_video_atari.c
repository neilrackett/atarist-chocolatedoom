//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//     The Atari screen, on STDL's SDL2 add-on: i_video.c's interface.
//
//     Doom draws into I_VideoBuffer, 320x200 bytes. The add-on converts
//     an 8-bit texture into the screen's planes, only the boxes it is
//     given: what Doom marked since the last frame (I_MarkRect, fed by
//     V_MarkRect and the renderer), in bands of 8 rows rounded out to
//     16-pixel groups. The texture lies over I_VideoBuffer itself, and
//     the picture is Doom's 320x200, 1:1, centred on a TT's or a
//     Falcon's 240 rows. aspect_ratio_correct 1 stretches it to 320x240
//     instead, 1.2 times as a 4:3 VGA screen did, each fifth row shown
//     twice, by the copies alone: a fifth more rows to convert, and
//     copies. An ST always shows 320x200.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_stdl.h"

#include "config.h"
#include "d_event.h"
#include "deh_str.h"
#include "doomtype.h"
#include "i_atari.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_diskicon.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

static SDL_Window *screen;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_Palette *sdl_palette;

static SDL_Color palette[256];
static boolean palette_to_set;

static boolean initialized = false;

// The picture: 200 rows, or 240 with Doom's rows stretched
static int picture_height;
static pixel_t *video_buffer_mem;

// What changed since the last frame: bands of BAND_ROWS rows, each with
// the groups [x0, x1) of 16 pixels that changed in it (x0 >= x1: none).
#define BAND_ROWS 8
#define NUM_BANDS ((SCREENHEIGHT + BAND_ROWS - 1) / BAND_ROWS)
static short band_x0[NUM_BANDS], band_x1[NUM_BANDS];
static boolean anything_marked;

// The 3D view's box when it is in pixel pairs (low detail), copied
// apart from the marks with STDL_SDL2_RenderCopyPairs: the add-on
// converts a group of pairs in 56 instructions where any other takes
// 74, and converts whole any group that a mark's copy also reaches
// (messages, the menu, the crosshair over the view). x0 >= x1: none.
static int view_x0, view_x1, view_y0, view_y1;
static boolean nopairs;

// Configuration variables, all of i_video.c's, so a config file written
// on another machine reads and writes back unchanged. Most have no
// meaning here.

static boolean nomouse = false;
int usemouse = 1;
int png_screenshots = 0;
char *video_driver = "";
char *window_position = "center";
static int video_display = 0;
static int window_width = 800;
static int window_height = 600;
static int fullscreen_width = 0, fullscreen_height = 0;
static int max_scaling_buffer_pixels = 16000000;
int fullscreen = true;
// 1:1 by default: the stretch costs a fifth more conversion
int aspect_ratio_correct = false;
int smooth_pixel_scaling = true;
int integer_scaling = false;
int vga_porch_flash = false;
int force_software_renderer = false;
static int startup_delay = 1000;
static int grabmouse = true;
pixel_t *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible = true;
static boolean display_fps_dots;
static boolean noblit;
static grabmouse_callback_t grabmouse_callback = NULL;
int usegamma = 0;
unsigned int joywait = 0;
int screen_width = SCREENWIDTH;
int screen_height = SCREENHEIGHT;

// A buffer on 16 bytes: the add-on copies whole groups from an even
// address fastest.
static pixel_t *AllocAligned(int size, pixel_t **mem)
{
    *mem = malloc(size + 15);

    if (*mem == NULL)
    {
        I_Error("I_InitGraphics: no memory for a %i byte screen buffer",
                size);
    }

    return (pixel_t *) (((unsigned long) *mem + 15) & ~15UL);
}

void I_MarkRect(int x, int y, int w, int h)
{
    int x0, x1, b, b1;

    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > SCREENWIDTH)
    {
        w = SCREENWIDTH - x;
    }
    if (y + h > SCREENHEIGHT)
    {
        h = SCREENHEIGHT - y;
    }
    if (w <= 0 || h <= 0)
    {
        return;
    }

    x0 = x & ~15;
    x1 = (x + w + 15) & ~15;
    b1 = (y + h - 1) / BAND_ROWS;

    for (b = y / BAND_ROWS; b <= b1; ++b)
    {
        if (band_x0[b] >= band_x1[b])
        {
            band_x0[b] = x0;
            band_x1[b] = x1;
        }
        else
        {
            if (x0 < band_x0[b])
            {
                band_x0[b] = x0;
            }
            if (x1 > band_x1[b])
            {
                band_x1[b] = x1;
            }
        }
    }

    anything_marked = true;
}

void I_MarkView(int x, int y, int w, int h, boolean pairs)
{
    if (!pairs || nopairs)
    {
        I_MarkRect(x, y, w, h);
        return;
    }

    view_x0 = x;
    view_x1 = x + w;
    view_y0 = y;
    view_y1 = y + h;
    anything_marked = true;
}

static void ClearMarks(void)
{
    int b;

    view_x0 = view_x1 = 0;

    for (b = 0; b < NUM_BANDS; ++b)
    {
        band_x0[b] = SCREENWIDTH;
        band_x1[b] = 0;
    }

    anything_marked = false;
}

// Doom's rows [sy, sy + h), groups [x0, x1), to picture rows from dy;
// as pixel pairs with pairs
static void CopyRows(int x0, int x1, int sy, int dy, int h, boolean pairs)
{
    SDL_Rect src, dst;

    src.x = dst.x = x0;
    src.w = dst.w = x1 - x0;
    src.y = sy;
    dst.y = dy;
    src.h = dst.h = h;

    if (pairs)
    {
        STDL_SDL2_RenderCopyPairs(renderer, texture, &src, &dst);
    }
    else
    {
        SDL_RenderCopy(renderer, texture, &src, &dst);
    }
}

// One box of Doom's screen, rows [y0, y1), to the renderer. Stretched,
// picture row d shows Doom's row d * 200 / 240: Doom's rows 5k to 5k + 4
// are picture rows 6k + 1 to 6k + 5, and row 5k is picture row 6k too,
// so a block of five rows is two copies.
static void CopyBox(int x0, int x1, int y0, int y1, boolean pairs)
{
    int k, s0, s1;

    if (picture_height == SCREENHEIGHT)
    {
        CopyRows(x0, x1, y0, y0, y1 - y0, pairs);
        return;
    }

    for (k = y0 / 5; k * 5 < y1; ++k)
    {
        s0 = k * 5 > y0 ? k * 5 : y0;
        s1 = k * 5 + 5 < y1 ? k * 5 + 5 : y1;
        if (s0 == k * 5)
        {
            CopyRows(x0, x1, s0, k * 6, 1, pairs);
        }
        CopyRows(x0, x1, s0, s0 + k + 1, s1 - s0, pairs);
    }
}

// The marked bands as boxes: consecutive bands with the same groups
// make one box, as the add-on copies fewer, taller boxes faster.
static void CopyMarked(void)
{
    int b, b0, y1;

    if (view_x0 < view_x1)
    {
        CopyBox(view_x0, view_x1, view_y0, view_y1, true);
    }

    b = 0;
    while (b < NUM_BANDS)
    {
        if (band_x0[b] >= band_x1[b])
        {
            ++b;
            continue;
        }

        b0 = b;
        do
        {
            ++b;
        } while (b < NUM_BANDS && band_x0[b] == band_x0[b0]
                 && band_x1[b] == band_x1[b0]);

        y1 = b * BAND_ROWS;
        if (y1 > SCREENHEIGHT)
        {
            y1 = SCREENHEIGHT;
        }
        CopyBox(band_x0[b0], band_x1[b0], b0 * BAND_ROWS, y1, false);
    }
}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    grabmouse_callback = func;
}

void I_DisplayFPSDots(boolean dots_on)
{
    display_fps_dots = dots_on;
}

void I_ShutdownGraphics(void)
{
    if (initialized)
    {
        STDL_SDL2_Check chk;

        // -sdl2check's result: the pages that differed from the screen
        // Doom drew
        if (STDL_SDL2_GetCheck(renderer, &chk) == 0)
        {
            printf("sdl2check: %lu flips, %lu unlike the screen drawn"
                   " (first: flip %lu, row %d)\n",
                   (unsigned long) chk.flips, (unsigned long) chk.bad,
                   (unsigned long) chk.first_flip, chk.first_row);
        }
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(screen);
        SDL_FreePalette(sdl_palette);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        initialized = false;
    }
}

void I_StartFrame (void)
{
}

void I_GetEvent(void)
{
    SDL_Event sdlevent;

    while (SDL_PollEvent(&sdlevent))
    {
        switch (sdlevent.type)
        {
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                I_HandleKeyboardEvent(&sdlevent);
                break;

            // The mouse's motion is read as relative state, by
            // I_ReadMouse, as on a PC
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            case SDL_MOUSEWHEEL:
                if (usemouse && !nomouse)
                {
                    I_HandleMouseEvent(&sdlevent);
                }
                break;

            // The pad is read only after it changes (i_joystick.c)
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
            case SDL_CONTROLLERAXISMOTION:
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                I_AtariGamepadEvent();
                break;

            case SDL_QUIT:
                {
                    event_t event;
                    event.type = ev_quit;
                    D_PostEvent(&event);
                }
                break;

            default:
                break;
        }
    }
}

void I_StartTic (void)
{
    if (!initialized)
    {
        return;
    }

    I_GetEvent();

    if (usemouse && !nomouse)
    {
        I_ReadMouse();
    }

    if (joywait < I_GetTime())
    {
        I_UpdateJoystick();
    }
}

void I_UpdateNoBlit (void)
{
}

void I_FinishUpdate (void)
{
    static int lasttic;
    int tics;
    int i;

    if (!initialized || noblit)
    {
        return;
    }

    // draws little dots on the bottom of the screen

    if (display_fps_dots)
    {
        i = I_GetTime();
        tics = i - lasttic;
        lasttic = i;
        if (tics > 20) tics = 20;

        for (i=0 ; i<tics*4 ; i+=4)
            I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0xff;
        for ( ; i<20*4 ; i+=4)
            I_VideoBuffer[ (SCREENHEIGHT-1)*SCREENWIDTH + i] = 0x0;

        I_MarkRect(0, SCREENHEIGHT - 1, 80, 1);
    }

    // Draw disk icon before blit, if necessary.
    V_DrawDiskIcon();

    if (palette_to_set)
    {
        // The add-on stages the colours for the blank that shows the
        // next page.
        SDL_SetPaletteColors(sdl_palette, palette, 0, 256);
        palette_to_set = false;
    }
    else if (!anything_marked)
    {
        // Nothing new to show
        V_RestoreDiskBackground();
        return;
    }

    CopyMarked();
    ClearMarks();

    SDL_RenderPresent(renderer);

    // Restore background and undo the disk indicator, if it was drawn.
    V_RestoreDiskBackground();
}

void I_ReadScreen (pixel_t* scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH*SCREENHEIGHT*sizeof(*scr));
}

void I_SetPalette (byte *doompalette)
{
    int i;

    for (i=0; i<256; ++i)
    {
        // Zero out the bottom two bits of each channel - the PC VGA
        // controller only supports 6 bits of accuracy.

        palette[i].a = 0xFFU;
        palette[i].r = gammatable[usegamma][*doompalette++] & ~3;
        palette[i].g = gammatable[usegamma][*doompalette++] & ~3;
        palette[i].b = gammatable[usegamma][*doompalette++] & ~3;
    }

    // Register 0 is also the border round the picture, and the bands
    // above and below it: as on a PC, they stay black through the
    // damage and pickup palettes unless vga_porch_flash says otherwise
    // (colour 0 is black in PLAYPAL; only its tinted versions change).
    if (!vga_porch_flash)
    {
        palette[0].r = palette[0].g = palette[0].b = 0;
    }

    palette_to_set = true;
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex(int r, int g, int b)
{
    int best, best_diff, diff;
    int i;

    best = 0; best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
        diff = (r - palette[i].r) * (r - palette[i].r)
             + (g - palette[i].g) * (g - palette[i].g)
             + (b - palette[i].b) * (b - palette[i].b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

// There is no window to title, nor an icon to give it.

void I_SetWindowTitle(const char *title)
{
}

void I_InitWindowTitle(void)
{
}

void I_RegisterWindowIcon(const unsigned int *icon, int width, int height)
{
}

void I_InitWindowIcon(void)
{
}

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    *x = *y = 0;
}

void I_CheckIsScreensaver(void)
{
}

void I_GraphicsCheckCommandLine(void)
{
    //!
    // @category video
    // @vanilla
    //
    // Disable blitting the screen.
    //

    noblit = M_CheckParm ("-noblit");

    //!
    // @category video
    //
    // Disable the mouse.
    //

    nomouse = M_CheckParm("-nomouse") > 0;

    //!
    // @category video
    //
    // Convert the low-detail view as any other pixels, not as pairs:
    // for timing the difference.
    //

    nopairs = M_CheckParm("-nopairs") > 0;
}

void I_InitGraphics(void)
{
    byte *doompal;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        I_Error("Failed to initialize video: %s", SDL_GetError());
    }

    // An ST has no 240-row screen to stretch into (the add-on's tall
    // one opens the bottom border, two pages, and flickers).
    if (aspect_ratio_correct && I_AtariMachine() >= ATARI_TT)
    {
        picture_height = SCREENHEIGHT_4_3;
    }
    else
    {
        picture_height = SCREENHEIGHT;
    }

    screen = SDL_CreateWindow(PACKAGE_STRING, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, SCREENWIDTH,
                              picture_height, 0);
    if (screen == NULL)
    {
        I_Error("Error creating window for video startup: %s",
                SDL_GetError());
    }

    //!
    // @category video
    //
    // The add-on's screen check, for testing: each page is compared
    // with the screen Doom drew before it is shown, and the count
    // printed at the end (230K of memory, and a page's conversion a
    // frame).
    //

    if (M_CheckParm("-sdl2check"))
    {
        SDL_SetHint(STDL_SDL2_HINT_CHECK, "1");
    }

    renderer = SDL_CreateRenderer(screen, -1, 0);
    if (renderer == NULL)
    {
        I_Error("Error creating renderer for screen window: %s",
                SDL_GetError());
    }

    sdl_palette = SDL_AllocPalette(256);
    if (sdl_palette == NULL)
    {
        I_Error("I_InitGraphics: %s", SDL_GetError());
    }
    STDL_SDL2_SetRenderPalette(renderer, sdl_palette);

    I_VideoBuffer = AllocAligned(SCREENWIDTH * SCREENHEIGHT,
                                 &video_buffer_mem);

    texture = STDL_SDL2_CreateTextureFrom(renderer, I_VideoBuffer,
                                          SCREENWIDTH, SCREENHEIGHT,
                                          SCREENWIDTH);

    if (texture == NULL)
    {
        I_Error("I_InitGraphics: %s", SDL_GetError());
    }

    // Set the palette

    doompal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);
    I_SetPalette(doompal);

    V_RestoreBuffer();

    // Clear the screen to black, and show it.

    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT * sizeof(*I_VideoBuffer));
    ClearMarks();
    I_MarkRect(0, 0, SCREENWIDTH, SCREENHEIGHT);

    // clear out any events waiting at the start

    I_GetEvent();

    initialized = true;

    // Call I_ShutdownGraphics on quit

    I_AtExit(I_ShutdownGraphics, true);
}

// Bind all variables controlling video options into the configuration
// file system.
void I_BindVideoVariables(void)
{
    M_BindIntVariable("use_mouse",                 &usemouse);
    M_BindIntVariable("fullscreen",                &fullscreen);
    M_BindIntVariable("video_display",             &video_display);
    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
    M_BindIntVariable("integer_scaling",           &integer_scaling);
    M_BindIntVariable("smooth_pixel_scaling",      &smooth_pixel_scaling);
    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
    M_BindIntVariable("startup_delay",             &startup_delay);
    M_BindIntVariable("fullscreen_width",          &fullscreen_width);
    M_BindIntVariable("fullscreen_height",         &fullscreen_height);
    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
    M_BindIntVariable("max_scaling_buffer_pixels", &max_scaling_buffer_pixels);
    M_BindIntVariable("window_width",              &window_width);
    M_BindIntVariable("window_height",             &window_height);
    M_BindIntVariable("grabmouse",                 &grabmouse);
    M_BindStringVariable("video_driver",           &video_driver);
    M_BindStringVariable("window_position",        &window_position);
    M_BindIntVariable("usegamma",                  &usegamma);
    M_BindIntVariable("png_screenshots",           &png_screenshots);
}
