"""rsprox_book.py <transcript> [first_tick] [last_tick] : every spread of interface 392 `book` the server painted.

For each mount of `book` (and each page turn after it) prints the tick, the title if one was sent,
the two page numbers, and both sides as the rows Jagex broke them -- joined with `|`, a blank row
empty -- which is the form `~book_read_spread(left, right, ...)` (interface_book/scripts/book.rs2)
takes. Rows a spread does not resend keep their last value.
"""
import re
import sys

path = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
last = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30

ROW = re.compile(r"\[if_settext\] com=book:page_(left|right)_text_(\d+) \(392:\d+\), text=\"(.*)\"$")
NUM = re.compile(r"\[if_settext\] com=book:page_(left|right)_number \(392:\d+\), text=\"(.*)\"$")
TITLE = re.compile(r"\[if_settext\] com=book:title \(392:\d+\), text=\"(.*)\"$")

tick = 0
rows = {"left": [""] * 15, "right": [""] * 15}
nums = {"left": "", "right": ""}
title = None
dirty = False
spreads = []


def flush(at):
    global dirty, title
    if not dirty:
        return
    spreads.append((at, title, dict(nums), list(rows["left"]), list(rows["right"])))
    title = None
    dirty = False


for raw in open(path, encoding="utf-8", errors="replace"):
    line = raw.rstrip("\n")
    m = re.match(r"^\[(\d+)\]", line)
    if m:
        flush(tick)
        tick = int(m.group(1))
        continue
    if tick < first or tick > last:
        continue
    s = line.strip()
    m = ROW.match(s)
    if m:
        rows[m.group(1)][int(m.group(2)) - 1] = m.group(3)
        dirty = True
        continue
    m = NUM.match(s)
    if m:
        nums[m.group(1)] = m.group(2)
        dirty = True
        continue
    m = TITLE.match(s)
    if m:
        title = m.group(1)
        dirty = True
flush(tick)


def side(r):
    while r and r[-1] == "":
        r = r[:-1]
    return "|".join(r)


for at, t, n, left, right in spreads:
    print("[%d] pages %s-%s%s" % (at, n["left"], n["right"], ("  title " + repr(t)) if t else ""))
    print("  L " + side(left))
    print("  R " + side(right))
