# Animal Magnetism from RSProx 4511-4514 (rev 242, 2026-11-10)

Sessions (recorder LoveTori, world 496):

| Session | Index title | What it actually holds |
|---|---|---|
| 4511 | "Quest Start Refusal" | The refusal (short of requirements); Ava's Trade and Devices before the quest |
| 4512 | "Animal Magnetism and Dearyk up to undead chickens" | Woodcutting only: oaks and willows, nests, the Strange Plant random event. No quest content. |
| 4513 | "up to iron bar" | Veronica's lamp; stages 0 to 150 with the chicken scene; Droalak (Making History, post-quest); the Ectofuntus; Giles (Certer) |
| 4514 | "finish" | Stages 150 to 240: the trees, Turael, the notes, the container, completion |

"Dearyk" in the title is Droalak (Making History), who appears in 4513 at [1933].

The previous port (OSRS-Content 4e3f67ed6d) was built from the wiki transcript. It had no moods. Several stages were written on the wrong talk. The chicken scene was plain pages. The journal was invented.

## Stages (varbit 3185 `anma_main`)

Every value matches our constants.
Each write now goes out on the recorded click: on the click of the page before it, unless noted.

| Value | Recorded write | Ours, before → now |
|---|---|---|
| 10 | Click of "I'll use one for my bed..." [233], with "You've started a new quest" | After the last page → recorded |
| 20 | Malcolm's last page click [460] | Before the last page → after |
| 30 | Alice [514] | Before the last page → after |
| 40, 50, 60, 70 | [580], [628], [684], [732] | Before the last page → after |
| 73, 76 | The crone [964], [1003] | Before the last pages → after |
| 80 | Malcolm, "Lucky we had such a brilliant idea." [1387] | Before "Ta, mate." → after |
| 90 | Click of "Stand back..." [1445] | Same |
| 100 | Inside the scene, the grab [1631] | Before the player's last page → [1631] |
| 110 | Ava holds the **first** chicken [2572] | Never written (a bit in varp 9594); a sale wrote 110 → written on the first hand-in |
| 120 | Ava, "Go and talk to the Witch next door." [2764] | Before the chain → after |
| 130 | Witch, "I'll be back." [2911] | Before "Just bring me..." → after |
| 140 | Witch's last page, with the bars swapped [3114] | On the first page → after the last |
| 150 | Ava, a tick after the last click, with the crafting line and the magnet removed [3890] | On the first page → recorded |
| 160 | The tree op, on the op tick [235] | Same |
| 170 | **Turael**, "If you can give me a mithril axe..." [475] | Ava's bounce talk wrote it → Turael |
| 180 | Turael, "Here's a new axe..." [1002] | Same |
| 190 | Ava receives the **twigs** [1590] | Written on cutting → on the hand-in |
| 200 | Ava gives the **notes** [1622] | On the hand-in → on the notes |
| 210 | The puzzle solved [1661] | Same |
| 220 | Ava, the pattern [1774] | Mid-chain → after |
| 230 | The container made [1845] | Same |
| 240 | Ava's last page click, then the scroll [1862] | Before the page → recorded |

Bits 0..2 of varp 9594 (`anma_progress`) are no longer written: the recorded stages already carry that state.

## Pages

There are 306 distinct recorded pages and menu rows.
`/private/tmp/.../pagecheck.py` checks each against every string literal in the tree, with its mood.

| | Before | Now |
|---|---|---|
| Exact text and mood | 152 | 291 (all of Animal Magnetism and Droalak) |
| Mood wrong | 86 | 0 |
| Missing or worded differently | 68 | 15 (see below) |

The 15 left are all accounted for:
- Strings under 6 characters ("Yes.", "No.", "Yes?", "Bye!"), which the checker skips. They are present.
- Pages our layout splits the same way Jagex does: "ecto-tokens", "spit-roast", and "...At least the trees are / pretty close."
- `<$name>` substitutions for Turael/Aya, and Turael's slayer-task line.
- Giles (Certer random event): built in `macro_certer/` (test/randoms/certer.lua).

### Flow changes from the recording

