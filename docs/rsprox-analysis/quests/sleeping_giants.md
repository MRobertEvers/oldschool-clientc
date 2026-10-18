# Sleeping Giants from RSProx recording 96

Recording 96 is a Leagues V session (rev 227, 30 Nov 2024). It plays the
whole quest, from striking the Hill Giant (tick 10026) to the reward scroll
(tick 10856). Afterwards it plays about 30 Giants' Foundry swords. The quest
in `OSRS-Content/.../quest_sleepinggiants/` was rebuilt against it on
2026-10-18.

To reproduce the timeline:

    python3 tools/cox/rsprox_fetch.py get 96          # from the NAS mirror
    <transcribe 96>                                     # see the rsprox memory note
    python3 -I tools/rsprox/rsprox_timeline.py ~/.rsprox/binary/archive/96.txt 10020 10870 > t.txt
    python3 -I tools/rsprox/rsprox_annotate.py t.txt    # names the rev-227 ids from our compacks
    python3 -I tools/rsprox/rsprox_pages.py ~/.rsprox/binary/archive/96.txt 10020 10870   # every page

Leagues relics do not touch anything below except XP. Leagues V multiplies
XP by 16: the recorded per-action 72/51/52 are the wiki's 4.5/3.2/3.3.

## Stages and bits (all recorded)

| Bit | Values |
|---|---|
| `varb13902` stage | 5 at the Yes; 10 when the copy opens; 15 at "We fix tools..."; 20 in the talk after the repairs; 25 when Kovac "gets things ready"; 30 at the end |
| repair bits 13904..13906 | 1 when the repair is offered (just before its Yes/No); 2 when repaired |
| `varb13903` tutorial | 5 words given, 10 sent to the crate, 15 crate taken, 20 first metal in, 25 full, 30 mould talk, 35 mould set, 40 pour talk, 45 poured, 50 preform, 55 handed in, 60 "Kovac take care of it", 65 done |
| alloy / difficulty | 5 for 16 bronze + 12 iron. The wiki metal score is 19, and every minigame pour later in the session matches the wiki formula (89, 130). So the quest pins it. |
| quality | alloy + mould score: 5 + 16 + 14 + 6 = 41 (Medusa ricasso, Medusa blade, saw tip on Flat Broad) |
| seeds | seed_0..2 = 0, 1, 2 (trip hammer, grindstone, polishing wheel) |
| lift temperature | 980 + random(21), over 30 lifts |

## Dialogue

- **Filling pages.** Kovac's halting lines are one page that fills in, and
  each fragment is a click. The page is padded with blank rows to the row
  count of its last fragment: `Do...<br> <br> `, then
  `Do...<br>do not...<br> `, then the full page. A fragment joins the page
  when the previous fragment ends in "...". This holds on every Kovac page,
  and on the message boxes too ("Kovac ponders your question..." / "...").
- **Chathead length variants (all dialogue).** The chathead plays the
  expression's variant for the page's row count (chatneu1..4). This holds on
  2,835 of 2,835 pages across recordings 96, 1636 and 1695.
- **Script 600 line height (all dialogue).** Recorded on both sides and on
  rev 239:

  | Box | 1 row | 2 rows | 3 rows | 4 rows |
  |---|---|---|---|---|
  | chat_left / chat_right | 16 | 28 | 20 | 16 |
  | messagebox | 0 | 31 | 24 | 17 |

  Both are now in `interface_chat/scripts/chat.rs2` for every quest.
- **Chathead npc by phase:**
  - the Hill Giant pages and "Kovac." use `giants_foundry_kovac_multi_outside`;
  - the rest outside use `giants_foundry_kovac_fake_attack`;
  - the introduction uses `giants_foundry_kovac_quest_cutscene`;
  - the repairs and the talk after them use `giants_foundry_kovac_quest`;
  - from the commission on, `giants_foundry_kovac_multi`.
- **Menu titles:**
  - "Start the Sleeping Giants quest?"
  - "Repair the trip hammer?", "Repair the grindstone?", "Repair the polishing wheel?"
  - "Take the items?"
  - make-X: "What metal would you like to add?"
  - "How many would you like to add?" (1 / X / All)
- **Obor branch.** The hand-in branches on the Obor kill count
  (`varp1529_total_hillgiant_boss_kills`). The recording took the "at least 1"
  branch.

## The cutscene (T = the fade after "No... follow.")

