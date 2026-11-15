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
//     A C name as the assembler sees it: "_name" with gcc 4.6.4 (a.out),
//     "name" with gcc 13 (m68k-atari-mintelf, ELF), for top-level asm.
//

#ifndef ATARI_ASMSYM_H
#define ATARI_ASMSYM_H

#define ASM_STR2(x) #x
#define ASM_STR(x) ASM_STR2(x)
#define ASM_SYM(name) ASM_STR(__USER_LABEL_PREFIX__) #name

#endif
