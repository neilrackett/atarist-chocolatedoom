#!/usr/bin/env python3
# profagg.py PROFILE PROGRAM [N]: sums a Hatari CPU profile (run.sh's prof
# command, NAME/profile.txt) per function, with the symbols of PROGRAM
# (the build/GAME/ program, unstripped), and lists the top N by cycles:
# their share, instructions, cycles an instruction, instruction cache
# misses and data cache hits. (After ataritt-doom's tools/bench/profagg.py.)
import sys, re, bisect, subprocess, os
prof, prog = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 40
nm = subprocess.run(['stcmd', 'm68k-atari-mint-nm', '-n', prog],
                    capture_output=True, text=True,
                    env=dict(os.environ, STCMD_NO_TTY='1')).stdout
syms = []
for l in nm.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'TtW' and not p[2].endswith('.o') \
       and not p[2].startswith('.L'):
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [a for a, _ in syms]
base = None
lines = open(prof, errors='replace').read().splitlines()
for l in lines:
    m = re.search(r'PROGRAM_TEXT:\s*0x([0-9a-f]+)', l)
    if m:
        base = int(m.group(1), 16)
        break
pat = re.compile(r'^([0-9a-f]{8}) .*\s([0-9.]+)% \((\d+), (\d+), (\d+), (\d+)\)$')
agg = {}
total = 0
for l in lines:
    m = pat.match(l)
    if not m:
        continue
    a = int(m.group(1), 16)
    ins, cyc, im, dh = (int(m.group(i)) for i in (3, 4, 5, 6))
    total += cyc
    name = '(outside)'
    if base is not None and a >= base:
        i = bisect.bisect_right(addrs, a - base) - 1
        if i >= 0:
            name = syms[i][1]
    e = agg.setdefault(name, [0, 0, 0, 0])
    e[0] += ins; e[1] += cyc; e[2] += im; e[3] += dh
print('total cycles %d' % total)
print('%-32s %7s %9s %7s %7s %8s' % ('function', 'cyc%', 'Minstr', 'cyc/ins', 'i-miss%', 'Mdhits'))
for name, (ins, cyc, im, dh) in sorted(agg.items(), key=lambda kv: -kv[1][1])[:N]:
    print('%-32s %6.2f%% %9.2f %7.2f %6.1f%% %8.2f' % (
        name[:32], 100.0 * cyc / total, ins / 1e6, cyc / max(ins, 1),
        100.0 * im / max(ins, 1), dh / 1e6))
