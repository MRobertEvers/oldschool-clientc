# Tutorial Island from RSProx 4289

Source: local index session **4289** ("rsprox local (mrobe)", recorded 2026-10-20, rev 241.1,
world 433, normal world). It is one whole run, from setting a display name to the Home
Teleport into Lumbridge: ticks [0]..[1407]. The index failed to index it
(`NoSuchElementException`), so search cannot find it, but `rsprox_index.py get 4289` transcribes
it. Tick numbers below are the transcript's `[n]`. **L** is the player's last step of a walk, and
a loc or npc op fires at L+1 (`tools/rsprox/rsprox_interact.py` labels both).

Content: `OSRS-Content/osrs-content/server/scripts/tutorial/`. Test:
`test/quests/tutorialisland.lua`.

## The step varp (281): Jagex's own values

No table of these had been published, so our numbers were this port's own. The recording gives
every transition (old -> new, at the event):

| new | tick | event | | new | tick | event |
|---|---|---|---|---|---|---|
| 1 | 0 | name set | | 360 | 675 | dagger made: gate |
| 2 | 132 | experience chosen: talk to guide | | 370 | 693 | through the gate: talk to Vannaka |
| 3 | 150 | guide's page: open settings | | 390 | 717 | Vannaka's 2nd page: open worn |
| 7 | 156 | settings opened | | 400 | 723 | worn opened |
| 10 | 184 | guide done: door | | 405 | 725 | equipment stats opened |
| 20 | 190 | door: Survival Expert | | 410 | 728 | dagger equipped |
| 30 | 198 | talking: given net | | 420 | 734 | "butter knife" page |
| 40 | 209 | inventory opened | | 430 | 741 | sword + shield equipped |
| 50 | 219 | shrimp caught | | 440 | 750 | combat tab opened |
| 60 | 229 | skills opened | | 450 | 760 | through the pen gate |
| 70 | 233 | talking: given axe | | 460 | 765 | attacked a rat (sit back) |
| 80 | 246 | logs | | 470 | 775 | rat killed |
| 90 | 262 | fire lit | | 480 | 807 | "Next up, ranged" page |
| 120 | 268 | shrimp cooked | | 490 | 817 | shot at a rat |
| 130 | 283 | gate | | 500 | 833 | ranged kill: ladder |
| 140 | 331 | chef's door | | 510 | 895 | up the ladder: bank |
| 150 | 338 | talking: flour + water | | 520 | 980 | bank opened |
| 160 | 349 | dough made | | 525 | 1011 | poll booth done |
| 170 | 360 | bread baked | | 530 | 1027 | door: Account Guide |
| 183, 200 | 389 | chef's back door (both in one tick) | | 531 | 1036 | talking: open account tab |
| 210 | 394 | run toggled | | 532 | 1051 | account tab opened |
| 220 | 429 | quest guide's door | | 540 | 1078 | account talk done |
| 230 | 508 | talking: open quest list | | 550 | 1083 | door: Brother Brace |
| 240 | 512 | quest list opened | | 560 | 1159 | talking: open prayer |
| 250 | 531 | quest talk done: ladder | | 570 | 1163 | prayer opened |
| 260 | 540 | down the ladder | | 580, 610 | 1169 | Brace done (both in one tick) |
| 270 | 583 | talking to the miner | | 620 | 1173 | chapel door |
| 300 | 585 | pickaxe given: mine tin | | 630 | 1216 | talking: open magic |
| 310 | 595 | tin | | 640 | 1221 | magic opened |
| 320 | 607 | copper: smelt | | 650 | 1232 | runes given |
| 330 | 619 | bar | | 660, 670 | 1240 | spell cast (it HIT for 1 at [1243]; the boxes are scripted) |
| 340 | 644 | talking: given hammer | | 671 | 1241 | Learning the Ropes complete |
| 350 | 652 | anvil menu opened | | 680 | 1335 | told Terrova "go to the mainland" |
| | | | | 1000 | 1361 | Home Teleport landed |

