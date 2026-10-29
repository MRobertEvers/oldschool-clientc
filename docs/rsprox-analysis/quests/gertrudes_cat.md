# Gertrude's Cat vs RSProx index 4401

Recording: index 4401 (rev 241.1, world 495, 2026-10-28, "Gertrude's Cat"), the
whole quest from the not-started overview [18] and Gertrude's first page [164]
to the scroll [1031], then logout [1052]. One playthrough, so every random
(the kitten's crate, the kitten's colour) is one roll. Ours:
`test/quests/fluffs.lua` on scriptrun, tick log with `leg.*` marks; two seeds
(`fluffs1`, `fluffs2`) pass every row but `quest.journal`, which fails on
scriptrun for every quest (no clientscripts run there).

Content: `quests/quest_fluffs/` (`quest_fluffs.rs2` the boys, crates, fence,
Fluffs; `kitten_pet.rs2` the reward follower; `fluffs_journal.rs2`),
`areas/varrock/scripts/gertrude.rs2`. First ported from LostCity (2004), so per
the rsprox-content skill §16 every page and shape was rebuilt from the
recording. Branches the recording never took keep the wiki's text
(`Transcript:Gertrude's_Cat`, marked `[wiki]`, moods unverified) or LostCity's
(`[LostCity]`, in neither source).

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 3 | 1 | welcome_screen:play | engine, login |
| 18 | 3 | questlist op 2 (journal) | this content: the overview [18] and the journal [550], [620] |
| 23 | 1 | questjournal_overview:close | generic |
| 28 | 4 | poh_tablet_varrockteleport Break | generic, bound (`teleport_tablet.rs2`) |
| 75 | 2 | ge_clerk Exchange, GE offers, collect | generic (Grand Exchange): the recorder buying sardines, milk and the lumberyard scrolls |
| 97 | 1 | banker Bank, bank withdraws | generic (bank) |
| 160 | 4 | opnpc1 gertrude | this content |
| 193 | 1 | opobj3 doogleleaves (3157,3400) | this content: all five recorded leaf spawns are in `m49_53.spawn`; take is the engine's |
| 212 | 3 | opnpc1 wilough | this content |
| 230 | 4 | opnpc1 shilop | this content |
| 339 | 1 | oploc2 fai_varrock_bankbooth | generic (bank) |
| 362 | 1 | 2dosestamina Drink | generic |
| 404 | 2 | oploc1 gertrudefence | this content (rebuilt) |
| 417 | 4 | oploc1 gertrudeempty_crate | this content (rebuilt) |
| 423 | 13 | opnpc1 kittens_mew | this content (rebuilt) |
| 453 | 4 | oploc1 fai_varrock_ladder | generic ladder: L+1 face, L+2 seq 828, L+3 up a plane [455..458]; ours the same (t92..t95) |
| 463 | 5 | opnpc1 gertrudescat (Pick-up) | this content (rebuilt) |
| 469 | 1 | opnpc3 gertrudescat (Stroke) | clicked while the Pick-up's box was due; dropped by Jagex, no answer recorded |
| 473 | 4 | opnpc4 gertrudescat (Talk-to) | this content (rebuilt) |
| 498 | 1 | bucket_milk on Fluffs | this content |
| 506 | 1 | opnpc5 macro_maze_invitation | random event (the Maze): our tree has no random events, so the npc never appears; not this content |
| 519 | 1 | raw_sardine on Fluffs | this content |
| 530 | 2 | doogleleaves on raw_sardine | this content |
| 541 | 1 | poh_tablet_faladorteleport Break | generic, bound |
| 555 | 3 | questjournal:close | generic |
| 557 | 1 | fai_falador_bankbooth, bank ops [561..613] | generic (bank, stamina, dropping cooked meat) |
| 685 | 1 | oploc1 exchange_bank_wall_exchange | generic (GE booth) |
| 698 | 1 | GE buy teleportscroll_lumberyard | generic |
| 715 | 1 | teleportscroll_lumberyard Teleport | generic, **was unbound**: now `skill_magic/scripts/spells/teleport_scroll.rs2` |
| 754 | 1 | seasoned_sardine on Fluffs | this content |
| 776 | 2 | oploc1 fai_varrock_laddertop | generic ladder down |
| 899 | 1 | gertrudekittens on Fluffs | this content (rebuilt) |
| 1038 | 1 | questscroll:close_button | generic |
| 1052 | 1 | logout | engine |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 32, 204, 570, 926 | -> Varrock square | Varrock teleport tablet | generic, bound |
| 409, 727 | 3308,3492 -> 3308,3494 | the fence's hop | rebuilt, matched (below) |
| 458, 743, 882 | 3310,3508,0 -> plane 1 | the yard's ladder up | matched |
| 545 | -> 2965,3378 | Falador teleport tablet | generic, bound |
| 719 | 3166,3487 -> 3302,3486 | Lumberyard teleport scroll | new binding; one landing recorded |
| 780, 919 | plane 1 -> 0 | the ladder down | generic |

