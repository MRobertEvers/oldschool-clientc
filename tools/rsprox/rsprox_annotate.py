"""rsprox_annotate.py <timeline.txt> : names the bare rev-227 ids in a timeline.py dump from our compacks."""
import re
import sys

ROOT = "/Users/matthewevers/Documents/git_repos/3draster/OSRS-Content/osrs-content/"


def table(path):
    names = {}
    with open(ROOT + path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if "=" in line and line[0].isdigit():
                k, v = line.rstrip("\n").split("=", 1)
                names[int(k)] = v.split()[0] if v else v
    return names


SEQ = table("configs/all.seq.compack")
LOC = table("configs/all.loc.compack")
NPC = table("configs/all.npc.compack")
OBJ = table("configs/all.obj.compack")
SPOT = table("configs/all.spotanim.compack")
SOUND = table("pack/4_soundeffects.pack")
JINGLE = table("pack/11_musicjingles.pack")
SONG = table("pack/6_musictracks.pack")


def name(t, m):
    n = t.get(int(m.group(2)))
    return "%s%s(%s)" % (m.group(1), m.group(2), n) if n else m.group(0)


RULES = [
    (re.compile(r"^\s*(head \d+ anim=|sequence .*: id=|\[loc_anim\] .*anim=)"), [(re.compile(r"((?:anim|id)=)(\d+)"), SEQ)]),
    (re.compile(r"\[(loc_add_change_v1|oploc\w*)\]"), [(re.compile(r"(id=)(\d+)"), LOC)]),
    (re.compile(r"\[(opnpc\w*)\]|sequence npc"), [(re.compile(r"(npc )(\d+)"), NPC), (re.compile(r"(\bid=)(\d+)(?=, multinpc)"), NPC)]),
    (re.compile(r"^\s+- slot=|\[obj_add\]|\[if_setobject\]"), [(re.compile(r"(id=)(\d+)"), OBJ)]),
    (re.compile(r"^\s*spotanim |\[map_anim\]"), [(re.compile(r"(id=)(\d+)"), SPOT)]),
    (re.compile(r"\[(synth_sound|sound_area)\]"), [(re.compile(r"(id=)(\d+)"), SOUND)]),
    (re.compile(r"\[midi_jingle\]"), [(re.compile(r"(id=)(\d+)"), JINGLE)]),
    (re.compile(r"\[midi_song\w*\]"), [(re.compile(r"(id=)(\d+)"), SONG)]),
]

for line in open(sys.argv[1], encoding="utf-8"):
    line = line.rstrip("\n")
    for test, subs in RULES:
        if test.search(line):
            for pattern, t in subs:
                line = pattern.sub(lambda m, t=t: name(t, m), line, count=1)
    print(line)
