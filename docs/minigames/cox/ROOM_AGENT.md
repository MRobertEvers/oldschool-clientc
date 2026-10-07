# Chambers of Xeric — per-room orchestrator brief

One agent owns **one room**. Parallel agents must not edit each other's files.
This is the CoX instance of [`docs/RAID_ORCHESTRATOR.md`](../../RAID_ORCHESTRATOR.md)
section 3 (spec) + section 6 (room test with tick ledger), proved through the
AutomationRunner (`./launch run osrs239-scripts` / `tools/raid_gate/run.py`).

## Room ownership (edit only these)

| Room id (`t.raid.enter`) | Content script(s) | Spec table | Test |
|---|---|---|---|
| `tekton` | `cox_tekton.rs2` | `encounters/tekton.tsv` | `test/raids/cox_tekton.lua` |
| `guardians` | `cox_guardians.rs2` | `encounters/guardians.tsv` | `test/raids/cox_guardians.lua` |
| `vespula` | `cox_vespula.rs2` | `encounters/vespula.tsv` | `test/raids/cox_vespula.lua` |
| `icedemon` | `cox_icedemon.rs2` | `encounters/icedemon.tsv` | `test/raids/cox_icedemon.lua` |
| `tightrope` | `cox_puzzles.rs2` (tightrope procs only) | `encounters/tightrope.tsv` | `test/raids/cox_tightrope.lua` |
| `crabs` | `cox_crabs.rs2` | `encounters/crabs.tsv` | `test/raids/cox_crabs.lua` |
| `thieving` | `cox_puzzles.rs2` (thieving procs only) | `encounters/thieving.tsv` | `test/raids/cox_thieving.lua` |
| `resource` | `cox_resource.rs2`, `cox_herblore.rs2`, `cox_bats.rs2` | `encounters/resource.tsv` | `test/raids/cox_resource.lua` |
| `shamans` | `cox_minions.rs2` (shaman procs only) | `encounters/shamans.tsv` | `test/raids/cox_shamans.lua` |
| `mystics` | `cox_minions.rs2` (mystic procs only) | `encounters/mystics.tsv` | `test/raids/cox_mystics.lua` |
| `vasa` | `cox_vasa.rs2` | `encounters/vasa.tsv` | `test/raids/cox_vasa.lua` |
| `vanguards` | `cox_vanguards.rs2` | `encounters/vanguards.tsv` | `test/raids/cox_vanguards.lua` |
| `muttadiles` | `cox_muttadiles.rs2` | `encounters/muttadiles.tsv` | `test/raids/cox_muttadiles.lua` |
| `scavenger_small` | `cox_scavengers.rs2` | `encounters/scavenger_small.tsv` | `test/raids/cox_scavenger_small.lua` |
| `olm` | `cox_olm.rs2` | `encounters/olm.tsv` | `test/raids/cox_olm.lua` |

**Shared files are forbidden** unless the change is an additive constant /
varp / npc block prefixed with your room name (`^cox_<room>_…`,
`%cox_<room>_…`, `raids_<room>_…` authored rows). Never rewrite another room's
symbols. Never edit `cox_layout.rs2`, `cox.rs2` (except a new `::coxrun` gate
block clearly marked for your room), `cox_points.rs2`, or sibling rooms.
Never rename trees to `*.skip` (workspace rule `no-park-sibling-content`).

Config roots:

- `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/`
- `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/configs/`

## Precedence (sources)

1. Jagex statements in wiki `{{CiteTwitter}}` / `{{CiteDiscord}}` under
   `docs/minigames/cox/sources/wiki_*.wikitext`
2. Wiki article text (`?action=raw` mirrors in `sources/`)
3. Measured community data — `COX_MECHANICS.md`, `COX_PLAN.md` §11, plugin
   constants under `sources/runelite/`, `sources/de0/`, `sources/openosrs_coxhelper/`
4. Near-Reality / Zenyte behaviour described in `COX_NEARREALITY_PORT_PLAN.md`
   (Java tree may be absent on this machine; the plan + dossiers are the brief)
5. Existing tree code — only where 1–4 are silent

## Done means

A room is complete when **all** of the following hold:

1. **Mechanics** — every kill-path mechanic in `COX_MECHANICS.md` for the room
   is implemented (animations, npc forms, stats, AI timers/queues, hazards).
2. **Spec table** — `encounters/<room>.tsv` validates with
   `python3 tools/raid_gate/spec_check.py docs/minigames/cox/encounters/<room>.tsv`
   and every kill-path row is grade C or better, or listed open with what
   would close it.
3. **AutomationRunner test** — `test/raids/cox_<room>.lua` drives the fight
   for real (no `::godmode`, no narrated kill, no teleport past a phase):
   `t.raid.enter("cox", "<room>", { seed = 1 })`, barrier/approach by click,
   damage from player attacks, technique rows, tick-ledger `spec.*` rows.
   Pattern: `test/raids/tob_maiden.lua` + `test/raids/README.md` +
   `docs/minigames/raid_loop/DRIVER_NOTES.md` (CoX sections).
4. **Gate green** —
   `python3 tools/raid_gate/run.py cox_<room> --no-publish` then
   `python3 tools/raid_gate/gate.py cox_<room>` with coverage FULL
   (`python3 tools/raid_gate/raid_coverage.py cox_<room>`).
5. **Screenshots** — the published play evidence under
   `OSRS-Content/.../selftest/minigames/cox/<room>/play/` includes shots of
   idle/approach, mid-mechanic, and clear; also copy key frames to
   `/opt/cursor/artifacts/cox_<room>_*.png` for the walkthrough.
6. **Selftest** — room gates in `cox_selftest.rs2` stay green:
   `sh tools/cox_verify.sh` (requires `cache.osrs239`); timers armed:
   `python3 tools/cox_check_timers.py`.

## How to run / watch

```sh
# grade (virtual clock — prefer this while iterating)
python3 tools/raid_gate/run.py cox_<room> --no-publish
python3 tools/raid_gate/gate.py cox_<room>
python3 tools/quest_gate/fail.py cox_<room>   # never cat ledger.tsv whole

# watch in AutomationRunner (owner profile)
python3 tools/raid_gate/prepare_scripts.py
./launch run osrs239-scripts
# Scripts tab → Suite Raids → search cox_<room> → Play
# Runner has its own camera (AutomationRunner view); use Interact off while watching
```

Diagnose failures from the tick log first (`tools/raid_gate/raid_report.py`),
not by replaying visually. Save visual verification for a green run.

## Standing rules

- Assert contracts in C (`CLAUDE.md`); do not add silent NULL returns.
- Mutate only in a throwaway worktree when proving a gate can fail.
- Quote the source line in every constant change and every CONTENT_BUGS row.
- Output discipline: no command dumps >4 KB into a tool result; log to a file
  and `tail` / `grep`.
- Branch: stay on the assigned room branch; commit often; push with
  `git push -u origin <branch>`. Parent PR base is `v3`.