**Implemented:** every constant is now Jagex's (`tutorial.constant`), with 350 (`tut_smithing_menu`) set by the anvil.
- The test's table is updated to match.
- 460, 490 and 680 are declared, but nothing sets them yet: our rats have no attack-start hook.

## Varp 406: the overlay's progress bar

The cache draws `scale(%varp406, 20, 16384)` (script 2263): the varp is a **milestone out of 20**. Our formula `step * 16384 / 671` overflowed the bar.

Jagex's values:

| value | tick | event |
|---|---|---|
| 2 | [190] | first door |
| 3 | [232] | axe given |
| 4 | [283] | gate |
| 5 | [355] | bread into the range |
| 6 | [389] | chef's back door |
| 7 | [429] | quest guide's door |
| 8 | [540] | down the ladder |
| 9 | [607] | copper mined |
| 10 | [693] | mine gate |
| 11 | [750] | combat tab opened |
| 12 | [812] | bow given |
| 14 | [895] | up the ladder |
| 15 | [1027] | Account Guide's door |
| 16 | [1083] | Brace's door |
| 17 | [1169] | Brace done |
| 18 | [1173] | chapel door |
| 19, 20 | [1232] | runes given |

**Implemented:** `~tutorial_milestone(step)`. It is exact at each step change, and the three that land a tick or two off a step in the recording (3, 5, 12) land on the nearest step.

## The step box, hint arrows, overlay hints, flashes

Every step refresh ([190], [206], [209]) sends the same set of packets:
- `if_opensub toplevel:overlay_atmosphere <- tutorial_overlay 614`.
- An empty `if_setevents` on each side stone the player has not been given.
- `if_sethide tutorial_overlay:noclick true` (sent twice).
- `hint_arrow reset`, then the new hint.
- Then either:
  - **the box**: `if_opensub chatbox:chatoverlay <- mesoverlay 263` + clientscript `mesoverlay` (1974) with `"<col=0000ff>Title</col><br>text"`; or
  - **no box**: `if_closesub chatbox:chatoverlay` (the walk to the Survival Expert, step 20; and to Brace, 550).
- `tutorial_overlay_hint` (2584) with a picture case, or `if_sethide tutorial_overlay:hint_layer true` when the step has none.

Implemented:
- **Box:** `~tutorial_box` now opens `mesoverlay` into `chatbox:chatoverlay`, under the chat modal. It was `messagebox_titled` in the modal slot.
  - Every title is `0000ff`.
  - The "Your character is now attempting to..." boxes and the furnace's two lines have no title.
  - The mining one has a `<br>`.
  - Box text corrected where the transcript differed:
    - settings "at the bottom right";
    - woodcutting ends at "all you need is an axe.";
    - the chef's door and chef boxes;
    - "This will only take";
    - "copper." / "tin.";
    - "Smelting" not "Smithing".
- **Overlay hint per step:** cases 1, 6-11, 21, 12-16, 22, 17-20 and 23, all in `~tutorial_step_overlay_hint`. This tree's cache authored cases 5-23 with Jagex's numbering. The guide door is case 1, not our camera hint 2; the Survival Expert is 6, not move 3.
- **Hint arrows:** each step's tile, edge and height as recorded (`~tut_hint(position, coord, height)`; 2 centre, 3 west, 4 east, 5 south, 6 north). Three pointed at other tiles: the first tin rock (3077,9503), the copper rock (3083,9501) and the pen gate (3110,9518).
- **Tab flashes** (`flashside` 3756 = tab + 1): 12, 4, 2, 3, 5, 1, 9, 6, 7. Ours already matched.

**Not implemented: inventory and interface highlighting.** Jagex runs these clientscripts:
- `inv_highlight_item_on` 8461 `(obj, dbrow style, n)`:
  - shrimp ([219], alert style)
  - logs + tinderbox ([246])
  - raw shrimp ([262])
  - flour + water ([342])
  - dagger ([725])
  - sword + shield ([737])
  - bow + arrows ([812])
