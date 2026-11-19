# Nature Spirit from RSProx

Two sessions, both rev 242.1, the owner's account:

- **index 4592** "Nature Spirit Start" (2026-11-17, w407): Priest in Peril at 61,
  Drezel's offer, the start, the four-question menu, the "again" menu. Also a frog
  random event.
- **index 4602** "Nature Spirit - The Rest" (2026-11-18, w442): stage 5 to 110 in one
  session, the journal opened at 11 stages, three ghast kills, the scroll.

Our content was a LostCity (2004) port (`quests/quest_druidspirit/`). Nature Spirit
was not rewritten by Jagex, so its pages survived almost whole (the checker below
found 6 mood and 4 text defects in ~180 pages); the defects were in the shapes:
the stage's tick at the start, the gate, two inventory ops bound to the wrong
iop, every sound, the four casts, the ghasts' combat seqs and sounds, the pouch
line, the scroll's order and rows, and the journal's breaks.

Tools: `rsprox_actions.py`, `rsprox_pages.py`, `rsprox_npcinfo.py`, and three
scratch scripts (a window dumper that drops the clock varbits and wanderers, a
page checker that compares every recorded page's text, speaker and mood with our
`~chat*` calls, a journal extractor).

## 1. Actions (every row a verdict)

### index 4592

| tick | action | verdict |
|---|---|---|
| 28, 31 | quest list -> Nature Spirit overview | **this content**: overview start string added (`quest_overview_start.rs2`, [90]) |
| 68.. | fairy_chest_bank, bank tabs/items, glory | generic (bank); not diffed this pass |
| 160 | stamina potion | generic (drinking) |
| 162 | Varrock Teleport | generic (spell); not diffed |
| 220, 221 | Varrock members' gate | generic (gate) |
| 331, 338 | Paterdomus trapdoor, climb | Priest in Peril port (4459), unchanged |
| 348, 369 | mausoleum gates | Priest in Peril port |
| 376 | Drezel x4 | **this content**: the offer and start (§5) |
| 505..514 | frog crier, frogs | generic random event (`macro_frog/scripts/macro_frog.rs2`); not diffed |
| 540 | logout | engine |

### index 4602

