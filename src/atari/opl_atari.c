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
//     An OPL driver for the opl library that plays on the YM2149: each
//     register write goes to STDL_OplWrite, which keeps the OPL2's nine
//     channels and plays them as notes on the sound chip, a square-wave
//     cover of the FM music. i_oplmusic.c, its GENMIDI instruments and
//     its MIDI timing, works unchanged on top.
//
//     The library's timer callbacks (the music's events) run from a VBL
//     callback, on STDL's 5ms clock, so the music keeps its tempo
//     however slowly the frames come. The main line's locks, writes and
//     queue changes keep the VBL out while they work, by a count it
//     checks; a tick it skips is made up at the next.
//

#include <stdlib.h>

#include <stdl/stdl_opl.h>
#include <stdl/stdl_time.h>
#include <stdl/stdl_vbl.h>

#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"

static opl_callback_queue_t *callback_queue;

// The music's clock, in microseconds: it stops while paused
static uint64_t current_time;
static uint32_t last_ticks;
static volatile int paused;

// Main-line work under way (a lock, a write, a queue change): the VBL
// leaves the queue and the chip alone while it is non-zero.
static volatile int lock_depth;

// Inside the VBL's callbacks, whose own calls need no lock
static volatile int in_tick;

// The register the next data write goes to
static unsigned int register_num;

// Timer 1 and 2 started (register 4): what OPL_Detect looks for in the
// status register, which the chip sets once a timer runs out
static unsigned int timers_started;

static void Enter(void)
{
    if (!in_tick)
    {
        ++lock_depth;
    }
}

static void Leave(void)
{
    if (!in_tick)
    {
        --lock_depth;
    }
}

// The VBL: the time since the last tick that ran, and every callback
// due by it
static void OPL_Atari_Tick(void)
{
    uint32_t now;
    opl_callback_t callback;
    void *callback_data;

    if (lock_depth != 0)
    {
        return;
    }

    now = STDL_GetTicks();
    if (!paused)
    {
        current_time += (uint64_t) (now - last_ticks) * 1000;
    }
    last_ticks = now;

    if (paused)
    {
        return;
    }

    in_tick = 1;

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue))
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        callback(callback_data);
    }

    in_tick = 0;
}

// The VBL's own stack: the callbacks' MIDI event chain (and libgcc's
// 64-bit arithmetic) is deeper than TOS's supervisor stack has room for
// on an ST.
#define VBL_STACK 8192
static long vbl_stack[VBL_STACK / sizeof(long)];

static void OPL_Atari_VBL(void)
{
    __asm__ __volatile__ (
        "move.l	%%sp,%%d0		\n\t"
        "move.l	%0,%%sp			\n\t"
        "move.l	%%d0,-(%%sp)		\n\t"
        "jsr	(%1)			\n\t"
        "move.l	(%%sp)+,%%sp		\n\t"
        :
        : "a" (vbl_stack + VBL_STACK / sizeof(long)),
          "a" (OPL_Atari_Tick)
        : "d0", "d1", "a0", "a1", "cc", "memory");
}

static int OPL_Atari_Init(unsigned int port_base)
{
    callback_queue = OPL_Queue_Create();
    current_time = 0;
    paused = 0;
    lock_depth = 0;
    in_tick = 0;
    timers_started = 0;

    // From the main line, before the VBL may write (stdl_opl.h)
    STDL_OplReset();

    last_ticks = STDL_GetTicks();
    if (STDL_AddVBL(OPL_Atari_VBL) < 0)
    {
        OPL_Queue_Destroy(callback_queue);
        return 0;
    }

    return 1;
}

static void OPL_Atari_Shutdown(void)
{
    STDL_RemoveVBL(OPL_Atari_VBL);
    STDL_OplReset();
    OPL_Queue_Destroy(callback_queue);
}

// The status register: an OPL2's low bits read 6 (an OPL3's 0, which
// would put the music into OPL3 mode, the second register bank, which
// STDL_OplWrite does not have), and the timers' bits once started.
static unsigned int OPL_Atari_PortRead(opl_port_t port)
{
    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    return 0x06 | (timers_started ? 0xc0 : 0x00);
}

static void OPL_Atari_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_DATA_PORT)
    {
        if (register_num == OPL_REG_TIMER_CTRL)
        {
            if (value & 0x80)
            {
                timers_started = 0;
            }
            else
            {
                timers_started = value & 0x03;
            }
        }

        Enter();
        STDL_OplWrite((uint8_t) register_num, (uint8_t) value);
        Leave();
    }
}

static void OPL_Atari_SetCallback(uint64_t us, opl_callback_t callback,
                                  void *data)
{
    Enter();
    OPL_Queue_Push(callback_queue, callback, data, current_time + us);
    Leave();
}

static void OPL_Atari_ClearCallbacks(void)
{
    Enter();
    OPL_Queue_Clear(callback_queue);
    Leave();
}

static void OPL_Atari_Lock(void)
{
    Enter();
}

static void OPL_Atari_Unlock(void)
{
    Leave();
}

static void OPL_Atari_SetPaused(int p)
{
    paused = p;
}

static void OPL_Atari_AdjustCallbacks(float factor)
{
    Enter();
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    Leave();
}

opl_driver_t opl_atari_driver =
{
    "Atari",
    OPL_Atari_Init,
    OPL_Atari_Shutdown,
    OPL_Atari_PortRead,
    OPL_Atari_PortWrite,
    OPL_Atari_SetCallback,
    OPL_Atari_ClearCallbacks,
    OPL_Atari_Lock,
    OPL_Atari_Unlock,
    OPL_Atari_SetPaused,
    OPL_Atari_AdjustCallbacks,
};
