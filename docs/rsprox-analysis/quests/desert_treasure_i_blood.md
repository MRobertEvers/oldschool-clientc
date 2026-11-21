# Desert Treasure I: the Blood Diamond hunt (Malak, Ruantun, the pot)

Rebuilt from RSProx 4628 [3222..3941] and 4629 [0..3520] (rev 242, 2026-11-20). Files: `quest_deserttreasure/scripts/dt_blood.rs2`
(owner), the Desert Treasure part of `areas/entrana/scripts/high_priest_of_entrana.rs2`, one message in
`skill_herblore/configs/grinding/grind.dbrow`, the blood rows of `test/quests/deserttreasure.lua` (all marked `UNRUN`).
Nothing was run: the only check is the private compile (clean).

## Variables

| R | ours | verdict |
|---|---|---|
| `fdvw_subquest` (373) 0 -> 1 at 4628 [3848], on the click, same tick as the page | `%varb373_fdvw_subquest = 1` before the player's page | written |
| 1 -> 2 at 4628 [3902], the page "Was there anything else you wanted?" | set just before that page, only at the end of the How answer | written |
| 2 -> 3 at 4629 [3404], two ticks after Dessous's death anim | `[proc,dt_dessous_killed]` (stage + the two player lines [3404], [3409]); the fight agent calls it | procedure written, caller is Open |
| 3 -> 4 at 4629 [3481] with the diamond and the pot leaving | set after the player's demand, before the page | written |
| `deserttreasure` 11 -> 12 at 4629 [3490] with the player's line | `[proc,dt_diamond_gained]` (all four `fd_*` diamonds in the pack, stage 11 only) | written |
| `fdvw_cutscene` (372) | `dt_malak_scene.rs2` (orchestrator) | not touched |

Old -> new: `%varp6607_dt_blood_stage` none/offered(0,1) -> `fdvw_subquest` 0; agreed(2) -> 1 before "How" was asked, 2 after (a
saved agreed account cannot say which: map to 2); killed(3) -> 3; complete(100) -> 4. Base varp 442 already states `scope=perm`,
`transmit=yes` in `configs/deserttreasure.varp`; the generated block has a bare `[varp442_deserttreasurevarbit2]` with only
`wholewrite`/`wholeread` (no key stated twice). `varp6607` is still written by three `BRIDGE` lines in dt_blood.rs2 because
`dt_dessous.rs2` and `dt_journal.rs2` still read it; `~dt_blood_state` also reads a kill that dessous writes there (3) as subquest 3.

## Pages (distinct strings of the windows)

| part | recorded | verbatim | wiki | note |
|---|---|---|---|---|
| Malak, 4628 [3583..3910] + 4629 [191..3514] | 96 | 96 | 0 | the Drakan reason (menu answer 2 never picked), the ring variant, "Where can I find Dessous?" at sub-stage 2 and the High Priest's blood-pot lines are wiki, marked |
| Ruantun, 4629 [260..2624] | 14 | 14 | 0 | |
| High Priest, 4629 [2732..2737] | 3 | 3 | 5 (blood pot) | |
| generic/other sections in the windows | Mazchna, Roavar's innkeeper talk and menus, tavern scene, drill demon (4 menu strings) | | | not mine |

