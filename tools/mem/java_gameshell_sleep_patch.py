"""Make GameShell's per-iteration sleep switchable, to test what caps Java's fps.

The Java client renders 31 fps on the XP box while using only about half a
core, which is not what saturation looks like. GameShell.run() sleeps `var3` ms
every iteration, and `var3` collapses to `mindel` (1, or 5 when
signlink.sunjava) on any machine that cannot hold one iteration per `deltime`.

The suspicion is Windows XP's default timer granularity, ~15.6 ms: a
Thread.sleep(5) there does not sleep 5 ms, it sleeps to the next timer tick. A
16 ms frame plus a 15.6 ms forced wait is a 32 ms iteration, which is 31 fps --
so the frame rate would be an artifact of the sleep, not of the renderer, and
every "Java is saturated" statement built on it would be wrong.

This adds a switch rather than deleting the sleep, so one jar carries both arms
and the A/B is a command line rather than a rebuild:

    java -Dtorirs.nosleep=true ...   # skip the sleep entirely

Usage: python java_gameshell_sleep_patch.py <path to a COPY of GameShell.java>
"""
import io
import sys

path = sys.argv[1]
src = io.open(path, encoding="utf-8").read()

NEEDLE = "Thread.sleep((long) var3);"
count = src.count(NEEDLE)
if count != 1:
    # A patch that quietly does nothing is worse than one that crashes: the run
    # would then "measure" the unpatched client and read as a refutation.
    raise SystemExit("expected exactly one %r, found %d" % (NEEDLE, count))

src = src.replace(
    NEEDLE,
    'if (!Boolean.getBoolean("torirs.nosleep")) Thread.sleep((long) var3);',
    1)

io.open(path, "w", encoding="utf-8", newline="\n").write(src)
print("patched: sleep is now skipped under -Dtorirs.nosleep=true")
