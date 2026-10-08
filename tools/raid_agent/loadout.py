#!/usr/bin/env python3
"""A raid script's LOADOUTS: per-seat player saves, loaded at login.

The owner, 2026-10-07: "Scripts should be able to define and specify
loadouts of stats, quests, etc and load that when starting up." A loadout is
the server's own save format (src/torirsserver/torirs_server_save.c,
hand-editable): test/raids/fixtures/loadouts/<loadout>_seat<k>.ini. The bot
runner's bots and a party's clients log in on them, so setup is one load at
login -- no cheats in a tick order (a kit's ::wield lands a tick after its
::clearinv: a real account's bronze full helm took the Dawnbringer's slot).

    python3 tools/raid_agent/loadout.py make verzik     # (re)make from KITS
    python3 tools/raid_agent/loadout.py install verzik <saves dir> <prefix>

`make` runs the kit below on the bot runner, snapshots each bot's save
(--snapshot) and keeps the parts a loadout states: [stats], [inv], [worn]
(and their _var sections), under fresh_lumbridge.ini's [player] and
[varps] (the tutorial and the character creator done, the Lumbridge tile).
`install` copies seat k's file to <saves dir>/<prefix><k>.ini with its name.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER = os.path.join(ROOT, "src", "build_botrun_opt", "torirsserver")
LOADOUTS = os.path.join(ROOT, "test", "raids", "fixtures", "loadouts")
BASE = os.path.join(ROOT, "test", "raids", "fixtures", "fresh_lumbridge.ini")
KEEP = ("stats", "inv", "inv_var", "worn", "worn_var")
QUEST_CHEAT = os.path.join(ROOT, "OSRS-Content", "osrs239-content", "server", "scripts", "quests", "scripts",
                           "quest_cheat.rs2")


def all_quests():
    """Every quest and miniquest ::questmark (::complete's arms, state only) has an arm for (quest_cheat.rs2):
    a new account has them all done (owner 2026-10-07: "just unlock all quests
    and prayers"). Prayers need nothing past the level here -- prayer.rs2
    ~prayer_checks reads only the Prayer level -- and the kits set 99."""
    rows = re.findall(r"if \(\$row = ([a-z0-9_]+)\)", open(QUEST_CHEAT).read())
    return sorted(set(rows))

# The kits a loadout is made from: seat -> ticks of cheats (each tick a list,
# so a wield's displacement resolves before the bag is cleared).
_MELEE_GEAR = ["clearinv", "tobkit", "setlevel attack 99", "setlevel strength 99", "setlevel prayer 99",
               "setlevel magic 99", "setlevel agility 99", "setlevel slayer 37",
               "give slayer_boots 1", "wield slayer_boots"]
_WORN = {2: ["oathplate_helm", "oathplate_chest", "oathplate_legs"],
         3: ["neitiznot_faceguard", "tzhaar_cape_fire", "bandos_chestplate", "bandos_skirt"]}
_BAG = ["clearinv", "give serpentine_helm_charged 1", "give br_4dosepotionofsaradomin 4",
        "give br_4dose2restore 4", "give br_4dose2combat 2", "give dragon_claws 1"]


def _verzik(seat):
    # a tick each, two apart: the login settles, tobkit's set goes on, then the
    # seat's own pieces over it (a wield in tobkit's tick did not take), then
    # the bag once every displaced piece has landed
    wear = []
    for item in _WORN.get(seat, []) + ["slayer_boots"]:
        wear += ["give %s 1" % item, "wield %s" % item]
    bag = list(_BAG)
    if seat == 1:
        bag.append("give verzik_special_weapon 1")
    bag += ["give noxious_halberd 1", "give anglerfish %d" % (16 if seat == 1 else 14)]
    # the quests FIRST: a reward lands in the bag (myq3_xp_tome_3), and the
    # bag is cleared and filled last
    return [[], [], ["questmark " + q for q in all_quests()], [], _MELEE_GEAR[:-2], [], wear, [], bag]


def _melee_room(food):
    def kit(seat):
        return [[], [], ["questmark " + q for q in all_quests()], [], ["clearinv", "tobkit", "setlevel attack 99", "setlevel strength 99", "setlevel defence 99",
                 "setlevel prayer 99", "setlevel hitpoints 99", "setlevel magic 99", "setlevel ranged 99",
                 "setlevel agility 99"], [], [],
                ["clearinv", "give br_4dosepotionofsaradomin 4", "give br_4dose2restore 4",
                 "give br_4dose2combat 2", "give dragon_claws 1", "give anglerfish %d" % food]]
    return kit


KITS = {"verzik": _verzik, "maiden": _melee_room(16), "bloat": _melee_room(18)}


def sections(text):
    out, name = {}, None
    for line in text.splitlines(True):
        m = re.match(r"^\[([a-z_]+)\]\s*$", line)
        if m:
            name = m.group(1)
            out[name] = []
        elif name is not None:
            out[name].append(line)
    return out


def make(loadout, seats=3):
    kit = KITS[loadout]
    work = tempfile.mkdtemp(prefix="loadout_")
    agent = os.path.join(work, "kit.lua")
    with open(agent, "w") as f:
        f.write("local K = %s\n" % lua_table({s: kit(s) for s in range(1, seats + 1)}))
        f.write('''local seen, t0 = {}, nil
for line in io.lines() do
    local w, a = line:match("^(%a+)\\t?(%S*)")
    if w == "self" then seen[#seen + 1] = tonumber(a) end
    if w == "tick" then seen = {} end
    if w == "end" then
        t0 = t0 or tonumber(0)
        t0 = t0 + 1
        table.sort(seen)
        for seat, pid in ipairs(seen) do
            for _, c in ipairs((K[seat] or {})[t0] or {}) do io.write(pid, "\\tcheat\\t", c, "\\n") end
        end
        io.write("done\\n")
        io.flush()
    end
end
''')
    saves = os.path.join(work, "saves")
    snap = os.path.join(work, "snap")
    os.makedirs(saves)
    os.makedirs(snap)
    subprocess.run([SERVER, "--botrun", "--shared", "--bots", str(seats), "--name", "seat", "--ticks", "20",
                    "--snapshot", "16", snap, "--agent", "lua " + agent], cwd=ROOT,
                   env=dict(os.environ, TORIRSSERVER_SAVES=saves), stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=True)
    base = sections(open(BASE).read())
    os.makedirs(LOADOUTS, exist_ok=True)
    for seat in range(1, seats + 1):
        snapped = sections(open(os.path.join(snap, "seat%d.ini" % seat)).read())
        path = os.path.join(LOADOUTS, "%s_seat%d.ini" % (loadout, seat))
        with open(path, "w") as f:
            f.write("; LOADOUT %s, seat %d: made by tools/raid_agent/loadout.py from its KITS entry\n"
                    "; (re-run `loadout.py make %s` after changing the kit, or edit by hand).\n"
                    "; [player] and [varps] are fresh_lumbridge.ini's; the rest the kit's result.\n\n"
                    % (loadout, seat, loadout))
            f.write("[player]\n" + "".join(base.get("player", [])))
            # the kit's varps (quests done, spec energy, settings) under the base
            # account's two (the tutorial and the character creator finished)
            merged, order = {}, []
            for line in snapped.get("varps", []) + base.get("varps", []):
                m = re.match(r"^(\d+)\s*=\s*(-?\d+)", line)
                if m:
                    if m.group(1) not in merged:
                        order.append(m.group(1))
                    merged[m.group(1)] = m.group(2)
            f.write("[varps]\n" + "".join("%s = %s\n" % (k, merged[k]) for k in order) + "\n")
            for name in KEEP:
                if name in snapped:
                    f.write("[%s]\n" % name + "".join(snapped[name]))
        print("wrote", os.path.relpath(path, ROOT))
    shutil.rmtree(work)


def lua_table(v):
    if isinstance(v, dict):
        return "{" + ", ".join("[%d] = %s" % (k, lua_table(x)) for k, x in sorted(v.items())) + "}"
    if isinstance(v, list):
        return "{" + ", ".join(lua_table(x) for x in v) + "}"
    return '"%s"' % v


def install(loadout, saves_dir, prefix, seats=3):
    os.makedirs(saves_dir, exist_ok=True)
    for seat in range(1, seats + 1):
        src = os.path.join(LOADOUTS, "%s_seat%d.ini" % (loadout, seat))
        assert os.path.isfile(src), "no loadout %s (loadout.py make %s)" % (src, loadout)
        text = re.sub(r"(?m)^name = .*$", "name = %s%d" % (prefix, seat), open(src).read(), count=1)
        with open(os.path.join(saves_dir, "%s%d.ini" % (prefix, seat)), "w") as f:
            f.write(text)


if __name__ == "__main__":
    if sys.argv[1] == "make":
        make(sys.argv[2])
    elif sys.argv[1] == "install":
        install(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(__doc__)