| Tick | What happens |
|---|---|
| T | fade_overlay + script 948 (0,255,0,0,50) |
| T+2 | into the copy on local 38,27, stage 10. Lock: fov_clamp 3, cutscene_status 1, minimap 2, gravestone hide, side panels and xp drops closed. Hides npc/loc/obj ops: `hidenpcops` / `hidelocops` / `hideobjops`, so the world menu is Examine alone. |
| T+3 | cutscene loc set; cutscene Kovac on 40,27 facing south; The Foundry unlocked and played; camera to 39,31 h500, looking at 39,23 h300 (rate 232) |
| T+4 | camera to 39,36 h500 r4 / look 39,28 h280 r3; fade in |
| T+9 | camera to 41,21 h700 / look 36,37 h300, rate 4 |
| T+17 | emote_think; camera to 56,34 h700 / look 36,35 h400 |
| T+25 | emote_think; camera to 52,48 h900 / look 38,33 h400, look rate 2 |
| T+33 | "Woah...<br>What is this place?" |
| after it | camera cut to 43,37 h700 / look 40,30 h400; two ticks later the conversation goes on |
| after "We fix tools..." | fade; two ticks later the lock lifts, the cutscene Kovac becomes the quest Kovac (facing north), the player faces 41,31, the quest loc set goes down and the camera resets; the fade lifts the next tick |

Stage 25 takes the player out of the copy: fade, minimap off, two ticks, then
the player is teleported to the public foundry's 3366,11495, facing the trip
hammer, and the music plays. The public Kovac multinpc stands at 3369,11486.

## Repairs

All repairs use the same materials as the walkthrough.

| Tool | After the Yes |
|---|---|
| Polishing wheel | +1 tick `human_leather_crafting`; +3 `hosidius_plough_fix`; +4 repaired, items taken |
| Grindstone | +3 `human_pickupfloor` and `put_down` (delay 15); +4 `sg_grindstone_repair`; +5 repaired; +2 the line |
| Trip hammer | +1 `hosidius_plough_fix`; +4 repaired. Kovac walks to local 37,37 for this conversation. |

## Commission

- **Crate.** It gives 8 bronze bars, 6 iron bars, 2 bronze battleaxes, 2 iron
  longswords, 1 bronze platebody and 1 iron platebody. It plays
  `human_pickuptable` and `pick2`, then shows a two-object box (battleaxe and
  iron bar) at zoom 400.
- **Crucible.** It opens a make-X: mode 13, all six bars, with the max and the
  starting quantity both set to the most of any one bar carried. Each add
  plays `human_elem2_lever_3way_right` and `furnace`. Three ticks later comes
  the box: "You add 8 Bronze bars to the crucible." or "You add 2 Bronze
  battleaxes to the crucible, worth 4 Bronze bars."
  - Plurals just add "s" ("platelegss").
  - After a box is continued: "You need to add N more metal bars worth of
    material to pour the crucible into the mould jig." When full: "The
    crucible is now full of molten metal, it is ready to be poured into the
    mould jig."
- **Pour.** `human_push` with `sg_crucible_pour_start`. The bronze and iron
  counts drop to 10/28 (16→6, 12→4). The alloy is set the next tick, and the
  mould is poured six ticks after the push.
- **Lift.** With hands full: "You must free your hands so you can hold the
  preform." With ice gloves: "Your Ice gloves cool the preform as you reach
  for it." It plays `bf_cooldown` and opens the hud.

## Refinement

| Action | Start | Then |
|---|---|---|
| Trip hammer | `human_preform_lift_start02` on arrival, `..._loop02` every tick after | first work 2 ticks after the start, then every 5: +20 progress, −25 temperature, `sg_hammer_tool_hit` |
| Grindstone | `human_preform_lift_start`, `..._loop` | every 2 ticks: +10, +15, `sg_grindwheel_tool_grind` |
| Polishing wheel | same as grindstone | every 2 ticks: +10, −17, `sg_polishwheel_tool_polish` |
| Natural cooling | | −1 every 2 ticks, always |
| Dunk / Quench | step from the arrival tick, `100_sizzle_gloves` / `100_underwater_pick` every tick, `human_pickupfloor` / `human_pickuptable` from the second tick | per-tick steps 27, 30, 33, 37, 41, 45, 49, 53, 57, 62, 67, 72, 77, 83, 89, 95 (293 runs) |

- **Sweet spots.** Script 6122 opens a spot (1) straight after a machine
  tick. A click on the machine turns it green (2), and the next work gets +60
  instead of +10, plays `bell` and shows "<col=229628>Your quick reactions
  allow you to find the perfect position to work the preform!"; then 6122(0).
  In the quest they opened at completion 420 and 840.
- **Finish.** At 1000 the player says "Looks like the sword is finished. I
  should take it to Kovac." On the continue: `giants_foundry_finish_sword`
  and `blade4_5`.
- **Public foundry machines.** Every 2 ticks, `sg_polishwheel_loop` at 36,29
  and `sg_grindstone_loop` at 35,36 (range 12). Every 5 ticks,
  `giants_foundry_hammer01` on the trip hammer (35,40) and its belt (34,39),
  with `sg_big_hammer_hit` at 39,41 (range 15).

## Not settled by the recording

- **Heat-preform / Cool-preform.** Only one slow step was recorded (−7). The
  slow steps are the fast table scaled by 7/27.
- **Sweet spot placement** in later swords. The quest uses the recorded
  420/840 for both swords.
