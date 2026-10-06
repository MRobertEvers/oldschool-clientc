# Waves seam pass 7 triage (2026-10-05): what the first fought tests found

Pass: `matthew-mbp-m4-waves-b1-seam7`. Written by the waves orchestrator from test pass 1
(`matthew-mbp-m4-waves-b1-test1`: six early monster units, none kept; reports under
`build/author_state/matthew-mbp-m4-waves-b1-test1/`, rows TEST-1 to TEST-4 and the
authors' content bugs in `docs/minigames/waves_loop/CONTENT_BUGS.md`). Four seams, one
fixer at a time, in this order. The method is seam pass 5's ("How a content seam works
here", `SEAM_TRIAGE_2026-10-04b.md`); the standing rules are `docs/WAVES_ORCHESTRATOR.md`
section 12 (OSRS first, the wiki over the cache, never the 2004 source, prayer checked on
the animation tick).

## driver: npc_record_reads

Units: every unit (all six tests ended blocked on this).

Files: `script/plugins/quest_driver/world.lua`, `script/plugins/plugin_api.meta.lua`, `src/plugin/torirs_plugin_drive.c`, `src/plugin/torirs_plugin_drive.h`, `src/plugin/torirs_plugin_drive_record.c`

Summary. TEST-2: no read-only verb reads an npc record's static fields, so about ten spec
rows per unit (levels, bonuses, model, sounds, idle and walk sequences, animation lengths)
cannot be measured. Add `t.npc.record(symbol_or_slot)`: the cache npc record as the CLIENT
has it (id, name, size, combat level, the ready and walk sequence ids, model ids, the
params that carry sounds) plus the server-side stats our content config gives it (attack,
strength, defence, ranged, magic levels and the bonuses, from the server's record for that
id through the embedded-server query path the tick log uses), each field saying which side
it came from; and `t.seq.length(seq_id)`: frames, client cycles and game ticks from the
cache sequence record. Also fix `t.npc.state`'s `anim_id` reading -1 at rest and while
walking: report the idle or walk sequence the client is playing. Prove on a goblin against
`docs/minigames/inferno/sources/cache_npc.txt` and `cache_seq.txt`, and on one Inferno npc
in wave 1. Conformance rows for each verb. DRIVER_NOTES entries.

Evidence. `CONTENT_BUGS.md` TEST-2; all six `*.author.json` blockers.

## driver: shot_name_length

Units: every unit.

Files: `src/plugin/torirs_plugin_drive_ui.c (or wherever the shot writer names its file: name it)`, `tools/quest_gate/gate.py`

Summary. TEST-3: a shot whose row name runs past 71 characters is written cut at 71 with no
`.png`, and the gate then reports the claimed shot as missing, turning a green row red.
Spec row names are long by construction (`spec.<unit>.<mechanic>`). Fix the writer so the
file name is never cut without its extension (shorten with a stable hash suffix if a limit
is needed, and record the mapping in the ledger row's detail), make the gate find it, and
prove with a 90-character row name. Check that quest tests' shots are unchanged (short
names must produce byte-identical file names).

Evidence. `CONTENT_BUGS.md` TEST-3.

## content: unit_scope_sidecars

Units: the six early monster units; later units will follow the same rule.

Files: `docs/minigames/inferno/encounters/*.scope.tsv`, `docs/minigames/inferno/encounters/wave_table.scope.tsv`, `tools/waves_gate/waves_coverage.py (only if a new scope word is needed)`

Summary. TEST-4: a unit test enters only its unit's first wave and a practice entry fights
one wave, so rows about which waves carry the monster (count_by_wave, waves_present,
first_wave), the wave-66 end collapse and the no-pillar waves 67-68 cannot be measured by
a unit test and the authors tried to sweep every wave to reach them (a wave skip, rejected).
Move each such row's scope to `wave_table` (the wave-table unit's test enters a sample of
waves by design) or to `full_run` (a new scope word for rows only the full-run test can
reach), and mark max-hit rows the authors called luck-dependent as `stat` (settled by the
recorder tool over many runs, never by one). Read every sidecar of the nineteen units and
apply the same rule, listing each move with the reason. The grader must skip and list the
moved rows; coverage of a unit test is then FULL on what one wave can show.

Evidence. `CONTENT_BUGS.md` TEST-4; the sampler's sent_back list.

## content: early_monsters_round2

Units: nibblers_and_pillars, bat, melee, mager_resurrection.

Files: `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/scripts/inferno_ai.rs2`, `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/scripts/inferno_zek.rs2`, `OSRS-Content/osrs239-content/server/scripts/minigames/minigame_inferno/configs/inferno.npc`

Summary. What the fought tests found against the tables: TEST-1 (the nibblers on one
pillar never bite on the same tick: 576 gaps of 1, 2 or 3 and never 4, while each alone
bites every 4; Blert's modal gap is 4, the pack biting together: our per-npc timer is
offset by spawn order where the real nibblers share a cadence; fix so a pack bites together
as the recordings show, citing the Blert distribution), BAT-DRAIN (0 of 102 unprayed hits
drained a stat; the table's row says what the bat drains and by how much: cite the wiki
line and fix), MAGER-RANGE (the mager swings from 18 tiles where the table says 15) and
MAGER-MELEE-CHANCE (40% measured against the table's 50%: re-read the source line first;
a measured 40% over a small sample may be the table's 50%: compute the sample and say so
before changing anything), MELEE-DIG-LAND (the dig lands at offset 1,0 where the table says
north-west of the player: re-read the source and fix). Prove each by playing the unit's
wave with real attacks and quoting tick-log distributions.

Evidence. `CONTENT_BUGS.md` TEST-1, BAT-DRAIN, MAGER-RANGE, MAGER-MELEE-CHANCE, MELEE-DIG-LAND;
the four tables.
