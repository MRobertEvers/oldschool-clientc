# The Ascent of Arceuus, Hunter to 12 and Kharedst's memoirs (RSProx index 4426)

Session 4426 (rev 241.1, world 369, 2026-10-29, 2,870 ticks): Hunter 9 to 12 on bird snares
at Feldip and Piscatoris [369..1442], The Ascent of Arceuus start to finish [1597..2805],
then the arc page into the memoirs and a Reminisce to each of the five places [2816..2860].
Cites are this session's ticks.

Our quest was a wiki port (2026-10, "parity b55"). Every page, menu, stage write, message
and scene below is now the recording's. Lines the recording does not reach come from the
wiki transcripts (Transcript:The_Ascent_of_Arceuus, Councillor_Andrews, Lord_Trobin_Arceuus)
and are marked UNVERIFIED in the scripts.

Files: `quests/quest_ascentofarceuus/` (scripts, `.npc`, `.constant`),
`skill_hunter/scripts/bird_snare.rs2`, `general/scripts/enchanted_jewellry/kharedst_memoirs.rs2`;
tests `test/quests/ascentofarceuus.lua`, `test/magic/kharedst_memoirs.lua`,
`test/hunter/bird_snare.lua` (new).

## 1. Action inventory (`rsprox_actions.py`), a verdict per row

| first | n | action | verdict |
|---|---|---|---|
| 7, 1464 | 2 | Varrock tablet break | generic: tablet shape checked for TGV; landing 3213..3214,3424..3425 matches |
| 22..115 | | bank booth, banker Bank (opnpc3), withdraw/deposit, bank tabs | generic: `_bank_teller`, `fai_varrock_bankbooth_multi` bound |
| 82..106 | | GE clerk Exchange (opnpc3), `ge_offers` setup/confirm/collect, objdialog search | **defect, open**: no GE clerk ops and no `ge_offers` interface script (only `ge_matching.rs2`) |
| 50, 54, 617 | | prayer restore doses | generic: `_` drink bound |
| 113, 641, 869 | | ring of dueling (bank withdraw, Wear, worn Rub) | generic: Castle Wars landings 2440,3091 / 2438,3089 are inside our findsquare radius 2 of 2440,3090 |
| 887, 896 | | necklace of passage to The Outpost | fixed: landed 2428,3349, ours 2429,3348 (one landing) |
| 177..191 | | Yanille gates (`xbowsdoubledoor*`), `poshdoor` | generic: bound |
| 206 | 1 | Leon Trade (opnpc3) | fixed: the shop is inv 479 (our cache names it `skill_guide_hunting_eagles`), titled "Leon's Prototype Crossbow." |
| 209 | 2 | Aleck Trade (**opnpc5**) | fixed: we bound op3, the cache's op3 is empty; Nardah's hunter shop had the same op and is fixed |
| 216..235 | | Aleck's shop buys (snares, net, jars) | generic shop |
| 212 | | "...completed an easy task in the Ardougne area." on Aleck's shop | open: which Ardougne easy task this is is not identified |
| 250, 984, 1777, 2044 | | stamina doses | generic |
| 262 | 2 | call follower | generic |
| 315..358, 833 | | world map, prayers, skill guide | generic interfaces |
| 369 | 22 | bird snare Lay (iop1) | fixed, section 7 |
| 406 | 17 | broken snare **Reset** (op2) | fixed: was unbound |
| 580, 1164 | 15, 7 | full snare (jungle, woodland) **Reset** (op2) | fixed: was unbound |
| 1123 | 5 | snare Dismantle (op1) | fixed (shape, message) |
| 1355 | 1 | snare Investigate (op2) | ours: smoke status (unchanged) |
| 594..638 | | bones bury/drop, lobster eat, feathers/meat drop, jar drop | generic: `_bones`, `_cooked_fish`, engine drop |
| 739, 857..861 | | ground pickups (opobj3), ground snare Lay (opobj4) | opobj3 engine; **opobj4 Lay unbound** (open) |
| 874 | 1 | Castle Wars bank chest Use | fixed: was unbound (`bank_booths.rs2`, Collect too) |
| 1014 | 1 | butterfly net Wield | engine equip |
| 1017 | 4 | ruby harvest at level 9/11 | fixed: "You need a Hunter level of at least 15 to catch this butterfly." (was "...in order to do that."); see Open for timing |
| 1162, 1985, 1320, 2654 | | kitten: feed (use-on), Interact menu (opnpc5), Stroke, Guess age, hunger/attention lines | **defect, open**: `kitten_pet.rs2` has Pick-up only, no growth, hunger or Interact |
| 1519..1537 | | bank: Kourend tablets, battlefront teletabs | generic |
| 1537..2674 | 4 | Kourend Castle tablet | generic: landings 1640..1642,3672..3674 inside our radius |
| 2036, 2212 | 2 | Battlefront teletab | fixed: landed 1348,3740 and 1348,3739; our row was 1400,3745 (wiki), 52 tiles east |
| 1656, 1680 | | Kourend Castle stairs up/down | fixed: 1618,3680 -> 1614,3680,1 and back (generic climb landed on the same tile); `maplink_landings.dbrow` |
| 1597.. | | Mori, Andrews, tower door, mages, souls, stairs, Asteros, Tasakaal, grave, trail, soul, rocks, Trobin | this content, sections 3..6 |
| 1804..2754 | | Arceuus Library doors, Arceuus bank booth | generic: bound |
| 2133, 2449 | 2 | Karuulm elevator | fixed, section 4 |
| 2200..2684 | | quest list -> journal | section 5 |
| 2816 | 1 | wield memoir page | generic equip |
| 2819 | 1 | arc page on the memoirs | matched (the memoirs commit, 4416/4418) |
| 2823 | 5 | Reminisce to all five | matched + the diary task, section 8 |
| 2870 | | logout | engine |

