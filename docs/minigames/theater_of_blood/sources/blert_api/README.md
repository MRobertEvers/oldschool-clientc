# Blert HTTP API — harvested evidence

Provenance for the measurements in [`docs/TOB_RESEARCH.md`](../../../TOB_RESEARCH.md).
Pulled 18 August 2026 from <https://blert.io>, which serves an unauthenticated JSON API
alongside its website. Nothing here is authored by this project.

## Endpoints used

```
GET /api/v1/challenges?type=1&mode={10|11|12}&scale=eq{1..5}&status=eq1[&startTime=lt<epoch_ms>]
GET /api/v1/raids/tob/{uuid}                       # one raid's summary
GET /api/v1/raids/tob/{uuid}/events?stage={10..15} # full per-tick event stream
GET /api/v1/trends/bloat-downs[?downNumber=eq{n}&mode=&scale=]
GET /api/v1/trends/bloat-hands[?mode=&intraChunkOrder=]
```

`type=1` is the Theatre of Blood. `mode` is `10` Entry, `11` Regular, `12` Hard.
`stage` is `10` Maiden, `11` Bloat, `12` Nylocas, `13` Sotetseg, `14` Xarpus, `15` Verzik.
`status=eq1` selects completed raids (Blert's ChallengeStatus: 1 completed, 2 reset, 3 wiped);
a raid whose `stage` is past a room completed that room even when it reset later, which is
how `reference/` counts a room as done.  **Entry mode is `mode=10`**, and Blert holds very
few Entry raids: on 2026-10-06 `mode=10&scale=eq1` listed three challenges in all, of which
ONE has a recorded Maiden room (`6ae3d9f8`, 82 ticks; `664c1f8b` completed but its Maiden
stream is empty, `ac51db90` reset inside Maiden). Numeric filters take a comparator prefix
(`eq`/`lt`/`gt`/`le`/`ge`/`ne`, or the symbol forms), e.g. `downNumber=eq1`.

**Throttle from the start.** A first crawl at ~3 requests/second was rate-limited with
HTTP 429 partway through; everything here was re-fetched at one request every 3 seconds.
This is a volunteer-run service and the event streams are ~200–400 KB each.

## Per-room references (`reference/`, seam40)

`tools/raid_gate/blert_reference.py <room> --mode <entry|normal|hard> --scale <1-5> [--rooms N]
[--offline]` harvests N successful rooms (the raid got past the room, nobody died in it;
rooms with a death are used only when no candidate is death-free, and the file says how
many) at one request per three seconds, caches each stage stream as
`build/blert/<room>/<uuid>.json` beside its challenge-list entry (`.json.meta`; a room
Blert recorded nothing of gets a `.json.empty` marker so it is never asked for again), and
writes `reference/<room>_<mode>_<scale>.json` plus the tables in `reference/README.md`:
outcomes (room and phase ticks, the boss's hitpoints lost per tick per phase, heals,
leaks, deaths, hitpoints lost by the recorders), per role (weapons per phase, attack
gaps, the protection prayer against each boss attack and how long it had been lit when
the attack was sent, eats and drinks, where the role stands relative to the boss's SW
tile and how far from the nearest raider), reactions (ticks from a phase start or a boss
attack to each role's first attack, swap, step, prayer change) and the boss's own
attacks.  `tools/raid_gate/raid_report.py RUN --against reference/<file>` prints a run's
same numbers beside the reference's median and range and flags every one outside it.

Written so far: `maiden_normal_3.json` (24 of the 26 trio rooms of
`maiden_trio_crabs/`, the other two had a death in Maiden) and `maiden_entry_1.json` (the
one Entry solo room above: a single room is a sample, not a range).

Decoding notes (measured on those streams): a player's `hitpoints` and `prayer` are
`current << 16 | base` and only the recording player (`source` 0) carries them; an
npc's `hitpoints` is the same packing; `prayerSet` bits follow Blert's Prayer enum
(16 Protect from Magic, 17 Missiles, 18 Melee, 26 Piety, 27 Rigour, 28 Augury);
`equipmentDeltas` entries are `slot << 48 | item id << 32 | added << 31 | quantity`
(slot 4 the weapon); npc coordinates are the SW tile (Maiden at 3162,4444, size 6).

## Files

| File | What it is |
|---|---|
| `reference/` | per-room references from `tools/raid_gate/blert_reference.py` (above), with Blert's own `attack_definitions.json` / `spell_definitions.json` |
| `harvest3.py` | throttled fetcher (3 s/request), by mode and scale |
| `harvest2.py` | earlier fetcher, kept for its `startTime=lt<epoch_ms>` backwards-paging idiom |
| `extract.py` | reduces raw streams to the CSVs below; its docstring records the event-type and NpcAttack id numbers observed |
| `trend_bloat_downs_all.json` | blert's aggregate over 216 886 recorded Bloat downs |
| `trend_bloat_downs_{1..4}.json` | the same split by down number — `_1` is the 98 445-sample first-walk distribution behind M17 |
| `trend_bloat_hands.json` | 5 802 952 hands across 26 095 Bloat rooms, bucketed by tile |
| `maiden_attacks.csv` | 608 Maiden attacks: tick and blackstorm-vs-blood (M1, M2) |
| `maiden_crab_spawns.csv` | 462 crab spawns: tile, count, and whether the tick was a transmog tick (M4) |
| `maiden_blood_trail_runs.csv` | every blood-trail tile's contiguous active run (M5) |
| `bloat_events.csv` | downs, ups and hand drops/splats with Bloat's HP % at the time (M6, M17) |
| `nylo_boss_spawn.csv` | wave-1 tick, cleanup end, boss spawn, and the predicted spawn from the cycle formula (M8) |
| `nylo_boss_styles.csv` | 185 Vasilias style switches (M9) |
| `sote_maze.csv` | 26 mazes: proc, re-activation, first attack after (M10) |
| `xarpus_exhumeds.csv` | 391 exhumeds: spawn, despawn, lifetime, heal amount, heal ticks (M12–M15) |
| `verzik_phases.csv` | P1 opening and both phase transitions per raid (M16, M20) |

Raw event streams are **not** committed (~30 MB); regenerate them with `harvest3.py`.

## The one trap

Blert's stream mixes **observed** events (animations, projectiles, ground/graphics objects,
npc id changes, spawns and despawns) with **asserted** ones that its own tick clock
generates — Xarpus' spits and turns, and Verzik's P3 opening, are blert constants, not
observations. Measuring those from this data is circular. See the provenance audit in
`TOB_RESEARCH.md` before drawing a conclusion from any attack-cadence figure.
