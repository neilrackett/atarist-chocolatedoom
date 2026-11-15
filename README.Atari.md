# Chocolate Doom for the Atari ST, TT and Falcon

Chocolate Doom 3.1.1 on Atari machines, through the SDL2 add-on of
[STDL](https://github.com/neilrackett/atarist-stdl) (the submodule in
`extern/stdl`). Only Doom is ported; Heretic, Hexen, Strife and the setup
program are not built.

| Program | For | Code |
| --- | --- | --- |
| `CHOCTT.TOS` | a TT or a Falcon (any 68020 to 68060) | 68030 |
| `CHOCST.TOS` | every Atari: an ST, an STE, a TT, a Falcon | 68000 |

`CHOCTT.TOS` is the one to play: a TT with 2 MB of ST-RAM and 4 MB of
TT-RAM is the target. `CHOCST.TOS` shows on a TT or a Falcon what
`CHOCTT.TOS` does, more slowly. On an ST it has STDL's 16-colour screen,
which is for trying it out, not for playing: a frame takes seconds.

## Running it

Put the program beside an IWAD (`DOOM1.WAD`, `DOOM.WAD`, `DOOM2.WAD`,
`TNT.WAD`, `PLUTONIA.WAD`) and run it. The configuration
(`DEFAULT.CFG`, Doom's own, and `CHOCDOOM.CFG`, Chocolate Doom's extras)
and the savegames are written beside it, as DOS Doom writes them.

Command-line options: rename the program to `.TTP`, or put them in a file
`ARGS.TXT` beside it, which is read when there are none (a `.TOS` started
from the desktop has none). `-file`, `-deh`, `-warp`, `-skill`,
`-timedemo`, `-playdemo`, `-record` and the rest work as on a PC.

## What it does

- **The screen.** Doom's 320x200 in 256 colours, 1:1, centred on a TT's
  or a Falcon's 240 rows. `aspect_ratio_correct 1` in `CHOCDOOM.CFG`
  stretches it to 4:3 instead, each fifth row twice, as a VGA monitor
  did: 6% slower, for a fifth more rows to convert. Only what changed
  since the last frame is converted to the screen's planes: the 3D view,
  and the status bar, menu and messages when they change. The bands
  above and below stay black through the damage and pickup palettes
  (colour 0 is kept black, unless `vga_porch_flash 1`).
- **Game speed.** Up to a second of tics is run before each frame (on a
  PC, 3), so the game keeps its speed down to 1 frame a second, with
  fewer frames, rather than slowing down.
- **Low detail** (F5) works, and is the faster way to play.
- **Effects** play on the DMA sound (STE, TT, Falcon), on STDL's four
  voices, mixed from the VBL so they keep playing however slowly the
  frames come. They are mono: stereo separation is not heard. A plain ST
  has no DMA sound and no effects.
- **Music** is Chocolate Doom's OPL music (the GENMIDI instruments,
  `snd_musicdevice 3`), played on the YM2149 through STDL's OPL mapping:
  a three-voice square-wave cover of the FM music, on every machine. It
  runs from the VBL, so it keeps its tempo too.
- **Controls**: the keyboard, the mouse, and the joystick port (or an
  Xpad pad), which is on from the start as a game controller.
- **The game is exact.** Demos stay in sync: every optimisation below was
  checked against a build of the same source for the PC (see "Testing").

Not available: network games, the setup program, music packs, the PC
speaker, and ENDOOM's colours (its text is printed on the desktop when an
80-column console is there).

## Speed

Hatari, a TT (32 MHz, 2 MB + 4 MB TT-RAM), the first 600 tics of demo1,
ms a frame:

| | size 9 (default) | size 10 |
| --- | --- | --- |
| high detail | 121 | 140 |
| low detail | 86 | 99 |

