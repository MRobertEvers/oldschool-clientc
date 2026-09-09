# Troll Romance — audit record

Status: in-progress (Gate D open until named BMPs exist)
Worker: gp-troll-c1
Date: 2026-09-09

## Sources (pinned)

| Page | oldid | Retrieved |
|---|---|---|
| [Troll Romance](https://oldschool.runescape.wiki/w/Troll_Romance?oldid=15326713) | 15326713 | 2026-09-09 |
| [Quick guide](https://oldschool.runescape.wiki/w/Troll_Romance/Quick_guide?oldid=14845426) | 14845426 | 2026-09-09 |
| [Transcript](https://oldschool.runescape.wiki/w/Transcript:Troll_Romance?oldid=15327938) | 15327938 | 2026-09-09 |
| QH `trollromance` / `TrollRomance.java` | github.com/Zoinkwiz/quest-helper master | 2026-09-09 |

`python3 tools/questhelper_extract.py trollromance --check --qh-root <qh>`: clean. Cache/pack names win. QH reward row lists cut `diamond`/`ruby`/`emerald`; wiki Rewards and this port grant **uncut** gems.

## QH `steps.put` ladder (`%troll_love`)

| N | QH step | Writer |
|---|---|---|
| 0 | start / talk to Ug | `[opnpc1,trollromance_ug]` |
| 5 | talk to Aga | `[opnpc1,trollromance_aga]` |
| 10 | talk to Tenzing | `[opnpc1,death_sherpa]` option 4 then sled question |
| 15 | talk to Dunstan | `[opnpc1,death_smithy]` "Talk about a quest." / "I need a sled!!" |
| 20 | talk to Dunstan again | same opnpc1 with materials |
| 22 | wax recipe + wax sled | `[opheldu,bucket_wax]` / `[opheldu,swamp_tar]` / `[opheldu,trollromance_toboggon]` |
| 25 | get flower (travel + pick) | waxing writes 25; `[oploc1,trollromance_piste_walk_barrier_down]` slides; `[oploc2,trollromance_rareflowers]` writes 30 |
| 30 | flower to Ug | `[opnpc1,trollromance_ug]` |
| 35 | defeat Arrg | `[opnpc1,trollromance_arrg]` + `trollromance_arrg_attackable` |
| 40 | return to Ug | `[opnpc1,trollromance_ug]` → 45 + `~quest_complete_rewards(quest_trollromance, ...)` |

Endstate 45 is completion (not a QH `steps.put` key).

## Fixes this pass

1. Tenzing no longer auto-hijacks the post-Death-Plateau menu. Option 4 is the wiki Trollweiss line; state 15 advances only on "What would I need to make such a sled?".
2. Dunstan is two talks matching the transcript (QH 15 then 20). First talk cannot consume materials.
3. Waxing the sled writes `^troll_love_waxed_sled` (25) so QH leaves the wax step. Slide no longer writes progress.
4. Lost Trollweiss can be re-picked at state >= 25. Full-inv line matches the wiki trivia quote.
5. Wax mix mes is the transcript "You make some sled wax."
6. Ug start gates boostable Agility 28 (`{{Questreqstart}}` on the article).
7. Arrg attackable: `huntmode=none` `retaliate=no` (scripted `ai_timer` owns both styles).

## Disclosed leftovers

- Piste descent is still a single `p_teleport` (no animated cutscene / second-slope crash).
- Ice-troll cave walk and chill-zone drain are area content, not re-authored here.
- QH cut-gem reward names are rejected in favour of the wiki uncut table.

## Verification

C stanza: `src/torirsserver/test/trollromance_selftest.u.h`, called immediately before `selftest_reset_world`. Player `godmode = 1`. Named `TORIRS_EXIT_BMP` captures belong under this quest's selftest dir only.

Mutation that must go red: delete the `%troll_love = ^troll_love_waxed_sled` write in `trollromance_wax_sled` — `step 22->25 wax via opheldu` fails.