- `inv_highlight_clear_all` 8465 at the next step.
- `ui_highlight_update` 8478 `(3, 1, smithing:dagger, -1, ui_highlighting_style_tutorial_cta, 0)` on every anvil open ([652]..[671]), and `ui_highlight_clear` 8484 `(3, 1)` when the dagger is made.

None of these scripts, nor the `ui_highlighting_style_*` dbrows (7034 cta, 7036 alert), exist in the rev-239 cache. They have to be authored into it (as cases 5-23 of 2584 were) before content can call them.

## 1. The oak

[288], [291], [404]. The response comes at L+1:
- `[facing] coord=` the oak, `sizex=3, sizez=3`;
- a mesbox (`messagebox` 229, line height 17): "You won't be able to chop oak trees until you have a Woodcutting level of 15. You'll advance to higher Woodcutting levels by chopping down normal trees. At higher Woodcutting levels you will find even more trees you can cut."

Walking away closes it ([290]). The text is ours already. Any tree after step 130 answers "Perhaps you've done enough woodcutting now." ([314], [398], [419], [903], [1099]); ours does too.

## 2. The church organ

[1124]..[1136], `churchorgan` (416) at 3127,3105,1. The response comes at L+1:
- the face to the organ's 2x4 footprint;
- the piece's musicmulti bit set **silently** (no "You have unlocked" line): organ_2 = musicmulti_26 bit 24 → midi 40; organ_1 = bit 23 → midi 39;
- `midi_song` with the 0/60/60/0 crossfade;
- no animation and no message.

Three clicks played pieces 2, 1, 2. That fits the reference's `random(2)`.

**Implemented:** `general_use/scripts/organs.rs2` (`~church_organ` through the music dbrow: `music_organ_1`/`_2`) and `~music_unlock_silent`. It used to print the invented line "The organ plays a haunting melody."

## 3. Doors, gates, ladders and stairs

**Tutorial doors and gates** (newbie_door1..8, the fence gate, the mine gate, the rat-pen gate). At A+1, where A is the walk's end, one tick carries:
- the step change and the varp 406 milestone;
- a one-tile **walk** through the doorway (onto the door tile, or off it), then `facing reset`;
- the door's own slot changed to **`inviswall`** (38848, same shape and rotation), and an **inactive** leaf added beside it, turned a quarter;
- the open sound: `door_open` 62, `gate_open` 67 (fence), `iron_door_open` 71 (metal);
- the next step's overlay and arrow.

The closed door is restored **2 ticks later** (8 of 9; newbie_door1 after 1).

| door | swing tick | leaves (closed → inactive open) |
|---|---|---|
| newbie_door1/2/3/4/6/7/8 | [190] [331] [389] [429] [1027] [1083] [1173] | `inactivepoordoor` 1539 on `~door_open`'s tile |
| fence gate (3089,3092)+(3089,3091) | [283] | `inactivefencegate_l` 1566 at (3090,3092) rot 1, `inactivefencegate_r` 1565 at (3091,3092) rot 1 |
| mine gate (3094,9502)+(3094,9503) | [693] | `inacmetalgateopenl` 1573 at (3095,9502) rot 3 and (3095,9503) rot 1 |
| rat pen (3111,9519)+(3111,9518) | [760], [797] | `inacmetalgateopenr` 1574 (3110,9519) rot 1, `inacmetalgateopenl` (3110,9518) rot 3 |

**Implemented:** `~tut_door_swing` (inviswall + `inactivepoordoor` + sound) and `~tut_gate_swing` (the recorded leaves per gate); `^tut_door_open_ticks` 3 → 2. The refusals:
- "You need to open your bank first." at L+1 ([917]);
- "I don't think you're ready to go down there yet." (Quest Guide page, neutral; [433]).