Distinct recorded strings of my windows not in our files: 115 of 121 before (the old script had other wording), 0 after (the 4 left are the
drill demon's menus, a generic random event). Moods are the recorded `chat*` family via `^chat_*` (the layout picks `1..4` by rows).
Joined where the recording only overflowed a page ("form of | agreement?", "similar | natural causes", "responsible for the | death of",
"take you to | Lord Drakan."); "some of his... | more interesting" keeps its recorded ellipsis.

## Malak's menus (4628)

- First menu: five full sentences ([3655]); after each wrong reason the menu is the same four of the remaining five without the answer just given
  and with every other back (Zamorak shown as "I am here to worship Zamorak", no full stops) -- [3802], [3807], [3810], [3814], [3818], [3825].
- Bargain menu title `Agree to this arrangement?`, `Yes|No`. Declined: three pages, the whole talk again next time.
- Questions at sub-stage 1: title `Select an option`, all four at the start of every talk, then the menu omits only the question just asked
  ([3880], [3889], [3902]). Why and Where return to the menu; How ends with "Was there anything else you wanted?" (sub-stage 2) and the menu.
  "Actually..." ends the talk. The first Why in the recording ended after "Yes... I think so..." because the recorder clicked Malak again
  ([3868]); the wiki's closing line is used.
- Sub-stage 2 menu: `Where can I find Dessous?|How do I kill Dessous again?|Actually, I don't need to know anything.`; "Actually" ends the talk
  ([559]); the wiki's `above` is not followed.
- "Back for something, human?" (sub-stage 1) versus "Why are you still here?" (sub-stage 2).

## The pot (one row per state, every one is its own obj)

| R | ours | verdict |
|---|---|---|
| Ruantun: bar -> `fd_silver_pot` in the bar's slot on his last page, 4629 [270], [2618] | `dt_ruantun_make_pot`, both from the talk and from bar-on-Ruantun | written |
| Ruantun, any other item: "I have no ussse for that..." [316] (the pot), [2624] (stamina) | `[opnpcu,malak]` | written |
| Malak, empty pot of either kind, any sub-stage >= 1: the player's request, click -> message, `vampire_swipe`, Malak `vamp_claw`, pot -> `_blood` / `_blood_blessed` in its slot; next tick hit 5 and "Ow!" ([538..549], [2180..2193], [3086..3094]) | `dt_malak_fill_pot` (p_delay(0) then `damage(.., 5)`) | written; the recorded line has no "the High Priest blessed it" |
| Pot on Malak, either state: engine "Nothing interesting happens." ([534], [568]) | the default use-on-npc answer | not bound; unchecked in the engine (Open) |
| Crushed garlic / spice into any blood pot, both click orders, pot's slot kept, the added slot emptied ([794], [797], [3099], [3101]) | `[opheldu,<pot>]` x6, `[opheldu,fd_crushed_garlic]`, `[opheldu,spicespot]` -> `dt_blood_add_to_pot` | written, in-slot; all 8 reachable states, unblessed route included |
| Grinding garlic: spam "You crush the garlic into a fine powder.", seq `human_herbing_grind`, synth `grind` ([1834]) | `~grind_ingredient` (and the herblore dbrow's message corrected) | written |
| Unblessed route: pot -> blood [549] -> garlic [794] -> spice [797] -> examined [1203, client-side, no packet] -> poured at [1475] | all generic ops plus above; pour is dt_dessous.rs2 | written up to the pour |
| Pour (Dessous agent): unblessed pot -> `blooddiamond_vampirewarrior_unkillable`, pot returns as `fd_silver_pot`, message "You pour the blood from the pot onto the tomb."; blessed -> real Dessous, returns `fd_silver_pot_blessed` [3284]; blessed empty pot on the tomb: "There is nothing in the pot to pour onto the grave." [3416] | dt_dessous.rs2 | for that agent; blessed empty pot is what Malak takes at [3481] |
| Malak after the kill: diamond added first (slot 7, lowest free), the empty blessed pot removed from slot 0 in the same tick [3481] | `dt_malak_dessous_dead` (pot first only when the pack is full) | written |

## Actions and teleports of the windows

| R | ours | verdict |
|---|---|---|
| opnpc1 Malak 4628 [3582], 4629 [191], [538], [555], [563], [570], [2180], [3472], [3503], [3512] | `[opnpc1,fourdiamonds_vampire_lord]` | rebuilt |
| opnpc1 Ruantun [259], [279], [308], [2513] ([2516] is the drill demon taking over) | `[opnpc1,malak]` | rebuilt; before the bargain only his first line (nothing recorded or in the wiki; the old menu was 2009scape's and is deleted) |
| oploc1 `vampire_trap1` [233] "You open the trapdoor.", [234] `human_openchest` + synth `trapdoor_open`, [235] `vampire_trap2` | `[oploc1,vampire_trap1]` | rebuilt (the revert time is not recorded: 100 kept) |
| oploc1 `vampire_trap2` [237], [2476]: teleport 3083,3272 -> 3083,9672 at the click, no anim, no message | `[oploc1,vampire_trap2]` on 3084,3272 | fixed: the old code sent everyone to 3118,9644 |
| oploc1 `ladder_from_cellar` 3084,9672 [2649]: `human_reachforladder`, teleport up | maplink `0_48_151_11_8` -> 3083,3272 | generic, bound; landing is 3083, the recording 3085 (the player's own tile): Open |
| opnpc1 High Priest [2728]; blessing [2737] | high_priest_of_entrana.rs2 | rebuilt, pot blessed in its slot on the page |
| oploc1 `thrt_tavern_trap_door` [1244]: mesbox "The trapdoor is locked." | routequest_hideout.rs2 | not mine, not checked |
| teleports: teletab_fenk [332], [1875] (tablet), Varrock Teleport [1682], [1740], `wornitems:slot2 op4` -> 3105,3251 [202], [2228] (worn-ring teleport), Entrana boat [2686..2834], drill demon invitation [2516] and mat [2598] | generic | one verdict: bound and looked right where checked by the shared tools (not re-run); the ring op 4 and the drill demon are Open to whoever owns them |
| banks/Grand Exchange/Canifis bank booth/Varrock bank/depot, potions, food, prayers, spec bar, world map, quest list/journal ([177], [189], [3510]) | generic | one verdict each: generic, not re-checked; the journal belongs to the pyramid agent |
| drops/pickups of the pots and the diamond ([278], [311], [554], [565], [3510]): `put_down`, `pick2`, `obj_add_specific` ownership self | generic | generic |
| 4628 [3243] slayer master, [3259] canafis door, [3260], [3290] werewolves, [3378] innkeeper, [3921] bank | generic | one verdict: generic |

## npc info (blocks recorded in the windows)

| npc | recorded | written |
|---|---|---|
| `fourdiamonds_vampire_lord` (686) | add/del on every region entry, `idle`, `facing` (the recorder; 25 blocks), `sequence vamp_claw` x3 (each blood fill, [540], [2188], [3088]) | facing by `~chatnpc`'s `playerfaceclose`, `npc_anim(vamp_claw)` + `npc_facesquare(coord)` on the fill. No walk, no transform, no `[say]` recorded; stands still |
| `malak` / Ruantun (3461) | add/del, `idle`, `facing` x5 | facing by the chat proc; no walk or anim recorded |

## Open

1. `dt_dessous.rs2` must call `~dt_dessous_killed` where the real Dessous dies and read `fdvw_subquest` (2 while the pot is held and through the
   unkillable one; poured unblessed pot -> `fd_silver_pot`), then the three BRIDGE lines go. `dt_journal.rs2` still reads `varp6607`.
2. The other three hunts must call `~dt_diamond_gained` where their diamond is picked up or given (`fd_diamond_fire`, `fd_icediamond`, `fd_dark_diamond`).
3. Ring of Charos(a) wording and the Vampyre Slayer reply are the wiki's, unverified. The Blood Moon Rises variants are not written (not in this tree).
4. Whether a use-on-Malak with a pot gives "Nothing interesting happens." (recorded, type `engine`) was not checked in the engine.
5. The trapdoor on 3118,3244 (the same loc pair) keeps the 2009scape landing (3118,9644), unrecorded.
6. Roavar's innkeeper menu (4628 [3559..3572]) and Mazchna are other sections'; the drill demon is a generic random event.
7. Whether the hit sound (`human_hit_*`, delay 20) after the fill comes from `damage()` was not checked.
8. `UNRUN` test rows: the page splits of the long Malak texts are the layout's.
