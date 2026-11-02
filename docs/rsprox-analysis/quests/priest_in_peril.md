# Priest in Peril — RSProx index 4459

Recorded 2026-11-02, rev 241.1, world 495, one session end to end (2,106 ticks), labelled
"Priest In Peril": King Roald's start [91..165], the bank and Grand Exchange for the
essence [205..358], the Lumberyard teleport and the members' gate [391..402], the Devious
Minds monk [456..467], the cellar and the public guardian [470..499], Dr Jekyll [500..510],
the knock and the fake Drezel [513..585], Roald at stage 2 [622..648], the dog in the
cellar copy [728..796], the fake Drezel's thanks [804..836], Roald's anger [874..902], the
monks and the leader's key [1075..1141], Drezel in his cell [1142..1206], the mausoleum
(monuments, the key swap, the well) [1243..1452], the bookcases [1475..1485], the cell
gate, the blessing and the coffin [1503..1666], the mausoleum Drezel and the essence
[1700..1793], a trip out for more essence and Roald at stage 35 [1803..1915], the keyhole
[2027], the last essence and the end [2033..2085], and Nature Spirit's offer [2093..2099].
The journal was opened 14 times at 9 stages. It is the only recording of the quest on the
index (`search npc 3487`: 4459 alone; the other sessions naming Drezel are Sins of the
Father).

Jagex rewrote the quest; none of the recording's 289 page rows matched the tree. Before this
port it was a LostCity (2004) port: the knock's dual-voice mesboxes ("@dbl@...Psst..."), a
"Sure./Nope." accept, a 20-page history lesson, Drezel talked to through a "talk-through"
cell gate, a per-player random monument layout with 1-6 damage, "The golden key unlocks the
gate.", "STOP!", essence given in one write with "You give the priest your blank runes.",
the scroll before the speech, a LostCity journal, a per-player guardian spawned by a zone
trigger beside the public one, aggressive monks, the guardian on the human seq set and
silent, the monks' block wrong and silent, and the start with no confirm.

