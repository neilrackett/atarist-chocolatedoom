#!/bin/bash
# tools/atari/run.sh NAME PROG.TOS "cmd;cmd;..."
#
# Runs PROG (dist-atari/CHOC_TT.TOS or CHOC_ST.TOS) under Hatari, headless,
# from a hard disk folder of its own with the IWAD beside it, then plays
# a ;-separated script against Hatari's command fifo. Output goes to
# tmp/atari/NAME/: log (the console), shots/ (screenshots) and hd/ (the
# drive, with any CHOCDOOM.CFG, savegames or STDOUT the program wrote).
#
# Commands:
#   sleep N          wait N seconds (host time)
#   waitfor STR      wait for STR in the console log (up to 10 minutes)
#   shot             a screenshot
#   key VAL          press and release a key: an ST scancode in hex
#                    (0x1c Return, 0x01 Esc, 0x39 space) or a character
#   keydown VAL / keyup VAL
#   fifo CMD...      a raw command for the fifo
#   prof             (with PRGNAME=CHOCDOOM.TOS: see below)
#                    profile from the demo's first tic to I_Error (a
#                    timedemo's result), into NAME/profile.txt; give it
#                    the program with its symbols (build-atari/CHOC_TT.TOS),
#                    once the program runs (after "waitfor ST_Init").
#                    profagg.py NAME/profile.txt build-atari/CHOC_TT.TOS
#                    sums it per function.
#
# Environment:
#   MACHINE  tt (default; TOS 3.06, 2 MB ST-RAM + 4 MB TT-RAM, VGA),
#            tt4 (TT with 4 MB ST-RAM only), falcon (EmuTOS 512K, 14 MB,
#            VGA; MON=rgb for RGB), ste (TOS 1.62, 4 MB), st (TOS 1.04),
#            megaste (TOS 2.06, 4 MB: STDL runs it at 16 MHz with the cache)
#   ARGS     the program's arguments (written to the drive: Hatari can't
#            pass them to an autostarted program, so CHOCDOOM reads
#            ARGS.TXT when it has none)
#   IWAD     the IWAD to put beside it (default ../ataritt-doom's
#            DOOM1.WAD)
#   FF       fast-forward on (default) or off (for real-time behaviour)
#   CFG      a config file to start from, copied as DEFAULT.CFG
#   EXTRACFG Chocolate Doom's own settings to start from, as CHOCDOOM.CFG
#   TOSDIR   where the TOS images are (default ../ataritt-doom/tmp/tos)
#   HATARI   the emulator (default: hatari on the PATH, else the Mac app)
set -u
NAME=$1
PROG=$2
SCRIPT=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
TOSDIR=${TOSDIR:-$REPO/../ataritt-doom/tmp/tos}
ETOS=${ETOS:-$REPO/../atarist-stdl/tmp/tos/etos512uk.img}
IWAD=${IWAD:-$REPO/../ataritt-doom/dist/DOOM1.WAD}
MACHINE=${MACHINE:-tt}
FF=${FF:-on}
if [ -z "${HATARI:-}" ]; then
  HATARI=$(command -v hatari || echo /Applications/Hatari.app/Contents/MacOS/hatari)
fi

R=$REPO/tmp/atari/$NAME
rm -rf "$R"; mkdir -p "$R/hd" "$R/shots"
# Under its own name (CHOC_TT.TOS, CHOC_ST.TOS), which it moves itself onto
# a long by (atari_align.c); PRGNAME=X.TOS keeps it where TOS put it, as
# Hatari's profiler needs (it can't take a moved program's symbols)
PRGNAME=${PRGNAME:-$(basename "$PROG" | tr a-z A-Z)}
cp "$PROG" "$R/hd/$PRGNAME"
cp "$IWAD" "$R/hd/"
[ -n "${CFG:-}" ] && cp "$CFG" "$R/hd/DEFAULT.CFG"
[ -n "${EXTRACFG:-}" ] && cp "$EXTRACFG" "$R/hd/CHOCDOOM.CFG"
[ -n "${ARGS:-}" ] && printf '%s\r\n' "$ARGS" > "$R/hd/ARGS.TXT"

case $MACHINE in
  tt)     MACH="--tos $TOSDIR/tos306uk.img --machine tt --mmu off --monitor vga
            --memsize 2 --ttram 4 --addr24 off --cpu-exact on --data-cache on" ;;
  tt4)    MACH="--tos $TOSDIR/tos306uk.img --machine tt --mmu off --monitor vga
            --memsize 4 --cpu-exact on --data-cache on" ;;
  falcon) MACH="--tos $ETOS --machine falcon --mmu off --fpu none
            --monitor ${MON:-vga} --memsize 14 --dsp none --cpu-exact on" ;;
  ste)    MACH="--tos $TOSDIR/tos162uk.img --machine ste --memsize 4" ;;
  st)     MACH="--tos $TOSDIR/tos104uk.img --machine st --memsize 4" ;;
  megaste) MACH="--tos $TOSDIR/tos206uk.img --machine megaste --memsize 4" ;;
  *)      echo "unknown MACHINE $MACHINE" >&2; exit 2 ;;
esac

: > "$R/hatari.cfg"
FIFO=$R/fifo
LOG=$R/log
SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy} SDL_AUDIODRIVER=dummy "$HATARI" \
  --configfile "$R/hatari.cfg" $MACH --fast-forward "$FF" \
  --confirm-quit false --fast-boot on --sound 44100 --statusbar off \
  --alert-level fatal --conout 2 --cmd-fifo "$FIFO" \
  --screenshot-dir "$R/shots" --screenshot-format png \
  --harddrive "$R/hd" --auto "C:\\$PRGNAME" > "$LOG" 2>&1 &
HPID=$!
for i in $(seq 1 50); do [ -p "$FIFO" ] && break; sleep 0.2; done

IFS=';' read -ra CMDS <<< "$SCRIPT"
for cmd in "${CMDS[@]}"; do
  set -- $cmd
  [ $# -eq 0 ] && continue
  case $1 in
    sleep)   sleep "$2" ;;
    shot)    echo "hatari-shortcut screenshot" > "$FIFO" ;;
    key)     echo "hatari-event keypress $2" > "$FIFO" ;;
    keydown) echo "hatari-event keydown $2" > "$FIFO" ;;
    keyup)   echo "hatari-event keyup $2" > "$FIFO" ;;
    fifo)    shift; echo "$*" > "$FIFO" ;;
    prof)    printf 'profile save %s/profile.txt\nquit\n' "$R" > "$R/prof.ini"
             printf 'profile on\n' > "$R/profon.ini"
             echo "hatari-debug symbols prg" > "$FIFO"
             echo "hatari-debug b pc = _G_ReadDemoTiccmd :once :quiet :file $R/profon.ini" > "$FIFO"
             echo "hatari-debug b pc = _I_Error :quiet :file $R/prof.ini" > "$FIFO" ;;
    waitfor) shift
             for i in $(seq 1 1200); do
               grep -aq "$*" "$LOG" && break
               kill -0 $HPID 2>/dev/null || break
               sleep 0.5
             done ;;
    *)       echo "unknown command: $cmd" >&2 ;;
  esac
done

sleep 1
echo "hatari-debug quit" > "$FIFO" 2>/dev/null
for i in $(seq 1 20); do kill -0 $HPID 2>/dev/null || break; sleep 0.5; done
kill -9 $HPID 2>/dev/null
tr -d '\r' < "$LOG" | grep -av "^$" | tail -${TAIL:-40}