## What was wrong (LostCity) and what the recording says

| item | LostCity port | recording | cite |
|---|---|---|---|
| quest start | `p_choice3` incl. "What's in it for me?" | `Start the Gertrude's Cat quest?` Yes./No., the player says the line | [170], [182] |
| "You've started a new quest" | missing | with the stage, on the "Well, I suppose I could." click | [184] |
| "Do I look ok?" | "ok" | "okay" | [165] |
| boys' menu option 3 | "Fluffs' loss." | "Fluffs loss." (option and the player's line) | [220], [280] |
| "Where is this play area?", "What will make you tell me?" | quiz | neutral | [218], [222] |
| "10 coins." | happy | neutral | [225] |
| "Okay then, I'll find another way..." | neutral | bored | [254] |
| the 100 coins | taken at the box | taken with the stage on the last page's click | [314] |
| "You give the lad 100 coins." | objbox `coins_25` | objbox `coins` | [306] |
| "now where did you see Fluffs ?" | space before `?` | none | [307] |
| boys after the cat is found | one form | two: with the brother beside the player the long form; three tiles off, "I can beat anyone up." alone and a forced break in the box | [937..946], [954..959] |
| fence | exactmove 3308,3491 -> 3308,3492 | L+1 face, L+2 step onto the fence tile, L+4 seq 882 + exactmove 3308,3493 -> 3308,3494 (0..104), put down two tiles past | [404..409], [722..727] |
| Fluffs' scratch | `damage 3` | no hit (no splat in the session) | [463..466] |
| Fluffs' face in the scratch | `npc_facesquare` | `[facing] player=` held, no reset until [535], no wander meanwhile | [465], [535] |
| Talk-to | "Miaoww" page + the stage's boxes | "Miaoww" overhead, nothing else | [473..474], [890..896] |
| stage 4 box | two boxes | one, broken after "leave." | [769] |
| raw sardine box | one line | broken after "sardine." | [524] |
| empty crate | 5-tick waits, mewing at every stage | "You find nothing." on O+4; the mewing line O+8 only while the kittens are lost | [417..421], [788..796] |
| kitten crate | waits 5 | O+4; "You find a kitten! You carefully place it in your backpack."; the kitten on the box's click | [866..874] |
| kittens' mew | `Mew!` 1 in 5, every 5..24 ticks, silent; no `timer`, so never ran | every fire is `Mew!` + `sound_area 339` (28/28), gaps 25..47 in a window | [411..924] |
| kitten returned | one `Purr...`, stage after a delay, Fluffs stays | stage + kitten gone + `lostkitten` on the player's tile on O; O+1 Fluffs' face reset; O+2/O+3 both `Purr...`; O+6/O+7 both step east (Fluffs then north); O+11 both gone + box | [899..910] |
| Gertrude's last pages | no "Oh by the way" page; "...a kitten. And some food!" box | `chatneu3` "Oh by the way, the kitten can live in your backpack..."; box "Gertrude gives you a kitten." | [1018], [1020] |
| the reward | kitten in the pack, everything at once | on the box's click: stage 6, the kitten OUT as a follower (`follower_obj` 1555, `kittenpet1` beside the player, "Miaow!", "Meeooow!" a tick on); C+5 "... and some food!" + cake + stew; C+9 completion | [1022..1031] |
| scroll | "1525 Cooking XP / A kitten / Gertrude sells kittens..." , `kittenobject` | "1,525 Cooking XP / A Kitten / A Chocolate Cake / Some Stew", `kittenobject_dummy` at 79,126 | [1031] |
| jingle | difficulty default 154 | 152 `quest_complete_1` | [1031] |
| Gertrude's movement | stationary (generated) | wanders out of her door, 24 steps over 3 lives (x 3148..3156, z 3412..3415) | [160..175], [1003..] |
| not-started overview | "speaking to Gertrude." | "talking to Gertrude at her home west of Varrock." | [18] |
| journal, stage 3 | LostCity text, blank rows | the rows below | [550], [620] |