**Ours:** `quests/quest_priestperil/` (temple_doors.rs2, temple_guardian.rs2 (the cellar
copy), evil_monks.rs2 (the hunt), trapped_drezel.rs2, vampire_coffin.rs2, monuments.rs2,
mausoleum_interactions.rs2, mausoleum_drezel.rs2, priestperil_journal.rs2,
quest_priestperil.npc, .enum, .constant, .loc, .spawn (new));
`areas/varrock/scripts/king_roald.rs2` (the Priest in Peril section);
`areas/area_mausoleum/scripts/gates.rs2` and `paterdomus.rs2` (new: the bookcases, the
keyhole); `general_use/scripts/trapdoors.rs2` and `ladders_stairs/scripts/ladders.rs2`
(the cellar copy's hooks); `quests/scripts/questscroll.rs2` (the talk after the scroll),
`questpoints.rs2` (jingle 153); `interface_questjournal/scripts/quest_overview_start.rs2`
(six recorded overviews); `quests/quest_druidspirit/scripts/druidspirit_drezel.rs2` (Nature
Spirit's offer); `quests/quest_deviousminds/scripts/deviousminds_monk.rs2` (the
not-eligible pages); `macro_jekyll/` (new); `areas/world/configs/m53_54.spawn` and
`tools/gen_spawns.py` (one leader, not three); the monks' generated npc block;
`tools/check_quest_combat_contract.py` (check_priest_in_peril pins the recording). Test:
`test/quests/priestperil.lua`.

## Actions (rsprox_actions.py) — a verdict per row

| first tick | action | verdict |
|---|---|---|
| 2, 12..14, 178, 2047, 2106 | welcome screen, quest list, overviews, journal and scroll closes, logout | interface generic; the six overviews opened ([12], [21], [22], [28], [42], [651]) had no start text in the tree: added |
| 58, 253, 354, 589, 842, 1799 | Varrock teleport tablet Break (x7) | generic tablets, audited 2026-10-29 |
| 87 | fai_varrock_castle_door | door generic (L+1, synth 44) |
| 91, 622, 874, 1907 | King Roald | **this content** |
| 205, 212 | poll booth, ballot close | generic (tutorial/scripts/guides/banker.rs2) |
| 213..358, 959..992, 1831..1849 | bank booths, banker, GE clerk and offers, bank tabs/withdraws/deposits | generic |
| 365, 918, 953, 1849 | stamina potions | generic |
| 375, 1138, 1319 | essence and murky water drops; pick-ups (essence, buckets, the golden key) | generic; the murky water drops as a private quest item (`ownership=self, neverturnpublic=true`) |
| 389..391 | Lumberyard teleport scroll | generic, audited 2026-10-29 (teleport_scroll.rs2) |
| 402, 1032 | the Varrock members' gate | double door generic |
| 456, 1081 | devious_monk_hooded | **fixed**: the recorder could not start Devious Minds, and Jagex's answer is three pages and the requirements box (below), not LostCity's "Good day to you" |
| 470, 476, 728, 1242.. | trapdoor / trapdoor_open | **fixed**: generic, now spam lines and synth `door_open` [475]; at stage 2 into the cellar copy |
| 481, 1246, 1437.. | pip_underground_door1 | **this content** |
| 487, 490 | the guardian, Attack, stage 1 | **this content**: "You have no reason to do that!" |
| 492.., 791 | ladder_from_cellar | maplink generic (face L+1, reachforladder, teleport L+3); out of the copy at stage 2 |
| 500..510 | Dr Jekyll (Talk-to, Dismiss) | **new**: macro_jekyll (below) |
| 513..834 | the temple's large doors | **this content** |
| 1080..1137 | Monks of Zamorak (Attack) | **this content** |
| 1128..1135 | prayers | the recorder's kit |
| 1142, 1215, 1466, 1682 | paterdomus_spiralstairs, spiralstairstop_m, paterdomus_spiralstairs_m | stairs generic: up on the click from standing [1142], down L+1 [1219] |
| 1147..1673 | ladder / laddertop (levels 1-2) | ladders generic: reachforladder up, pickupfloor down, teleport L+2 |
| 1160.., 1507.. | Drezel in the cell | **this content** |
| 1205, 1503, 1617, 1631, 1666 | the coffin | **this content** |
| 1264..1406 | the well | **this content** |
| 1265..1381, 1745 | the monuments | **this content** |
| 1317 | Examine a monument | cache examine |
| 1475..1485 | myq6_bookcase_paterdomus_3..5 | **new**: paterdomus.rs2 (no binding before) |
| 1506, 1570 | pip_prisondoor | **this content** |
| 1736, 2018 | pip_underground_door2 | **this content** |
| 1744 | the holy barrier | **this content** |
| 1762, 2033.. | Drezel in the mausoleum | **this content** |
| 2027 | burgh_library_keyhole | **new**: Inspect had no binding (paterdomus.rs2); its use-on is In Aid of the Myreque's |
| 2093..2099 | Nature Spirit's offer | **fixed**: druidspirit_drezel.rs2's offer was LostCity's (below) |

**Teleports:** every row is one of the above: seven tablet breaks, the Lumberyard scroll,
the trapdoor and cellar ladder (twice into the stage-2 copy and out of it: 3405,9906 is
the copy's own tile, [734], [796]), the spiral stairs and ladders, and none of the quest's
own (Drezel's blessing does not move the player).

## Stages and vars

Varp 302 `priestperil` (perm, transmit) and seven new varbits on `varp5662_vampire_secondary`
(perm, transmit), all as recorded. Stage numbers were already Jagex's.

| var | value | recorded | goes out with | ours |
|---|---|---|---|---|
| 302 | 1 | [116] | the start's "Yes.", with "You've started a new quest" and the player's page | same |
| 302 | 2 | [558] | the click of "Then please, deal with that dog for me.", with "Hmm..." | same |
| 15612 returned_to_roald | 1 | [646] | the click of Roald's "Now, run along.", with "Well... okay then." | same |
| 302 | 3 | [776] | the guardian's death, held by a level-up box until its close (4 ticks) | a normal queue on the death |
| 15613 returned_to_fake_drezel | 1 | [823] | the click of "I think it's safe to say...", with the player's last page | same |
| 302 / 15614 hunt | 4 / 1 | [902] | the click of "Now you get back there...", with "Y-yes, your highness." | same |
| 15615 evilmonk_dead | 1 | [1136] | the leader's drop, with "Untradeable drop: Golden key" | same |
| 302 / 15614 | 5 / 2 | [1192] | the click of "I would not risk waking it...", with "Right. So, I need..." | same |
| 302 / 15614 | 6 / 1 | [1570] | the iron key on the gate, with the box | same |
| 15618 salve_dialogue | 1 | [1577] | the click of "Like the water of the Salve perhaps?" | same |
| 302 | 7 | [1624] | the click of "Yes." to the pour, with the box | same |
| 302 | 8 | [1646] | the click of "I poured the blessed water...", with Drezel's page | same |
| 302 | 10 | [1782] | the click of "Ah, I see where this is going...", with "Exactly." | same |
| 302 | 11..35, 36..60 | [1791], [2034] | the hand-in's box: one write per essence | same |
| 15617 ending_dialogue | 1 | [2040] | the click of "They hopefully shouldn't...", with the dagger page | same |
| 302 | 61 | [2078] | the click of "Right, I think that's everything...", with the blessing box | same |

`priestperilevilmonk_hunt` drives the monks' aggression (0 none; ours treats any non-zero
alike: what 2 changes is not recorded). `evilmonk_dead` is the leader's multivarbit (hidden
from that player once dead). `tried_fake_key` (15616) is never written in 4459; ours sets
it when the golden key is tried on the cell gate (the wiki's box).

## The start and the end

- Start [91..165]: "Greetings, your majesty." -> "Yes, citizen. Do you need something?" ->
  "I'm looking for a quest!|Not really." -> two pages -> "Yes, I think I've heard of it.|No, I
  don't think I know it..." -> Roald's page -> "Start the Priest in Peril quest?" Yes./No.
  (no combat warning: the recorder was 47; ours shows the wiki's below 15) -> stage 1 and
  the line with "Sure. I don't have anything better to do right now." -> the three-question
  menu. Its rule, recorded three times: the menu holds the two of Where/Why/Reward not
  last asked, in that order, then "I'll get going."; straight after the start "Where" is
  left out.
- End [2033..2085]: "I brought you some more essence." -> the box with the last writes to 60
  -> three pages -> the ending varbit with the dagger page -> on its click: qp 113 -> 114,
  the line, disease -50/0, the dagger into the pack, 1,406 Prayer XP, song 147, jingle 153
  (quest_complete_2: added to the recorded table), the scroll "1 Quest Point | Access to
  Morytania | 1,406 Prayer XP | The Wolfbane Dagger" with the dagger at 76,128 -> the
  scroll's close and "So can I pass through that barrier now?" on one tick [2047] ->
  eleven pages -> stage 61 with "Drezel blesses you..." (Drezel's human_castentangle, his
  face on the player, heal_casting on the player at 120, synth `heal`) -> "Good luck out
  there. Stay safe." -> "Is there anything else I can do to help you out?|And you. See you
  later." The first leads into Nature Spirit's offer.

## Pages

`rsprox_pages.py` lists 289 rows. Every recorded page and menu line is in our scripts
verbatim (checked mechanically: 300 of 301 strings; the one left is "Thank you. I need 25
more.", which ours writes from the count), with its recorded mood. The two voices behind
the door are chat_left pages named "Mysterious Voice" with priestperilevilmonk1's head and
"Mysterious Voice 2" with priestperilevilmonk3's; one page is `chat_concerned` (13951).
Pages no recording reaches are the wiki transcript's (Transcript:Priest in Peril, which is
the rewritten text), marked unverified in the scripts: "Yes, I think I've heard of it.",
"Not really.", "Where am I supposed to go again?", the start's "No.", Roald at stages 1 and
4..7 and after the quest, the three joke answers at the door ("Actually, never mind." after
one), the knock before the quest, the meeting without a key, "No, not yet...", the golden
key tried on the lock, blessed water in hand, the use-on lines at the coffin, "What am I
supposed to do again?", "How much more essence do I need to bring you?", noted essence.

## Journal

Recorded at stage 1 [173], 2 before and after reporting to Roald [562], [651], 3 [780], 5
with the golden key [1233] and with the iron key [1410], 7 [1640], 8 [1666], 35 [1808] and
61 (complete) [2091]: the current format (every finished row `<str>`, the current step in
`<col=000080>` with names `<col=800000>`, no blank rows; a blank row and `QUEST COMPLETE!`).
Ours sends those rows verbatim (generated from the transcript), the essence count from the
stage. Unverified, made from recorded rows: stage 4 (Roald's paragraph unstruck), stage 5
with no key, stage 6 (stage 7's paragraph up to "brought with them."), stage 60 (61's).

## Sections: recorded and ours

### King Roald, the knock, the fake Drezel

| | recorded | ours |
|---|---|---|
| knock | op1 Open on either leaf; synth `knock_knock` (1735) and "You knock at the door..." on the op tick; the voice the tick after its click [518..520] | same (`p_delay(0)` after the box) |
| player face | [518] `coord=(3418,3488)` (ten tiles east) | the engine's face on the door; the far face is not built |
| doors from stage 4 | on the op tick both leaves: inviswall on 3408,3488/3489 rot 2, inactivetempledoorr (3409,3488 rot 3), inactivetempledoorl (3409,3489 rot 1), synth `door_open`, the step one tile; back after 2 ticks ([1087], [1227], [1463], [1689]) | same |

### The guardian (the cellar copy)

| | recorded | ours |
|---|---|---|
| public guardian | static spawn wandering near 3403,9899 [478], shown for stages 0..2 | quest_priestperil.spawn (the dump's tile 3405,9902); gen_spawns.py keeps it out of the generated file |
| Attack before the errand | "You have no reason to do that!" [487], [490] | same |
| stage 2 trapdoor | "You climb down through the trapdoor...", crouch, the player in a copy of the square [734] (the ladder back starts from 11085,6234 [796]) | ~map_instance_from_square(53_154); the player at the copy's 3405,9906 |
| the copy's guardian | added at 3405,9900 facing south [734], creation cycle set | npc_add in the copy |
| seqs and sounds | dm_attack/dm_block/dm_death (18/19/20), wolf_attack/wolf_hit/wolf_death (909/912/911) | set in quest_priestperil.npc (the generated block stated none: it swung the human set, silent) |
| hp | 45 (headbar fill out of 30) | 45 |
| death to stage | death seq [772], del [774], stage on the level-up box's close [776]; no line | a normal queue; the wiki's "There we go, one dead dog." is not sent |
| ladder out | face L+1, reachforladder L+2, 3405,3506 L+3 [794..796] | ladders.rs2 asks ~priestperil_cellar_leave after the anim; the copy is released |

### The monks

| | recorded | ours |
|---|---|---|
| aggression | none at stages 1..3 (the recorder walked among them [455..470]); from stage 4 they came for the level-47 recorder, one at a time, on entering ([1088], [1099], [1137], [1146], [1673]) | hunt varbit + a soft timer armed by the temple's zones (the engine's own hunt refuses a player over twice the npc's level) |
| melee | human_unarmedpunch (422), human_unarmed_def (425), human_death (836); synths unarmed_punch 2566, human_hit 513, human_death 512 | set (the generated block had human_unarmedblock and null sounds); the punch's synth is played by the monks' own AI, as `[ai_opplayer2,_]` does (missing in run pip4) |
| magic | the leader: Confuse [1119] and Fire Bolt [1123] (both splashed); a level-17 monk Weaken [1674], landed ("You feel weakened."). Confuse: `human_castconfuse_walkmerge` (11424), confuse_casting at 92, projectile 61..90 heights 172/124 (v2), synth 119; the splash `failedspell_impact` at 124 delay 90, synth spellfail 227 delay 90 | ~npc_cast_spell, one swing in four (unverified), curses for the lesser monks, curse or Fire Bolt (max 8) for the leader. **Differs** (the generic npc spell table, every npc caster): `human_castconfuse`, projectile 51..56, impact at 92 delay 56 (run pip3 [401]) — Open |
| key | the leader's death: "Untradeable drop: Golden key" and evilmonk_dead [1136] | same |
| spawns | ground floor 2 x monk1, 2 x monk2, 1 x leader; first floor 3 x monk1, 2 x monk2 | the dump had two leaders on the ground floor and one upstairs: corrected (gen_spawns.py NPC_SPAWN_ID_CORRECTIONS) |

### Drezel's cell, the gate, the coffin

| | recorded | ours |
|---|---|---|
| talk | opnpc1 through the bars, answered on the click: the player on the gate's tile 3415,3489, Drezel three tiles off on 3418,3490 [1160], two [1193] | [apnpc1] at the default range (a `p_aprange(2)` walked the player off the gate's tile, behind the wall: run pip4 "I can't reach that!" x4) |
| Drezel's wander | every recorded tile within 2 of his spawn (x 3416..3418, z 3487..3491, 66 steps); the mausoleum's within 1 | wanderrange 2 and 1 in quest_priestperil.npc (the generated 5 deleted) |
| priority at stage 5 | murky water, then the iron key, then the golden key ([1512], [1523]) | same |
| iron key | "You use the key to unlock the cell gate." objbox (the iron key), synth `unlock`, stage 6, hunt 1, then "The key fit the lock!" [1570] | same |
| blessing | "Drezel inspects the water..." objbox (blessed water), Drezel human_castentangle, heal_casting on the player at 120, synth `heal`, the bucket swapped in its slot [1598] | same |
| pour | Open on the coffin with the blessed water: the line, "Pour the blessed water over the coffin?" Yes/No; on "Yes.": stage 7, throw_bucketofwater, two priestperil_coffin_spell graphics at delay 34, synth holy_water_pour at delay 30, the objbox, the bucket emptied [1617..1624] | same |
| locked gate's Open | not recorded ([1506] was replaced by a click on Drezel) | the mausoleum gate's locked answer, unverified |

### The mausoleum

| | recorded | ours |
|---|---|---|
| gates | locked: synth `locked` + "The gate is securely locked." [485]; open: inviswall + inacpip_underground_door for 2 ticks, synth `grate_open`, a one-tile step on the op tick | same; the golden key's first-time requirement is the wiki's (no line) |
| monuments | each holds a fixed gift (studied all seven [1265..1310]); a swapped one shows its new item [1745]; Take-from: one line, human_opencupboard, synth `sizzle`, 1 damage, 8 of 8, at any stage | same; the random layout and 1-6 damage are gone |
| key swap | objectbox_double (golden, iron at 400) "You swap the golden key for the iron key.", human_opencupboard, synth `pick2` [1381] | same |
| well | Inspect: box [1399]; fill: human_pickuptable, synth `well_fill`, the objbox [1396] | same |
| barrier | Drezel's "Stop!" from 8 tiles at L+1 [1749] | same |
| essence | the player's line, the objbox with the essence given, one stage write per essence, "Thank you. I need N more." [1789..1793] | same |

### Dr Jekyll (random event)

| | recorded | ours |
|---|---|---|
| arrival | added beside the player facing south [500]; S+1 face, "Hello, LoveTori. I need a herb.", smoke puff at 124 with synth `smokepuff` range 7 [501]; follows | macro_jekyll.rs2 (`::jekyll`) |
| talk with no herb | "Have you got any guam leaf? I've fixed my Hyde problem, so it's not urgent, but I could do with a guam leaf." chathap [506] | same; the rest of the talk and the trade are the wiki's |
| Dismiss | the player's emote_no at delay 10 [508], Jekyll gone [509], the puff [510] | same |

### Others

| | recorded | ours |
|---|---|---|
| Devious Minds monk, not eligible | "Monk": "Excuse me... Oh, wait, I thought you were someone else." (chatcon), "No problem. Have a good day!", "And yourself.", box "You do not meet all of the requirements to start the Devious Minds quest." [459..463], [1084..1086] | same |
| bookcases | "You search the bookcase, but don't find anything worth reading." box [1476..1485] | same, all five |
| keyhole | "You inspect the panel. It looks like there's some sort of keyhole in it." [2030] | same |
| Nature Spirit's offer | "Hello again, Drezel. Is there anything that I can help you out with around here?" -> "Well... there is something..." -> "Well, what is it, I may be able to help?|Sorry, not interested." -> two pages -> "Start the Nature Spirit quest?" [2093..2099] | same; after the confirm the wiki's pages |
| ghouls north of the temple | 11 ghouls near 3409..3419,3506..3520 [1690] | in m53_54.spawn (22 rows) |

## Test and wire audit

`test/quests/priestperil.lua` follows the recorded route (run on private packs because
the shared pack build was failing on other sessions' contract checks: The Feud's missing
`[apnpc2]` twin and Troll Stronghold's `[ai_queue2,troll_champion]` dialogue). Run pip6:
201 of 207 rows pass; the six failures are generic (below). Its tick log against the
recording:

| section | recorded | ours (pip6, and pip3 for the fights) |
|---|---|---|
| stages | 1, 2, 3, 4, 5, 6, 7, 8, 10, 11..35, 36..60, 61 | 1, 2, 3, 4, 5, 6, 7, 8, 10, 34, 60, 61 (the tick log keeps one row per tick; every write is its own packet, ToriRSServer_WorldMarkVarp) |
| knock | knock_knock x5 | knock_knock x5 |
| doors and gates | door_open, grate_open, the inviswall leaves | door_open x10, grate_open x6 |
| guardian | dm_block on the swing, wolf_hit the next tick, dm_attack + wolf_attack, dm_death + wolf_death on D+1, the copy's spawn at 3405,9900 | the same, 6413,108 in the copy |
| monks | punch + unarmed_punch, human_unarmed_def + human_hit, human_death + human_death | the same (unarmed_punch x6 after the fix) |
| monuments, well, cell, coffin | sizzle, pick2, well_fill, unlock, heal, holy_water_pour | each once (heal twice: the blessing and Drezel's) |
| completion | song 147, jingle 153 (9,955 ms) | the same |

## Open

- The generic npc spell shape (seq, projectile timing and heights, impact height and
  delay, cast and splash synths) differs from the monks' recorded casts; it is the shared
  magic_spell_table every npc caster uses, so it is not changed here.
- The Varrock members' gate (`fai_varrock_member_gatel`) timed out in the test's essence
  trips (runs pip4..pip6: "the closed leaf is still at 3319,3468"), passing on the earlier
  legs of the same run: generic, not this content.

- Packet-level rows (interfaces, chatheads, menus) were checked against the transcript by
  reading and by the page check above, not by a wirelog diff.
- The monks' cast rate and spell sets, and what hunt value 2 changes: one recording.
- The cell gate's locked answer, the golden key tried on the lock, the souvenir swaps for
  the other six monuments, the barrier at stage 60, the pass at 61, the dagger reclaim and
  the post-quest well: the wiki's or LostCity's, unverified.
- The player's ten-tile face on the knock [518] is not built.
- `t.player.teleport_cast` reports "CAST and no teleport" for the Varrock Teleport: the tick
  log has the cast on C and the landing on C+3 (run pip1 [19..22]); the verb gives up first.
  A driver timing issue, not this content.
