# Zulrah and Zul-Andra, from RSProx

The fight (rotations, attacks, damage, snakelings, clouds, the boss's npc info) is
[FIGHT.md](FIGHT.md). This file covers everything around it: Zul-Andra, the boat,
the arrival, the exit, the kill's lines and loot, death storage, the diary, the
scroll, the book, the fairy ring and the master scroll book.

## Sessions

| index | rsprox.net | rev | activity | what it holds |
|---|---|---|---|---|
| 3259 | 3276 | 234 | Grid Master | first visit: refusal, High Priestess, ritual, first row with camera [8240..8325]; 2nd row (Quick-Board) + kill 1 [9076..9407] |
| 819 | 820 | 234 | Grid Master | 47 Quick-Boards and kills, KC 2..48, BJS ring [10313], stepping stones, exits, Talk-to [10630], [12192] |
| 3324 | 3342 | 235 | Grid Master | 4 kills, KC 49..52, the 50-kill fee notice [3877] |
| 4574 | - | 242 | normal | master scroll book: open [430], three scrolls stored [435], [439], [444] |
| 906, 907, 941, 945 | | 235 | normal | Zul-Andra npcs passing by (movement) |

No recording dies at Zulrah, reads a Zul-andra teleport scroll, uses a world fairy
ring, the telescope, the crate or the deposit chest, or teleports from the book.

## The stage word

`%varb4391_snakeboss_info` (cache varbit on `varp1117_aide2`, perm, sent):

| value | set on | recorded |
|---|---|---|
| 0 -> 1 | the High Priestess's "Willing volunteers..." page | 3259 [8271] |
| 1 -> 2 | "... and I accept." | 3259 [8282] |
| 2 -> 3 | the first box of the first row out | 3259 [8311] |

The boat (multiloc) and Zul-Gwenwynig (multinpc, shell `snakeboss_priest` 2124)
show their 2-op forms (Quick-Board; Collect) only at 3. The old
`%varp7390_zulrah_sacrifice_offered` is retired.

## Pages

| speaker | source | verdict |
|---|---|---|
| Zul-Gwenwynig at the boat before volunteering (2 pages, menu "But I really want to ride the boat!\|I'm off.") | [R] 3259 [8246..8252] | text + moods copied |
| High Priestess: greeting, Tell (7 pages), How (2), Take me (2), Advice (1), menus in recorded order | [R] 3259 [8263..8287] | text + moods copied; stage writes on their pages |
| High Priestess: Who are you all (3), stage 1/2/3 greetings, "I kinda left"/"I killed Zulrah" | [W] | moods unverified |
| Zul-Gwenwynig: ritual (player line + 5 pages + menu + 2 pages) | [R] 3259 [8299..8309] | copied; shared by boat and Talk-to at stage 2 |
| Zul-Gwenwynig stage 3: "You left the shrine..." | [R] 819 [10630] chatcon1 | copied; its menu [W] |
| Zul-Gwenwynig stage 0-1, Remind, Collect lines | [W] | moods unverified |
| Sacrifice, Zul-Cheray, Zul-Gutusolly, Zul-Urgish, Zul-Onan, Zul-Aniel/Areth, Broken Handz | [W] transcripts | all invented 2004-style lines replaced; moods unverified |
| "The priestess rows you to Zulrah's shrine, then hurriedly paddles away." | [R] 53/53 | the box, both ticks |

## The row out (53 boardings)

| tick | recorded | ours (`~zulrah_row`) |
|---|---|---|
| O | echo + fade out (948 0,255,0,0,50) + no-continue box; stage 2->3 on a first row | `~zulrah_row_box_still` |
| O+3 | teleport to (2268,3068) = copy (28,28) | `p_delay(2)`; `p_telejump` |
| O+4 | echo, `cam_forceangle(280,1780)`, fade in, the box with a continue; first visit: `cam_moveto` (2258,3065) h750, `cam_lookat` (2268,3075) h200, rate 232/100 | `~zulrah_row_box_arrive` |
| R | continued | `p_pausebutton` |
| R+1 | Zulrah added + `snakeboss_spawn`; Coil unlocked (musicmulti_14 bit 0) and played 0/60/60/0; first visit: the camera again | `~zulrah_rise` |
| max(R, O+6) | fade overlay shut | both branches |
| rise+5 | first visit: `cam_reset`, `cam_forceangle` | `[softtimer,zulrah_arrive_cam_reset]` |

Board = with camera (3259 [8315] first visit), Quick-Board = without (all others,
wiki: "Quick-board ... does not lock players' camera").

## The kill (52 kills)

| | recorded | ours |
|---|---|---|
| death seq D | 52/52 | the fight's |
| D+6 KC line | 52/52 | `~zulrah_kill_lines` (varp1518; the shared KC proc now skips Zulrah) |
| D+6 duration | round(0.6*(D - rise - 3)) s, 52/52 | `~zulrah_fight_ticks`, PB varp9691 |
| D+6 diary | "Well done! ... hard task in the Western Provinces area" (3259 [9388]) | `~diary_task_award(western, hard, 11)` |
| D+6 drops | killer's tile, despawn 18000 | `~zulrah_drop` |
| D+6 map anim | `failedspell_impact` h124 on the tile | `spotanim_map` |
| D+6 exit scroll | `snakeboss_exit` shape 10 rot 0, a tile beside (52/52 adjacent) | `map_findsquare(coord, 1, 1)` |
| D+7 at KC 50 | the fee mesbox (3324 [3877]) | `[queue,zulrah_fee_notice]` |
| "received a drop" lines | the recorders' loot-notification setting | not built (tree-wide setting) |
| synth 2401 `liquid` | the recorders' Bottomless Brew sips | kit |

Drop table: the wiki's 249 rows (every recorded drop is a row at its quantity).
Fixed: uniques 1/256 per roll (was 1/128), pure essence (was rune essence),
mutagens 10/5264 each (was 5).

## Exit scroll (4 recorded)

C menu "Are you ready to leave the shrine?" Yes/No; R+1 `teleport_scroll_open`,
`telescroll_teleport`, `teleport_all` r10; R+4 in a 3x3 on 2196,3056 (4/4), seq
cleared, `cam_reset`.

## Death storage

Death on the shrine stores the items with `^retrieval_zul_gwenwynig` (the shared
retrieval service); `~zulrah_after_store` sets the 100,000 fee from the 50th kill
(not for UIM) and gives the area back; no bones in Zul-Andra. Collect (op3)
returns what fits with the wiki's lines. Boarding while she holds items is
refused. UNVERIFIED: no recording dies at Zulrah; the cache's Item Retrieval
interface is not built (same gap as ToB's chest).

## Zul-Andra npcs

World roster already right (11 rows, `snakeboss_priest` 2124 the shell). Added
`snakeboss_fishingspot_fake` 2189,3071 (7/7 adds). Wander range 1 (not 5) for
the priestess, both Tyras guards, the sacrifice, Zul-Gutusolly, Zul-Urgish (every
recorded tile within 1); the High Priestess faces east (11/11).

## Master scroll book (index 4574)

Open, store-by-use-on with the recorded lines, the interface's Activate / Set as
default / Remove, the book's Teleport (default, index hi*15+lo) and Remove
default. Every loose scroll without a Read binding now has one through the same
destination table.

## Open

- Board vs Quick-Board at stage 3: Board's camera is from the first visit only; a stage-3 Board is unrecorded.
- Item Retrieval Service interface; the telescope's spectator view; the crate's lines.
- Brimstone key and on-task Slayer XP (boss tasks not assignable tree-wide).
- Elite diary revive: no line/graphic sent (none sourced).
- Fairy rings: world rings work only with the permission varbit (no Fairytale II in tree); world-ring departure unrecorded.
- Master scroll book: quest gates other than Regicide; the Revenant cave scroll's landing; every book line except open/store.
- The shared retrieval code: `%varp7672_retrieval_service` defaults to 0, which is Zul-Gwenwynig's index, and `~retrieval_has_items` tests `inv_size` (capacity). Zulrah counts the store's items itself (`~zulrah_storage_held`); the shared procs are untouched.

## Test

`test/bosses/zulandra.lua` (scriptrun, 11 rows, all PASS 2026-11-17): the High
Priestess to "I accept" (stage 0 -> 2), the ritual at the boat, the row (stage 3),
Zulrah risen, the debug leave into the 3x3 on 2196,3056, Zul-Gwenwynig's stage-3
greeting, the book storing four Zul-andra scrolls. Its tick log against the frame:
last page continued [64], box + stage [65], arrival box continued [71], Zulrah and
Coil (script, 0/60/60/0) [72] = R+1; no music at the move (the region held with
`midi_hold` until the shrine is left), Zul-Andra's 433 back on leaving.