The stage values are Jagex's (varp 180 `fluffs`, 0..6), `transmit=yes`:
1 [184], 2 [314], 3 [499], 4 [756], 5 [899], 6 [1022].

### Journal (stage 3, the only stage recorded)

```
<str>I accepted the challenge of finding Gertrude's lost cat.
<str>I spoke to Shilop, Gertrude's Son.
<str>I found the lost cat but it won't come back.

<col=000080>I still need to <col=800000>get her to follow me home.
```

Every other stage is UNVERIFIED (no recording opens it, and the wiki has no
journal transcript): the same layout with LostCity's wording.

## Wire audit (ours = scriptrun, 2026-10-28; wire log ticks, the tick log runs one later)

| section | recorded | ours | verdict |
|---|---|---|---|
| start | menu; stage 1 + start line + "Really?" page on the next click [184] | t25, together | matched |
| boys | speakers by the clicked boy; coins + stage 2 + close on the last click [314] | t57 | matched |
| fence | face L+1, step L+2, hop L+4 | face t61, step t62, hop t64 | matched |
| ladder up | L+1 face, L+2 828, L+3 plane | t92..t94 | matched |
| Pick-up (stage 2) | 827 O, cat_attack + face + "Hisss!" O+2, "Ouch!" O+3, box O+7 | O t99, scratch t101, box t106 | matched |
| Talk-to | "Miaoww" | "Miaoww" | matched |
| milk | 827 + milk gone O; stage + "Mew!" + empty bucket O+1 | t132/t133 | matched |
| raw sardine | 827 O; "Hiss!" O+1; box O+5 | t155/t156/t160 | matched |
| seasoned sardine | as the milk | t246/t247 | matched |
| Pick-up (stage 4) | box "afraid to leave" O+7 | t259 | matched |
| kitten crate | search O; box O+4; kitten on the click | t327/t331/t332 | matched |
| kitten returned | O..O+11 as above | stage, kitten, spawn O t349; face reset O+1; says O+2/O+3; steps O+6/O+7; gone + box O+11 t360 | matched |
| hand-in | stage 6 + follower + "Miaow!" C; "Meeooow!" C+1; food C+5; scroll + qp + jingle 152 C+9; level-up after the scroll closes | C t419; t420; t424; t428; level-up on the close | matched but the xp (below) |
| kittens' ambience | `Mew!` + 339 range 10, gaps 25..47 | 22/22 in-range sounds share their say's tick; 69 gaps, 25..47 | matched |

## Open

- **The Cooking xp goes out on C+9, not C.** Our level-up is a queue that runs
  when the reward script ends, ahead of the scroll's own queued mount
  (`questscroll.rs2`'s `[queue,quest_scroll_show]`), so an xp drop on C put the
  level-up box in front of the scroll. Jagex mounts the scroll inline. Moving
  the shared mount inline would fix it for every quest; not done here.
- **`follower_npc` / `follower_obj` (varps 447/448) are never sent.** Both are
  `temp, transmit=no` in `server_fields.generated.varp`, and our follower state
  is `%varp9497_kitten_pet`. The client uses 447 to rank its own follower's
  ops above other players' pets.
- **The kitten never grows.** Feeding, stroking, growth, the overgrown cat and
  rat-catching are not built. The kitten can be picked up (op 1) and put down
  (Drop) and comes back out at login; Pick-up's anim and Drop's shape are the
  phoenix's (unrecorded). Its Talk-to/Chase/Interact ops are unbound.
- **The way out over the fence** (north to south) is the recorded hop
  mirrored, unrecorded.
- **The short boys' form** is fitted on two talks: the brother within 2 tiles
  or not.
- **The stage-4 empty crate at [445]** faced and never answered; the next
  stage-4 search [788] did. Not reproduced.
- **Fluffs' respawn**: she is the map's one cat and is deleted when she runs
  off [910]; ours respawns her on the type's timer (25), unrecorded.
- **Unrecorded branches**: Gertrude at stages 1, 2 and 4, the boys at stage 2
  ([wiki]); the boys before the quest, the stage-0/1 Fluffs ops, the barrel,
  dropping Fluffs' kitten, the kitten shop ([LostCity]).
- **The Lumberyard teleport** has one recorded landing (3302,3486); no other
  teleport scroll was bound or checked.
- `synth_sound` 3120 ([793]) and 3424 ([817]) went out during crate searches
  and are not this content's (`clock_tick_1`, `tol_homonculus_crying`):
  unexplained, not built.
