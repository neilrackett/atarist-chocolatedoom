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
- **Low detail** (F5) works, is the faster way to play, and is the
  default here (`detaillevel 0` in `DEFAULT.CFG` for high). Its 3D view
  is drawn in pixel pairs, and STDL converts it as pairs, 56
  instructions a group of 16 pixels against 74; anything drawn over it
  (messages, the menu) is converted whole.
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
  So is the picture but for the floors and ceilings, which keep 16 bits
  of texture fraction where vanilla keeps 10 (ADoom's span, as TTDOOM's):
  a pixel in a hundred or so lands on the next texel.

Not available: network games, the setup program, music packs, the PC
speaker, and ENDOOM's colours (its text is printed on the desktop when an
80-column console is there).

## Speed

Hatari, a TT (32 MHz, 2 MB + 4 MB TT-RAM), the first 600 tics of demo1,
ms a frame:

| | size 9 (default) | size 10 |
| --- | --- | --- |
| high detail | 91 | 106 |
| low detail (default) | 65 | 75 |

In real play the game logic runs too, every tic: 4.7-6.1 ms a tic here
(whole demos 1 to 3, `-nodraw`; 9.1-10.8 before the work below). At a
real TT's pace (`-playdemo demo3 -benchtics 600 -benchpace 190`, size
9) the game runs at full speed, at 3.4 frames a second in low detail
and 2.35 in high (1.1 and 0.9 at 87-93% speed before). A Falcon (VGA)
takes about twice a TT's time: 153 ms a frame in low detail.

TTDOOM, the same engine's TT port, draws a taller view (320x208 at
size 10, its 240 rows standing for 200) and runs demo1 in 78 ms low and
116 high there; its game logic is about 6% faster (6.0 ms a tic on
demo3's first 600 against 6.4).

Where it comes from, against the port as first built (high detail,
size 9: 201 ms):

- `FixedMul` and `FixedDiv` as one inline `muls.l` and `divs.l` on a
  68020 to 68040 (gcc made the C's 64-bit arithmetic library calls), and
  STDOOM's 16-bit versions on a 68000: 17%.
- The column, span and fuzz drawers in assembler on a 68020
  (`src/atari/r_draw_m68k.c`), after TTDOOM's: the column four
  instructions a pixel, the span 7.5 (ADoom's crossed registers); low
  detail stores pixel pairs from word colormaps.
- Walls, flats and sprites batched as TTDOOM's are, each loop small
  enough for the 68030's 256-byte instruction cache: the seg loop in
  passes (clip and mark, then texture column, light and scale, then the
  columns set up as records and drawn together, a cached column fetched
  without W_CacheLumpNum); a visplane's spans noted, then set up from a
  cache of the rows a line of the data cache each, then drawn; a
  sprite's posts as column records; R_MakeSpans inline.
- The program moved onto a long when TOS loads it 2 bytes off, and its
  stack put on one (`atari_align.c`, TTDOOM's): 2-3% under Hatari, about
  2.5 times that on a real TT.
- The screen: only marked boxes converted, and 1:1 by default.
- The game logic, after TTDOOM's, every change checked against the PC
  build's state over whole demos: the angle test before the sight check
  in `P_LookForPlayers`; the sight check's BSP walk as a loop, on compact
  records of the nodes and lines made at each level's load, and on a
  68020 as one assembler routine laid out for the instruction cache
  (16-bit, so only on a map under 32767 units across); thinkers that
  only count their state's tics down, or stand still for ever, handled
  in `P_RunThinkers`'s loop; `R_PointInSubsector`'s walk in assembler
  on the sight check's compact nodes; the sight check's lines crossing
  handed to id's code without testing them again; the gamepad read
  only after it changes, not 15 buttons and 6 axes a tic: 45% off a tic.
- Low detail's view converted as pixel pairs (STDL's
  `STDL_SDL2_RenderCopyPairs`): 3.5 ms a frame.

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
| `atari_align.c` | the program moved onto a long, linked ahead of the C library's start |
| `i_endoom_atari.c`, `net_atari.c` | ENDOOM as text; no network |

## Testing

`tools/atari/run.sh` runs a program under Hatari from a drive of its
own, with the IWAD, and plays a script against Hatari's command fifo
(screenshots, keys, profiling); `MACHINE=tt` (2 MB + 4 MB TT-RAM),
`tt4`, `falcon`, `ste` or `st`. `tools/atari/profagg.py` sums a profile
per function; give the profiled program another name (`PRGNAME=X.TOS`) so
it doesn't move, as Hatari can't take a moved program's symbols.

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

With DOOM1.WAD 1.9 and the default settings but high detail
(`detaillevel 0`, the PC's default), 600 tics: demo1 state
`5d883da0` frames `3edfdd5a`, demo2 `a32fc838` / `414f485a`, demo3
`7f557ea2` / `4d508737`. The whole demos: demo1 `565a4537`, demo2
`d32c8c31`, demo3 `249c2864`. The frames' hashes of a TT build differ
from the PC's since the span loop (above); compare them between Atari
builds, with demo3 apart, where one pixel depends on memory layout (Doom
reads past a column's end). `-playdemo demoN -benchtics N` ends real play
at tic N with frames a second and the game's speed, and `-benchpace 190`
runs the game's clock 1.9 times as fast, as a real TT plays under
Hatari's faster one. `-sdl2check` turns on STDL's screen check, which
compares every page with the screen Doom drew before it is shown, and
prints the count of those unlike it at the end (none, for whole demos
1-3 in low detail on a TT and demo3 on a Falcon and stretched to 4:3);
`-nopairs` converts the low-detail view as any other pixels, for
timing.
