#!/usr/bin/env python3
"""Queries behind encounters/wave_presentation.tsv: OBSERVED Blert events only, cached sample, no network.
    python3 docs/minigames/colosseum/encounters/wave_presentation.queries.py
NPC_DEATH (type 9) is a DESPAWN (blert/PROVENANCE.md section 1, line 52): the tick the npc left the scene, with
the death animation inside it. NPC_UPDATE (type 8) hitpoints are packed (current<<16 | base); a fall of current
between two ticks is a damage hitsplat (observed). The killing hitsplat IS seen (the last update's hitpoints are 0 for most
instances); death tick minus that tick is the despawn lifetime after the killing hit, death animation inside it.
The Fremennik berserker is the exception: the recorder's asserted base of 50 against the cache's 48 (D15) leaves its last
update at 2 in all 196 instances; that final drop is the killing hit and is counted as such.
Also: the tick of the spawn events (first sighting) per kind, and the wave start events (type 200/201 ignored)."""
import collections, glob, json, os
API = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sources", "blert_api")
NAMES = {12810: "jaguar_warrior", 12811: "serpent_shaman", 12812: "minotaur", 12813: "minotaur_12813", 12814: "fremennik_archer",
         12815: "fremennik_seer", 12816: "fremennik_berserker", 12817: "javelin_colossus", 12818: "manticore",
         12819: "shockwave_colossus", 12821: "sol_heredit", 12824: "other_12824", 12825: "totem_12825"}


def dist(c):
    return " ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def q_death_lag():
    lag = collections.defaultdict(collections.Counter); gap_update = collections.defaultdict(collections.Counter)
    killedhp = collections.defaultdict(collections.Counter); n = collections.Counter()
    for f in sorted(glob.glob(API + "/*.json")):
        d = json.load(open(f))
        for wn, ev in d["waves"].items():
            inst = collections.defaultdict(dict); death = {}
            for e in ev:
                if "npc" not in e: continue
                r = e["npc"]["roomId"]
                if e["type"] in (7, 8): inst[(r, e["npc"]["id"])][e["tick"]] = e["npc"]["hitpoints"] >> 16
                if e["type"] == 9: death[(r, e["npc"]["id"])] = e["tick"]
            for k, t in death.items():
                hp = inst.get(k)
                if not hp: continue
                ticks = sorted(hp); last_dmg = None
                for a, b in zip(ticks, ticks[1:]):
                    if hp[b] < hp[a]: last_dmg = b
                last_upd = ticks[-1]
                nm = NAMES.get(k[1], k[1]); n[nm] += 1
                gap_update[nm][t - last_upd] += 1
                if last_dmg is not None and (hp[last_upd] == 0 or (k[1] == 12816 and hp[last_upd] == 2)): lag[nm][t - last_dmg] += 1
                killedhp[nm][hp[last_upd] == 0] += 1
    print("deaths with an update stream:", dist(n))
    print("death tick minus the last NPC_UPDATE tick (0 would mean the despawn is the same tick):")
    for nm in sorted(gap_update): print("  ", nm, dist(gap_update[nm]))
    print("last update hitpoints == 0 (True) or still above 0 (False; kill hit not observed or other cause):")
    for nm in sorted(killedhp): print("  ", nm, dist(killedhp[nm]))
    print("death tick minus the tick of the final hitsplat that took hitpoints to 0 (= despawn lifetime after the killing hit):")
    for nm in sorted(lag):
        c = lag[nm]; v = sorted(c.elements()); print("  ", nm, "n=%d min=%d median=%d max=%d" % (len(v), v[0], v[len(v) // 2], v[-1]), dist(c))


def q_spawn_death_counts():
    sp = collections.Counter(); de = collections.Counter(); w12 = collections.Counter()
    for f in sorted(glob.glob(API + "/*.json")):
        d = json.load(open(f))
        for wn, ev in d["waves"].items():
            s = set(); dd = set()
            for e in ev:
                if "npc" not in e: continue
                k = (e["npc"]["roomId"], e["npc"]["id"])
                if e["type"] == 7 and k not in s: s.add(k); sp[NAMES.get(k[1], k[1])] += 1
                if e["type"] == 9 and k not in dd: dd.add(k); de[NAMES.get(k[1], k[1])] += 1
    print("spawn events (type 7, first per instance):", dist(sp)); print("death events (type 9):", dist(de))


if __name__ == "__main__":
    q_death_lag(); q_spawn_death_counts()
