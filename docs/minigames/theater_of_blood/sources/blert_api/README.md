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
`status=eq1` selects completed raids. Numeric filters take a comparator prefix
(`eq`/`lt`/`gt`/`le`/`ge`/`ne`, or the symbol forms), e.g. `downNumber=eq1`.

**Throttle from the start.** A first crawl at ~3 requests/second was rate-limited with
HTTP 429 partway through; everything here was re-fetched at one request every 3 seconds.
This is a volunteer-run service and the event streams are ~200–400 KB each.

## Files

| File | What it is |
|---|---|
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
