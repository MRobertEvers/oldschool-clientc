# Wanted! — rebuilt from RSProx index 4464

**Session:** index 4464 (rev 241.1, w438, 2026-11-02, "Wanted!"): the whole quest, from Sir
Tiffy's first page [175] to Sir Amik's scroll [5109]. Transcripts:
`~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx4464.txt` (and `home-bas`).

**What was there before:** a quest built from quest-helper and the wiki, with invented pages.
124 of the recording's 618 distinct pages matched it, and most of those were Tiffy's shared
lines. Savant spoke in mesboxes, the scenes had no lock, camera or timing, and the
observation records, dossiers, Canifis briefing and essence-mine scene did not exist.

**Now:** `OSRS-Content/osrs-content/server/scripts/quests/quest_wanted/` is rewritten:
`wanted_tiffy_amik.rs2`, `wanted_commorb.rs2`, `wanted_records.rs2` (new),
`wanted_daquarius.rs2`, `wanted_mage.rs2`, `wanted_hunt.rs2`, `wanted_journal.rs2`,
`wanted_shared.rs2`, `wanted_debug.rs2`, `configs/wanted.constant`. Hooks:
`recruitmentdrive.rs2` (Tiffy, unchanged call), `sir_amik_varze.rs2` (every stage below 11),
`templeoftheeye.rs2` (the Varrock mage asks Wanted! first), `drop_tables/black_knight.rs2`
(Daquarius's "Stop!"), `skill_runecraft/essence_mine.rs2` (the last stop's copy),
`player/death.rs2` (Tiffy's Falador respawn).

The generic interactions in the same session are audited separately, with a verdict per
row, in [wanted_generic.md](wanted_generic.md).

## Pages

`rsprox_pages.py` → 861 rows. A static check (every recorded page's text, `<br>` → space,
the recorder's name → `<displayname>`, against the scripts):

| check | result |
|---|---|
| distinct npc/player/mesbox/objbox pages found verbatim | **615 / 618** |
| the 3 not found | Turael's task count (generated), Rick Turpentine (macro file, matched by the generic audit), Savant's "'slow-<br>teleport'" (one string here, broken at the hyphen by our layout) |
| pages whose mood (`chat<mood><rows>`) matches ours | **688 / 688** |

Pages the recorder never saw are the wiki's (`Transcript:Wanted!`, `Transcript:Sir Tiffy
Cashien`) and are marked `UNVERIFIED` where they are used: the squire branch, buying the
Commorb, the lost Commorb, the hat in the bank, Daquarius before the mage, the "No." to the
start, Amik's "I don't feel like helping", four of the ten failed-scan lines, and the clue
items and reminders of the twelve pool locations never visited.

## Stages (varbit 1051 `wanted_main`) and every quest var

| var | value | tick | goes out with |
|---|---|---|---|
| wanted_main | 1 | [255] | the start confirm's "Yes." (+ "You've started a new quest") |
| | 2 | [261] | the click of "...work out for us both, what?" |
| | 3 | [279] | the click of "He will then offer you to join as a squire..." |
| | 4 | [525] | the click of Amik's last squire page |
| | 5 | [597] | the click of "I have already notified Sir Amik..." |
| | 6 | [712] | the click of "Hey, there's nothing I like more than fighting!..." |
| | 7 | [776] | the click of "Any tips on how to find him?" |
| | 8 | [919] | "YES" to the making (the Commorb with it) |
| | 9 | [2601] | the click of Savant's "You do?" |
| | 10 | [4951] | Solus's del tick (death + 2), with the hat |
| | 11 | [5109] | the click of "Well hand it over then, and let's see it!" |
| wanted_contacted_savant | 1 | [949] | the first call's first Savant page |
| wanted_commorb_intel | 1 | [1034] | the click of "So I have to just wander around..." |
| wanted_zammy_mage_hint | 1 / 2 | [1184] / [1948] | "Twenty parts..." / the 20 pure essence taken |
| wanted_daquarius_hint | 1 / 2 | [2484] / [2539] | Savant's advice / the click of "Stop!" |
| wanted_lord_d_exposition | 1 | [2598] | the click of "...lie-detector? That's pretty useful!" |
| wanted_missionN / complete | see the hunt | | |
| wanted_black_knight_dead | 1 | [4504] | the sixth stop's knight dead |

Both carriers (varp 571, 572) are declared `scope=perm, transmit=yes` (`wanted.varp`).

## The journal

Opened 28 times. Every distinct text is copied row for row (`~quest_journal_rows`): stage 2
[271], 3 [441], 4 [531], 5 [606], 6 [745], 8 contacted [1015], with Savant's data [1051], with
the mage's price [1199], with his answer [2188], 9 before Canifis [2621], after it [2966]
(unchanged at every later stop through [4568]), 10 [4974]. UNVERIFIED: stage 1 (stage 2's
rows), 7 (stage 6's), 8 before the first call, 11 (stage 10's struck, then QUEST COMPLETE!).

## The Commorb (wanted_crystal_ball)

| op | recorded | ours |
|---|---|---|
| Scan before Canifis | "There is nothing nearby to scan." [997], [2618], [2760] | same |
| Scan after Solus | "You have nothing to scan for..." [4983] | same |
| Scan, nothing found | C: `wanted_crystal_ball_scan` 2721 + spotanim 454 @92 + sound_area 1900 r5; C+2 a clickable Savant page [3142] | same; one of the wiki's 10 lines at random (7 recorded) |
| Contact | first call [947..953], then "Hello there..." + "What would you like to ask about?" Current Assignment / Savant / The Temple Knights / Nothing | same; Current Assignment per stage ([1004], [1791], [2975], [4988], [5021]) |
| Playback | "Select Information" → "Select Information Log" (5) → "Select Dossier" (3); "There is no information..." [933]; "You have no previous information..." [1249] | same |
| Dossiers | journalscroll 741 "CLASSIFIED", 65 / 51 / 43 rows, `%pagepos` counted [1235], [1536], [1553] | same, row for row |
| Records | only inside the White Knights' Castle ([1256] refused at 3254,3420) | castle walls 2954..2995 × 3328..3354 (judged; the recorder watched at 2965,3353) |
| Destroy | confirmdestroy with the recorded note [939] | same |

## The observation records (`wanted_records.rs2`)

Drafted with `rsprox_cutscene.py`, then merged with each tick's chathead page. Record 1
[1296..1521] plays in a copy of square 45_151, and record 2 [1567..1781] in a copy of 50_52.
Each line is a page with nothing to click and no chat echo, plus the speaker's overhead in
the recorded pieces, 2–3 ticks apart. The barrage, deaths, transforms (`..._cutscene2`,
`_telly`, `_pray`), spotanims, the projectile and the camera all land on their recorded
ticks. At the end the lock lifts, the player returns to the tile they watched from, and
the screen fades in.

## The hunt (`wanted_hunt.rs2`)

| place | mission | recorded at | Solus | reaction |
|---|---|---|---|---|
| Canifis | 1 | [2831..2949] | 3488,3489 SE | teleports away; Savant pulls the next stop's item |
| 2nd | pool (13 Brimhaven) | [3235..3356] | 2798,3168 S | Teleother Camelot → 2759,3480 [3289]; blue cape |
| Champions' Guild | 2 | [3459..3534] | 3195,3356 NW | Smoke Barrage, black fade; next item |
| 4th | pool (7 Goblin Village) | [3681..3882] | 2956,3513 S | Flames of Zamorak: 29 → 10 hp [3732]; wake 2971,3338; bone spear |
| Dorgesh-Kaan | 3 | [3993..4180] | the Woman 3309,9611 NE | turns into Solus, backs away facing the player to 3298,9605; next item |
| 6th | pool (17 Slayer Tower) | [4426..4546] | 3423,3535 NE | summons a Black Knight beside the player; on its death 20 noted pure essence |
| Essence mine | 4 | [4596..4972] | copy of 45_75, 20,21 | rangers, Ice Barrage, the fight; hat, back to the teleport's return tile |

Every scene is timed exactly as recorded: a page with nothing to click every 6–7 ticks,
`// [tick]` on each delay. The cutscene lock comes two ticks after the scan, and the last
page is clickable after the unlock.

**The Canifis briefing** is an area trigger: entering map square 54_54 at stage 9 before the
first stop ([2749], [2763]). It fired on both entries and never while the player stayed in
the square. `[mapzone,0_54_54]`.

**Where the scan works (judgement).** The scan only works on Solus's level. In Canifis it
works within 5 tiles of his tile with a line of sight to it (found 4 tiles off [2831]). At
every other stop it works anywhere in the place. These are quest-helper's boxes, and
Brimhaven's is cut at z 3168: found at 2798,3167 [3235], nothing at 2800,3169 across the
pub's wall [3229]. His own tile is no guide: the Champions' Guild's 3195,3356 is on a wall
line by the inner door, and a first try at distance plus line of walk/sight failed there in
our test. Recorded finds: 4 [2831], 1 [3235], 1 [3459], 3 [3681], 1 [3993], 3 [4426]. At
the twelve unrecorded locations Solus stands on the nearest free tile to the place's
middle. "Slightly above average" is one of the wiki's ten random failure lines, not a
near-miss rule (it was recorded twice, at [3639] and [3664]).

**The pool:** missions 5..19, three drawn at random. Clue items, Savant's three lines and
her reminders for the twelve unrecorded locations are the wiki transcript's. The fur has
no wiki reminder, so it uses the others' wording (UNVERIFIED).

**Lord Daquarius's "Stop!"** comes from a Black Knight killed within 16 tiles of him while
the hint is 1. It appears on the drop tick [2536], with the hint on its click [2539]. Two
earlier kills, before Savant's advice, said nothing ([2324], [2402]).

## Npcs

| npc | recorded | ours |
|---|---|---|
| wanted_solus_attackable | 40 hp; pummel 414 every 3 ticks, block 415; no swing/block sound; death 836 + human_death synth [4949]; del + 2 | `wanted.npc`; anim params moved out of the generated block |
| wanted_summoned_black_knight | hacksword_slash / steel; human_death on death [4504] | death_sound added |
| the cast (wanted_solusN, Woman, rangers, cutscene types) | never wander | `~wanted_cast_add` (`npc_setmode(none)`, spawn angle) |

## Completion

[5109]: stage 11, qp +1, the hat taken, disease −50/0, the scroll ("You have completed
Wanted!!", the hat at 76,128, "1 Quest Point", "5,000 Slayer XP", "Access to the",
"White Knight Armoury"). Amik's explanation goes on after the scroll closes [5118] → [5119].

## Other content in the session

See [wanted_generic.md](wanted_generic.md). Fixed there: Sir Burrus, potion last-dose lines,
super restore, Turael's assignment and shop, Black Knight death sounds, Captain Barnaby,
the Champions' Guild door. Wanted!-side: Tiffy's armour shop page and the Falador respawn
(a new varp 9514; "Yes" branches UNVERIFIED).

## Open

- The quest test (`test/quests/wanted.lua`) is rewritten for this flow; its result is in
  the session report.
- The scenes' camera is audited on our wire (scriptrun, `TORIRS_SCRIPTRUN_WIRELOG`): Canifis
  (lookat at the lock, reset +27, unlock +113), record 1 (moveto/lookat 232/232 a tick after
  the copy's REBUILD_REGION) and Dorgesh-Kaan (no camera at all, unlock +154) match the
  recording packet for packet. Dorgesh-Kaan used to send a lookat the recording does not
  have. The rest of the scenes' packets are not measured on our wire yet.
- The client's camera follows the deob now: CAM_RESET zeroes both targets and all rates, so
  a look-at-only scan scene turns to Solus without moving the eye; before, the records'
  232/232 move rates and height 1000 survived the reset and lifted the next scan's eye.
  Roofs in a scene are the deob's `Statics.method7854`: hidden only with Hide roofs on, or
  when the camera is low under a remove-roof tile.
- Multinpc shells (`wanted_solusN`, `wanted_solus_fake_woman`): an older note said our
  client drew a spawned shell as nothing. Not re-checked with a photograph.
- The ess mine's static portal locs in the copy: varbit 2313 is set to 0 so the loc portals
  show; not photographed.
- The Black Knight at the sixth stop: the wiki says the player can run instead of killing
  it. Here the essence comes only from its death.
- Unrecorded spawn tiles and scan zones for missions 5, 6, 8–12, 14–16, 18, 19 are judged.
- Tiffy's Falador respawn tile 2971,3340 is judged.
