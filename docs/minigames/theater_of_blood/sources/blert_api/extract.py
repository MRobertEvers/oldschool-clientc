#!/usr/bin/env python3
"""Distil blert per-tick event streams into the measurement tables cited by
docs/TOB_RESEARCH.md.

Input:  a directory of raw streams named <uuid>_<stage>.json, as produced by
        harvest3.py (GET /api/v1/raids/tob/{uuid}/events?stage=N).
Output: one CSV per measurement, written next to this script.

Stages: 10 Maiden, 11 Bloat, 12 Nylocas, 13 Sotetseg, 14 Xarpus, 15 Verzik.
Event types (blert proto): 7 NPC_SPAWN, 8 NPC_UPDATE, 9 NPC_DEATH, 10 NPC_ATTACK,
  101 MAIDEN_BLOOD_SPLATS, 110 BLOAT_DOWN, 111 BLOAT_UP, 112 BLOAT_HANDS_DROP,
  113 BLOAT_HANDS_SPLAT, 120 NYLO_WAVE_SPAWN, 121 NYLO_WAVE_STALL,
  122 NYLO_CLEANUP_END, 123 NYLO_BOSS_SPAWN, 130 SOTE_MAZE_PROC,
  131 SOTE_MAZE_PATH, 140 XARPUS_PHASE, 141 XARPUS_EXHUMED, 150 VERZIK_PHASE.
NpcAttack ids used: 1 maiden auto, 2 maiden blood throw, 12 verzik p1 auto.
"""
import csv, glob, json, os, sys, collections

RAW = sys.argv[1] if len(sys.argv) > 1 else "/tmp/blertdata"
OUT = os.path.dirname(os.path.abspath(__file__))

MAIDEN_IDS = range(8360, 8366)
CRAB_IDS = (8366, 10828, 10820)
BLOAT_IDS = (8359, 10813, 10812)
SOTE_ACTIVE, SOTE_IDLE = 8388, 8387
NYLO_BOSS = {8355: "melee", 8356: "range", 8357: "mage"}

def streams(stage):
    for f in sorted(glob.glob(f"{RAW}/*-*_{stage}.json")):
        uuid = os.path.basename(f)[:36]
        ev = json.load(open(f))
        if isinstance(ev, list) and ev:
            yield uuid, sorted(ev, key=lambda e: e["tick"])

def write(name, header, rows):
    with open(f"{OUT}/{name}", "w", newline="") as fh:
        w = csv.writer(fh); w.writerow(header); w.writerows(rows)
    print(f"{name}: {len(rows)} rows")

def hp_of(e):
    v = e["npc"]["hitpoints"]
    return (v >> 16, v & 0xFFFF)

# --- Maiden: attack ticks and types (M1, M2) + crab spawns (M4) -------------
atk_rows, crab_rows = [], []
for uuid, ev in streams(10):
    atks = [(e["tick"], e["npcAttack"]["attack"]) for e in ev
            if e["type"] == 10 and e["npc"]["id"] in MAIDEN_IDS]
    for i, (t, a) in enumerate(sorted(set(atks))):
        atk_rows.append([uuid, i, t, a, "blood_throw" if a == 2 else "blackstorm"])
    ids, last = [], None
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in MAIDEN_IDS and e["npc"]["id"] != last:
            ids.append(e["tick"]); last = e["npc"]["id"]
    groups = collections.defaultdict(list)
    for e in ev:
        if e["type"] == 7 and e["npc"]["id"] in CRAB_IDS:
            groups[e["tick"]].append((e["xCoord"], e["yCoord"]))
    for t in sorted(groups):
        for x, y in groups[t]:
            crab_rows.append([uuid, t, t in ids, len(groups[t]), x, y])
write("maiden_attacks.csv", ["raid", "attack_index", "tick", "attack_id", "attack"], atk_rows)
write("maiden_crab_spawns.csv",
      ["raid", "tick", "tick_is_maiden_id_change", "crabs_this_spawn", "x", "y"], crab_rows)

# --- Bloat: downs/ups and hand drops vs hp (M6, M17) -----------------------
rows = []
for uuid, ev in streams(11):
    hp = {e["tick"]: hp_of(e) for e in ev if e["type"] in (7, 8) and e["npc"]["id"] in BLOAT_IDS}
    for e in ev:
        if e["type"] == 110:
            d = e["bloatDown"]
            rows.append([uuid, "down", e["tick"], d["downNumber"], d.get("walkTime"), "", ""])
        elif e["type"] == 111:
            rows.append([uuid, "up", e["tick"], "", "", "", ""])
        elif e["type"] in (112, 113):
            k = max([x for x in hp if x <= e["tick"]], default=None)
            cur, base = hp.get(k, (0, 0))
            rows.append([uuid, "hands_drop" if e["type"] == 112 else "hands_splat",
                         e["tick"], "", "", len(e["bloatHands"]),
                         round(100 * cur / base, 2) if base else ""])
