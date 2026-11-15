/* SDL_gamecontroller.h for the Atari build: the add-on's, plus the
 * controller type Chocolate Doom records (STDL's are all one kind). */

#ifndef ATARI_SDL_GAMECONTROLLER_H
#define ATARI_SDL_GAMECONTROLLER_H

#include_next "SDL_gamecontroller.h"
#include "SDL_joystick.h"

typedef enum
{
    SDL_CONTROLLER_TYPE_UNKNOWN = 0
} SDL_GameControllerType;

static inline SDL_GameControllerType SDL_GameControllerTypeForIndex(int i)
{
    (void) i;
    return SDL_CONTROLLER_TYPE_UNKNOWN;
}

#endif
