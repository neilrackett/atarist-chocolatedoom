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
//     The profile of a profiling build (gcc 13, PGO=gen in Makefile.atari,
//     after TTDOOM's): written to PGO.DAT when a timedemo ends, as one
//     stream. gcc's own writer wants a file a object with a long name, at
//     exit, which TOS's 8.3 names can't give, so the objects register their
//     counters in a section (-fprofile-info-section) and this writes them;
//     gcov-tool merge-stream makes the .gcda files of it
//     (tools/atari/pgo.sh). Linked only into that build, which sets
//     g_game.c's hook.
//

#include <stdio.h>
#include <stdlib.h>
#include <gcov.h>

extern void (*pgo_dump_hook) (void);

extern const struct gcov_info *const __start_gcov_info[];
extern const struct gcov_info *const __stop_gcov_info[];

static FILE *pgo_file;
static unsigned long pgo_bytes;

static void pgo_write(const void *data, unsigned n, void *arg)
{
    (void) arg;
    fwrite(data, 1, n, pgo_file);
    pgo_bytes += n;
}

static void pgo_filename(const char *name, void *arg)
{
    __gcov_filename_to_gcfn(name, pgo_write, arg);
}

static void *pgo_alloc(unsigned n, void *arg)
{
    (void) arg;
    return malloc(n);
}

static void pgo_dump(void)
{
    const struct gcov_info *const *info = __start_gcov_info;

    pgo_file = fopen("PGO.DAT", "wb");
    if (!pgo_file)
    {
        fprintf(stderr, "PGO: can't create PGO.DAT\n");
        return;
    }
    // Hide where info starts, or gcc may assume two arrays' addresses
    // never meet (gcc's own example does this)
    __asm__ ("" : "+r" (info));
    for ( ; info != __stop_gcov_info; info++)
    {
        __gcov_info_to_gcda(*info, pgo_filename, pgo_write, pgo_alloc, NULL);
    }
    fclose(pgo_file);
    fprintf(stderr, "PGO written: %lu bytes\n", pgo_bytes);
}

__attribute__((constructor)) static void pgo_init(void)
{
    pgo_dump_hook = pgo_dump;
}
