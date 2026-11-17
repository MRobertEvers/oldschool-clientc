# Tai Bwo Wannai Trio — port from RSProx

Content: `quests/quest_tbwt/` and `areas/area_karamja/scripts/tbwt_*.rs2`,
`trufitus.rs2`; shared fixes in `skill_herblore/scripts/herblore.rs2` (grind
line type), `skill_cooking/scripts/cooking.rs2` (bone hook),
`skill_fishing/scripts/fishing_spots/tbwt.rs2`. Test: `test/quests/tbwt.lua`.

The content was a LostCity (2004) port. Most of its text survived into OSRS,
but moods, menus, timing, the hunt scenes, the journal and the post-quest
rewards (rewritten by Jagex in 2026) did not.

## Sessions

| index | rev | what it covers |
|---|---|---|
| 4504 | 242 | Trufitus's pointer, Timfraku and the start (stage 0..3), the village talk, Tamayu's intro and first hunt, Tiadeche's intro |
| 4572 | 242 | Lubufu (apprentice, karambwanji, vessel), Tiadeche's three attempts and the catch (321: 2..4), Tamayu's potions + black spear + two hunts, Tinsay's intro |
| 4573 | 242 | Tinsay's three requests and his finish, the manual (321: 4..6), the poisoned spear and the kill (320: 3..4), Timfraku's reward and the completion (320: 4..6), the three village rewards |
| 4574 | 242 | GE/bank prep; one journal open |
| 213 (rsprox.net 75) | 227 | Leagues V full playthrough [3068..5138]: Lubufu's first meeting, the "nothing really" reward, the old Tinsay reward |
| 1099 (rsprox.net 1102) | 237 | Leagues VI: the adventurer start, Trufitus before the quest |

## Action inventory (rsprox_actions.py, every row a verdict)

| action | verdict |
|---|---|
| opnpc1 Timfraku, Tamayu, Tinsay, Tiadeche, Lubufu; opnpct on each brother | this content, rebuilt below |
| opnpc1 Trufitus | this content (pointer), `trufitus.rs2` fixed |
| opnpc1 general_karambwan / lubufu_karambwan | this content: no-vessel messagebox added, level line already right |
| oploc1 tbwt_bamboo_door (x10) | this content: was a 500-tick swing, now the recorded walk-through |
| oploc1 ladder 16683 / laddertop 16679 (x22) | generic, Timfraku's hut; existing climb (test passes up and down) |
| oploc1 zq_logbalance, zqclimbingrocks | generic agility shortcuts (test crosses both) |
| opnpc2 monkey, jogre; opobj3 corpses/bones | this content (monkey corpse drop, existing); jogre fight generic |
| use-on crafting (banana+knife, rum, seaweed, paste, spear) | this content: lines checked, three fixed |
| oploct fire / range with bones, paste | this content: fire lines + timing fixed |
| pestle on karambwan(ji) | this content + generic grind (type fixed for all grinds) |
| opnpc2 tbwt_beast (4504 [1882]) | post-quest Shaikahan gate, existing; no fight recorded |
| oploc4 jungle vegetation, opnpc on tbwcu_* villagers, sandwich lady | Tai Bwo Wannai Cleanup / random events: out of scope, not audited |
| captain_barnaby, gangplanks, tablets, spells, bank, GE, shops, Zembo, bartenders | generic travel/services; Zembo's spawn tile matches (2925,3143) |
| quest list / journal (x18) | this content: journal rebuilt |

Teleports: ladders (±1 plane), boats, tablets/spells (generic), and the
hunt moves 2845,3046 -> 2520,4568 (the scene, below).

## Stages (all writes)

| var | value | goes out with | recorded |
|---|---|---|---|
| 320 tbwt_main | 0->1 | the menu after the title answer | 4504 [521], 213 [3073] |
| | 1->2 | "Start the Tai Bwo Wannai Trio quest?" | 4504 [665] |
| | 2->3 | "Ok, I'll do it." + start line | 4504 [696] |
| | 3->4 | last brother's closing page click | 4573 [4836] |
| | 4->5 | coins objbox | 4573 [4906] |
| | 5->6 | scroll, on the box's click | 4573 [4907] |
| 321 tbwt_tiadeche | 0->1->2 | intro pages | 4504 [1957], [1966] |
| | 2->3 | loaded vessel objbox | 4572 [1498] |
| | 3->4 | "Take a Karambwan vessel..." | 4572 [1552] |
| | 4->5 | manual objbox | 4573 [4082] |
| | 5->6 | "You hand over the crafting manual" | 4573 [4275] |
| | 6->7 | village reward objbox | 4573 [4969] |

320 and 321 are `perm`/`transmit=yes` in `quest_tbwt.varp`. Jagex sends no
other quest var: Tamayu/Tinsay/Lubufu state is server-only (ours: perm,
transmit=no, varps 6726..6729).

## Pages

All recorded pages of the six sessions checked mechanically (text verbatim,
then speaker + mood). Fixed: ~45 moods, Timfraku's "You ask too many
questions!", "What do you want, stranger?", "Is there anything I need?",
the "<col=0000ff>Return to previous menu", the 4-option village menu, the
"Yes./No." start menu, Tinsay's colour tags and his "hand it here" exchange,
Lubufu's 3-option top menu before the karambwan topic and his 3-option
"something else", Tamayu's four verdict lines ("Shaikhan is at present far
too agile for me!" etc., trailing/leading spaces as sent), the three village
rewards (rev 242 text and order; Tinsay's box carries the forced second row
"You learn how to cook Karambwan properly."). Trufitus's forced `|` breaks
removed.

## Interaction shapes (from the click C of the last page)

