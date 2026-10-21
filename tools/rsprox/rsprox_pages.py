"""rsprox_pages.py <transcript> [first] [last] : every dialogue page the server opened, in order.

One line per page: tick, side (npc / plr), the chathead seq's 239 name and the speaker, the text.
Boxes (219 chatmenu rows, 229 messagebox, 193 objectbox, 11 objectbox_double, 633 graphicbox) print as
`<interface>:<component>`, each menu as MENU with its title and rows, each answer as CHOSE <row>.

Reads both transcript formats: the old bare ids (`com=231:2, id=614`, rev 227 shared transcripts)
and the index's named ones (`com=chat_left:head (231:2), id=chatshifty3 (594)`, every `get`).
"""
import os
import re
import sys

path = sys.argv[1]
first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
last = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30

SEQ = {}
_seq_pack = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                         "OSRS-Content", "osrs-content", "configs", "all.seq.compack")
for l in open(_seq_pack):
    if "=" in l:
        k, v = l.strip().split("=", 1)
        SEQ[k] = v

# `com=231:2` or `com=chat_left:head (231:2)` -> (231, 2)
COM = r"com=(?:[\w:]+ \()?(\d+):(\d+)\)?"
# `id=614` or `id=chatshifty3 (594)` -> 614 / 594
SEQ_ID = r"id=(?:\w+ \()?(-?\d+)\)?"
BOXES = {"219", "229", "193", "11", "633"}

tick = 0
anim = {}
name = {}
for raw in open(path, encoding="utf-8", errors="replace"):
    s = raw.strip()
    m = re.match(r"^\[(\d+)\]$", s)
    if m:
        tick = int(m.group(1))
        if tick > last:
            break
        continue
    if tick < first:
        continue
    m = re.match(r"\[if_setanim\] " + COM + ", " + SEQ_ID, s)
    if m and m.group(1) in ("231", "217") and m.group(2) == "2":
        anim[m.group(1)] = SEQ.get(m.group(3), m.group(3))
        continue
    m = re.match(r'\[if_settext\] ' + COM + r', text="(.*)"', s)
    if m:
        iface, child, text = m.group(1), m.group(2), m.group(3)
        if iface in ("231", "217") and child == "4":
            name[iface] = text
        elif iface in ("231", "217") and child == "6":
            print("%d\t%s\t%s\t%s" % (tick, "npc" if iface == "231" else "plr",
                                      anim.get(iface, "?") + " " + name.get(iface, "?"), text))
        elif iface in BOXES and text.strip() and "Click here" not in text:
            print("%d\t%s:%s\t\t%s" % (tick, iface, child, text))
        continue
    m = re.search(r'\[runclientscript\] id=(?:chatbox_multi_init \()?58\)?, .*values=\["(.*?)", "(.*)"\]', s)
    if m:
        print("%d\tMENU\t%s\t%s" % (tick, m.group(1), m.group(2)))
        continue
    m = re.match(r"\[resume_pausebutton\] " + COM + r", sub=(\d+)", s)
    if m and m.group(1) == "219" and m.group(2) == "1":
        print("%d\tCHOSE\t\t%s" % (tick, m.group(3)))