write("bloat_events.csv",
      ["raid", "event", "tick", "down_number", "walk_ticks", "hand_count", "bloat_hp_pct"], rows)

# --- Nylocas: waves, cleanup, boss spawn, boss style switches (M8, M9) -----
rows, style_rows = [], []
for uuid, ev in streams(12):
    w1 = next((e["tick"] for e in ev if e["type"] == 120), None)
    cleanup = next((e["tick"] for e in ev if e["type"] == 122), None)
    boss = next((e["tick"] for e in ev if e["type"] == 123), None)
    if cleanup is not None and boss is not None and w1 is not None:
        phase = (cleanup - w1) % 4
        predicted = cleanup + 16 + ((4 - phase) % 4)
        rows.append([uuid, w1, cleanup, boss, boss - cleanup, phase, predicted, predicted == boss])
    last = None
    for e in ev:
        if e["type"] in (7, 8) and e["npc"]["id"] in NYLO_BOSS and e["npc"]["id"] != last:
            style_rows.append([uuid, e["tick"], NYLO_BOSS[e["npc"]["id"]]]); last = e["npc"]["id"]
write("nylo_boss_spawn.csv",
      ["raid", "wave1_tick", "cleanup_end", "boss_spawn", "delta", "cleanup_cycle_phase",
       "predicted_boss_spawn", "prediction_correct"], rows)
write("nylo_boss_styles.csv", ["raid", "tick", "style"], style_rows)

# --- Sotetseg: maze proc / reactivation / first attack after (M10) ---------
rows = []
for uuid, ev in streams(13):
    atk = [e["tick"] for e in ev if e["type"] == 10]
    procs = [e["tick"] for e in ev if e["type"] == 130]
    reacts, last = [], None
    for e in ev:
        if e["type"] in (7, 8, 9) and e["npc"]["id"] in (SOTE_ACTIVE, SOTE_IDLE):
            if e["npc"]["id"] != last:
                if last == SOTE_IDLE and e["npc"]["id"] == SOTE_ACTIVE:
                    reacts.append(e["tick"])
                last = e["npc"]["id"]
    for i, r in enumerate(reacts):
        nxt = [t for t in atk if t > r]
        rows.append([uuid, i, procs[i] if i < len(procs) else "", r,
                     nxt[0] if nxt else "", (nxt[0] - r) if nxt else ""])
write("sote_maze.csv",
      ["raid", "maze_index", "proc_tick", "reactivate_tick", "first_attack_after",
       "gap"], rows)

# --- Xarpus: exhumeds and phases (M12-M15) --------------------------------
rows = []
for uuid, ev in streams(14):
    ph = {e["xarpusPhase"]: e["tick"] for e in ev if e["type"] == 140}
    ex = [(e["tick"], e["xarpusExhumed"]) for e in ev if e["type"] == 141]
    ex.sort(key=lambda x: x[1]["spawnTick"])
    for i, (despawn, x) in enumerate(ex):
        rows.append([uuid, i, x["spawnTick"], despawn, despawn - x["spawnTick"],
                     x["healAmount"], ";".join(map(str, x["healTicks"])),
                     len(ex), ph.get(1, ""), ph.get(2, "")])
write("xarpus_exhumeds.csv",
      ["raid", "index", "spawn_tick", "despawn_tick", "lifetime", "heal_amount",
       "heal_ticks", "exhumed_count", "p2_tick", "p3_tick"], rows)

# --- Verzik: phase transitions and attack cadence (M16, M20) --------------
rows = []
for uuid, ev in streams(15):
    ph = [(e["tick"], e["verzikPhase"]) for e in ev if e["type"] == 150]
    atk = [(e["tick"], e["npcAttack"]["attack"]) for e in ev if e["type"] == 10]
    p1 = [t for t, a in atk if a == 12]
    rows.append([uuid, "p1_first_auto", p1[0] if p1 else "", "",
                 ";".join(map(str, p1[:8]))])
    for pt, pv in ph:
        nxt = [t for t, _ in atk if t >= pt]
        rows.append([uuid, f"phase{pv}", pt, (nxt[0] - pt) if nxt else "",
                     nxt[0] if nxt else ""])
write("verzik_phases.csv",
      ["raid", "marker", "tick", "ticks_to_first_attack", "detail"], rows)
