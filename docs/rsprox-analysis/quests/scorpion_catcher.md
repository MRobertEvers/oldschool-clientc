# Scorpion Catcher — RSProx index 4421

Recorded 2026-10-29, rev 241.1, world 369, one session end to end (2,163 ticks), by
the same recorder as the barcrawl (index 4420, the day before): the start [202..221],
the Seer [263..303], Taverley Dungeon's secret room and scorpion A [1400..1428], the
monastery's scorpion C [1524..1530], the Barbarian Outpost's scorpion B [1613..1629],
the journal opened at stage 2 [1730], [1773], [1783], [2045], and the hand-in
[2142..2148]. The recorder never asked the Seer for the second hint: they went straight
to all three, and the Seer still gave the first hint with the full cage held.

Before this port the quest was a LostCity/wiki port (2004): a start menu with three
offers, the cage given after an `if_close` and 4 ticks, a mesbox and a 500-tick
`loc_del` for the secret wall, the hit landing with the sting's line, completion queued
with the full cage as the scroll's model, a 2004 journal, scorpion C standing still and
no barcrawl requirement.

**Ours:** `quests/quest_scorpcatcher/` (`scorpcatcher_scorpions.rs2`: the wall, the
sting, the catch, the completion; `scorpcatcher_journal.rs2`),
`areas/area_seers/scripts/thormac.rs2`, `areas/area_seers/scripts/seer.rs2`.

**Tests** (scriptrun, private packs, `src/build_sc_opt/torirsserver`):

- `test/quests/scorpcatcher.lua`: 89 of 90 rows pass on each seed. The one failure,
  `quest.journal`, is one no scriptrun can pass.
- `test/quests/_journals.lua`: `journal.scorpioncatcher.2`, the 11 rows of [1730]. This
  row needs the live client; headless it was checked through `::scorpcatcherjournal`
  and a `TORIRSSERVER_TRACE_OUT=2` run, and all 11 rows match.