## 2. Teleports

| tick | cause | R landing | ours |
|---|---|---|---|
| 1847 | the tower door's Yes | a copy of m24_59, the same tile (1596,3819) | same (`~aoa_enter_tower_copy`) |
| 2004 | copy stairs up | 1580,3820,1, face L+1, busy L+2, teleport L+3 | same (was 1580,3821,1 and a line "You climb the stairs.") |
| 2783 | tower stairs up | 1580,3820,1, face L+1, teleport L+2 | same |
| 2144, 2457 | elevator | 1311,10188 (`^karuulm_lobby`) | same; the shape is section 4 |
| 2216, 2506, 2678, 1541, 1644 | tablets | above | above |
| 2828..2860 | memoirs | 1714,3611 / 1803,3749 / 1479,3575 / 1545,3762 / 1681,3747 | inside our pins' radius 2 |

## 3. Stages (arcquest, varbit 7856 on varp 2071, perm/transmitted)

Each goes out on the click that opens the page after it.

| stage | tick | with | ours before |
|---|---|---|---|
| 1 | 1628 | Mori's "Thank you... Could you head to Kourend Castle..." + "You've started a new quest" | after the last page, no start line |
| 2 | 1676 | Andrews' "If I could do something, I would..." | after the last page |
| 3 | 1765 | Mori's "He'll be in the Tower of Magic..." | after |
| 4 | 1844 | the door mage's "A death? This isn't good..." | on entering the copy |
| 5 | 1980 | the last soul's death (no message) | with "You have defeated the tormented souls." (invented, deleted) |
| 6 | 2013 | Asteros's "Alas, whatever happened..." | after his first page |
| 7 | 2029 | the player's "I'd better not waste any time then." | after the last page |
| 8 | 2187 | Mej's "Free this soul..." | after the last page |
| 9 | 2221 | the grave's mesbox, trail_1 | a `mes` (invented) |
| 10 | 2297 | "A trapped soul that was lurking in the plants jumps out and attacks you!", `current_hint_arrow` 7, hint arrow on the soul | no hint |
| 11 | 2355 | the soul gone, no message | "You have defeated the Trapped Soul." (invented) |
| 12 | 2487 | the player's "So I just have to go...", trail 1..6 cleared, `device_location` rolled (3) | trail left set |
| 13 | 2666 | the rocks' objbox, `trobin_arceuus_tower_vis` 1 | no vis bit (Trobin stayed in his trance form) |
| 14 | 2798 | the last page's click: reward bit, qp, the line, coins + page, scroll | |

## 4. Scenes and interactions

**Mori, Andrews, Asteros, the Tasakaal, Trobin: every page**, mood and menu as recorded
(`rsprox_pages.py`, 223 pages). Fixed: Mori's second menu has a third option ("I
shouldn't get involved."), her return menu "I need to go."; "Council" and "King" are
capitalised; Andrews' menu has four options ("Who are you?", "Tell me about the Kourend
Council." UNVERIFIED answers from the wiki); the door mage speaks with the door guard's head
(`arceuus_towerguardian`), the soul call with `arcquest_towermage4`'s; the Tasakaal speak on
`tasa_kaal_chathead_ready` with the player facing the speaker (Xil 1317,10206, Mej
1307,10206, Ket 1311,10210); Trobin on `arceuus_lord_ready`. The recorder asked for Lord
Arceuus in his trance and Asteros answered [2007..2008]: that is now the Talk-to on
`trobin_arceuus_focus`. After the scroll Trobin's "Now, can I help you further?" and his
five-option menu (answers UNVERIFIED, wiki) [2803..2804].

