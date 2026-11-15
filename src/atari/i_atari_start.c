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
//     The Atari program's entry. Compiled for the 68000 in both builds
//     (Makefile.atari), so the TT and Falcon build can say it needs a
//     68020 on a machine without one, before any of its 68020 code runs.
//

#include <stdio.h>
#include <stdlib.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#include "asmsym.h"

// i_main.c's main, renamed under ATARI
int I_ChocolateMain(int argc, char **argv);

// m_fixed.c: the fixed-point multiply and divide for this CPU
void M_InitFixed(void);

// MiNTLib: the size of the blocks malloc takes from GEMDOS
void _mallocChunkSize(size_t size);

// A program started from the desktop as a .TOS has no arguments, and
// Hatari can't give an autostarted one any: with none, they are read
// from ARGS.TXT beside it, if there is one (words split at white space).
#define MAX_FILE_ARGS 64

static char args_text[1024];
static char *file_argv[MAX_FILE_ARGS + 1];

static int ReadArgsFile(char *argv0, char ***argv)
{
    FILE *f;
    size_t n;
    char *p;
    int argc;

    f = fopen("ARGS.TXT", "r");
    if (f == NULL)
    {
        return 0;
    }
    n = fread(args_text, 1, sizeof(args_text) - 1, f);
    fclose(f);
    args_text[n] = '\0';

    file_argv[0] = argv0;
    argc = 1;
    p = args_text;
    while (argc < MAX_FILE_ARGS)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        {
            ++p;
        }
        if (*p == '\0')
        {
            break;
        }
        file_argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r'
            && *p != '\n')
        {
            ++p;
        }
        if (*p != '\0')
        {
            *p++ = '\0';
        }
    }
    file_argv[argc] = NULL;
    *argv = file_argv;
    return argc;
}

int main(int argc, char **argv)
{
    int n;

    // GEMDOS before TOS 2 can hold only so many blocks, and MiNTLib's
    // malloc takes small ones: an STE with 400K free failed an 11K
    // realloc (a song's MIDI events). Fewer, larger blocks.
    _mallocChunkSize(64 * 1024L);

#ifdef ATARI_NEEDS_68020
    long cpu = 0;

    if (Getcookie(C__CPU, &cpu) != C_FOUND || cpu < 20)
    {
        Cconws("\r\n" ATARI_PROG "_TT.TOS is for a TT or a Falcon: it needs"
               "\r\na 68020 or better. " ATARI_PROG "_ST.TOS runs on every"
               " Atari.\r\n"
               "\r\nPress any key.\r\n");
        Cconin();
        return 1;
    }
#endif

    if (argc <= 1 && (n = ReadArgsFile(argc > 0 ? argv[0] : "", &argv)) > 1)
    {
        argc = n;
    }

    M_InitFixed();

    // MiNTLib puts the stack a multiple of 4 above the basepage, which
    // GEMDOS places only on an even address: a stack 2 bytes off a long
    // makes a 68030 read and write every long on it in two bus cycles
    // (TTDOOM measured that half of a misaligned program's cost). On a
    // long, then, with the game below it.
    {
        register int ret __asm__("d0");
        register int a_argc __asm__("d1") = argc;
        register char **a_argv __asm__("a0") = argv;

        __asm__ __volatile__ (
            "move.l	%%sp,%%a1		\n\t"
            "move.l	%%sp,%%d2		\n\t"
            "and.w	#-4,%%d2		\n\t"
            "move.l	%%d2,%%sp		\n\t"
            "move.l	%%a1,-(%%sp)		\n\t"   // the old stack
            "move.l	%%a0,-(%%sp)		\n\t"
            "move.l	%%d1,-(%%sp)		\n\t"
            "jsr	" ASM_SYM(I_ChocolateMain) "	\n\t"
            "addq.l	#8,%%sp			\n\t"
            "move.l	(%%sp),%%sp		\n\t"
            : "=r" (ret), "+r" (a_argc), "+r" (a_argv)
            :
            : "d2", "a1", "cc", "memory");
        return ret;
    }
}
