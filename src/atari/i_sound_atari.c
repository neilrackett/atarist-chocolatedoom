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
//     Sound effects on an STE, a TT or a Falcon: STDL's voices (four,
//     mixed into the DMA sound from the VBL, so they keep playing
//     however slowly the frames come). Each effect is converted once to
//     signed 8 bits and kept in the zone, purgeable while no voice plays
//     it. The voices are mono: stereo separation is not heard.
//
//     Also the variables and modules i_sound.c expects from the
//     SDL_mixer, music pack and GUS files the Atari build leaves out, so
//     the config file keeps them.
//

#include <stdio.h>
#include <string.h>
#include <mint/cookie.h>

#include <stdl/stdl_voice.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "i_system.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#define NUM_VOICES STDL_VOICES

// The device's rate: Doom's effects are 11025Hz, and the voices take
// any rate to this one. 12517 costs half what 25033 does.
#define VOICE_RATE 12517

// Stopping the DMA while the voices are quiet avoids the clicks a real
// STE's idle DMA makes now and then (stdl_voice.h): after this many
// updates (tics) with nothing playing.
#define IDLE_UPDATES 70

// An effect as the voices play it, in the zone at sfxinfo->driver_data
typedef struct
{
    unsigned int length;        // frames, at most 65535
    unsigned int rate;          // Hz, from the lump
    int8_t samples[1];
} atari_sfx_t;

typedef struct
{
    int channel;                // Doom's channel, -1 for none
    sfxinfo_t *sfx;
    unsigned int started;       // when, to steal the oldest
} voice_t;

static voice_t voices[NUM_VOICES];
static unsigned int start_count;
static int idle_updates;
static boolean sound_initialized = false;

static boolean voices_open = false;
static boolean use_sfx_prefix;

// i_sound.c's variables from the modules the Atari build leaves out
char *timidity_cfg_path = "";
char *music_pack_path = "";
char *gus_patch_path = "";
int gus_ram_kb = 1024;
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

void I_InitTimidityConfig(void)
{
}

// No music packs: nothing to play them with
static boolean MusicPackInit(void) { return false; }
static void MusicPackNothing(void) { }
static void MusicPackVolume(int volume) { }
static void *MusicPackRegister(void *data, int len) { return NULL; }
static void MusicPackUnRegister(void *handle) { }
static void MusicPackPlay(void *handle, boolean looping) { }
static boolean MusicPackPlaying(void) { return false; }

const music_module_t music_pack_module =
{
    NULL,
    0,
    MusicPackInit,
    MusicPackNothing,
    MusicPackVolume,
    MusicPackNothing,
    MusicPackNothing,
    MusicPackRegister,
    MusicPackUnRegister,
    MusicPackPlay,
    MusicPackNothing,
    MusicPackPlaying,
    NULL,
};

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.

    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    // Doom adds a DS* prefix to sound lumps; Heretic and Hexen don't
    // do this.

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static int I_Atari_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));

    return W_GetNumForName(namebuf);
}

// The effect, converted and held in the zone while it plays
static atari_sfx_t *LockSfx(sfxinfo_t *sfxinfo)
{
    atari_sfx_t *snd;
    byte *data;
    unsigned int lumplen, length, i;
    int lumpnum;

    if (sfxinfo->driver_data != NULL)
    {
        Z_ChangeTag(sfxinfo->driver_data, PU_STATIC);
        return sfxinfo->driver_data;
    }

    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0)
    {
        return NULL;
    }

    data = W_CacheLumpNum(lumpnum, PU_STATIC);
    lumplen = W_LumpLength(lumpnum);

    // As DMX reads it (i_sdlsound.c): a header, and the first and last
    // 16 bytes skipped.

    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    if (length > lumplen - 8 || length <= 48)
    {
        W_ReleaseLumpNum(lumpnum);
        return NULL;
    }

    length -= 32;
    if (length > 65535)
    {
        length = 65535;
    }

    snd = Z_Malloc(sizeof(atari_sfx_t) + length, PU_STATIC,
                   &sfxinfo->driver_data);
    snd->length = length;
    snd->rate = (data[3] << 8) | data[2];

    // Unsigned to signed
    for (i = 0; i < length; ++i)
    {
        snd->samples[i] = (int8_t) (data[24 + i] ^ 0x80);
    }

    W_ReleaseLumpNum(lumpnum);
    return snd;
}

// A voice no longer playing its effect: purgeable, unless another voice
// still plays it
static void ReleaseVoice(int v)
{
    sfxinfo_t *sfx = voices[v].sfx;
    int i;

    voices[v].channel = -1;
    voices[v].sfx = NULL;

    if (sfx == NULL || sfx->driver_data == NULL)
    {
        return;
    }

    for (i = 0; i < NUM_VOICES; ++i)
    {
        if (voices[i].sfx == sfx)
        {
            return;
        }
    }

    Z_ChangeTag(sfx->driver_data, PU_CACHE);
}

static int VoiceForChannel(int channel)
{
    int v;

    for (v = 0; v < NUM_VOICES; ++v)
    {
        if (voices[v].channel == channel)
        {
            return v;
        }
    }

    return -1;
}