The Tasakaal are answered from 5 tiles of their footprints ([2156] from 5, [2197] from 3;
not from 7 [2155]): `[apnpc1]` + `p_aprange(5)`.

**The tower door.** A walk-through door: on the op tick, both leaves inviswalls and the open
leaves on the inside tiles for two ticks, `nicedoor_open`, the player stepped in [2775..2778].
Ours was a `p_telejump`. Stage 3/4: the door mage's four pages, then on Yes the copy, the
step and the doors a tick later [1848], and "Somebody deal with these souls!" opened, then
opened again two ticks later [1850] (the first never waits).

**The copy's cast** [1847], square-local tiles of m24_59: souls at 52,46 (goes for the player),
52,41 / 46,51 / 40,49 / 38,36, each fighting a mage (towermage3 52,39, towermage5 47,51,
towermage4 38,49, towermage1 37,39), two towermage2 left alone (42,32 and 42,59), the two door
guards outside (61,42, 61,45). A soul on a mage every 4 ticks: its attack seq and
`ghost_attack` (sound_area range 12) at the soul, on the mage a block splat (66 of 66) and
"Clear off!" / "Get out of it!" / "Somebody deal with this!" (27/23/14 across four mages). A
mage's Talk-to at stage 3/4 answers overhead, "We need to deal with these souls!" or
"Someone sort out these souls!" ([1856], [1885]). Sounds: `ghost_attack` / `ghost_hit` /
`ghost_death` (were null). Death seq the tick after the kill, gone 5 ticks after it
(`death_delay` 4). The souls never hunt (huntmode none): one kept on its mage with the
player beside it [1856..1857].

**Karuulm's elevator** [2133..2146]: onto the central tile, then `human_boxlever` + `lever` with
the fade down, `squeaky_valve` delay 20 the tick after, the lobby two ticks after the lever,
the fade up two ticks later. Ours was an instant `p_teleport` and an invented line.

**The grave** [2219..2221]: the mesbox the tick after the face. **The trail** [2231..2285]:
`hunting_searching_bushes` O+1, footprints and "The plant has been disturbed recently.
Something came this way." O+3 ("The tree stump has..." for the stump). Our messages were
invented. **The end plant** [2288..2297]: the same search, then stage 10 and the Trapped Soul
at **1282,3729** facing south (ours spawned at 1281,3724, three tiles off and out of reach), a
hint arrow on it.

**The Trapped Soul**: `zombie_attack` / `zombie_hit` / `zombie_death` (were null); no death
seq at all, the ops off the tick after the killing splat, gone with its bones six ticks on
[2349..2355] (`death_anim` null, `death_delay` 5).

**The rocks** [2663..2668]: on arrival the stage, `glass_break`, an objbox of
`raids_ancient_relic`, the player's chatneu3 page on its click. A rock without the device:
UNVERIFIED (ours answers nothing).

