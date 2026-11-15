/* SDL_endian.h for the Atari build: STDL's SDL2 add-on has none. The
 * 68000 is big-endian; WAD data is little-endian, so the LE swaps are
 * the ones that work (rol.w / swap). */

#ifndef ATARI_SDL_ENDIAN_H
#define ATARI_SDL_ENDIAN_H

#include "SDL_stdinc.h"

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_BIG_ENDIAN

static inline Uint16 SDL_Swap16(Uint16 x)
{
    __asm__ ("rol.w #8,%0" : "+d" (x) : : "cc");
    return x;
}

static inline Uint32 SDL_Swap32(Uint32 x)
{
    __asm__ ("rol.w #8,%0\n\t"
             "swap %0\n\t"
             "rol.w #8,%0" : "+d" (x) : : "cc");
    return x;
}

#define SDL_SwapLE16(x) SDL_Swap16(x)
#define SDL_SwapLE32(x) SDL_Swap32(x)
#define SDL_SwapBE16(x) ((Uint16) (x))
#define SDL_SwapBE32(x) ((Uint32) (x))

#endif