// A voice for a new effect: a free one, or the one playing the effect
// that matters least (Doom's priority: the larger, the less), the
// oldest of those.
static int ChooseVoice(void)
{
    int v, best;

    for (v = 0; v < NUM_VOICES; ++v)
    {
        if (voices[v].channel < 0)
        {
            return v;
        }
    }

    best = 0;
    for (v = 1; v < NUM_VOICES; ++v)
    {
        if (voices[v].sfx->priority > voices[best].sfx->priority
         || (voices[v].sfx->priority == voices[best].sfx->priority
          && voices[v].started < voices[best].started))
        {
            best = v;
        }
    }

    return best;
}

// Doom's volume, 0-127, as a voice's, 0-64
static int VoiceVolume(int vol)
{
    if (vol < 0)
    {
        vol = 0;
    }
    else if (vol > 127)
    {
        vol = 127;
    }

    return (vol + 1) >> 1;
}

static void I_Atari_UpdateSoundParams(int handle, int vol, int sep)
{
    int v;

    if (!sound_initialized)
    {
        return;
    }

    v = VoiceForChannel(handle);
    if (v >= 0)
    {
        STDL_SetVoiceVolume(v, VoiceVolume(vol));
    }
}

static int I_Atari_StartSound(sfxinfo_t *sfxinfo, int channel, int vol,
                              int sep, int pitch)
{
    atari_sfx_t *snd;
    unsigned int freq;
    int v;

    if (!sound_initialized || channel < 0)
    {
        return -1;
    }

    // The channel's last effect stops: Doom has picked this channel
    v = VoiceForChannel(channel);
    if (v >= 0)
    {
        STDL_StopVoice(v);
        ReleaseVoice(v);
    }

    snd = LockSfx(sfxinfo);
    if (snd == NULL)
    {
        return -1;
    }

    v = ChooseVoice();
    if (voices[v].channel >= 0)
    {
        STDL_StopVoice(v);
        ReleaseVoice(v);
    }

    voices[v].channel = channel;
    voices[v].sfx = sfxinfo;
    voices[v].started = ++start_count;

    // A pitch shift plays the effect faster or slower, as
    // i_sdlsound.c's lengthens or shortens it: 2 - pitch / NORM_PITCH
    freq = snd->rate;
    if (pitch != NORM_PITCH && pitch > 0 && pitch < 254)
    {
        freq = freq * NORM_PITCH / (254 - pitch);
    }

    STDL_ResumeVoices();
    STDL_SetVoice(v, snd->samples, snd->length, 0, 0, freq,
                  VoiceVolume(vol));
    idle_updates = 0;

    return channel;
}

static void I_Atari_StopSound(int handle)
{
    int v;

    if (!sound_initialized)
    {
        return;
    }

    v = VoiceForChannel(handle);
    if (v >= 0)
    {
        STDL_StopVoice(v);
        ReleaseVoice(v);
    }
}

static boolean I_Atari_SoundIsPlaying(int handle)
{
    int v;

    if (!sound_initialized)
    {
        return false;
    }

    v = VoiceForChannel(handle);
    return v >= 0 && STDL_VoiceActive(v);
}

static void I_Atari_UpdateSound(void)
{
    int v, busy = 0;

    if (!sound_initialized || !voices_open)
    {
        return;
    }

    // Effects that ended: their data may go
    for (v = 0; v < NUM_VOICES; ++v)
    {
        if (voices[v].channel >= 0)
        {
            if (STDL_VoiceActive(v))
            {
                busy = 1;
            }
            else
            {
                ReleaseVoice(v);
            }
        }
    }

    if (!busy && ++idle_updates == IDLE_UPDATES)
    {
        STDL_PauseVoices();
    }
}

// Effects are loaded as they are first played: only their lumps are
// found here, which costs no disk reads at start-up.
static void I_Atari_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    char namebuf[9];
    int i;

    for (i = 0; i < num_sounds; ++i)
    {
        GetSfxLumpName(&sounds[i], namebuf, sizeof(namebuf));
        sounds[i].lumpnum = W_CheckNumForName(namebuf);
    }
}

static void I_Atari_ShutdownSound(void)
{
    if (!sound_initialized)
    {
        return;
    }

    if (voices_open)
    {
        STDL_CloseVoices();
        voices_open = false;
    }
    sound_initialized = false;
}

static boolean I_Atari_InitSound(GameMission_t mission)
{
    int v;

    use_sfx_prefix = (mission == doom || mission == strife);

    for (v = 0; v < NUM_VOICES; ++v)
    {
        voices[v].channel = -1;
        voices[v].sfx = NULL;
    }

    // An ST without DMA sound (the _SND cookie's bit 1) has no effects
    {
        long snd = 0;

        if (Getcookie(C__SND, &snd) != C_FOUND || (snd & 2) == 0)
        {
            printf("I_Atari_InitSound: no DMA sound for the effects\n");
            return false;
        }
    }

    if (STDL_OpenVoices(VOICE_RATE) < 0)
    {
        printf("I_Atari_InitSound: the voices would not open\n");
        return false;
    }
    voices_open = true;

    sound_initialized = true;
    return true;
}

static const snddevice_t sound_atari_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

const sound_module_t sound_atari_module =
{
    sound_atari_devices,
    arrlen(sound_atari_devices),
    I_Atari_InitSound,
    I_Atari_ShutdownSound,
    I_Atari_GetSfxLumpNum,
    I_Atari_UpdateSound,
    I_Atari_UpdateSoundParams,
    I_Atari_StartSound,
    I_Atari_StopSound,
    I_Atari_SoundIsPlaying,
    I_Atari_PrecacheSounds,
};
