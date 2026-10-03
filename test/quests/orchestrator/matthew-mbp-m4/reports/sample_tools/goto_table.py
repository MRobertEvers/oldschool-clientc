#!/usr/bin/env python3
"""goto_table.py <ledger.tsv> -- every goto row: departure, landing, static-collision reachability."""
import re, subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
for line in open(sys.argv[1]):
    c = line.rstrip("\n").split("\t")
    if len(c) < 6 or "goto" not in c[1]:
        continue
    m = re.search(r"at (\d+),(\d+),(\d+) from (\d+),(\d+),(\d+)", c[5])
    if not m:
        print("%s|%s|%s|no stamp: %s" % (c[0], c[1], c[2], c[5][:120]))
        continue
    tx, tz, tl, sx, sz, sl = map(int, m.groups())
    if (sx, sz) == (tx, tz):
        r = "same tile"
    elif tl != sl:
        r = "LEVEL CHANGE %d->%d" % (sl, tl)
    elif abs(sx - tx) + abs(sz - tz) > 400:
        r = "far (%d tiles) not checked" % (abs(sx - tx) + abs(sz - tz))
    else:
        r = subprocess.run([sys.executable, here + "/reach.py", str(sx), str(sz), str(tx), str(tz), str(tl), "30"],
                           capture_output=True, text=True).stdout.strip()
    print("%s|%s|%s|%d,%d,%d <- %d,%d,%d|%s" % (c[0], c[1], c[2], tx, tz, tl, sx, sz, sl, r))