- `src/torirsserver/test/split_test.c`: `test_wrap_restates_colour` (§6).

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 3 | welcome screen | engine |
| 6, 1616 | Barbarian Outpost gate | generic: the barcrawl port (`barcrawl.md` §5), matched there |
| 17 | Pick-up scorpion B before the quest | **this content**: the sting (§4) |
| 31, 36, 42 | kitten: sardine, wool, the player's page | the recorder's pet; `kitten_pet.rs2` ported from this recording, matched |
| 70, 240, 312, 676, 814, 1654 | Camelot, Falador, Varrock, Ardougne tablets | generic: audited 2026-10-29 (fe73b7bd3, `teleport_tablets.lua`) |
| 101..115 | Seers' church: door, altar, organ | generic. Door matched; altar line fixed (spam, "Prayer Points"); organ had no binding, now `~church_organ` |
| 130, 414, 857, 1039 | stamina, prayer restore, antipoison sips | generic consumption, bound |
| 172.., 177..235, 2118..2139 | tower door, ladders, laddertop | generic. Door and ladders matched; floor 2→3 **fixed** (see "Fixed after the first pass") |
| 202..230, 2142..2148 | Thormac | **this content** (§2, §7) |
| 263..303, 1927..2036 | the Seer x6 | **this content** (§3) |
| 324.., 715.., 1558.., 1850.. | bank booths, bankers, bank tabs, withdraw/deposit | generic, bound; not diffed |
| 418..906 | Falador crumbling wall x5 | generic: **fixed** to the recorded exactmove (30/100, `human_walk_crumbledwall` delay 30, `climb_wall` delay 40) |
| 451, 934 | dead tree, no axe | generic: line and tick **fixed** (one gamemessage with `<br>`, L+2) |
| 452/942 | Taverley ladder down | generic: **fixed** to `human_reachforladder` (828); landing matched |
| 476, 565, 1217, 1373, 1455 | prayers, quick prayers | the recorder's |
| 477..487, 959..966 | Taverley cauldron doors and suits | generic: **fixed** (suit order, the second suit is silent, the walk-through's locs and synth 71) |
| 487, 585 | prisondoor_r, castledoubledoorr | generic: prison door sound **fixed** to 71; castle door matched |
| 603..1216 | jailer fights, drops bones + jail key | generic: the "Untradeable drop" line **added** |
| 606, 1224, 1235, 1267..1273 | dungeonjail: locked, key in, way out | generic: **fixed** (walk-through locs, synth 71, the tick) |
| 737..768 | GE clerk, an offer, collect | generic: audited 2026-10-29 (a72638397) |
| 954 | taverley_dragon_jumpup x3 | dropped by the client before it landed: nothing to compare |
| 1218..1223, 1307 | bones and jail key taken, bones buried, key dropped | generic, bound |
| 1238..1265 | Velrak x5 | generic: pages **fixed** (forced breaks removed), key box now `objectbox` 193 at zoom 400 |
| 1343 | deepdungeondoor with the dusty key | generic: **fixed** (it said "This gate is locked." with the key held) |
| 1358 | blue dragon attack | the recorder's |
| 1408, 1424 | the Old wall x2 | **this content** (§4) |
| 1415, 1420 | scorpion A: sting, catch | **this content** (§4) |
| 1428, 1432 | coffin open, close | generic: **fixed** (spam lines, synths 54/53) |
| 1454, 1553 | glory worn op 2 → Edgeville | generic: audited 2026-10-29 (fe73b7bd3) |
| 1490, 1521 | monastery ladder | generic: **fixed** ("You climb up the ladder." spam); 828 + teleport matched |
| 1495..1516 | Abbot Langley: heal, "out the way", join | generic: 5 moods **fixed**; the join is Jagex's varbit 4907 |
| 1529 | scorpion C catch | **this content** (§4) |
| 1532, 1534 | monastery poordoors | generic doors, matched |
| 1543 | monks' altar | generic: **fixed** (spam, "Prayer points") |
| 1599 | games necklace submenu → Barbarian Outpost | generic: audited 2026-10-29 (fe73b7bd3) |
| 1629 | scorpion B catch | **this content** (§4) |
| 1633..1636 | agility pipe, Agility too low | generic: **fixed** (the recorded mesbox text) |
| 1672, 1674 | call follower, items kept on death | the recorder's UI |
| 1708 | tree | the walk was abandoned: nothing answered |
| 1730.., 2045 | quest list → journal | **this content** (§6) |
| 1824, 1830 | world map | generic UI |
| 1968, 2039 | cage used on a Seer | "Nothing interesting happens." (the engine default), matched |
| 1990 | kr_poordoor | generic door, matched |
| 2148 | quest scroll close | **this content** (§7) |
| 2163 | logout | engine |

The generic fixes were made by one audit pass over the files named above. Its notes on
each row are summarised in the table.

**Teleports** (`rsprox_actions.py`): the six tablets and two jewellery routes are as in
fe73b7bd3; the tower ladders are §1's rows; [426..906] are the Falador wall; [463, 945]
the Taverley ladder; [1524] the monastery ladder. None is quest content.

## 2. Thormac's start (4421 [202..221])

| R | ours | verdict |
|---|---|---|
| "Hello I am Thormac the sorcerer. I don't suppose you could be of assistance to me?" chatneu2 | same | matched |
| menu "What do you need assistance with?" / "I'm a little busy." | same | matched |
| player chatquiz1; Thormac chatsad2 x2, chatneu2 | same | matched |
| **no menu**: the player says "How would I go about catching them?" (chatquiz1) | was a three-offer menu (LostCity); now the line | fixed |
| "Well I have a scorpion cage..." chatneu2; on its click "Thormac gives you a cage." + the cage, **with** the next page [214] | was `if_close`, the line, `p_delay(4)`, a fresh page | fixed |
| "If you go up to the village of Seers..." chatneu3 | same | matched |
| messagebox: "Before starting this quest, be aware that your combat level is lower than the recommended level of 40." [215] (recorder 35) | same, under `~player_combat_level < 40` | fixed (was absent) |
| menu "Start the Scorpion Catcher quest?" Yes./No. [218] | `~p_choice2_header` | fixed |
| Yes [220]: varp 76 = 1, "You've started a new quest: @q_mes@Scorpion Catcher</col>", the player's "Okay, I will do it then." chathap1 | same tick, same order | fixed |
| [224..230] "How goes your quest?" chatquiz1 / "I've not caught all the scorpions yet." chatsad1 / "Well remember to go speak to the Seers, North of here, if you need any help." chatneu2 | same | matched |

The requirement is the quest dbrow's: `requirement_quests miniquest_barcrawl`. The
dbrow also has `requirement_stats prayer 31` with `requirement_check_skills_on_start
false`, so Prayer is not checked here (the monastery's Abbot asks for it).

Unrecorded branches, from the wiki transcript, moods unverified: without the barcrawl
("Hello I am Thormac the sorcerer." / "Do you need anything?" / "Not right now, but
thank you for offering." + the mesbox), "I'm a little busy." + "Fair enough.", and No at
the confirm ("I'm not interested." / "Blast! I suppose I'll have to find someone else
then."). LostCity's, unverified: the lost cage and the cage in the bank (the cage's
Destroy text says Thormac gives another), and the battlestaff enchant (the wiki says it
is an interface now, with diary discounts).

## 3. The Seer (4421 [263..303], [1927..1964], [2002..2036])

| R | ours | verdict |
|---|---|---|
| "Many greetings." chathap1, menu "I need to locate some scorpions." / "I seek knowledge and power!" | same | matched |
| player chatneu1; Seer **chathap2**, **chatquiz2**; player chatneu2 | were all neutral | fixed |
| "Let me look into my looking glass." chatneu1; its click closes the chat with "The seer produces a small mirror" (K); "The seer gazes into the mirror" K+4; "The seer smoothes his hair with **his** hand" K+8; the page K+12. 3 of 3 recorded the same | `if_close`, the lines with `p_delay(3)` between, the page after a third: our trace K=88, 92, 96, page 100 | fixed (were three lines on one tick; "with hand") |
| "I can see a scorpion..." **chatcon3**; varp 76 1→2 on its click, with "The scorpion seems to be going..." **chatcon2** [297] | same (stage after the page, our trace with the next page) | fixed (was set after the last page; moods neutral) |
| "Well see if you can find that scorpion then..." chatneu2 | same; the repeat now has it too | fixed (the repeat dropped it) |
| stage 2, **full cage held**: "Where did you say that scorpion was again?" (chatquiz1) and the whole looking glass, twice | the full cage no longer offers "I've retrieved..."; the player's line is chatquiz1 | fixed |

The second hint ("I've retrieved the scorpion from near the spiders.", stage 3) is the
wiki's, moods unverified. It is now offered for cages a, ab and ac, not for the full one.
Which cages Jagex offers it for is not recorded.

## 4. The wall, the sting, the catches

| R | ours | verdict |
|---|---|---|
| Old wall in [1408..1413]: the op at L+1; "You've found a secret door!"; `inviswall` (38848) rot 3 at 2875,9799 and `oldwall` (1631) rot 0 at 2875,9798, shape 0, for 2 ticks; synth 62; the player stepped 9799→9798 | same, in one enclosed zone update; reverts 2 ticks on; the step | fixed (was a mesbox and `loc_del(500)`) |
| Out [1424..1428]: the same, the player stepped 9798→9799 | same (test leaves through it) | fixed |
| Sting [17] (scorpion B, stage 0) and [1415] (A): "The scorpion stings you!" on the op, the 3 (`hitsplat_damage_me`) and synth 518 `human_hit_1` delay 20 the tick after | the line on the op, a `queue` lands the 3 and 518/20 a tick later (trace 144 / 145) | fixed (the hit was on the op tick) |
| Catch [1420/1421], [1529/1530], [1629]: "You catch a scorpion!", the npc `[del]`, the cage swapped in its slot; after a walk at L+1, from adjacent on C | same | matched |

The wall is still gated on the Seer's hint ("You find nothing of interest." before it,
LostCity's, unverified): no recording searches it before.

## 5. Npc info and movement

| npc | recorded | ours | verdict |
|---|---|---|---|
| questscorpiona | 3 lives, 17 walk steps inside the room (2874..2880, 9793..9797), no seqs | wander 5, the room encloses it | matched |
| questscorpionb | 6 lives (4421, 4420, 1042 ...), 43 steps | wander, indoors | matched |
| questscorpionc | 4 lives (4421, 468, 3114, 1501), **206 steps** over 3056..3059, 3485..3491 (both sides of the dormitory door) | was `defaultmode=none`, `wanderrange=0` | fixed: `defaultmode=wander`, `moverestrict=indoors` (server_fields block), `npc_movement/q/questscorpionc.move` authored, the generated `wanderrange=0` block deleted |
| thormac | 2 lives, 5 steps | wander | matched |
| seer | 20 lives, 212 steps | wander | matched |

No quest npc plays a seq, spotanim, transform or headicon in the recording; their
facing is the dialogue's (the npc faces the player on each page).

The dormitory door's open leaf (3058,3486 rot 2) walls off the nook at 3059,3486, a
recorded scorpion tile. The test waits for the scorpion to leave the nook before it
opens the door.

## 6. The journal (4421 [1730], [1773], [1783], [2045])

Stage 2, full cage held, the same 11 rows every time:

```
<str>I've spoken to Thormac, he's lost his pet Kharid Scorpions and
<str>needs my help to find them.

<str>I've spoken to the Seer and been given the location of one of
<str>the Kharid Scorpions.

<str>The first Kharid Scorpion is in a secret room near some Nasty
<str>Spiders with two Coffins nearby.

<col=000080>I need to talk to the <col=800000>Seer<col=000080> again now I've caught the first
<col=000080><col=800000>Kharid Scorpion.
```

The older layout: a blank row between paragraphs, none before the first. The journal
reads only the stage and whether A is caught. B and C, both caught by then, are not
mentioned. Ours reproduces all 11 rows exactly, after one engine fix.

**The splitter fix** (`src/torirsserver/torirs_server_split.c`). After a wrap, Jagex
restates the carried colour even when the row opens with its own colour: row 11 above.
There are 51 such rows across the recorded journals, always `<col=000080><col=800000>`
and never the same colour twice. Dialogue pages wrap the same way
(`<br><col=000000><col=000080>`, 34 recorded). Ours dropped the carried tag
whenever the next row began with `<col=`. That rule had been fitted to Fishing Contest
(4404 [677]), but there the row follows a hard `|` break, not a wrap. The carried tag is
now dropped only after a hard break (`|`, `<br>`) or when the row opens with that same
colour. `split_test.c` passes with the Fishing Contest case and the new wrap case.

Not started: the quest list's overview (shared). Every other state is UNVERIFIED (no
recording opens it, and the wiki has no journal transcript): the 2004 text, laid out
the recorded way.

## 7. The hand-in and completion (4421 [2142..2148])

| R | ours | verdict |
|---|---|---|
| "How goes your quest?" chatquiz1 / "I have retrieved all your scorpions." chathap1 / "Aha, my little scorpions home at last!" chathap1 | same | matched |
| on that page's click [2145]: varp 76 2→6, **varp 77 (barcrawl) 2→0**, qp +1, quests completed +1, the line, disease -50/0, the cage gone, `midi_jingle 154` (`quest_complete_3`), the scroll | same tick, same writes, jingle 154 | fixed: was queued, kept varp 77, played `quest_complete_2` (difficulty 1); a row in `quests/scripts/questpoints.rs2` |
| scroll: "You have completed Scorpion Catcher!", model **scorpioncagea** (457) zoom 400 at 78,128, "1 Quest Point", "6,625 Strength XP" | same | fixed (model was the full cage) |
| [2148] close → [2149] the Strength level-up (jingle 65) | the level-up after the close, jingle 65 | matched |

## 8. Vars

| var | value | R tick | ours |
|---|---|---|---|
| varp 76 `scorpcatcher` | 1 | [220] the Yes | same, sent (`transmit=yes`) |
| varp 76 | 2 | [297] the click of "I can see a scorpion..." | same |
| varp 76 | 6 | [2145] | same |
| varp 77 `barcrawl` | 0 | [2145] | same (the barcrawl reads `barcrawl_progress`) |

`varp_transmit_audit.py` over 4421 lists no quest var we fail to send.

## Fixed after the first pass

1. The tower's floor 2→3 ladder now lands on 2699,3404,3 ([198], [2139], 2 of 2), as
   recorded. The row is in `maplink_landings.dbrow`. `tools/maplink_import.py` now
   drops a generated row on the same `(src, dir)` as an authored one (`docs/MAPLINKS.md`
   §4). Its `--check` was already failing on `maplink.dbrow` before this change, from
   unrelated drift. A scratch run of the patched importer drops exactly this row.
2. The Abbot's join sets Jagex's `monastery_enter` (varbit 4907), and its base varp 190
   is now `perm, transmit=yes`. A save holding the old server-only
   `%varp6426_prayer_guild` is moved over on the next ladder or Abbot click
   (`~prayer_guild_migrate`). The test reads the client's varbit: 1.
3. The no-axe line waits `p_arrivedelay` (L+2, [940]).
4. `~objbox_scaled_page` now sends Jagex's order: the open, `objbox_setbuttons`, the
   events, then the object and the text (21 of 21 recorded object boxes).
5. `split_test.c`'s `JOURNAL_WIDTH` is now 350, `^questjournal_text_width`; every case
   passes.

The test now passes 89 of 90 rows on 2 seeds. The one failure is `quest.journal`.

## Open

1. The second hint, every journal state but stage 2 with A, and all the unrecorded
   branches above: wiki or LostCity text, unverified.
2. The battlestaff enchant is still LostCity's dialogue, not the interface (no
   recording).
