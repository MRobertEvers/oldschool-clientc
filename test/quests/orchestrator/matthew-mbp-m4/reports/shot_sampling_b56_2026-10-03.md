# b56 shot sampling of tests that landed without visual review (2026-10-03)

Read-only Opus samplers opened the published `play/` shots. One section per
sampler, added as each reports. "Verified" = the orchestrator checked the claim
against the file; anything else is the sampler's reading.

## swansong, contact (sampler 2)

### swansong: SEND BACK (shots fine, two rows cannot fail)
242 shots: 42 opened singly, 201 by montage. Fights real (three trolls, Queen
200 hp, 16 sharks -> 4 left, lowest hp 24/99); scroll and rewards right.
- `wizardsGuildDoor-tile`, `craftingGuildDoor-tile` (swansong.lua:299, :345):
  the goto before the door click already lands within the row's +-2 tolerance,
  so the row passes with the door shut. Verified. Fix: require the inside tile
  (x >= 2585; z <= 3288).
- `184-wear-apron`: the brown apron takes the body slot; the black d'hide body
  is never worn again, so the Queen is fought without the body the header says
  was added for her. Re-wear it after the Crafting Guild.
- CONTENT: `swansong_colony.rs2:268-277` `[oplocu,swan_firebox]` sets the varbit
  and never deletes the log. Verified. Needs a source for the real behaviour
  (wiki Swan Song walkthrough) before the `inv_del`.
- `227-killQueen-present`: the Queen is not on screen until 230 (minor).

### contact: SEND BACK (rows cannot fail, thin margin, evidence stale)
110 shots: 13 opened singly; the 97 montage tiles were opened but NOT judged
(the sampler lost them from context and its re-read was refused), so contact
has no complete visual pass yet.
- Evidence is stale: `quest_contact/play` was last committed in 513d69e5b3
  (2026-10-02 22:39), before fbfe8c61c rewrote the rows; ec28da6a2f touched
  only swansong. Verified. The b55 log line "both rerun green FULL" is not
  backed by a republish for contact.
- Ten `r, d = t.world.tile(); t.check(name, r, ...)` rows pass on any tile
  (bank.arrived, dungeon.arrived, maze1.tile, chasm.arrived, maisa.across,
  bank2.arrived, dungeon2.arrived, chasm2.arrived, chasm2.near, chasm.left);
  ten `t.world.tile()` reads verified. `chasm2.hop1` is `t.check(..., true)`
  and hid a walk timeout in this run. Three snapshot rows are pure reads.
  The lint rule does not see a pure read reached through a local.
- Giant Scarab: lowest hp 9/99, no margin row; poison keeps ticking after the
  fight (hp 50 -> 26 -> 13 by shot 101) with 4 sharks unused.
- `100-quest.scroll`: title reads "Contact!!" (the name already ends in "!").
  Not verified in the .rs2.

Of the 18 rows fbfe8c61c rewrote, 16 now assert a real value; the two swansong
door rows above do not.

## mortton, mourningsendpartii (sampler 4)

### mortton: SEND BACK for a content defect (the run itself is sound)
185 shots: 26 at full size, 159 by montage. Prerequisite really set: shot 001
chat reads "Priest in Peril complete.", qp 5 -> 8.
- CONTENT: every npc and player dialogue page prints its mood tag as text
  ("<p,happy>Ah, excellent, ...") and the longer pages overrun the speaker
  name and "Click here to continue" (worst: 100; also 053, 054, 071, 098, 150,
  151). Verified: 196 calls in 12 files pass `"<p,<mood>>..."` to `~chatnpc` /
  `~chatplayer` (ulsquire_shauncy 75, razmire_keelgan 65, zulandra 19,
  quest_mortton 13, mortton_coffin 6, afflicted 5, pest_activity 4,
  pest_shop 3, ...). Ours: `[proc,chatnpc]` = `~chatnpc_anim(^chat_neutral,
  $text)` (interface_chat/scripts/chat.rs2:195-196, :266-267), which never
  reads the tag. LostCity: `[proc,chatnpc]` runs `split_init` and each page
  takes its head animation from `split_getanim($page)`
  (LostCity_Content2 scripts/interface_chat/scripts/chat.rs2:283-295,
  :323-333). The defect predates the prerequisite fix. Seam row for b56.
- `175-temple.pyreLit`: no fire or spirit in the shot; the PASS rests on the
  stage varp. Probably capture timing (minor).
- Not shown: the Morytania entry (the run teleports to 3481,3279).

### mourningsendpartii: KEEP
532 shots: 46 opened large (one door or chest per colour stage, the gates, the
Death Altar, the scroll), 486 by montage. No findings. Scroll and rewards
right; qp 2 -> 4; shot 001 chat reads "Mourning's End Part I complete.".
- Note: setup's `::mend2` also force-completes Part I, so the `::complete` arm
  fix is redundant in this test (harmless).
