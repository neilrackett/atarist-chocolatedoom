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
//     The program on a long (TTDOOM's atari_align.c, the code that stays).
//
//     GEMDOS keeps blocks only even, so TOS can load a program 2 bytes off
//     a long (TOS 3.06 does, on a TT), and then every long a 68030 reads
//     from its data takes two bus cycles: 2.6% of TTDOOM's frame on a real
//     TT. So a program that finds itself 2 bytes off moves up 2 bytes: all
//     of it but this code, which is the first thing in the text and stays
//     where it is, with a 2-byte gap behind it.
//
//     Moving needs every address TOS relocated put right, and TOS doesn't
//     keep the relocation table: it is read again from the program's file
//     (ATARI_ALIGN_NAME, .TOS or .TTP, in the current folder, with this
//     program's sizes in its header) into the free memory above the
//     program. An address that points past this code gets 2 more, then
//     the rest of the text and the data move up, and the basepage is told
//     (the text 2 longer, the data and the BSS 2 on).
//
//     It is done before anything else runs: atari_align_entry is the
//     program's entry (the link's -e), ahead of the C library's _start,
//     linked ahead of crt0.o by a specs file (Makefile.atari, which checks
//     that atari_align_front comes first). Without the file, with -noalign
//     on the command line, or on anything but a 68020 or 68030, the
//     program runs where it is. The stack is i_atari_start.c's main's.
//


#include "asmsym.h"

