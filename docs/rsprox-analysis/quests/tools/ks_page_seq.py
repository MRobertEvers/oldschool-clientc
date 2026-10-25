"""page_seq.py rec <transcript> [from] [to] | ours <wirelog>: the dialogue as one
line per page -- speaker side, chathead seq name, text with <br> -- plus menus,
mesboxes, objectboxes (obj, zoom, armed or not), stage writes (varp 122) and
synths, in wire order. Diff two outputs to compare a playthrough page by page."""
import re, sys, os
SEQ = {}
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "OSRS-Content", "osrs-content", "configs")
for l in open(os.path.join(root, "all.seq.compack")):
    if "=" in l:
        k, v = l.strip().split("=", 1); SEQ[k] = v
OBJ = {}
for l in open(os.path.join(root, "all.obj.compack")):
    if "=" in l:
        k, v = l.strip().split("=", 1); OBJ[k] = v

def rec(path, a, b):
    tick = 0; anim = {}; out = []
    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.strip()
        m = re.match(r"^\[(\d+)\]$", s)
        if m:
            tick = int(m.group(1)); continue
        if tick < a or tick > b:
            continue
        m = re.match(r"\[if_setanim\] com=(chat_left|chat_right):head \(\d+:\d+\), id=(\w+)", s)
        if m: anim[m.group(1)] = m.group(2)
        m = re.match(r'\[if_settext\] com=(chat_left|chat_right):text \(\d+:\d+\), text="(.*)"$', s)
        if m: out.append((tick, "%s %s %s" % ("npc" if m.group(1) == "chat_left" else "plr", anim.get(m.group(1)), m.group(2))))
        m = re.match(r'\[if_settext\] com=messagebox:text \(\d+:\d+\), text="(.*)"$', s)
        if m: out.append((tick, "mesbox " + m.group(1)))
        m = re.match(r'\[if_setobject\] com=objectbox:item \(\d+:\d+\), id=(\w+) \(\d+\), zoomorcount=(\d+)', s)
        if m: obj = "%s/%s" % (m.group(1), m.group(2))
        m = re.match(r'\[runclientscript\] id=objbox_setbuttons \(2868\), types=\[string\], values=\["(.*)"\]', s)
        if m: btn = m.group(1)
        m = re.match(r'\[if_settext\] com=objectbox:text \(\d+:\d+\), text="(.*)"$', s)
        if m: out.append((tick, "objbox %s [%s] %s" % (obj, btn or "-", m.group(1))))
        m = re.match(r'\[runclientscript\] id=chatbox_multi_init \(58\), types=\[string, string\], values=\["(.*)", "(.*)"\]', s)
        if m: out.append((tick, "MENU %s :: %s" % (m.group(1), m.group(2))))
        m = re.match(r"\[varp_small\] id=squire \(122\), old=(\d+), new=(\d+)", s)
        if m: out.append((tick, "STAGE %s" % m.group(2)))
        m = re.match(r"\[synth_sound\] id=(\d+)", s)
        if m: out.append((tick, "synth " + m.group(1)))
        m = re.match(r'\[message_game\] type=gamemessage, message="(.*)"$', s)
        if m: out.append((tick, "mes " + m.group(1)))
    return out

def ours(path):
    anim = {}; out = []; obj = None; btn = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        f = raw.rstrip("\n").split("\t")
        if len(f) < 3: continue
        tick, kind = int(f[0]), f[1]
        if kind == "if_setanim" and f[2] in ("231:2", "217:2"):
            anim[f[2][:3]] = SEQ.get(f[3], f[3])
        elif kind == "if_settext" and f[2] in ("231:6", "217:6"):
            out.append((tick, "%s %s %s" % ("npc" if f[2] == "231:6" else "plr", anim.get(f[2][:3]), f[3])))
        elif kind == "if_settext" and f[2] == "229:3":
            out.append((tick, "mesbox " + f[3]))
        elif kind == "if_setobject" and f[2] == "193:1":
            obj = "%s/%s" % (OBJ.get(f[3], f[3]), f[4] if len(f) > 4 else "?")
        elif kind == "runclientscript" and f[2] == "2868":
            btn = f[3].strip('"')
        elif kind == "if_settext" and f[2] == "193:2":
            out.append((tick, "objbox %s [%s] %s" % (obj, btn or "-", f[3])))
        elif kind == "runclientscript" and f[2] == "58":
            m = re.match(r'"(.*)","(.*)"$', f[3])
            out.append((tick, "MENU %s :: %s" % (m.group(1), m.group(2))))
        elif kind == "varp" and f[2] == "122":
            out.append((tick, "STAGE " + f[3]))
        elif kind == "synth_sound":
            out.append((tick, "synth " + f[2]))
        elif kind == "message_game" and f[2] == "0":
            out.append((tick, "mes " + f[3]))
    return out

if sys.argv[1] == "rec":
    a = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    b = int(sys.argv[4]) if len(sys.argv) > 4 else 1 << 30
    rows = rec(sys.argv[2], a, b)
else:
    rows = ours(sys.argv[2])
for t, l in rows:
    print("%d\t%s" % (t, l))
