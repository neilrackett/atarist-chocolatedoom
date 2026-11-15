/* SDL_joystick.h for the Atari build: the add-on's, plus the GUID calls
 * Chocolate Doom keeps a configured joystick by. STDL's joysticks have
 * no hardware identity, so a joystick's GUID is its index: "STDL" and
 * the index, which keeps i_joystick.c's look-up and the config file's
 * joystick_guid working unchanged. */

#ifndef ATARI_SDL_JOYSTICK_H
#define ATARI_SDL_JOYSTICK_H

#include_next "SDL_joystick.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    Uint8 data[16];
} SDL_JoystickGUID;

static inline SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int device_index)
{
    SDL_JoystickGUID guid;

    memset(&guid, 0, sizeof(guid));
    memcpy(guid.data, "STDL", 4);
    guid.data[4] = (Uint8) device_index;
    return guid;
}

static inline void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid,
                                             char *buf, int size)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 16 && 2 * i + 2 < size; ++i)
    {
        buf[2 * i] = hex[guid.data[i] >> 4];
        buf[2 * i + 1] = hex[guid.data[i] & 15];
    }
    buf[2 * i] = '\0';
}

static inline SDL_JoystickGUID SDL_JoystickGetGUIDFromString(const char *s)
{
    SDL_JoystickGUID guid;
    int i, hi, lo;

    memset(&guid, 0, sizeof(guid));
    for (i = 0; i < 16 && s[0] != '\0' && s[1] != '\0'; ++i, s += 2)
    {
        hi = s[0] <= '9' ? s[0] - '0' : (s[0] | 0x20) - 'a' + 10;
        lo = s[1] <= '9' ? s[1] - '0' : (s[1] | 0x20) - 'a' + 10;
        guid.data[i] = (Uint8) (hi << 4 | lo);
    }
    return guid;
}

#endif
