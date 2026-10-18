"""rsprox_pages.py <transcript> <first> <last> : every dialogue page the server opened, in order."""
import re, sys
path, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
SEQ = {}
for l in open("/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs239-content/configs/all.seq.compack"):
    if "=" in l: k, v = l.strip().split("=", 1); SEQ[k] = v
tick = 0; anim = {}; name = {}; opts = []
for raw in open(path, encoding="utf-8", errors="replace"):
    s = raw.strip()
    m = re.match(r"^\[(\d+)\]$", s)
    if m:
        tick = int(m.group(1))
        if tick > last: break
        continue
    if tick < first: continue
    m = re.match(r"\[if_setanim\] com=(231|217):2, id=(\d+)", s)
    if m: anim[m.group(1)] = SEQ.get(m.group(2), m.group(2)); continue
    m = re.match(r'\[if_settext\] com=(231|217):4, text="(.*)"', s)
    if m: name[m.group(1)] = m.group(2); continue
    m = re.match(r'\[if_settext\] com=(231|217):6, text="(.*)"', s)
    if m:
        i = m.group(1)
        print("%d\t%s\t%s\t%s" % (tick, "npc" if i == "231" else "plr", anim.get(i, "?") + " " + name.get(i, "?"), m.group(2)))
        continue
    m = re.match(r'\[if_settext\] com=(219|229|193|11):(\d+), text="(.*)"', s)
    if m and m.group(3).strip() and "Click here" not in m.group(3):
        print("%d\t%s:%s\t\t%s" % (tick, m.group(1), m.group(2), m.group(3)))
        continue
    m = re.match(r'\[resume_pausebutton\] com=219:1, sub=(\d+)', s)
    if m: print("%d\tCHOSE\t\t%s" % (tick, m.group(1)))
