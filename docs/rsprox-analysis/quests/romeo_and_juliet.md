# Romeo & Juliet, from RSProx

Written 2026-10-24. The quest was first ported from LostCity's 2004 scripts, and every page
of it was the 2004 text. OSRS plays the 2007 rewrite ("Varrock's New Look", 29 May 2007): none
of the 189 recorded pages matched ours, and both of its cutscenes, the quest's own berry bush,
Juliet's absence from the balcony and the Apothecary's brew were missing. The quest passed its
quest gate the whole time, because the gate checks stages, not content. It is now rebuilt from
the recordings:

| session | rev | world | what it shows |
|---|---|---|---|
| index 4361 (owner, 2026-10-24) | 241.1 | w631, normal | the whole quest, from Romeo's approach [168] to the scroll [918] |
| index 1647 (rsprox.net 1663) | 226.1 | w367 | Romeo after the quest [44..48] |
| index 3196 (rsprox.net 3213) | 232.1 | w367 | Draul [1060], Phillipa [1309] and Juliet [1366] after the quest |
| index 3203 (rsprox.net 3220) | 232.1 | w367 | Father Lawrence after the potion [3954..3956] |
| 192 sessions holding a cast npc, the berries or a bush | 224..241 | | npc movement and spawns, the congregation's zeds |

Index 4361 was uploaded while the index worker was down: it sat `queued`, invisible to every
search, while `search varp 144` said no session had ever finished the quest. It is the only
playthrough on the index. Every rule below that rests on that one playthrough says so. Branches
it does not take use the OSRS wiki's text (`Transcript:Romeo & Juliet`, the npcs' own transcripts,
`Transcript:Cadava potion`), with `^chat_*` base moods marked unverified in the scripts:
- Romeo's five other openers, his "No." and "Where can I find Juliet?" branches, and his talks at
  stages 10, 30, 40 and 50;
- Juliet at stages 0, 20, 30, 40, and at 50 without the potion;
- Draul and Phillipa at every stage except 10 and 100;
- Father Lawrence before the quest and at stages 40 and 50;
- the Apothecary's berry questions, his other states and his gossip;
- the potion's Look-at and Drink.

Files: `areas/varrock/scripts/{romeo,juliet,phillipa,draul_leptoc,father_lawrence,apothecary,
cadava_bush,romeo_juliet_pews}.rs2`, `quests/quest_romeojuliet/scripts/{quest_romeojuliet,
romeojuliet_items}.rs2` and its `configs/`; `test/quests/romeojuliet.lua`.

## The stage ladder (`%varp144_rjquest`)

| value | 4361 tick | written on |
|---|---|---|
| 10 | [315] | the "...kiss her a give." page, with "You've started a new quest: @q_mes@Romeo & Juliet</col>" |
| 20 | [495] | the "Juliet gives you a message." objbox, the letter added on the same tick |
| 30 | [552] | the "Father Lawrence?" page; the letter leaves the pack on this tick, not at the hand-over box ([509]: a talk broken off after it starts again from the box, [518]/[524]/[530]) |
| 40 | [637] | the "Then Romeo can collect her from the crypt!" page |
| 50 | [751] | "I have all that, but I need some Cadava berries.", with `%varb6530_cadavabush` set back to 0 |
| 60 | [831] | Phillipa's last page in the potion scene, with `%varb1838_romjul_juliet_visible` 0 -> 1 and the potion removed |
| 100 | [918] | E+6 of the crypt scene: varbit 1838 1 -> 2, quest points 10 -> 15, the message, song 147 + jingle 154, the scroll (icon `cadava`, "5 Quest Points") |

## What changed, and the evidence

- **Every page.** Each recorded page is copied with its recorded chathead seq (`chatbored3`,
  `chathap4`, ...), taken from the page text rather than the echo (the echo stops at 200
  characters). The recorder's name inside Juliet's letter is `<displayname>`.
- **Romeo's approach** ([152..168]): he walks up to an idle player who has not started the quest
  and opens his opener, with "Romeo approaches you...". Ours: an `[ai_timer,romeo]` every 4 ticks
  hunts a player beside him (`huntall`, 1 tile) and queues the opener. There is one recorded
  approach, so the cadence and range are unverified.