In real play the game logic runs too, every tic: 9-11 ms a tic here
(`-nodraw`), so at full speed about a third of Hatari's TT's time. At a
real TT's pace (`-playdemo demo3 -benchtics 600 -benchpace 190`) the game
runs at about 90% of its speed, at 1.1 frames a second in low detail and
0.9 in high: the game logic, still id's C, is most of each tic's time
there, where TTDOOM's was cut to two thirds.

A real TT takes about twice Hatari's time over a game like this
(TTDOOM's measurements); a Falcon about twice a TT's. `CHOCST.TOS`
takes about twice `CHOCTT.TOS`'s time on a TT (68000 code, and none of
the assembler drawers), and an 8 MHz STE about 3 seconds a frame.

Where it comes from, against the port as first built (high detail,
size 9: 201 ms):

- `FixedMul` and `FixedDiv` as one inline `muls.l` and `divs.l` on a
  68020 to 68040 (gcc made the C's 64-bit arithmetic library calls), and
  STDOOM's 16-bit versions on a 68000: 17%.
- The column, span and fuzz drawers in assembler on a 68020
  (`src/atari/r_draw_m68k.c`), after TTDOOM's: the column four
  instructions a pixel, the span vanilla's packed position exactly; low
  detail stores pixel pairs from word colormaps: 22%.
- The screen: only marked boxes converted, and 1:1 by default.

## Building

```
git submodule update --init --recursive extern/stdl
make -f Makefile.atari          # dist-atari/CHOCTT.TOS and CHOCST.TOS
make -f Makefile.atari tt       # or one of them
```

Run on the host, `Makefile.atari` runs itself in
[atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker)
through `stcmd` (gcc 4.6.4, which STDL is built and tested with), and
builds STDL's archives there too. `build-atari/` keeps the programs with
their symbols, for Hatari's debugger and profiler.

The Atari code is in `src/atari/` and under `#ifdef ATARI` in the shared
sources:

| File | What |
| --- | --- |
| `i_atari_start.c` | `main`: the CPU check (68000 code in both builds), `ARGS.TXT`, the fixed-point routines' choice |
| `i_atari.c` | the machine, the zone's size from the free memory |
| `i_video_atari.c` | the screen: `i_video.c`'s interface on the add-on |
| `i_sound_atari.c` | effects on STDL's voices |
| `opl_atari.c` | an OPL driver that plays on the YM2149 |
| `r_draw_m68k.c` | the 68020 drawers |
| `i_endoom_atari.c`, `net_atari.c` | ENDOOM as text; no network |
| `compat/` | the few SDL2 calls the add-on lacks (`SDL_endian.h`, joystick GUIDs) |

## Testing

`tools/atari/run.sh` runs a program under Hatari from a drive of its
own, with the IWAD, and plays a script against Hatari's command fifo
(screenshots, keys, profiling); `MACHINE=tt` (2 MB + 4 MB TT-RAM),
`tt4`, `falcon`, `ste` or `st`. `tools/atari/profagg.py` sums a profile
per function.

Two options, for the Atari build and for a PC build made with
`-DCHOC_BENCH` (`cmake -DCMAKE_C_FLAGS=-DCHOC_BENCH`), check that nothing
changed: `-timedemo demoN -benchtics N` ends the demo at tic N, and its
result gives a hash of the game's state (every thing's position, angle,
momentum, health and type, and the random number index); `-framehash`
adds a hash of every frame drawn. A PC build's hashes are the reference:

```
ARGS="-timedemo demo1 -benchtics 600 -framehash" \
    tools/atari/run.sh d1 dist-atari/CHOCTT.TOS "waitfor realtics"
```

With DOOM1.WAD 1.9 and the default settings, 600 tics: demo1 state
`5d883da0` frames `3edfdd5a`, demo2 `a32fc838` / `414f485a`, demo3
`7f557ea2` / `4d508737` (an Atari build draws one pixel of one frame of
demo3 differently, where Doom reads past a column's end into memory that
differs from machine to machine). The whole of demo1: state `565a4537`.