- Not shown: the mourner HQ entrance (the run teleports into the basement).

## forgettabletale, ghostsahoy (sampler 3)

### forgettabletale: KEEP
371 shots: all by montage, 6 opened singly, plus a pixel scan for the clear
colour (12 hits, all the void around the underground cart hub, not empty
frames). No findings. Both prerequisites paid: qp 3 -> 5, scroll total 5.

### ghostsahoy: SEND BACK (shots clean; the setup fix does nothing here)
969 shots: 389 as full frames, the Rune-Draw stretch 221-808 as chatbox
montages (every page read; the full frames of 221-800 were not opened).
- `003-quest.stage.not_started`: chat reads "Priest in Peril is already
  complete." Setup runs `::ghostsahoy` (ghostsahoy.lua:19) BEFORE
  `::complete quest_priestinperil` (:47), and `~ahoy_reset` sets varp107 and
  varp302 complete without paying points (ahoy_shared.rs2:240-241). Verified.
  So `::complete` takes its "already" branch.
- `955-quest.scroll`: "Total Quest Points: 2" with two prerequisites complete;
  it should read 4. Fix: `::complete quest_restlessghost` and
  `::complete quest_priestinperil` before `::ghostsahoy`, then rerun.
- Low confidence: hp stays 23 through the whole giant lobster fight (885-896,
  48 ticks). `[giant_lobster]` has an .npc block; check that it swings back
  on the rerun.
- Not shown: the gate past Drezel (both runs teleport from Lumbridge).

## troll, regicide, deserttreasure (sampler 1) -- the raid-branch hardening (50d90d513)

All three KEEP. Every published shot was seen (85 / 632 / 443; 7 / 14 / 20
opened singly, the rest by montage). No error text, no empty frames (the dark
groups regicide 123-138, 518-522 and deserttreasure 126-151 are real dark
scenes). The hardening added no forbidden cheat: new ::setlevel rows are in
setup, supplies are bring-alongs (antipoison: Quest Helper Regicide.java:260).

Fight margins read from the ledgers:
- troll, general: lowest hp 71/99, 1 of 26 sharks eaten. Four of the five
  general attacks returned timeout/no_row; only troll_general2 was fought
  (the quest needs one key). Shots 059/060 show a Mountain troll, not a
  general.
- regicide, Tyras guard: lowest hp 32/70, 1 shark left; tripwire poison cured.
- deserttreasure: Damis lowest 28/99 with 6 of 20 sharks left (the true form's
  aura drains prayer to 0, so more prayer will not help if it fails again);
  Kamil lowest 89/99, magic 91 at the cast; Fareed (not hardened) lowest
  24/99 with no margin row.

Hygiene, none of it new in 50d90d513 (carry item 18 material):
- The margin rows pass on `sharks left >= 2 OR lowest hp > 25`
  (regicide.lua:914, verified): a fight ending at 26 hp with no food passes.
  Should be AND, or a floor on each.
- deserttreasure.lua has mid-run `::setlevel` rows (:243-245 leg 2 magic 70 and
  melee 99; :701 leg 5 magic 99) and hands out a new lockpick on every miss
  (:345). Verified. Move the levels to setup; the lockpicks are a bring-along
  count question.
- Troll agility 40 -> 70 and deserttreasure thieving 53 -> 99 exist only to
  remove dice failures.
- regicide 555: the "Tyras guard" chathead is a plain bald head (cache
  chathead id not checked). 619: mostly black viewport on a floor change.

# b56 round 1: the five greens the batch sampler did not open

The batch sampler opened three of eight (kept anothersliceofham and
gardenofdeath, sent taleoftherighteous back for a goto through Phileas's house
door the grader read as driven). Two more read-only Opus samplers judged every
goto of the other five.

## sleepinggiants: KEEP
295 shots: 10 opened singly, 285 by montage. The foundry is really played:
heat 0 -> 826 at the lava, trip hammer at 747 (completion 0 -> 340), grindstone
at 571 (-> 670), polishing wheel at 224 (-> 1000), quality 31 throughout. Both
gotos are open-air travel. Rewards literal (6000 Smithing, 1 qp).
Minor: constant-true note rows (lua:56, 60, 67, 119, 127, 144, 228, 274, 286);
three details print "table: 0x..." (lua:56, 67, 179); the foundry hud stays on
screen outside the foundry (shots 248/249, 274-293).