- **Start**: chatquiz3 greeting, then `Start the Animal Magnetism quest?` Yes./No. The "What's a nice girl..." option is gone ([160..233]).
- **Refusal**: greeting, "Well maybe I could help with that?", "Tempting, but...", then the mesbox ([131..136]). This replaces a game message.
- **Trade/Devices before 240**: "Hello there, I'm busy with my research..." (chatneu2).
- **Alice**: her menu, with "I'm here about a quest." last, at every stage from 10 to 76. From 80 she greets with thanks instead. "What are you selling?" opens the shop with no player page ([391]).
- **Malcolm at 70/73**: the three-page "I'm working on getting the amulet anyway." exchange.
- **Malcolm at 76**: the full exchange plays even without the amulet. Choosing "Okay" without it gets "Ya din't seem to 'ave it." ([1185]).
- **Crone**: with Ghosts Ahoy complete, a Necrovarus/farmers menu. The Necrovarus row plays "Good news - the enchantment worked!" From 80, the farmers row plays the thank-you ([1818..1832]).
- **Ava**: chickens are handed in one at a time. Every recorded use-item reply is added (chicken, selected iron, twigs, notes, buttons/leather).
- **Ava at 210** without the translation: the garbled notes again, to be retranslated ([1679..1681]).
- **Witch after the magnet**: "Hello my ickle...oh, it's you." ([842..850]).
- **Turael**: the five-row menu ("Let's talk about the difficulty of my assignments." included), with the quest row's three labels. Added the replacement-axe talk. Added the 170 "a holy symbol" and "holy symbol" refusals. His task line is now "You're still hunting goblins, you have 27 to go. Come back when you've finished your task." (chatcon2, all masters).

## Interactions

| Interaction | Recorded | Ours now |
|---|---|---|
| Hammer the bar (either order) | Every try: seq and synth `anma_hammer_iron`, a game message on the click. Out of the mine: "You aren't in the right area for this to work." (32 tries) | Same, including "You think that facing North might work better." when the player does not face due north (`p_orientation` ! 1024). The mine box `^anma_rimm_mine_sw..ne` is fit to the 31 recorded tries; in the mine, 2 of 2 tries after a due-north step succeeded and 0 of 16 others did |
| Tree bounce | On the op: `anma_chopping_failed_<axe>`, synth `anma_axe_bounce`, 150 → 160. The line one tick later [235..236] | Same, with a per-axe seq (mithril/adamant/rune/dragon/crystal) |
| No usable axe | "You don't have an axe which could possibly affect this wood." on the op [254] | Same |
| Twigs | `human_woodcutting_blessed_axe(_miss)` with "You cut some undead twigs." / "You almost remove a suitable twig..." on the op. Two twigs cut at 180 | Same. No stage. The miss rate (2 of 4 recorded) is UNVERIFIED. |
| Notes puzzle | "You fiddle with the notes.", `toplevel_mainmodal_open(7562576, 50)`, no setevents. Each button swaps its layer and its text layer, with `anma_interface_button`. Solved: the line, 210, the translation in the slot, `anma_puzzle_complete`; the interface stays open | Same, with Jagex's components (the old port toggled the wrong ones and used a mesbox) |
| Container | 220 → 230, `anma_craft_container`, no line [1845] | Same. Removed our invented line. |
| Completion | Page → click: 240, device in the container's slot, scroll lines and model `anma_50_reward` | Same. Scroll lines: 2,500 Woodcutting / 1,000 Crafting / 1,000 Fletching / 1,000 Slayer / An Ava's Device. |
| Crone-made amulet, Wear | One game message with Jagex's `<br>` | Same |

## The chicken scene (`anma_chicken_scene.rs2`)

Drafted with `rsprox_cutscene.py` (origin 3584,3520), then cut into ten click segments.
It is queued from Malcolm's talk: a scene run from the npc op itself was dropped once the player was moved into the copy (the first test run).
The frame uses `general/scripts/cutscene_frame.rs2`: fov 1, the farm square's copy, the player at (46,13), and the camera at rate 90.
The jingles are `here_chicky_chicky_animal_magnetism` and `chicken_grabbed_animal_magnetism`, with morytania on the way out.
The chatbox closes on the 7 clicks the recording closes it on.

Our tick log (test run 3) shows every recorded cast seq, delay, synth, spotanim and say, in order:
- the chase and evade anims at 31/48, 184/161 and 8/15;
- `anma_ghost_confused`;
- the grab spotanim and seq at 9, with `anma_chicken_catch` at 200;
- the cowkiller's slash and pickup;
- the cow's death;
- the chicken's "Woo woo!".

From `npc_block_inventory.py` after the fix:
- The cow's `[hit]` (17, 8) and the chicken's block are added.
- The cow's `enabled_ops 0b00000` is `npc_setops(0)`.

Open:
- The [1448] exactmove is not sent.
- The [1589] chicken walk and [1617] death go out with the page they fall under (the death on a 30-cycle delay).
- The headbars use the cast's own values.

## Journal (`anma_journal.rs2`)

Recorded at 22 stages (41 opens). The rows are generated offline and match all 22 row for row (`gen_journal.py`).
The format is the older one: no blank rows, every row opens its colour, finished steps are struck.
- Stage 0 opens the overview, with the recorded start text added to `quest_overview_start.rs2` (4513 [218]).
- UNVERIFIED: 90's, 120's and 150's current steps (their struck text in colour), and 240.