**Completion** [2798]: points, the line, disease -50/0, coins and page, `quest_complete_2`
(jingle 153), scroll "You have completed The Ascent of Arceuus!" with `arcquest_altar_dummy`,
rows "1 Quest Point | 2,000 Coins | 1,500 Hunter XP | 500 Runecraft XP | A Memoir Page"
(ours had a Graceful line and the page's title), the xp last. Trobin's next page waits for
the scroll's close (a queue, which waits out any modal) and the level-ups wait for his menu
[2807..2808].

**Movement** (`rsprox_npcinfo.py`): Trobin in both forms, the door guards and the Tasakaal
never step; Mori takes two steps back onto her tile; Asteros four. Ours had Trobin, the
guards, Mori and Asteros on the generated wander-5 default: now none / none / none / wander 1.

## 5. Journal (opened at stages 8 [2200], 9 [2228], 11 [2461], 13 [2684])

Current format: every finished step a struck paragraph, the current step unstruck in
`<col=000080>` with names in `<col=800000>`, no blank rows. All four recorded stages are copied
row for row into `ascentofarceuus_journal`. A step's struck form can differ from its current
one (the Tasakaal paragraph at 8 vs 9+). Stages 1..7, 10, 12 and complete: UNVERIFIED (the
struck text written unstruck; complete ends with the stage-13 step struck and QUEST
COMPLETE!). Not checked on the wire: the scriptrun lane has no quest list (`quest.journal`
fails there on every quest).

## 6. Wire audit (ours: `test/quests/ascentofarceuus.lua` tick log, scriptrun)

| section | R | ours | verdict |
|---|---|---|---|
| door step | leaves + inviswalls for 2 ticks, 81 | same, restored 2 ticks on | matched |
| souls on mages | seq 5532 every 4, say + block | same | matched |
| soul death | seq kill+1, gone kill+5 | seq kill+1, gone kill+5 | matched (after `death_delay` 4) |
| trail | seq O+1, bit O+3 | +2 | matched |
| elevator | step, lever+fade, valve +1, teleport +2, fade in +4 | same | matched |
| Trapped Soul | add, step + swing +1 | add, step +1, swing +2 | 1 tick late, see Open |
| rocks | glass_break + objbox, stage 13 | same | matched |
| completion | jingle 153, music 147, level-up after the menu | same | matched |

Test: 120/123 on scriptrun; the three fails are the Veos ferry leg (Open) and `quest.journal`
(no quest list on scriptrun).

## 7. Bird snares (Hunter)

Recorded shapes (28 lays, 39 Resets, 14 catches), all now ours:

- **Lay** C: "You begin setting up the trap." (spam), the snare on the ground (300 ticks,
  theirs), `human_laytrap`, `hunting_setnoose` (ours: `hunting_setsnare`, no ground snare);
  C+3 the ground snare gone, the seq again, the trap at rotation 2 (ours 0, at once); C+5 a
  step off, west first [369..374] (ours south at once).
- **The catch**: a bird within 2 tiles glides onto the snare and sits 4 ticks; caught, it is
  gone and `*_trapping_*` stands 2 ticks before `*_full_*`; missed, `trap_failing`, then the
  bird hops off and `trap_broken` the tick after [492..507], [392..397]. **No snare of ours
  had ever caught**: `npc_huntall` skips any npc type with no op2, which every bird is.
- **Take** (Dismantle / Check / Reset) O = the face: O+1 `human_hunting_dismantle_net`; O+3
  "You dismantle the trap." (+ "You've caught a crimson swift."), the snare back, 5..10
  feathers (6 5 10 5 9 7 / 5 6 8 6 7; ours 8 always), meat, bones, `hunting_dismantle`.
  Full pack: "You don't have enough inventory space. You need 2 more free slots." [589].
- **Reset** (op2 on broken and full snares; unbound before): the take, then the snare laid
  again where it stood, onto its tile +3, the lay +4 [1168..1175].
- A snare left too long: "The bird snare that you laid has fallen over." [1533] (ours:
  "Your bird snare has collapsed.").

`test/hunter/bird_snare.lua` (18/18): lay, a miss and a catch on the wire match the above
tick for tick.

## 8. Kharedst's memoirs: all five places

The five Reminisces land as section 2. They also complete the Kourend & Kebos medium task
[2828..2860]: per city its bit on `kourend_achievement_diary_multistage` (varp 2087), with
"<col=0040ff>Achievement Diary Stage Task - Current stage: N.</col>" while any are left; with
the fifth `started_kourend_diary`, bit 14 on `kourend_achievement_diary` (varp 2085, 16384),
the diary's line and `kourend_med_count`. Varps 2085 and 2087 were temp and never sent: now
perm and transmitted. `test/magic/kharedst_memoirs.lua` (46/46) does all five.

## Open

- **Grand Exchange**: no clerk ops (Talk-to, Exchange, History, Sets) and no `ge_offers`
  interface script. A system, not this session's.
- **Kitten**: no Interact menu (Stroke, Chase Vermin, Guess age, Shoo away), no feeding, no
  hunger/attention messages or growth (`kitten_pet.rs2` has Pick-up only). All recorded here
  [1162..2659].
- **Engine**: an npc put into `opplayer2` as it spawns swings on three consecutive ticks
  before its 4-tick rate (the Trapped Soul [476..478], the tower's first soul). Jagex: one
  swing on add+1, then every 4.
- **Engine**: npc defend and death sounds go out as area sounds (range 12); Jagex sends them
  to the dealer as `synth_sound` (439, 438, 923, 922).
- The same `npc_huntall` call is in `box_trap`, `deadfall`, `magic_box`, `maniacal_monkey`,
  `net_trap` and `rabbit_snare`: any of their creatures without an op2 can never be caught.
  Not checked against a recording.
- Bird snares: the `add_overlaytimer_loc` clientscript (5474) with each trap state is not
  sent; the ground snare's Lay (opobj4) is unbound; the snare's life (100 ticks) is not
  measured.
- Butterflies: Jagex answers the level check on the step that reaches the butterfly (ap,
  [1018] [1233]); ours a tick later (op).
- `test/quests/ascentofarceuus.lua`'s Veos ferry leg fails to choose "Can you take me
  somewhere?" on scriptrun (Client of Kourend's menu; the test then walks on by goto).
- The Ardougne easy task completed when Aleck's shop opened [212] is not identified.
- UNVERIFIED (no recording): Andrews' "Who are you?" / Council answers, Trobin's menu answers,
  Mori, Asteros and the Tasakaal at other stages, leaving the tower or the copy by the door,
  the stairs down, a wrong rock, the journal at stages 1..7, 10, 12 and complete.
