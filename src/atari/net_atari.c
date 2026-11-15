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
//     The network game's waiting room (net_gui.c, which needs the
//     textscreen library) and its server (net_server.c), neither of
//     which the Atari build has: it has no network (DISABLE_SDL2NET).
//     The server's tables alone took 450K of BSS, which on a TT with
//     4 MB of TT-RAM is the zone's.
//

#include "doomtype.h"
#include "i_system.h"
#include "net_defs.h"
#include "net_gui.h"
#include "net_server.h"
#include "net_dedicated.h"

void NET_WaitForLaunch(void)
{
    I_Error("Network games are not available on an Atari.");
}

void NET_SV_Init(void)
{
    I_Error("Network games are not available on an Atari.");
}

void NET_SV_Run(void)
{
}

void NET_SV_Shutdown(void)
{
}

void NET_SV_AddModule(net_module_t *module)
{
}

void NET_SV_RegisterWithMaster(void)
{
}

void NET_DedicatedServer(void)
{
    NET_SV_Init();
}