| tick | action | verdict |
|---|---|---|
| 9, 18 | drop teak logs, wield dragon axe | recorder's kit / generic |
| 23, 202 | Varrock Teleport | generic |
| 38, 175, 229 | bank booths / banker | generic (bank) |
| 41 (x17), 45 | quest journal | **this content**: §9 |
| 128, 132 | wear ghostspeak / glory | generic equip |
| 143..171 | GE: buy a silver sickle | generic (Grand Exchange); not diffed |
| 314 | Varrock members' gate | generic |
| 365, 371 | trapdoor | Priest in Peril port |
| 376, 397 | mausoleum gates | Priest in Peril port |
| 397 | priestperil_grave_base5 | Priest in Peril port (`monuments.rs2`) |
| 408 | Drezel | **this content** ("Hello again!" + again menu) |
| 414, 1178 | the Salve passage (`pip_underground_wall_side_withportal`) | Priest in Peril port |
| 416 | burgh_library_keyhole | In Aid of the Myreque port |
| 431 | music settings | UI |
| 445 | Ulizius x3 | **this content**: pages match |
| 476, 1071 | Mort Myre gate | **this content**: rebuilt (§2) |
| 480 | cws_warning_20 warn1 | **this content**: new |
| 487 | Monkey Greegree outside Ape Atoll | generic (`mm_greegree.rs2`, line matches) |
| 498, 1181 | stamina | generic |
| 523 | drop rotten food x3 | generic drop |
| 553 (x19) | druidjump_loc | **this content**: rebuilt (§2) |
| 600, 604 | bowl, mirror take | **this content**: bowl rebuilt; mirror is the generic table take |
| 603 (x17) | grotto door | **this content** |
| 610, 611 | examine bowl, mirror | engine |
| 619, 752 | grotto (Look-at, Search) | **this content**: lines match |
| 649 (x18) | Filliman (spirit) talk | **this content** |
| 756 | read the journal | **this content**: boxes match |
| 776..1134 | bowl/mirror/journal/bloom on Filliman, on the rock, on Drezel, on each other | **this content**: defaults ("Nothing interesting happens.") match |
| 992 (x5), 1670 | bloom spell Cast | **this content**: sounds added |
| 1101 | Hiylik Myna | Paterdomus (`hiylik_myna.rs2`, from 4461) |
| 1110, 1113 | east trapdoor | Priest in Peril port |
| 1360, 2045, 2060 | pick log | **this content**: spam line, `pick` added |
| 1398 | fungus on Filliman | **this content** (nothing happens) |
| 1523, 1528 | drop used/fresh bloom spell | generic drop |
| 1553, 1554 | take them back | generic take |
| 1610..1621 | stones: Search | **this content**: boxes match |
| 1629..1674 | items on the stones | **this content**: spirit stone's wrong-item line and Filliman's line added |
| 1764..1773 | mirror/bowl/pouch on the pool | **this content**: defaults match |
| 1768 | take the grotto's pouch | **this content**: level-0 spawn added (`druidspirit.spawn`) |
| 1775 (x9) | the pool (Filliman/Nature Spirit) | **this content** |
| 1785..1937 | items on Filliman / the Nature Spirit | **this content**: defaults match |
| 1942 (x7) | Nature Spirit talk | **this content** |
| 1948 | unwield the sickle | generic |
| 1951, 1958 | drop, take the sickle | generic |
| 1994 (x3) | root wall (exit) | **this content**: shape matches |
| 2005 | quick prayers | generic |
| 2042 | blessed sickle Cast Bloom | **this content**: was unbound (ifop1, ours bound op3) |
| 2047 | pick branch | **this content** |
| 2051, 2062 | mushroom on empty pouch | **this content**: line, sound |
| 2075 | pouch on a ghast (x3) | **this content**: rebuilt (§11) |
| 2080 (x17) | attack the ghast | **this content**: combat params (§11) |
| 2183 | pouch on the Nature Spirit | default |
| 2186 | wield the blessed sickle | generic |
| 2454 | scroll close | **this content**: the farewell now waits for it (§5) |

### Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 4602 [372], [1115] | trapdoor / east trapdoor | Priest in Peril | unchanged |
| [422], [1180] | Salve passage | Priest in Peril | unchanged |
| [558]..[2413] | druidjump_loc, 15 rows | the jump | §2 |
| [1757], [2170], [2421] | grotto door -> 3442,9734,0 | the door from stage 60 | matches |
| [1997], [2216] | root wall -> 3440,3337,0 | exit | matches |
| [2447] | 3438,9737,0 -> 3438,9737,1 | the altar (§5) | rebuilt |

## 2. The moves