## Action inventory (`rsprox_actions.py`), a verdict per row

| Row | Verdict |
|---|---|
| Ava op1/op3/op4, use-on | This content, rebuilt above |
| Malcolm, Alice (`farming_shopkeeper_4`) op1, crone op1 | This content |
| Alice op3 (Trade) | Generic farming shop, bound in `shop/alices_farm` |
| `anma_witch_multi` op1, use-on | This content |
| `nasty_tree` op1 | This content. The trees' attacks hit 0 ×10 on the recorder (combat 51); the test character gets hitpoints/defence 40 |
| Turael op1/op3/op4/op5, use-on | This content (op1). op3/op5 are the generic slayer verbs; the op3 task line is fixed. The use-on line "You're not sure they can help you with this." is **Open** |
| `slayer_master_1` op4 | Not answered by a page; no verdict beyond "generic" |
| `hauntedbookcase*`, `haunteddoor*`, `draynor_panelled_door`, `hauntedleverup`, `spookywardrobe` | Draynor Manor doors (quest_haunted / doors), exercised by the test's routes |
| Veronica op1, `ernest_chicken_lamp` op | **Added**: Veronica hands over Ernest's antique lamp once, and the lamp opens xpreward for 500 xp (`haunted_lamp.rs2`; varbit 16778) |
| `ahoy_*`: barrier, trapdoor, cavern stairs, grinder, Ectofuntus, disciple, shopkeeper, crone, villager, Necrovarus | Ghosts Ahoy / Ectofuntus. The grinder and disciple lines match (templated). Necrovarus and the villager are UNVERIFIED (no page in the sweep). |
| `ectophial` op | **Rebuilt** from 6 recorded uses: pour (seq 878, spotanim 1273, area `liquid`), O+3 teleport (3659..3661, 3522) with its line, O+4 kneel facing the Ectofuntus with `fill_ectoplasm`, O+5 the refill. The old version was marked UNVERIFIED. |
| `makinghistory_droalak_multi` op1 | Making History post-quest. Pages already matched; moods fixed (chathap1 ×4) |
| `rimmington_wheel_barrow` op1 | **Added**: two lines on the op ([3259..3261]) |
| `generalassistant6` (Rimmington general store) | Generic shop |
| `macro_giles`, `macro_certer` | Built: `macro_certer/` from 4513 [3002..3006], 4498, 4437, 1102. The reward table and the wrong answer come from the wiki (UNVERIFIED) |
| `macro_triffidseed` (4512 [511]) | Built: `macro_strange_plant/` from 4512 [500..631] and 27 recorded lives. The owner's pick and Dismiss come from the wiki (UNVERIFIED) |
| `ii_impling_type_4` op1 (4514 [67]) | Generic hunter (impling.rs2). Not audited. |
| Bank, GE, worn/inventory ops, stamina, glory, games necklace, Varrock/house tablets | Generic. Two fixes: "You have nothing to deposit." for both deposit buttons (4506 [23], [26]), and the stamina "about to expire" warning on the 3 → 2 interval (4513 [958]) |
| Quest rating after the scroll (4514 [1870..1872]) | **Open**: `quest_rating` (983) is never opened. Recorded in 9 sessions (4469..4514). Adding it changes every quest's post-scroll flow. |
| oak/willow trees (4506, 4510, 4512) | Woodcutting. Fixed "Your inventory is too full to hold any more oak logs." (the log's name). **Added the bird-nest drop**: `~nest_roll` was never called. Now a nest per log at 1/256, beside the player, private for 200 ticks, with the red line, `woodcutting_birdsnest` and the local notification (4512 [2932]). **Open**: the Forestry group-gathering buff (varp 4007 = 96 / -1, varbit 15347 players, 15348 bonus) is not sent. |

## Verification

- `sscompile`: whole tree, 0 errors, no ambiguity warnings in the touched files.
- `run.py animalmagnetism`: PASS, every row from start to completion, including the chicken scene's nine pages. Published to selftest/quests/quest_animalmagnetism/play.
- Not run: no test exercises the ectophial, the lamp, the wheelbarrow, the nest drop or the bank/stamina lines.

## Open

1. The quest-rating prompt after every scroll (generic).
2. The Forestry group-gathering buff while woodcutting.
3. Turael's use-item line, and his answer to "Let's talk about the difficulty of my assignments." (the recorder clicked away).
4. Chicken scene: the exactmove, two mid-page events a tick early, and the headbar fills.
5. Journal stages 90, 120, 150 and 240, and the stage-100 pages for Ava without a chicken (wiki text).