- **Hearts**: `trollromance_heart` (574) on four pages: "Oh yes, Juliet!" [308], "Oh, there is so
  much to tell..." [324], Juliet's "Oh how my heart soars..." [490], "Oh, a message! A message!"
  [510, 526, 532].
- **The sermon** ([602..639]):
  - The camera moves to the pews for the Congregation page: `cam_moveto 0_50_54_54_30` h325 and
    `cam_lookat 0_50_54_56_20` h25, both at rate 100 [607].
  - The Congregation page has head `romeo_juliet_pew_sleeping_man`, seq `chat_sleep` (13977) and
    no continue. It stands 5 ticks; then the player's answer opens by itself and goes overhead in
    three `say`s two ticks apart ([612], [614], [616]).
  - `cam_reset` comes with the next page [621].
- **The cadava bush** ([385..391]):
  - The south-western bush is `fai_varrock_cadavabush_tailored` (33183), the quest's own: its
    form is `%varb6530_cadavabush`, one berry per player.
  - The pick: on the op tick, the face and `picking_low`; on the next tick, the varbit 0 -> 1, the
    berry and synth `pick` (2581) at delay 10. No chat line.
  - The wiki's "always empty" is what the bush says once the player's berries are gone. A
    second berry from it is unverified.
  - The three ordinary bushes keep the two-berry/one/none states and the 200-tick regrow.
    "You pick some cadava berries from the bush." was invented and is gone.
- **The Apothecary**:
  - The opening is "I am the Apothecary. I brew potions. / Do you need anything specific?", then
    "Can you make potions for me? | Talk about something else." [740..746]. The second menu holds
    "Talk about Romeo & Juliet." at stages 40..60 [746] and the gossip row before and after the
    quest.
  - The brew [753..758]: the berries objbox; a blank messagebox with no continue [754];
    `smokepuff_large` h64, `human_apothecary_create_potion` and area sound `smokepuff` (1930,
    range 10) on him [755]; "Phew! Here is what you need." [757]; the swap with the potion box
    [758].
  - He walks his shop: 39 sessions, 23 tiles, x 3192..3197, z 3402..3406. Ours had him stationary
    (`npc_movement/a/apothecary.move` is now `authored`, and his generated block is wander/5,
    indoors).
- **The potion scene** ([806..837]):
  - It runs in a copy of the house square (m49_53, level 1).
  - [806]: the lock, the hud and the black, on the click of Juliet's last page.
  - [809], T+3: into the copy, the cast and the hud again, the black lifted, song 147 and jingle
    120.
  - [811]: the camera.
  - [816]: Juliet drinks (spot 1618, seq 3284, synth 2401). [820]: she falls (`human_death`,
    synth 505).
  - [827]: Draul comes through the balcony door, the camera panning at rate 5. [828]: he steps in.
  - [831]: the stage. E+3 [834]: out and the unlock. E+4 [835]: the black lifted. E+6 [837]: the
    overlay shut.
  - Juliet's balcony npc `juliet_multi_visible` is a multinpc on varbit 1838: 1 hides her until
    the end of the quest. That varp (673) was generated temp and untransmitted; it is now perm and
    transmitted.
- **The crypt scene** ([853..920]):
  - It runs in a copy of the Varrock crypt (m36_72). The tomb Juliet lies in is the map's
    `romeo_juliet_crypt_tomb_no_headstone` at 2322,4642.
  - [853]: the black and `trek_walkaway`, then T+3: the lock, the copy, Romeo, song 147 and
    jingle 168 under his first page. The black lifts after the player's "Oh, be quiet...".
  - The camera: eight re-aims, each on its recorded tick and with its recorded heights and rates.
  - Romeo slides 3 tiles and creeps to the tomb one step at a time, on the recorded ticks.
    Phillipa is added [894] and walks in behind him, two of her steps on page clicks.
  - E+3: out. E+4: the unlock and the black lifted. E+6: the completion. E+8: the overlay shut.
- **After the quest** (recorded):
  - Romeo: "I heard Juliet had died. Terrible business." / "...Thanks for your help." (1647).
  - Juliet's four angry lines (3196).
  - Phillipa: "Hello, I seem to be getting on really well with that dashing lovely dovely,
    Romeo..." (3196). Ours had an invented line.
  - Draul: "I suppose you're quite pleased with yourself now..." (3196). Ours had his stage-60
    line.
  - Father Lawrence's whiskey lines (3203).