**Gate** (4602 [476..484] in, [1071..1078] out). In: the op on the leaf at L+1 opens
`cws_warning_20` (interface 580) as the main modal, varbit 3870 0 -> 1
(`toplevel_mainmodal_open` 2524 first); warn1 at [480]; [481] the modal shut,
stage 10, the step onto the clicked leaf's tile; [482] "You walk into the gloomy
atmosphere of Mort Myre.", both leaves inviswall + `inacmetalgateopenl` (3444,3457
rot 2) / `inacmetalgateopenr` (3443,3457 rot 0), `iron_door_open` (71), the step
in; [484] the leaves back. Out: on the op tick, "You skip gladly out of murky Mort
Myre.", the same leaves, the step, Ulizius's `[say] Oh my! You're still alive!`.
Ours was a `p_teleport` with no warning, no leaves and no sound; rebuilt
(`[label,mortmyre_gate]`). The warning's count lives in varp 1046, which the
generated block made temp/untransmitted: stated perm + transmit in the quest's
`.varp` (§4). warn2/close/toggle UNVERIFIED (as the Observatory's).

**Stepping stones** (19 jumps). Success, standing on the stone: C exactmove
(58/70) + `human_steppingstonejump` (30), C+2 `jump` (2461), C+4 15 xp; walked
to: L+1 face, L+2 the step onto the stone, L+3 the jump. Fall (3 recorded:
[575], [999], [2156]): C exactmove to 3439,3330 (58/60) with
`human_wobbleandfall_r` (south) / `_l` (north); C+1 `jump`; C+2 the swim stance
(`--bas`: ready/turn `human_swim_ready` 773, walk* `human_swim` 772, run -1), a
glide to one tile short of the bank, `watersplash` (delay 3) and `pool_plop`
(1658); C+5 the stance back, the step onto the bank (3438,3327 / 3438,3332), "You
nearly drown in the disgusting swamp.", the hit (2, 7), 2 xp. Ours had no sounds,
no stance, and the splash/line/hit on C+1; rebuilt.

## 3. Ambience

Ghasts (`ghast_invis` 945) wander and hunt the player everywhere in the swamp
(each life's walk in `rsprox_npcinfo.py`); their attacks are §11. Snails
(`mmsnailround_*`) and will-o'-the-wisps wander. No `sound_area`, no `map_anim`
not caused by the player. "The aura of Fillimans camp protects you from the
swamp." with `druidicspirit_druidsshield` (255) on the player x6 in the camp:
our `swamp_decay` timer, matches. No "The swamp decays you!" in 4602 (the
recorder was rarely outside the camp with the timer due).

## 4. Stages and vars

Jagex's stage values are exactly ours (5..110 in 5s); every write's tick:

| stage | 4602 tick | goes out with | ours |
|---|---|---|---|
| 5 | 4592 [400] | the pies' double box, after "Now, please be aware..." | **was** on the confirm: moved |
| 10 | [481] | warn1, the step onto the gate | moved into the warning (§2) |
| 15 | [636] | the click on "What did I write down now?" | matches |
| 20 | [703] | the click on "Don't be silly now!" | matches |
| 25 | [822] | the click on "It must be a sign..." | matches |
| 30 | [881] | the click on "It's all coming back to me now", the journal gone | matches |
| 35 | [943] | the papyrus box's click, the scroll added | matches |
| 40 | [1152] | C+3 of the blessing, with its page | matches |
| 45 | [1357] | the bloom's cast tick | matches |
| 50 | [1360] | the pick | matches |
| 55 | [1409] | the click on "Wonderful, the mushroom..." | matches |
| 60 | [1723] | C+4 of the ritual, with its page | **was** C+2: rebuilt |
| 65 | [1757] | the grotto door | matches |
| 70 | [1799] | C+4 of the transformation | **was** C+3: rebuilt |
| 75 | [1964] | C+2 of the sickle's blessing | **was** C+3: rebuilt |
| 80 | [2043] | the sickle bloom's C+1 | **was** C: rebuilt |
| 85 | [2045] | the pick | matches |
| 90 | [2063] | the pouch line | matches |
| 95/100/105 | [2139], [2294], [2397] | the death seq, with the kill line | matches |
| 110 | [2447] | C+9 of the altar, with the scroll | **was** on the page's click (queued): rebuilt |

| var | fields before | fields now |
|---|---|---|
| varp 307 `druidspirit` | perm, transmit (quest .varp) | unchanged |
| varp 1046 `cws_var_2` (varbit 3870 `cws_warning_20`) | temp, transmit=no (generated) | perm, transmit (quest .varp) |
| varp 6875 (server-only bits) | perm, transmit=yes | perm, **transmit=no** (the client's cache has no varp 6875); bits 0/1 the stones, 2 the gate leaf (cleared once used) |
| varbit 16741 `last_quest_id` and the started/completed lists | engine | not this content |

## 5. The start and the end

**Start** (4592 [379..452]). At Priest in Peril 61 with Nature Spirit unstarted,
Drezel goes straight to the offer, the player's "Hello again, Drezel. Is there
anything that I can help you out with around here?" (chatquiz2) first. Ours
played LostCity's "Greetings again adventurer..." idle menu: removed. The
confirm `~p_choice2_header("Yes.", 1, "No.", 2, "Start the Nature Spirit
quest?")` matches; the stage, "You've started a new quest: @q_mes@Nature
Spirit</col>" and the pies come with the double box after the warning page.
UNVERIFIED: the confirm's No.

**End** (4602 [2426..2458]). The last page's click C: his cast; C+2 three stars;
C+3 three effects (delay 126), `bloom_pears`, `fire_bolt_all` x2; C+6 his cast;
C+9 stage 110, "You see a beautifully tended small grotto area.", the
completion, the move to level 1 (same tile), the Nature Spirit added at
3441,9738,1 for 5 ticks, the scroll (jingle quest complete 3, model
`silver_sickle_blessed`, rows "2 Quest Points / 3,000 Crafting XP / 2,000
Hitpoints XP / 2,000 Defence XP"). Shutting the scroll ([2454]) brings him
back for 5 ticks with "Welcome to my Altar to Nature!...". Ours completed on the
click, played the farewell before the scroll, and the scroll said "3000 Crafting
XP|2000 Defence XP|2000 Hitpoints XP|Access to Mort Myre Swamp|Ability to fight
ghasts": all rebuilt. The farewell is `queue(druidspirit_after_scroll)` made right
after the scroll's own mount and before the xp: a queue waits behind the scroll and
the queue drains in order, so on the close the farewell runs before the level-ups.

## 6. Pages

Every page of both sessions through the checker (text with `<br>` -> " ", the
speaker, the mood from `if_setanim`) against our `~chatnpc_anim` /
`~chatplayer_anim` / boxes:

| defect | recorded | ours before | fixed |
|---|---|---|---|
| mood | "That's great! Many thanks!" chatneu1 | happy | yes |
| mood | "Who exactly is this Filliman?", "Where's Mort Myre?", "What's a ghast?" (player) chatneu1 | quiz | yes |
| mood | "Most people that come this way..." chathap3 | neutral | yes |
| mood | "Why you poor fellow..." chatsad2 | neutral | yes |
| answer | ghost menu "Ok, thanks." -> "Ok thanks." chatquiz1 [738] | "Ok, thanks." neutral | yes (all 5 ghost menus) |
| text | "...if you use that  Bloom spell I gave you, you should be able to get  something from nature. ... we may be  able to..." (Jagex's double spaces) | single | yes |
| text | "...dipping it in the holy water of the grotto." | "waterof" | yes |
| box | "Your sickle has been blessed!  <col=0000ff>~ If you lose the blessed <col=0000ff>sickle, you can bless ~ <col=0000ff>~ a new sickle by dipping it in <col=0000ff>the grotto waters. ~" (the echo is the source) | `||@blu@` forced breaks | yes |
| missing | Filliman "Aha, yes, that seems right well done!" on the scroll on the spirit stone [1678] | none | added |
| line | "You add four harvested items to your druid pouch." | "You add 4 natures harvests..." | yes |
| line type | "You pick a mushroom from the log.", "You take a cutting from the budding branch.", "An attacking Ghast just misses you." are spam | game | yes |

Menus: every recorded menu's options and order match (Hiylik Myna's is
Paterdomus's own). Not recorded, kept as LostCity's and UNVERIFIED: Filliman at
20 and 30 with the amulet, "Hello again.." onward at 25, the stage-45 talk, the
Nature Spirit's pouch-recovery line, Drezel's per-stage advice from 15 onward,
the spirit's "Hmm, that doesn't seem quite right.".

## 7. Ops

| op | cache | ours before | now |
|---|---|---|---|
| silver_sickle_blessed "Cast Bloom" | ifop1 | `[opheld3]` (dead) | `[opheld1]` |
| druid_pouch(_empty) "Fill" | ifop4 | `[opheld1]` (dead) | `[opheld4]` |
| bloom_spell "Cast", filliman_journal "Read" | ifop1 | `[opheld1]` | unchanged |
| the sickle's worn "Bloom" (param_451) | worn op 2 | none | UNVERIFIED, not bound |

## 8. Recurring frames (the four casts)

No camera, no `cutscene_status`, no fade in either session (the `cam_reset` /
`minimap_toggle` lines are logins and the Varrock teleport). The scenes are
casts on the open map, each copied from its click C:

| scene | 4602 | frame |
|---|---|---|
| Drezel's blessing | [1149..1152] | C his cast (30), `human_pray` (30), his line; C+1 his star to the player (v2 128/128 = 32/32, 30..90, 90, 0), `prayer_recharge` (30); C+2 `druidicspirit_priest_bless` (30) on the player; C+3 the stage and page |
| the ritual | [1719..1723] (fail [1482..1484]) | C his cast (30), `spirit_transform_start` (30), page shut (fail: C+2 his page); C+1 cast again; C+2 three stars from 2 tiles S/E/W of him (0/48, 30..130, 180, 0), the synth again; C+4 the stage, three `druidicspirit_effect` (128/40, 64/35, 0/30), `bloom_pears` (30), his page |
| the transformation | [1795..1799] | C+1 page shut; C+2 his cast (5), seven stars from fixed tiles (0/32, 30..76+10/tile, 180, 0), `spirit_transform` (30); C+3 three effects; C+4 the Nature Spirit + effect at 64 on him, the stage, his page |
| the sickle's blessing | [1962..1967] | C `druidicspirit_bloom_player_spotanim` (200, 120), his cast (30), four stars from the pool (0/42, 30..160/150, 180, 0), `prayer_recharge` (30), page shut; C+1 `druidicspirit_human_bloom` (30); C+2 stage 75, the sickle blessed in its slot; C+5 the box |
| the altar | [2438..2447] | §5 |

## 9. The journal

Opened at stages 5, 15, 25, 35, 40, 50, 55, 75, 90, 95, 105 (4602 [42]..[2428]),
and the overview at 0 (4592 [90]: "talking to <col=800000>Drezel<col=000080> in
the <col=800000>Mausoleum<col=000080> beneath <col=800000>Paterdomus<col=000080>.").
An old-style journal: every finished row `<str>`, Jagex's own breaks (and
oddities: "something of <str>faith'", "a ghost. " with its trailing space), no
blank rows. Ours had the text at 2004 breaks with `|`. Rebuilt as rows
(`~quest_journal_rows`), the recorded stages verbatim. UNVERIFIED (derived from
the neighbours with LostCity's current-step wording): 10, 20, 30, 45, 60, 65,
70, 80, 85, 100, 110, and 50/55 with a stone filled.

## 10. Rewards

2 QP, 3,000 Crafting, 2,000 Hitpoints, 2,000 Defence (the xp on [2447]; the level
ups after the farewell's close, [2459]). The blessed sickle and the pouch are
the quest's own features (blooming, the pouch, the ghasts) and are exercised by
the test; the Altar of Nature (prayer +2) is LostCity's `[oploc1,
druidic_spirit_grotto_naturealtar]`, UNVERIFIED.

## 11. Ghasts (regular mobs: no Opus agent, owner rule)

| | recorded (4602) | ours before | now |
|---|---|---|---|
| invisible attack | `inv_ghast_attack` + say ("OOooooohhh" x8 / "Oouuaarrr!" x3), player defend seq, `ghast_attack` synth; a miss is a spam line; a hit on food the backpack line + `food_rot` (1494) | seq, say, game line; no synth, no defend | added |
| the pouch on a ghast | C face; C+1 the line, one out of the pouch, a star from the player (v2 172/124 = 43/31, 51..90, 16, 64), `druidpouch_impact` (124, 90), `ghast_appear`; C+3 enabled_ops + `ghast_vis` + level 30 and its first swing | the line, spotanim, `npc_delay` from a player script | rebuilt |
| ghast_vis combat | `ghast_attack` 1087 / `ghast_block` 1088 / `ghast_death` 1089; synths 433 / 435 / 434; every 4 ticks; max 3 seen | `npc_param(attack_anim)` with no param: the human defaults, null sounds | params in the quest `.npc` |
| death | killing hit, +2 the death seq, `ghast_spotdeath`, `ghast_death`, stage + line, enabled_ops 0; +4 the drop and `[del]` | | matches; `ghast_death` added on the reward |
| prayer xp | 30 per kill | 30 | matches |

## 12. Npc info

Filliman's spirit (9 lives) and the Nature Spirit (5): added `south`, never move,
`[facing] player=` from the tick after the add (`npc_setmode(playerface)`),
`human_casting` at the casts, one `[transformation]` + `level_change 0` +
`enabled_ops 0b11111` at [1799] (`npc_changetype`). The ghasts: `[transformation]`
+ `level_change 30` + `enabled_ops` at the reveal, `enabled_ops 0b00000` at death.

## Wire audit

`test/quests/druidspirit.lua` now drives the recorded order (Priest in Peril's
barrier talk, the start's pages, the warning, Filliman without the amulet first,
the stones with Filliman standing by, the pouch used on a ghast, the fight
unarmed with the sickle in the pack, the scroll, its close, the farewell, the
level-ups) with `t.ticklog.start()` and a mark per leg. Audited on its tick log
(`build/quest_gate/druidspirit/ticklog.tsv`, runs 3 and 4), against 4602 by the
event the two sides share (the click, the op, the stage):

| section | recorded | ours | verdict |
|---|---|---|---|
| start stage | click on "Now, please be aware..." +1, the box, next page +1 | the 10th continue click; one tick each side of the box | matched (run 4) |
| gate in | warning on the op, count 0->1, warn1, stage 10, leaves + `iron_door_open`, leaves back +2 | same rows, [122..125] | matched; ours writes stage 10 on warn1's tick, the recording a tick after its click (one sample, the click's ±1) |
| gate out | line, leaves, sound, Ulizius's say on the op | same, [231..233] | matched |
| jump fall | C seq, C+1 `jump`, C+2 splash (d3) + `pool_plop`, C+5 seq -1, hit | [128..133], [223..228], [278..283] | matched |
| bowl, mirror | seq -1, then walkmerge + `pick2` | [164..169] | matched |
| Filliman's call | add + `ghast_appear` on the op | [137], [287] | matched |
| Drezel's blessing | C seqs + say; C+1 star 30->90 + `prayer_recharge` d30; C+2 bless spotanim d30 | [245..247] | matched |
| bloom spell | C+2 `human_casting` d30, `cast_bloom`, 8 blooms d30 h46, one log, `bloom_mushroom` once | blooms were h30 d46 and the sound once per log | **fixed** (run 4) |
| ritual | C cast + `spirit_transform_start`; C+2 three stars 30->130; C+4 three effects + `bloom_pears` | [348..352] | matched |
| transformation | C+2 cast d5 + seven stars (116, 96, 86, 136 x3, 116) + `spirit_transform`; C+3 effects; C+4 the type, effect h64, `enabled_ops`, `level_change 0` | [365..367]; no `enabled_ops`/`level_change` | **fixed**: `npc_setops(31)`, `npc_setlevel(0)` |
| sickle's blessing | C spotanim (200, d120), cast, four stars (160 x3, 150) + `prayer_recharge`; C+1 bloom seq | [386..387] | matched |
| sickle bloom | C seq d10 + spotanim (200, d30) + `cast_bloom`; C+1 the blooms and synths | [422..423], [444..445] | matched |
| picks | `human_pickupfloor`, loc back, `pick` | [437..454] | matched |
| pouch fill | the line, `lunar_hunter_kit_inv` | [457] | matched |
| invisible ghast | `inv_ghast_attack`, say, the player's defend seq, `ghast_attack` | the player played `swarm_walk` (an unset `%varp6959_com_defendanim`) | **fixed**: `~combat_defend_anim` from the worn weapon/shield |
| reveal | ... C+3 transformation + `enabled_ops` + `level_change 30` | no `level_change` | **fixed**: `npc_setlevel(30)` |
| ghast_vis swing | `ghast_attack` seq + `ghast_attack` synth | seq only (its own `[ai_opplayer2]` has no sound) | **fixed** |
| ghast_vis death | +2 after the kill: seq, spotanim, sound, stage | the engine's seq + sound at +1 (from the new params), then the script's again at +3 | **fixed** the duplicate; the engine's +1 vs Jagex's +2 is engine-generic (open) |
| altar | three stars from fixed grotto tiles | from offsets of the spirit's found tile | **fixed** (fixed tiles, 76 + 10/tile) |
| jingle | `quest complete 3` (154) | 153 | **fixed** (`quest_complete_jingle_recorded`) |
| farewell / level-ups | scroll close -> farewell -> level-ups | level-ups first | **fixed**: the farewell queued with the scroll, before the xp |

Npc block inventory (`npc_block_inventory.py`, 4592 + 4602 vs run 3):

| type | kind | recorded | ours | verdict |
|---|---|---|---|---|
| filliman_tarlock_spirit | transformation | 1 | 1 | matched |
| | enabled_ops, level_change | 1, 1 | 1, 1 (run 4) | fixed; the tool files ours under the new type (944), the transcript under the old |
| | spotanim 266 h64 | 1 | 1 (counted under the new type) | matched |
| | facing | 26 | 11 | dialogue count (the test talks less) |
| filliman_tarlock_ns | sequence | 3 | 3 | matched |
| ghast_invis | transformation, spotanim 269 | 3, 3 | 3, 3 | matched |
| | level_change | 3 | 3 (run 4, under 946) | fixed |
| | say / sequence / facing | 11 / 15 / 717 | 2 / 3 / 22 | exposure: the recorder crossed the swamp ~10 times, the test 4 |
| ghast_vis | spotanim 265, enabled_ops 0 at death | 3, 3 | 3, 3 | matched |
| mort_myre_gate_guard | say | 1 | 1 | matched |
| | facing | 4 | 0 | the test never talks to Ulizius |
| priestperiltrappedmonk2 | say | 1 | 1 | matched |

Engine-generic, named: the npc defend sound goes out as an area sound (ours
`ghast_hit` `r12`) where Jagex sends a synth; the death seq a tick early.

## Test

`python3 tools/quest_gate/run.py druidspirit`: 173/173 PASS (run 4, 2026-11-18),
every stage written in the recorded order, 5 through 110 with 15 on the way.

## Open

- Bank, GE, Varrock Teleport, the frog random event: generic, not diffed this pass.
- The warning's warn2, close box and toggle; the worn sickle's Bloom; the pear's
  bloom and pick sounds; the empty-pouch line; the Altar of Nature: UNVERIFIED.
- Journal stages listed in §9: UNVERIFIED.
- The Nature Spirit's tile when the pool calls him (three recorded tiles, no rule
  fitted): found near the player.
- Engine-generic: the npc defend/death sounds go out as area sounds where Jagex
  sends synths, and an npc's death seq goes out a tick after the killing hit
  where Jagex's is two.
- `tools/quest_gate/lint_quest.py` aborts on an unrelated duplicate var name
  (`ge_newoffer_price`: varp 5753 and 9507); the NBSP and recorder-name checks
  were run by hand (none).