__asm__ (
"	.text\n"
"	.balign	4\n"
"	.globl	atari_align_entry\n"
"	.globl	" ASM_SYM(atari_align_front) "\n"
"	.globl	" ASM_SYM(atari_align_end) "\n"
"	.globl	" ASM_SYM(atari_align_moved) "\n"
ASM_SYM(atari_align_front) ":\n"
ASM_SYM(atari_align_moved) ":\n"	// how far the program moved: 0 or 2
"	.long	0\n"
".Lalign_cpuv:\n"			// the _CPU cookie
"	.long	0\n"
".Lalign_hdr:\n"			// the file's header: magic, text,
"	.space	28\n"			// data, bss, symbols, long, flags,
".Lalign_noarg:\n"			// no-relocation word
"	.ascii	\"-noalign\"\n"
".Lalign_path:\n"
"	.asciz	\"" ATARI_ALIGN_NAME ".TOS\"\n"
".Lalign_path2:\n"
"	.asciz	\"" ATARI_ALIGN_NAME ".TTP\"\n"
"	.even\n"

"atari_align_entry:\n"
"	move.l	4(%sp),%a1\n"
"	moveq	#3,%d0\n"
"	and.l	8(%a1),%d0\n"			// the text: on a long?
"	jbeq	.Lalign_go\n"
"	move.l	%a0,%d0\n"			// a desk accessory
"	jbne	.Lalign_go\n"
"	move.l	%a1,%a5\n"

// -noalign on the command line?
"	lea	0x80(%a5),%a0\n"
"	moveq	#0,%d1\n"
"	move.b	(%a0)+,%d1\n"
".Lalign_arg:\n"
"	subq.w	#8,%d1\n"
"	jbmi	.Lalign_cpu\n"
"	move.l	(%a0),%d0\n"
"	cmp.l	.Lalign_noarg(%pc),%d0\n"
"	jbne	.Lalign_arg1\n"
"	move.l	4(%a0),%d0\n"
"	cmp.l	.Lalign_noarg+4(%pc),%d0\n"
"	jbeq	.Lalign_asis\n"
".Lalign_arg1:\n"
"	addq.l	#1,%a0\n"
"	addq.w	#7,%d1\n"
"	jbra	.Lalign_arg\n"

// A 68020 or 68030?
".Lalign_cpu:\n"
"	pea	.Lalign_cookie(%pc)\n"
"	move.w	#38,-(%sp)\n"			// Supexec
"	trap	#14\n"
"	addq.l	#6,%sp\n"
"	move.l	.Lalign_cpuv(%pc),%d0\n"
"	moveq	#30,%d1\n"
"	cmp.l	%d1,%d0\n"
"	jbeq	.Lalign_file\n"
"	moveq	#20,%d1\n"
"	cmp.l	%d1,%d0\n"
"	jbne	.Lalign_asis\n"

// The program's file: the program's .TOS or .TTP here, if it is this program
".Lalign_file:\n"
"	lea	.Lalign_path(%pc),%a6\n"
"	jbsr	.Lalign_read\n"
"	jbeq	.Lalign_shift\n"
"	lea	.Lalign_path2(%pc),%a6\n"
"	jbsr	.Lalign_read\n"
"	jbeq	.Lalign_shift\n"
".Lalign_asis:\n"
"	sub.l	%a0,%a0\n"			// as TOS started us
".Lalign_go:\n"				// (the one relocated address
"	jmp	" ASM_SYM(_start) "\n"		// here: moved, it is 2 on)

// The addresses first: once to see that the table is whole, once to
// change them
".Lalign_shift:\n"
"	move.l	8(%a5),%a3\n"
"	lea	" ASM_SYM(atari_align_end) "(%pc),%a2\n"
"	move.l	%a2,%d5\n"
"	sub.l	%a3,%d5\n"
"	move.l	0xc(%a5),%d4\n"
"	add.l	0x14(%a5),%d4\n"
"	moveq	#0,%d2\n"
"	jbsr	.Lalign_walk\n"
"	jbne	.Lalign_asis\n"
"	moveq	#1,%d2\n"
"	jbsr	.Lalign_walk\n"

// then the text from a2 and the data, from the top down
"	move.l	%a3,%a0\n"
"	add.l	%d4,%a0\n"
"	lea	2(%a0),%a1\n"
"	move.l	%a0,%d0\n"
"	sub.l	%a2,%d0\n"
"	btst	#1,%d0\n"
"	jbeq	.Lalign_longs\n"
"	move.w	-(%a0),-(%a1)\n"
".Lalign_longs:\n"
"	lsr.l	#2,%d0\n"
"	jbeq	.Lalign_moved\n"
".Lalign_long:\n"
"	move.l	-(%a0),-(%a1)\n"
"	subq.l	#1,%d0\n"
"	jbne	.Lalign_long\n"
".Lalign_moved:\n"
"	move.l	%a3,%a0\n"			// the BSS's last 2 bytes are
"	add.l	%d4,%a0\n"			// new to it
"	add.l	0x1c(%a5),%a0\n"
"	clr.w	(%a0)\n"
"	addq.l	#2,0xc(%a5)\n"			// the text's size (the gap),
"	addq.l	#2,0x10(%a5)\n"			// the data
"	addq.l	#2,0x18(%a5)\n"			// and the BSS
"	lea	" ASM_SYM(atari_align_moved) "(%pc),%a0\n"
"	moveq	#2,%d0\n"
"	move.l	%d0,(%a0)\n"
"	jbra	.Lalign_asis\n"

// The _CPU cookie's value to .Lalign_cpuv (supervisor mode): 0 with no
// cookie or no jar
".Lalign_cookie:\n"
"	move.l	0x5a0.w,%d0\n"
"	jbeq	.Lalign_cookie2\n"
"	move.l	%d0,%a0\n"
".Lalign_cookie1:\n"
"	move.l	(%a0)+,%d1\n"
"	move.l	(%a0)+,%d0\n"
"	cmp.l	#0x5f435055,%d1\n"		// _CPU
"	jbeq	.Lalign_cookie2\n"
"	tst.l	%d1\n"
"	jbne	.Lalign_cookie1\n"
"	moveq	#0,%d0\n"
".Lalign_cookie2:\n"
"	lea	.Lalign_cpuv(%pc),%a0\n"
"	move.l	%d0,(%a0)\n"
"	rts\n"

// The relocation table of the file a6 names to a4, above the BSS, d7
// bytes of it (the file's rest, or what there is room for), if the file
// is this program: the sizes in its header ours. Z if so.
".Lalign_read:\n"
"	clr.w	-(%sp)\n"
"	move.l	%a6,-(%sp)\n"
"	move.w	#0x3d,-(%sp)\n"			// Fopen
"	trap	#1\n"
"	addq.l	#8,%sp\n"
"	move.l	%d0,%d3\n"
"	jbmi	.Lalign_read2\n"
"	moveq	#0,%d7\n"
"	pea	.Lalign_hdr(%pc)\n"
"	move.l	#28,-(%sp)\n"
"	move.w	%d3,-(%sp)\n"
"	move.w	#0x3f,-(%sp)\n"			// Fread
"	trap	#1\n"
"	lea	12(%sp),%sp\n"
"	moveq	#28,%d1\n"
"	cmp.l	%d1,%d0\n"
"	jbne	.Lalign_read1\n"
"	lea	.Lalign_hdr(%pc),%a0\n"
"	cmp.w	#0x601a,(%a0)+\n"
"	jbne	.Lalign_read1\n"
"	move.l	(%a0)+,%d4\n"
"	cmp.l	0xc(%a5),%d4\n"			// text,
"	jbne	.Lalign_read1\n"
"	move.l	(%a0)+,%d0\n"
"	cmp.l	0x14(%a5),%d0\n"		// data
"	jbne	.Lalign_read1\n"
"	add.l	%d0,%d4\n"
"	move.l	(%a0)+,%d0\n"
"	cmp.l	0x1c(%a5),%d0\n"		// and bss
"	jbne	.Lalign_read1\n"
"	add.l	(%a0)+,%d4\n"			// the symbols
"	tst.w	8(%a0)\n"			// relocated at all?
"	jbne	.Lalign_read1\n"
"	add.l	%d1,%d4\n"			// the table's place in the file
"	clr.w	-(%sp)\n"
"	move.w	%d3,-(%sp)\n"
"	move.l	%d4,-(%sp)\n"
"	move.w	#0x42,-(%sp)\n"			// Fseek
"	trap	#1\n"
"	lea	10(%sp),%sp\n"
"	cmp.l	%d4,%d0\n"
"	jbne	.Lalign_read1\n"
"	move.l	0x18(%a5),%d0\n"		// past the BSS as it will be,
"	add.l	0x1c(%a5),%d0\n"		// on a long
"	addq.l	#5,%d0\n"
"	and.w	#0xfffc,%d0\n"
"	move.l	%d0,%a4\n"
"	move.l	4(%a5),%d6\n"			// up to 4K short of our stack
"	sub.l	%a4,%d6\n"
"	sub.l	#4096,%d6\n"
"	jble	.Lalign_read1\n"
"	move.l	%a4,-(%sp)\n"
"	move.l	%d6,-(%sp)\n"
"	move.w	%d3,-(%sp)\n"
"	move.w	#0x3f,-(%sp)\n"			// Fread
"	trap	#1\n"
"	lea	12(%sp),%sp\n"
"	move.l	%d0,%d7\n"
".Lalign_read1:\n"
"	move.w	%d3,-(%sp)\n"
"	move.w	#0x3e,-(%sp)\n"			// Fclose
"	trap	#1\n"
"	addq.l	#4,%sp\n"
"	moveq	#4,%d0\n"			// the first offset and an end
"	cmp.l	%d0,%d7\n"
"	jble	.Lalign_read2\n"
"	moveq	#0,%d0\n"
"	rts\n"
".Lalign_read2:\n"
"	moveq	#1,%d0\n"
"	rts\n"

// Walk the table: a long, the first relocated long's offset in the text
// (0: none), then a byte for each further one, the distance on from the
// last (1: 254 on and none there), 0 the end. d2 = 0: only see that it
// ends within what was read and stays within the text and data (Z if
// so). Else add 2 to each relocated long that points to a2 or beyond.
".Lalign_walk:\n"
"	move.l	%a4,%a0\n"
"	lea	(%a4,%d7.l),%a6\n"
"	move.l	%a3,%a1\n"
"	move.l	(%a0)+,%d0\n"
"	jbeq	.Lalign_walk3\n"
".Lalign_walk1:\n"
"	add.l	%d0,%a1\n"
"	move.l	%a1,%d1\n"
"	sub.l	%a3,%d1\n"
"	btst	#0,%d1\n"
"	jbne	.Lalign_walk4\n"
"	cmp.l	%d4,%d1\n"
"	jbcc	.Lalign_walk4\n"
"	tst.w	%d2\n"
"	jbeq	.Lalign_walk2\n"
"	move.l	(%a1),%d1\n"
"	sub.l	%a3,%d1\n"
"	cmp.l	%d5,%d1\n"			// into what stays
"	jbcs	.Lalign_walk2\n"
"	addq.l	#2,(%a1)\n"
".Lalign_walk2:\n"
"	cmp.l	%a6,%a0\n"
"	jbcc	.Lalign_walk4\n"
"	moveq	#0,%d0\n"
"	move.b	(%a0)+,%d0\n"
"	jbeq	.Lalign_walk3\n"
"	cmp.b	#1,%d0\n"
"	jbne	.Lalign_walk1\n"
"	lea	254(%a1),%a1\n"
"	jbra	.Lalign_walk2\n"
".Lalign_walk3:\n"
"	moveq	#0,%d0\n"
"	rts\n"
".Lalign_walk4:\n"
"	moveq	#1,%d0\n"
"	rts\n"
"	.balign	4\n"
ASM_SYM(atari_align_end) ":\n"
);