- **Juliet stands still**: 0 steps in the two sessions that show her ([469..502], [789..806] in
  4361). Ours wandered; her generated block is now `defaultmode=none`.
- **The congregation** (`romeo_juliet_pew_sleeping_*`): `troll_zeds` (277) at a random 10..19
  ticks each. That is 1,350 recorded gaps across 37 and 34 sessions, flat over the range (101..141
  per value). Ours had none.

## Wire audit (index 4361 vs our tick log)

Our tick log is `build/quest_gate/romeojuliet/ticklog.tsv` from `run.py romeojuliet` (the test
calls `t.ticklog.start()`). Sections are aligned on their stage writes; the driver clicks pages
at its own pace, so absolute ticks inside a dialogue drift.

| section | recorded | ours | verdict |
|---|---|---|---|
| quest start | stage + message on the 6th page after the opener; heart on "Oh yes, Juliet!" | the same; heart on that page | matched |
| bush | `picking_low` O, varbit + berry + `pick` d10 O+1 | `picking_low` [213], `pick` d10 + varbit [214] | matched |
| Juliet 1 | heart on her first page; stage + letter with the objbox | heart [110]; stage [116] | matched |
| Romeo 2 | heart on "Oh, a message!"; letter gone with the stage on "Father Lawrence?" | heart [138]; stage [160] | matched |
| sermon | camera h325/h25 rate 100 then reset; zeds | `cutscene:` row PASS (coords and heights); zeds | matched |
| Apothecary | spot 188 h64 + seq 3283 + area 1930 r10, 4 ticks after the stage | the same, 5 ticks after (driver pace) | matched |
| potion scene | song 147 + jingle 120 at T+3; drink, fall, door; no region song | the same events. The first run also sent region song 175 on the copy's tick | **fixed**: `midi_hold` from entering the copy to leaving it; the rerun sends 147 + 120 [273] and no song until the player is out [300] |
| crypt scene | trek_walkaway, song 147 + jingle 168, trek_walktoward, 8 camera aims, completion E+6 | the same; `cutscene:` row PASS | matched |
| after the crypt | no song at E+3; 147 + jingle 154 at E+6; 125 after the scroll | region 125 at E+3 (the crossing), 147 + jingle 154 at E+6 | open: the region song comes back on the crossing, two ticks early |
| page moods | 189 pages, each with its chathead seq | `TORIRSSERVER_TRACE_OUT=2`: 126 pages, 125 of them recorded pages with the recorded seq, 0 with another; the 126th is Romeo's opener, rolled differently | matched |
| npc movement | see "What changed" | Apothecary 15 tiles in the recorded box; Juliet and the pews 0 steps; Romeo, Phillipa, Lawrence inside their boxes; Draul once at x 3157 (recorded min 3158) | matched, one tile off for Draul |

The test (`test/quests/romeojuliet.lua`) passes 105 of 105 rows, `gate.py romeojuliet` is green,
and `::romeojulietrun` reaches its OK line. All of it ran on private packs (2026-10-24): another
session's Black Knights' Fortress edit and two npc movement blocks, `fortwitch` and `greldo`
(`moverestrict` in both generated files), stopped the shared packs building at the time.

## Open

- "Can you make potions for me?" opens the Apothecary's Potions interface in OSRS. This tree
  has no such interface, so the row keeps the old strength-potion talk.
- The cadava potion's ops (Look-at = iop1, Drink = iop2) follow the wiki's option order;
  no recording opens either. The drink's "screen starts swaying" is not built.
- Romeo's approach cadence (4 ticks, 1 tile) rests on one recorded approach.
- The completion's `disease` varp blip at [918] (-50 then 0) is not sent by ours; it is
  unexplained and not quest content.

## Tools

- `tools/rsprox/content_asset_inventory.py --names 'romeo|juliet|romjul|cadava|lawrence|leptoc|phillipa|apothecary'`
  is new. Before this rebuild it listed both jingles, the drink and brew seqs, the drink
  spotanim, varbits 1838 and 6530 as UNUSED, and the bush and Juliet as carriers whose switch is
  NEVER WRITTEN. Today it lists 7 UNUSED rows, none of them quest content: clue-helper, League and
  farming rows, and an obj named `romeo_juliet_pew`.
- `rsprox_pages.py` and `rsprox_tells.py --timeline` on index 4361 for the pages and events.
