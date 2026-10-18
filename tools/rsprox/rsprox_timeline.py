"""rsprox_timeline.py <transcript.txt> <first_tick> <last_tick> : the quest-relevant packets, one tick per block.

Keeps what a quest script decides: the player's ops, dialogue pages, choices, game messages, varbits
in a given set, inventory and stat changes, loc/obj changes, camera and jingles. Drops movement,
clocks and sounds. Dialogue typed out in pieces (the same speaker's line growing) keeps the last piece.
"""
import re
import sys

path, first, last = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
VARBITS = set(range(13900, 13961))
KEEP = re.compile(r"^\s*\[(op(npc|loc|obj|player)\w*|if_button\w*|resume_\w+|message_game|update_stat\w*|"
                  r"loc_add_change\w*|loc_del|obj_add|obj_del|cam_\w+|midi_jingle|if_opensub|if_closesub|"
                  r"if_setobject|update_inv_\w+|teleport|spotanim|set_player_op|sound_area|synth_sound|midi_song\w*|map_anim|map_projanim\w*)\]")
TEXT = re.compile(r'\[if_settext\] com=(\d+):(\d+), text="(.*)"$')
CHAT_IF = {"219", "229", "193", "11", "153", "233"}  # choices, mesbox, objbox, doubleobj, scroll, sprite box
VARBIT = re.compile(r"\[varbit\] id=(\d+),")
TICK = re.compile(r"^\[(\d+)\]$")
ENTITY = re.compile(r"^\s*\[(idle|walk|run|teleport|add|del)\] (npc|player)=\((index=\d+, )?(name=)?([^,]*)")
NESTED = re.compile(r"^\s+\[(sequence|spotanim|say|exactmove)\] (.*)$")

tick = -1
out = []
prev_dialogue = None
in_inv = False
with open(path, encoding="utf-8", errors="replace") as handle:
    for raw in handle:
        line = raw.rstrip("\n")
        m = TICK.match(line)
        if m:
            tick = int(m.group(1))
            if tick > last:
                break
            if tick >= first:
                out.append("[%d]" % tick)
            in_inv = False
            continue
        if tick < first:
            continue
        s = line.strip()
        if in_inv and line.startswith("        ") and ("slot=" in s or s == "[objs]"):
            if s != "[objs]":
                out.append("      " + s)
            continue
        in_inv = False
        m = ENTITY.match(line)
        if m:
            who = m.group(5).strip()
            # keep npc type ids: "id=11494" -> the type; multinpc carries the base after it
            mt = re.search(r"\bid=(\d+)(, multinpc=([^,(]*))?", line)
            if m.group(2) == "npc" and mt:
                who = "npc " + mt.group(1) + ((" " + mt.group(3).strip()) if mt.group(3) else "")
        m2 = NESTED.match(line)
        if m2 and "who" in dir():
            out.append("  %s %s: %s" % (m2.group(1), who, m2.group(2)))
            continue
        if s.startswith("[loc_anim]"):
            out.append("  " + s)
            continue
        if s.startswith("[message_game] type=dialogue"):
            msg = s.split('message="', 1)[1].rstrip('"')
            if prev_dialogue is not None and msg.startswith(prev_dialogue[1].rstrip()) and out[prev_dialogue[0]].startswith("  say"):
                out[prev_dialogue[0]] = "  say " + msg
                prev_dialogue = (prev_dialogue[0], msg)
            else:
                out.append("  say " + msg)
                prev_dialogue = (len(out) - 1, msg)
            continue
        mh = re.match(r"\[if_setanim\] com=(231|217):2, id=(\d+)", s)
        if mh:
            out.append("  head %s anim=%s" % (mh.group(1), mh.group(2)))
            continue
        m = TEXT.search(s)
        if m:
            if m.group(1) in CHAT_IF and m.group(3).strip() and "Click here to continue" not in m.group(3):
                out.append("  text %s:%s %s" % (m.group(1), m.group(2), m.group(3)))
            continue
        m = VARBIT.search(s)
        if m:
            if int(m.group(1)) in VARBITS:
                out.append("  " + s)
            continue
        if KEEP.match(line):
            if s.startswith("[resume_pausebutton]") and out and out[-1].strip() == s:
                continue
            if s.startswith("[if_opensub]") and re.search(r"id=(217|231|219|229|193|11|162)\b", s):
                continue
            out.append("  " + s)
            if s.startswith("[update_inv"):
                in_inv = True

# drop empty ticks
result = []
for i, l in enumerate(out):
    if l.startswith("[") and (i + 1 == len(out) or out[i + 1].startswith("[")):
        continue
    result.append(l)
print("\n".join(result))