## dreammentor: SEND BACK
511 shots: 22 opened singly (all goto shots, every boss shot, the scroll), 489
by montage. Fights are real (lowest hp 61, 52, 61, 93 of 99). Verified in the
file:
- Four gotos past obstacles: `goto-talkToOneiromancer` (lua:333) out of the
  Cyrisus cave past the wall crawl and lunar_mine_slanty_ladder_up;
  `goto-fillVialWithWater` (:348) into the sink house past its closed door at
  2091,3916; `goto-lightBrazier` (:373) out of that door and in through the
  brazier hall's at 2082,3913; `goto-returnToOneiromancer` (:417) out of the
  hall. The test never clicks lunar_moonclan_door (0 occurrences).
- The four fight margin rows are `t.check(name, true, ...)` (:409).
- `::give shark 25` mid-run (:387) after dropping the pestle, tinderbox and
  hammer to make room; the pack holds 1 shark from the third fight on.
Minor: constant-true "bars" rows (:103, 115, 131, 154, 174, 209); the comment
at :242 names the wrong kit; 4-5 line dialogue pages crowd "Click here to
continue" (shots 195, 234, 365, 373, 407, 410, 413).

## atfirstlight: SEND BACK
126 shots, all by montage, 9 opened singly; 117 rows PASS, coverage FULL, no
fights; rewards literal (4500 Hunter, 800 Construction, 500 Herblore).
- `goto-talkToVerity` (lua:57) and `goto-talkToVerityEnd` (:253) land at
  1559,9467 in the sealed pocket BEHIND the bar (counter at z 9463, flap
  hg_table_tavern02_door01 at 1556,9463 has no op). Talk across the counter
  from about 1559,9461. The parity note "talk from 1559,9467"
  (atfirstlight.notes.md:6) is wrong and must be corrected.
- `goto-talkToAtza` (:203), `goto-takeHammer` (:213), `goto-makeEquipmentPile`
  (:219) land inside Atza's house and the hammer house past fortis_door_l at
  1698,3064 and 1695,3068; the test never clicks a fortis door (verified: 0).
- `catchJerboa-lay<r>.<i>` (:157) passes on `ok` OR `timeout` (verified); in
  this run the second trap was not laid (shot 060: "You can't lay a trap
  here.").

## twilightspromise: SEND BACK (smaller faults)
413 shots, all by montage, 10 opened singly; 155 rows PASS, coverage FULL.
Mezan lowest hp 74/80, cultists 75/80, 12 of 12 sharks left.
- `goto-enterHQ` (lua:460) lands on the staircase at 1660,3150 inside the
  side room whose only exit is fortis_door_l_reverse at 1657,3150, then opens
  that door from the inside. The HQ ground floor is walked into through the
  open arches at x 1652; the note "door on the EAST wall"
  (twilightspromise.notes.md:17) has it backwards.
- `goto-enterColosseum` (:364) lands on a blocked tile (1797,3106); the
  walkable approach is 1795,3106.
- Margin rows pass on `food >= 2 OR lowest > 25` (:413 verified, :604).
- Presence-only checks (`x ~= nil`) at :212, :393, :431. No shot shows a live
  cultist. The Civitas illa Fortis Teleport reward is not asserted.

## wanted: SEND BACK (the fixed branch and most older gotos)
236 shots, all by montage, 8 opened singly; 112 rows PASS, coverage FULL. This
run drew position 4 = the Lumbridge Swamp Caves, so the changed branch ran.
- The cave entrance is now a real click, but `pos4.goto` (lua:334) then
  teleports from 3169,9571 to 3227,9547 past the stepping stone
  swamp_cave_steppingstone_b at 3221,9554
  (skill_agility/configs/maplink_agility.dbrow:854-866); no light source or
  spiny helmet is brought.
- `::setlevel hitpoints 99` mid-run (:359, verified) after Flames of Zamorak
  took hp to 9, with five sharks uneaten.
- `t.check("huntDownSolus", true, ...)` (:462, verified) is the grader's only
  DRIVEN evidence for that guide step.
- Older gotos past guide steps: the White Knights' Castle stairs (amik1,
  amik2, amik3, tiffy2, tiffy3), the Taverley Dungeon ladder and the Black
  Knights' base door (daquarius1), the Varrock chapel door (mage), the Castle
  Wars lobby door (pos2), the Champions' Guild door (cg), the Lumbridge cellar
  trapdoor (dk), McGrubor's Wood railing (pos6), the essence mine teleport
  (mine).

## What this says about the grader
Five of the eight round-1 tests had a goto into an enclosed space that
helper_coverage read as FULL (taleoftherighteous, dreammentor, atfirstlight,
twilightspromise, wanted). The grader merges a guide ObjectStep for a door or
stair as TRAVEL into the step it leads to. The third sampler's static
walkability search (floor flags + walls from the map files) is saved beside
this report in `sample_tools/` (reach.py, locs_near.py, comp.py,
goto_table.py); it does not know stair links, script-spawned locs or door
states.