| interaction | recorded | ours |
|---|---|---|
| Timfraku thinks | mes on C, page C+4 (4/4: 4504, 213, 1099) | was C+5, fixed |
| Tiadeche karambwanji | mes C, human_smallnet C+2, page C+6 | was human_octopus_pot, C+11; fixed |
| Tiadeche vessel (x2) | mes + face player tile C, human_octopus_pot C+2, page C+12; empty vessel back on the page click | fixed |
| Tinsay mutters / thinks | mes C (or nothing), page C+4 | was C+5; fixed |
| Tinsay hand-over | objbox, seq on its click (human_drink_rum for rum, human_eat else), page +4; last meal human_wretch +2, page +9, jingle 188 | rum was human_eat; jingle missing; fixed |
| Tamayu skin | human_cooking on the box click, items +4 | matched; gate was "after the kill", recorded before it once assisted: fixed |
| Tamayu potion | "Thank you" page, then human_eat, verdict +4 | matched |
| Lubufu thinks | mes + emote_think C, page C+3 | matched |
| Bones on tinderbox | drop + human_createfire + spam C; fire (shape 10 rot 1) + fire_lit C+4; out at +48, private burnt bones (despawn 75) +1 | lines were game messages, bones dropped public before the fire went; fixed |
| Paste-bones on fire | spam line + human_firecooking + fry + swap on the op tick | was 2 ticks later, wrong line; fixed |
| Bamboo door | inviswall in slot, `_inactive` leaf far tile rot+1, bamboodoor_open, one step, 2 ticks | was a 500-tick swing; fixed |

## Cutscenes (§2b)

No scene sets `cutscene_status`; there is no fade. Two shapes, both ported
(`tbwt_tamayu.rs2`), `docs/quests/CUTSCENES.tsv` row `tbwt`:

| scene | sessions | frame |
|---|---|---|
| failed hunt | 4504 [1499..1531], 4572 [1894..1927], [2038..2071] | T move to 2520,4568 facing 2523,4567, hunter (east) + beast (south-west) added face to face; T+1 look 2523,4567 h160 / eye 2518,4571 h350 at 232/232; eyes T+5,9,13,17,21; exchanges every 3 ticks from T+2; T+25 back beside Tamayu, cam_reset, three angry verdict pages |
| the kill | 4573 [4786..4815] | first exchange on the adds; look+eye every 3 ticks from T+2; T+14 player to 2537,4568, killing pair added, look 2541,4565 h320 / eye 2552,4566 h700; spikes T+15..24, says at T+15/T+21, jingle 190 T+21, beast_death T+24; first pair fights on unheard; T+29 back, cam_reset, no page |

Exchange: 16 blocks of 36 recorded; spike = staff_crush + beast_hit, block =
beast_attack + one of human_hit..human_hit4 (ours used staff_stab and no hit).

## Npc info

All five brothers and Lubufu wander (1-tile walk steps, 92..133 per type);
hunters and beasts stand. Spawn angles of the cast copied. Faces on pages
are the engine's. Gap: Jagex lets a wanderer keep walking through a dialogue
(Tiadeche 4572 [1425..1428]); our chat freezes it (engine, not changed).
House Tamayu at stage 6 is the shell's `tbwt_tamayu` rung (no Trade); our
`multinpc7=tbwt_tamayu_final` override only applies at an unused value.

## Journal

Ten states recorded (4504 [705], [1451], [1559], [2052]; 4572 [1189],
[1582], [2105]; 4573 [661], [2358], [3083], [4107], [4718]): older format,
rows sent pre-wrapped. `tbwt_journal.rs2` rebuilt row for row with
`~quest_journal_rows`; not started opens the overview. UNVERIFIED: Lubufu's
rows before the lesson, Tamayu's after the kill, stage 4, the completed one.

## Wiki corrections (from the recordings)

Prepared for Transcript:Tai_Bwo_Wannai_Trio: Timfraku's "!" on the recorded
branch; skinning before the kill once Tamayu is helped; the stuffed-monkey
skinning; "It's monkey skin."; Tinsay's "skin a monkey?" answers; the
karambwan-on-spear line; the two fire messageboxes; the no-vessel line; the
2026 post-quest Tiadeche and Tinsay rewards. Also contradicts the quest page:
a black spear counts as "enough damage" (4572 [2069]).

## Verification (2026-11-16)

`run.py tbwt`: 263/263 PASS (1645 ticks), no script abort. Our tick log
(`build/quest_gate/tbwt/ticklog.tsv`) against the recordings:

| section | recorded | ours |
|---|---|---|
| failed hunt | adds T, exchanges T+2..T+23 / 3, synth pairs | adds 909, exchanges 911..932 / 3, same pairs: matched |
| the kill | 4573 [4786..4815] | 973..1002: exchanges T..T+12, pair 2 at T+14, says T+15/T+21, jingle 190 T+21, beast_death T+24, pair 1 silent: matched |
| Tinsay finish | eat, jingle 188, wretch +2 | 1408, 1408, 1410: matched |
| manual | jingle 189 on the box click | 1524: matched |
| bamboo doors (x6) | inviswall rot 2 + inactive rot 3 far tile, bamboodoor_open, back after 2 | 1594..1633: matched |
| bones fire | bones_in_paste_fire shape 10 rot 1 + fire_lit | 621: matched |

## Open

- Unrecorded branches kept from LostCity, each marked `[unverified]` in the
  scripts (pre-Jungle Potion Timfraku, "So far??", two reward answers,
  "When will you be finished?", the post-reward chats, Lubufu's extra topics).
- A (kp) spear below iron and a plain poisoned spear: verdict lines are the
  wiki's.
- Dialogue freezes wanderers (engine).
