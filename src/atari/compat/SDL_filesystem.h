/* SDL_filesystem.h for the Atari build: TOS has no per-user folder, so
 * there is no pref path; Chocolate Doom then keeps its files beside the
 * program, as on Windows (m_config.c). */

#ifndef ATARI_SDL_FILESYSTEM_H
#define ATARI_SDL_FILESYSTEM_H

#include "SDL_stdinc.h"

static inline char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void) org;
    (void) app;
    return NULL;
}

#endif
