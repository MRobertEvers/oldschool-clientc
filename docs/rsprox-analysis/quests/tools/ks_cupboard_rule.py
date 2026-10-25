"""cupboard_rule.py <txt>...: every Search of Sir Vyvin's cupboard with its answer,
Sir Vyvin's tile and last facing, and the player's tile."""
import re, sys
for path in sys.argv[1:]:
    tick = 0; vy = None; vyface = None; pl = None; pending = None
    out = []
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    for i, s in enumerate(lines):
        m = re.match(r"^\[(\d+)\]$", s.strip())
        if m:
            tick = int(m.group(1)); continue
        if "sir_vyvin" in s and "npc=(index" in s and "facing" not in s:
            m = re.search(r"newcoord=\((\d+), (\d+), (\d+)\)", s) or re.search(r"coord=\((\d+), (\d+), (\d+)\)", s)
            if m: vy = tuple(map(int, m.groups()))
            if "[del]" in s: vy = None
            # the facing line follows on the next line(s)
            for j in (i + 1, i + 2):
                if j < len(lines) and "[facing]" in lines[j] and lines[j].startswith(" " * 12):
                    vyface = lines[j].strip()
        m = re.search(r"\] player=\(index=\d+, name=[^,]+, coord=\((\d+), (\d+), (\d+)\)\)(?:, newcoord=\((\d+), (\d+), (\d+)\))?", s)
        if m:
            g = m.groups(); pl = tuple(map(int, g[3:6] if g[3] else g[0:3]))
        if "oploc1" in s and "vyvincupboardopen" in s:
            pending = tick
        if pending is not None and tick - pending <= 4:
            ans = None
            if "STAY OUT" in s and "message_game" in s: ans = "CAUGHT"
            elif "load of junk" in s and "message_game" in s: ans = "junk"
            elif "small portrait" in s and "message_game" in s: ans = "portrait"
            if ans:
                out.append((pending, tick, ans, pl, vy, vyface)); pending = None
    for o in out:
        print(path.rsplit("/", 1)[-1], *o, sep="\t")
