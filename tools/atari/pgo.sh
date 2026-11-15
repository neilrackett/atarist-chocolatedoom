#!/bin/bash
# tools/atari/pgo.sh [DEMO:DETAIL ...]
#
# CHOC_TT.TOS with gcc 13, LTO and profile-guided optimisation (after
# TTDOOM's tools/bench/pgo.sh):
#  1. builds it with PGO=gen: a CHOC_TT that writes its profile to PGO.DAT
#     when a timedemo ends (src/atari/pgo.c);
#  2. runs that under Hatari on the training timedemos, whole, DEMO:DETAIL
#     (low or high, size 9; default demo1:low demo1:high demo2:low
#     demo2:high, leaving demo3 unseen to check the profile isn't just
#     fitted to its demos);
#  3. merges their profiles into .gcda files beside the objects
#     (gcov-tool merge-stream, in the toolkit image) and writes in the
#     whole-program summary the stream leaves out (gcdasum.py);
#  4. rebuilds with PGO=use: dist-atari/mintelf-lto-pgo/CHOC_TT.TOS.
#
# LTO=0 for PGO alone (dist-atari/mintelf-pgo).
set -eo pipefail
RUNS=${*:-demo1:low demo1:high demo2:low demo2:high}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
LTO=${LTO:-1}
export STCMD_NO_TTY=1
V=mintelf$([ "$LTO" = 1 ] && echo -lto)-pgo
MAKEVARS="TOOLCHAIN=mintelf LTO=$LTO"
cd "$REPO"
LOG=tmp/atari/pgo_build.log
mkdir -p tmp/atari

# 1. The profiling build
rm -rf "build-atari/$V" "dist-atari/$V"
stcmd make -f Makefile.atari $MAKEVARS PGO=gen tt > "$LOG" 2>&1 \
  || { tail -30 "$LOG"; exit 1; }

# 2. Training runs, in parallel: each writes its drive's PGO.DAT
printf 'detaillevel 0\n' > tmp/atari/pgo_high.cfg
printf 'detaillevel 1\n' > tmp/atari/pgo_low.cfg
for r in $RUNS; do
  d=${r%%:*}; m=${r#*:}
  CFG=tmp/atari/pgo_$m.cfg ARGS="-timedemo $d" \
    "$HERE/run.sh" "pgo_${d}_$m" "dist-atari/$V/CHOC_TT.TOS" \
    "waitfor PGO written;waitfor realtics" > /dev/null 2>&1 &
done
wait

# 3. The .gcda files go where the stream names them: the objects' folder,
# at the path the tree has in the container
n=0
for r in $RUNS; do
  d=tmp/atari/pgo_${r%%:*}_${r#*:}/hd/PGO.DAT
  [ -s "$d" ] || { echo "no profile from $r ($d)" >&2; exit 1; }
  cp "$d" "build-atari/$V/pgo$n.dat"
  stcmd m68k-atari-mintelf-gcov-tool merge-stream "build-atari/$V/pgo$n.dat" \
    2>&1 | grep -v ST_WORKING || true
  n=$((n + 1))
done
find "build-atari/$V/tt" -name '*.gcda' -print0 \
  | xargs -0 python3 -I "$HERE/gcdasum.py" $n

# 4. Rebuild with them: the objects and the program go, the profiles stay
find "build-atari/$V/tt" -name '*.o' -delete
rm -f "dist-atari/$V/CHOC_TT.TOS"
stcmd make -f Makefile.atari $MAKEVARS PGO=use tt > "$LOG" 2>&1 \
  || { tail -30 "$LOG"; exit 1; }
echo "built dist-atari/$V/CHOC_TT.TOS ($(find build-atari/$V/tt -name '*.gcda' | wc -l | tr -d ' ') profiles)"
