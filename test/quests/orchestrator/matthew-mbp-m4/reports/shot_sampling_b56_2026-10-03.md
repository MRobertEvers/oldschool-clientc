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