**Ladders** (newbie and generic ladders, [536], [892], [927], [940], [947], [956], [965], [971], [1199], [1207]; also rsprox.net 1102):
- L+1: face.
- L+2: seq 828 `human_reachforladder` (up) or 827 `human_pickupfloor` (down), and `busy`=1.
- L+3: the teleport (player's tile, ±1 plane, or ±6400 z for the mine), and `busy`=0.
- The tutorial step changes **on the teleport tick** ([540], [895]).

**Implemented (engine-wide content):** `~climb_ladder_anim` is now `p_arrivedelay; anim; p_delay(0)`. It was `anim; p_delay(1)`, which put the anim on the op tick; the old reason for `p_delay(1)` went away when input moved inside the tick (2026-10-19). The tutorial ladders set the step after the climb. The 101 hand-ported ladders elsewhere that copied `p_delay(1)` are not changed.

**Spiral stairs:** L+1 face (`sizex/sizez` 2x2), then the teleport at L+2, with no anim. From a standing start ([496]) the teleport comes on the click tick. **Implemented:** `~climb` starts with `p_arrivedelay`.

Landings are per staircase. These went in as `maplink_landings.dbrow` rows:
- Quest Guide's house: up 3083,3123,0 → 3084,3124,1; down 3084,3124,1 → 3083,3123,0.
- Chapel: down 3117,3107,1 → 3116,3106,0. Its way up lands on the tile above, which the default already does.

Only the recorded standing tiles are rows.

## 4. The Combat Instructor

The pages and text match. **Implemented:**
- 43 chathead moods across the tutorial taken from the recorded anims (the Vannaka pages: player happy, quiz on the menu lines).
- The step moves on **as a page opens**, not after the conversation:
  - 370→390 on "I am Vannaka..." ([717]); the box and the flashing worn tab still wait for the end ([721]).
  - 470→480 on "Next up we have ranged combat..." ([807]), before the bow ([810]).
- A re-talk at 390 re-flashes the worn tab.

The menu at [845]: "Tell me about combat stats.|...melee...|...ranged...|...the Wilderness.|Nope, I'm ready to move on!"; every branch matched.

Not implemented:
- 410→420 also happens on the "butter knife" page ([734]); ours sets it after the gift.
- "Sit back and watch" (460, [765]) and 490 ([817]) need an attack-start hook on the rats.

## 5. Mining and smithing, and jumping ahead

Refusals: all text matched ours.

| tick | action | response |
|---|---|---|
| [557] | tin before talking | "You are not ready to mine yet. Please follow the tutorial and you'll be mining in no time." |
| [562], [570] | furnace | "This is a furnace for smelting metal. You'll learn how to use it soon." |
| [574] | anvil | "This is an anvil used for smithing. You'll learn how to use it soon." |
| [632] | anvil with a bar but no hammer | "You need a hammer to work the metal with. Talk to the mining instructor to get one." (+ arrow back to Dezzick) |
| [655], [660], [668] | axe, kiteshield, sword | "You cannot make this on Tutorial Island." (menu closed) |

**Mining** ([588..597], [598..609]):
- L+1: face.
- L+2: seq 625 (delay 0), the "attempting" box, and `busy`.
- L+7: the step, seq -1, `newbierocks1` for **2 ticks**, the ore, `mine_ore` synth 3600 **once**, and an **objectbox** with the ore.

**Implemented:** `tut_mining.rs2`. Ours swung with delay 10, played `mine_quick` twice, landed at L+8, held the rock 20 ticks and used a mesbox.

**Smelting** ([613..622]):
- L+2: `human_furnace` 899, `furnace` 2725, and **overlay** text "You smelt the copper and tin together in the furnace.".
- L+5: the bar, the step, seq 899 again, and overlay "You retrieve a bar of bronze.".
- L+8: the next box and the arrow to Dezzick.

There are no modal boxes. **Implemented:** both lines go through `~tutorial_mesoverlay`.

**Anvil:** opening the menu is step 350 ([652]); every re-open re-sends the box. The dagger: seq 898 and the menu closed on the click; the dagger and step 360 at C+3. **Implemented:** 350, with its own box ("Use an anvil to open the smithing menu...") on the first open, then the menu's box and the anvil's arrow cleared; re-opens send the menu's box alone. The dagger is the tutorial's own make (`~tutorial_smith`): seq 898 on the click, the dagger, xp and 360 at C+3, and none of the ordinary anvil's `anvil_4` synth or "You hammer the bronze" line (neither is on the wire at [672..675]). The no-hammer refusal re-sends the bar's box and the arrow to Dezzick ([635]).

**Implemented 2026-10-26, every anvil:** the `smithing` interface (312) replaces our `skillmulti` chain. Open: `smithing_bar_type` (varbit 3216), `makexcrafting` (varp 2224) = bars held capped at 28, clientscript 2524, the modal. A cell's IF_BUTTON: the lasttype varbits, seq 898, the modal closed, `smithing_close` (6123); products at C+3 then every 5 ticks (index 3449 [7275..7288]), spam-typed "You hammer..." lines, no synth. The quantity buttons set 2224 (index 800 [12729]). A cell finds its row through the new `smithing:component` column. Unverified: which bar "Smith" picks when several kinds are held (lowest tier here), and the X button's prompt.

## Dialogue findings

- **Moods:** the 43 tutorial pages above. The recorded anim gives the mood: `chat<mood><rows>`.
- **Chat-history echo, not implemented (engine).** Jagex echoes every page to the chat history:
  - each dialogue page as `message_game` type 114 `"Name|text"`;
  - each mesbox as type 115;
  - both cut at about 200 characters.

  We send neither, game-wide. It needs a typed-message command (`ToriRSServer_SendMessageTyped` exists).

## Not implemented, and why

- **Inventory and interface highlights:** rev-241 clientscripts that are not in our cache (above).
- **`busy` varbit 12393:** Jagex sets it while the player is held. Delays: 1 on the delayed tick, 0 on resume. Dialogues: 1 a tick after the page opens, 0 on close. Our engine never sets it.
- **Steps 183, 270, 460, 490, 580:** in-script transitions. 460 and 490 need an attack-start hook.
- **Wind Strike ([1240..1243], implemented 2026-10-26):** on the cast's tick 650 -> 660 ("Magic casting: You cast a spell! Sadly it didn't have any effect this time...", arrow still on the chicken) -> 670 ("To the mainland!", arrow on Terrova, overlay hint 20); 671 with the quest's line on C+1 (`[queue,tutorial_quest_complete]`). The cast resolves on the chicken BEFORE the tutorial is told: the 670 hint's `npc_find` repoints the active npc, and run first it put the chicken's hit on Terrova. Open: our scroll and jingle land at C+2, not C+1 -- `~quest_scroll_paint` opens the scroll through a queue, and a queue raised from a queue lands a tick later.
- **Terrova's goodbye ([1253..1335], implemented 2026-10-26):** Yes./No.; then "Do you want to go to the mainland? Or would you like to talk to the Ironman tutor first..." with three options; "Yes, I'd like to go to the mainland" -> Adventurer Jon's two pages, the `ap_guide_icon_dummy` objbox, overlay hint 23 with "Excellent...", and on its continue 680 with two "To the mainland!" boxes. "I'll talk to the Ironman tutor." was never chosen: its player line is unverified.
- **Display name (step 0 → 1)**, the "Setting your name" box, and the familiarity interface: our flow starts at the design panel.
- **Leaving ([1336..1364]):** Jagex's Home Teleport plays the full channel, sets 1000 and lands on **3232,3232** (by Adventurer Jon, `ap_guide_parent` at 3232,3235 in m50_50.spawn), with "Welcome to Old School RuneScape." (type 108), six music unlocks and Jon's "Hey <name>! Come over here." ([1364]). Ours lands at the castle with a Lumbridge Guide objbox. Jon has no scripts here.
