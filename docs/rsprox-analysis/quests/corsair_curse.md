# The Corsair Curse, from RSProx

Written 2026-10-23. The quest's content (`OSRS-Content/osrs-content/server/scripts/quests/
quest_corsaircurse/`: `corsaircurse.rs2`, `cc_cutscenes.rs2`, `cc_ithoi.rs2`) was rebuilt from
one recording:

| session | rev | world | what it shows |
|---|---|---|---|
| index 4352 (local, 2026-10-23) | 241.1 | w337, normal | the whole quest: Captain Tock at the crossroads [352] to the scroll [1845] |

It is the only session on the index with the quest (it was still `queued` when transcribed, so
search cannot see it yet). Every rule below that rests on one occurrence says so. Branches the
recording does not reach (the options it did not pick, re-talks at other stages) take the wiki
transcript's lines (`Transcript:The_Corsair_Curse`, fetched 2026-10-23) and are marked `[wiki]`
in the scripts; a branch neither shows is `[unverified]`.

Before this rebuild the quest was a Quest Helper stub: every crew member's investigation, the
food trail, the burning hut and the fight with Ithoi were one-line "Soft-skip:" messages, the
dialogue was invented ("Leave the kitchen. I am not naming favourite body parts today."),
`mes("You sail to Corsair Cove.")` with a plain teleport stood in for the voyage, and seven
"Leftover" mesboxes said what was missing. The stage values were wrong after 15 (it wrote 20,
25, 35, 45, 50, 52, 55 at other moments than Jagex does, and never 30, 40, 49).

Tools: `tools/rsprox/rsprox_cutscene.py` (drafts each scene), `tools/rsprox/rsprox_pages.py`
(every page with its mood), `tools/rsprox/rsprox_interact.py --list` (every client op), a
per-tick viewer over the default, `--raw` and `--bas` transcripts.

## The stage ladder (`%varb6071_corscurs_progress`) and the crew bits

| value | tick | written by |
|---|---|---|
| 5 | 363 | "Yes." to "Start The Corsair Curse quest?", with "You've started a new quest" |
| 10 | 428 | "West of Rimmington, you say?" clicked: the mesbox "Captain Tock swaggers off towards Rimmington." (+ synth `royal_footsteps`) |
| 15 | 501 | "Okay, I'm ready go to Corsair Cove." clicked: the voyage |
| 20 | 1334 | the last crew theory ruled out (Colin's, here), with all four bits in |
| 25 | 1385 | Tock's "Arrr, I don't rightly know..." clicked |
| 30 | 1428 | Gnocci's "It's an easy mistake to make!" clicked |
| 35 | 1452 | Arsen's "I suppose Ithoi's trying to be extra helpful..." clicked |
| 40 | 1496 | "Maybe because the Captain's thinking of firing you..." clicked, before Ithoi's confession |
| 45 | 1500 | "I know you've faked the curse." clicked |
| 49 | 1521 | the driftwood lit (two ticks after the use) |
| 50 | 1542 | Ithoi's water bolt lands on the fire |
| 52 | 1596 | "I've seen Ithoi running around..." clicked: "Kill him fer me." |
| 55 | 1790 | Ithoi removed, two ticks after his death seq |
| 60 | 1845 | Tock's "Anyways, ye've been a mighty help..." clicked: 2 quest points, the scroll ("2 Quest Points", "Corsair Cove Bank Access", model `poison`) |

| bit | value | tick | written by |
|---|---|---|---|
| navigator (6075) | 1 | 561 | Ithoi's "There be more things in the world..." clicked |
| cook (6073) | 1 | 722 | the flashback's unlock tick |
| | 2 | 1111 | the dig, the tick after the choice |
| | 3 | 1302 | "Well, sort of. That doll wasn't possessed..." clicked |
| cabinboy (6072) | 1 | 861 | the tick after the flashback's unlock |
| | 2 | 1209 | the tick after the telescope scene's unlock |
| | 3 | 1334 | "He's definitely not a mermaid." clicked |
| thief (6074) | 1 | 875 | "Well, I climbed down the vines..." clicked |
| | 2 | 878 | "But a couple of days later..." clicked |
| | 3 | 972 | Tock's "Arrr, I've no use fer the silly thing..." clicked (the relic given) |
| | 4 | 1076 | "You're saying this is a tooth-pick?" clicked (the relic taken) |
| | 5, 6 | 1321, 1323 | Arsen's two pages of the telling |

The cache does the rest: the sick crew, the two Tocks (crossroads 0..5, the ship 10+), the
driftwood (dry, burning at 49, damp at 50), the doll (cook 2 only), the sand by the tree, Ithoi
in his bed (hidden at 52) and Yusuf's booth and bank ops (60) are all multinpc/multiloc on
these varbits.

"The cache does the rest" was taken on trust, and for the sick crew it was wrong on our client
(fixed 2026-10-25). Each crew member is sent as the SHELL, `corscurs_*_sick` (size 2,
turnspeed 0, readyanim `dream_cyrisus_sitting_up` for Ithoi, `dream_cyrisus_barely_conscious`
for the other three), which multinpcs to the standing crew member (`human_ready`, size 1) at
every stage the recording reaches. No `[bas]` or `[sequence]` goes out for them, 0 in 4352, so
the stance rides in no packet. The client takes size, turn speed and the whole stance set off the
shell, and only the model, name and ops off the rung (deob `Statics.method4320` copies them off
`class86.field1237`, the untransformed type). Ours let a rung that stated a stance keep it, so all
four stood upright beside their beds (watched run `corsairc1`, shot 043). Ours now follows the
deob (`ToriRS_NpctypeEntityFacts`, `src/engine/torirs_types.h`); `t.npc.pose` reads `set ready
6284` (Ithoi) and `6282` (the others). `tools/rsprox/multinpc_stance_audit.py` lists every pair a
transcript shows whose shell and rung differ: 16 of 24 in 4352.

The same crew types also ran the generated `defaultmode=wander`. Arsen walked his bed tile
(4.1 steps a life); the recording has 0 steps in 9, 9, 9 and 7 lives
(`npc_movement_audit.py paths`). `corsaircurse.npc` now states `defaultmode=none,
wanderrange=0` for the cook, the cabin boy and the thief; Ithoi's block already said `none`.

## The scenes

Four play in a **private copy of the cove square** (m39_44): the `--raw` transcript moves the
player far outside the map (Gnocci's 13772,13144; Colin's 10904,13521; the telescope's
12608,14656; the hut's 10522,15641) while the default transcript prints the template tile.
Tock's walk to Rimmington is not a copy: the player stands unseen on the real Rimmington shore
(2918,3231) while the camera pans along it. Ours: `~cc_copy_enter(copy, dx, dz, level)` (one
fresh copy per scene, `%varp9414_cc_copy` says which, for the logout backstop).

The frame, the same in all five:

| tick | packets |
|---|---|
| T | fade out (948 `0,255,0,0,50`; `,15` for the pan and the hut), hp bar hidden, `cc_deleteall`; the page that leads in stays up with **no continue** (events none, text "") |
| T+2 (T+1 for the 15-cycle fades) | `fov_clamp` 1, `cutscene_status` 1, npc/loc/obj ops hidden, `minimap_state` 2, `gravestone_tli_hide` 1, `option_acceptaid` 0, `option_areasounds_override` 25, `areasounds_override` 1, `draw_distance_max_override` 1; the move; the player made `desert_treasure_invisible_npc` (not for the telescope); 5 zero `cam_shake`; xp drops and sides 0-6, 11, 12 closed; 6021; `highlighting_off`; `minimap_toggle 2` |
| ... | camera placed at rate 232/100, fade in, the scene |
| E | fade out |
| E+2 | the move back, the player visible again |
| E+3 | everything back (`option_acceptaid` 1), `cam_unlock false`, `cam_reset`, sides re-opened, `highlighting_on`, fade in, the last page up again; its click closes the overlay |

Unlike Misthalin Mystery's frame, the move back and the unlock are a tick apart (Gnocci [721] /
[722], Colin [859] / [860], the telescope [1207] / [1208], the hut [1546] / [1547]).

| scene | window | how it starts | copy entry (template) | first camera eye / look (height) |
|---|---|---|---|---|
| Tock's walk | 365..388 | the click on "Arrr! Thanks..."; "Now, I've a ship..." stays up | none: 2918,3231 | 2943,3224 h800 / 2928,3224 h100, then a 6/0 pan to 2926 / 2911 |
| Gnocci | 666..724 | the click on "I'll explain..."; "I was fishing..." stays up | 2500,2840 | 2499,2833 h800 / 2505,2837 h200 |
| Colin | 768..863 | the click on "Ithoi always told us..."; "I was using Ithoi's telescope..." stays up | 2528,2841,1 | 2527,2824 h700 (an untranslated tile: raw 10903,13504) / 2527,2839 h350 |
| the telescope | 1186..1269 | the player's "Let's see if I can find that mermaid he saw..." clicked | 2528,2816 | 2528,2821 h400 / 2528,2822 h300; the `wom_telescope` overlay over it |
| the hut | 1519..1549 | tinderbox on the driftwood at 45 | 2530,2841,1 then 2529,2838,0 | 2529,2835,1 h200 / 2529,2841,1 h170 |

Inside the scenes, every page the cast "speaks over" (Gnocci's scream [704], the burial line
[717], the doll [1114], the ogre [1199], the hut [1542]) goes up with no continue, and the same
page comes back with one once the scene ends. Between pages, the chatbox shows an empty
`messagebox` with nothing to click while the cast moves.

Gnocci's flashback plays `human_lobster` on him and on the unseen player every 4 ticks
([672]..[692]) whatever page is up (ours: a soft timer). Every jingle in a scene is preceded by
`midi_song` 147 with a zero envelope.

## Interactions

| interaction | recorded | ours |
|---|---|---|
| the voyage (Tock, or Travel) | on the click: box with no continue, fade, song 147 + jingle `quick_sail` (171); T+3 the player on deck; T+4 fade in; T+5 box closed; T+6 overlay closed ([501]..[507], [939]..[945], [1000]..[1006]) | `~cc_sail` |
| the cove's gangplank | one tick after arrival, the other end ([508]..[509], [928]..[933]) | `p_telejump` |
| Rimmington's plank off the ship | [945]..[947] | `p_telejump` |
| the hut stairs and Ithoi's ramp | no anim, the landing at L+1 | rows in `maplink_landings.dbrow`: without them `~climb` put the player on the same tile a plane up |
| the spade on the sand | face, then `human_pickuptable` + `sand_withoutspade` for 12 ticks + the spade + synth `remove_axe` ([899]..[912]) | `[oploc1,sand_withspade]` |
| the tinderbox under Gnocci's hut | face; `human_pickupfloor`; a tick later the tinderbox + synth `pick2` ([733]..[735]) | `[oploc1,raids_icedemon_tinderbox]` outside a raid |
| the telescope | before Colin's story: mesbox "You don't feel you need to see the sea any closer up..." [576]; after: the scene; after the ogre: the player's "I've seen enough of that ogre already..." [1272] | `[oploc1,corscurs_telescope]` |
| tinderbox on the driftwood before 45 | the player's "That seems a rather dangerous thing to do underneath this hut..." [1149] | `[oplocu,corscurs_driftwood_multi]` |
| a dig away from the doll's sand | "Nothing interesting happens." + `human_dig` + synth `digspade` (1470) ([1094], [1098]) | `spade.rs2` now sends `digspade` on every dig |
| the dig by the tree | the player's page on the click (no dig), the choice, then the dig + `digspade`; the doll added a tick later and walked (glides) to the water with `poh_clockwork` every tick, `watersplash` at [1121] | `~cc_try_dig` -> `~cc_doll_scene` |
| the relic | objbox at zoom 400 on the click that gives it ([972], again [1016] after the recorder destroyed it) | `~objbox_scaled(.., 400)` |

## Ithoi's fight ([1644]..[1790])

At 52 the ramp takes the player into a private copy with `corscurs_navigator_combat` standing by
his bed (spawn angle south). He casts three ticks after the add and every five ticks after
(27 casts, 0 steps): the spell and his line are each picked on their own (wind 5, water 9,
earth 3, fire 5, curse 5; "Arrr, land-lubber!" 6, "Curse you!" 10, "Ye'll pay!" 7, "Curse 'em
all!" 5 -- no line goes with a spell). Each cast: `human_caststrike_walkmerge` (9144; curse
`human_castcurse_walkmerge` 11428), the casting spotanim at 92, the projectile 51 -> 90 cycles
(91 four tiles off; v2 heights 172/124 = 43/31), the spell's cast synth, and with the landing
delay the player's block, the impact spotanim (or `failedspell_impact` at 124 and synth
`spellfail`) and the hit synth. The splat is three ticks after the cast; a splash still splats
a 0; the curse never splats, drains Defence once ("You feel weakened." on the landing) and lands
silently while it is drained. His defend seq is `human_unarmed_def` (425) on every swing at him
that is not a cast tick, his defend sound `human_hit` (513) to the dealer on every landing, his
death `human_death` + synth `human_death` (512) + "Awww!" the tick after the killing splat, and
he is removed two ticks later as the player is moved back into the real hut and 55 is written.

The recorder prayed Protect from Magic from [1660]: every strike after it splatted 0 with its
impact (18) or splashed (4). His max hit is not on the wire; ours is the strike table's (2, 4,
6, 8). Hitpoints 55 (the cache); the bar's fill and the 71 damage dealt are consistent with it
and the natural regen.

## Wire audit

The quest test (`test/quests/corsaircurse.lua`, 147 of 147 rows) against the client with the
embedded server; its tick log through `tools/rsprox/ticklog_tells.py`, each section against
`tools/rsprox/rsprox_tells.py --timeline` of the recording. A dialogue scene is paced by its
reader, so events are placed relative to the page clicks around them, not by absolute tick.

| section | recorded events | matched | the rest |
|---|---|---|---|
| Tock's walk | lock, 4 camera lines, 2 fades | all, on T+1 / T+2 / T+3 / T+12 / T+21..23 | |
| the voyages | box, fade, 147 + jingle 171, move T+3, fade T+4, box T+5, overlay T+6 | all | |
| Gnocci | 24 (seqs, type changes, synths, jingles) | 24 | `human_lobster` 4 times to the recording's 6: the pot runs every 4 ticks and the driver clicks faster than the recorder read |
| Colin | 17 | 17 | |
| the doll | 13 | 13, on the same offsets from the choice | |
| the telescope | the seq, the ogre's add/del, 2 jingles | all | |
| the hut | 17 | 17, on the same offsets from the use | the projectile's end height: v2 10 raw -> 2 (8 raw) in our one-byte packet |
| the fight | add, cast every 5 from add+3, splat cast+3, block anim at the landing, defend seq/sound, death seq + "Awww!" + `human_death` at hit+1, removal + stage + move at hit+3 | all | spell picks and hit rolls are random; our run splashed every fire strike, so `firestrike_impact`/161 are recorded-only |
| interactions | spade, tinderbox, planks, stairs, telescope, driftwood | all | |

Whole-quest presence diff (every synth, seq, spotanim and projectile id either side sends):
recorded-only are the recorder's own (stamina doses `human_eat`/2401 and their expiry 3120/3424,
the scimitar and meat picked up in Colin's hut, the relic destroyed 2381, Protect from Magic on
2675 / off 2663), ambient ducks and another player's sail, and the fire strikes above; ours-only
are the test character's own hit sounds (518/520/521: it does not pray). Every page's text and
mood was checked against the 226 recorded pages: all match.

Fixed on the way, outside the quest:
- `general_use/scripts/spade.rs2`: every dig now sounds `digspade` (recorded 3 of 3).
- `skill_combat/scripts/npc_combat_magic.rs2`: a player's first block against a caster went
  out as seq 0 (`%varp6959_com_defendanim` filled only by the hit roll after it); recorded
  `human_sword_def` on the first cast.
- `ladders_stairs/configs/maplink_landings.dbrow`: the cove's hut stairs, Ithoi's ramp and the
  vine ladder's other three sides.

## Open

- The driftwood fire (owner, 2026-10-25: "the sticks themselves never light on fire"). It is not
  reproduced here. Against 4352 the wire matches tick for tick (stage 49 at op+2 and 50 at op+23
  in the watched run's tick log too), and the burning rung (`corscurs_driftwood_burning`: model
  34312 + fire 26584, seq `dragon_slayer_qip_fire1x1`) draws as the cache defines it: flames in
  the middle of the pile with embers, in the copy from the light to the bolt (quest shots
  `lightDriftwood.burn-*`, and `tools/entity_viewer/ev_sheet --loc 31722 --seq 6646`). Waiting on
  the owner for what they expect to see.
- A multiloc's anim: the deob always takes the PLACED (shell) loc's seq (`Statics.method8559`
  reads `field4946` off the untransformed id); ours takes the rung's when the rung states one
  (`world_builder_resolve_loc_for_place`, the loc-change task). The driftwood's rungs share the
  shell's seq, so it is not the fire's cause; the other multilocs are unaudited.
- The sick crew turn to face the player on two late pages (Arsen [1456], Ithoi [1496]: `[facing]
  player=`); ours sends neither (turnspeed 0 means neither turns visibly). Madame Caldarium's
  face and its reset ([631], [652]) are missing too.
- `option_acceptaid` (4180) is set back to 1 at every unlock rather than restored (the recorder's
  was 1).
- The shared npc caster (`~npc_cast_spell`) still treats Protect from Magic as a splash; the
  recording shows a prayed strike landing its impact with a 0 splat (18 of 18). Ithoi does it the
  recorded way; other casters are untouched.
- Ithoi's projectile end time is fitted on two distances (90 at 1-2 tiles, 91 at 4).
- `busy` (12393) is engine-generic and not set by this content.
- Not recorded, and marked in the scripts: every wiki branch; Tess's answers to "What is this
  place?"; the post-quest ferry (Colin, `corscurs_cabinboy_2ops`), Tock on his throne and Yusuf's
  dialogue; the sick crew before the quest starts; leaving the fight by the ramp.
