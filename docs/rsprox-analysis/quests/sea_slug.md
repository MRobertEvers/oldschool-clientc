# Sea Slug — RSProx index 4405 (start) and 4406 (the quest)

Both rev 241.1, world 611, 2026-10-29, the same recorder. 4405 ends with the
start at Caroline ([2196]..[2214]) and the journal at stage 1 ([2222]); 4406
plays stage 1 to 12 in 1,015 ticks. Ticks below are 4406's unless marked.
Transcripts: `rsprox_index.py get 4405 4406`, and `get 4406 --raw` for the
crane scene's copy tiles.

Before this port the quest was LostCity's 2004 text: every page reworded or
split differently, no start prompt, no boat interface, no scenes (both were
`mes` narration), Holgart's Travel opened his dialogue, the torch went out on
every crossing, the platform allowed fires, the sticks dried with two messages
and a delay, and the completion scroll listed the wrong lines and icon.

Ours: `quests/quest_seaslug/` (`quest_seaslug.rs2` items and locs,
`seaslug_boat.rs2` the boat and both scenes, `seaslug_journal.rs2`,
`configs/quest_seaslug.constant`); `areas/area_fishing_platform/scripts/`
(`holgart`, `bailey`, `kennith`, `kent`, `fisherman`); `areas/ardougne_east/
scripts/caroline.rs2`; the platform Holgart's shell binding in
`quest_theslugmenace/scripts/slugmenace_pages.rs2`; the damp platform in
`skill_firemaking/scripts/firemaking.rs2`. Test: `test/quests/seaslug.lua`
(scriptrun 135/136; the miss is `quest.journal`, which needs the live client).

## Stages (varp 159 `seaslugquest`, perm, transmitted)

| value | written | on |
|---|---|---|
| 1 | 4405 [2211] | the click of "Ok, I'll go and see if they're ok.", with "You've started a new quest" |
| 3 | [61] | the click of Holgart's "Aye, that be perfect." (carrying paste: 2 is skipped) |
| 4 | [328] | the click of Kennith's last page, first talk |
| 5 | [374] | the click of Holgart's "Come on, we'd better go look for him." (the crossing's fade) |
| 6 | [436] | the click of Kent's "Please go back and get my boy..." |
| 7 | [693] | Rub-together lighting the torch |
| 8 | [769] | the click of "Okay, you wait here and I'll go figure another way..." |
| 9 | [811] | the panel's second line |
| 10 | [894] | the click of Kennith's "Ok, when you have I'll come out." ([848] showed it unclicked: no write) |
| 11 | [928] | inside the crane scene |
| 12 | [994] | the click of Caroline's pearls page, with the pearls and 7,175 Fishing xp |

2 (the no-paste branch) is never written in a recording: LostCity's, [unverified].

## The action inventory

`rsprox_actions.py` over 4405 from [2110] and all of 4406.

| action | verdict |
|---|---|
| Caroline Talk-to | this content, rebuilt (start, end) |
| Holgart (`holgartland`) Talk-to, Travel | this content; Travel boards with no dialogue ([90]) |
| Holgart (`slug2_holgart_jeb`, platform) Talk-to, Travel | this content; Travel boards ([108], [613]), was the dialogue |
| Holgart (`holgartsunkboat`) Talk-to, Travel | this content; both the same page ([388], [390]) |
| Jeb (`slug2_jeb`) Talk-to | The Slug Menace's npc before it starts: "Hello there." matched, mood fixed to chathap1 |
| sea slug (npc) Pick-up, `seasluginv` Take | this content; synth `leech_suck` added, the bite a tick later ([106]..[107]) |
| fishplatform1..3 Talk-to | this content, rebuilt (three of six themes recorded) |
| Bailey Talk-to | this content, rebuilt at stages 3 and 6 |
| Kennith Talk-to | this content: stages 3, 4, 7, 8, 9 matched as they were (breaks removed) |
| Kent Talk-to | this content, rebuilt with the slug scene |
| `seaslug_ladder` Climb-up | this content: the torch line moved to the teleport tick ([710]) |
| `seaslug_ladder_top` Climb-down | generic `climb_down_ladder` |
| `slug_breakable_panel` Kick | this content: `seaslug_human_kick` + `slug_kick_panel` (was `human_unarmedkick`, silent) |
| `seaslug_wall_closed` Examine | cache examine |
| `seaslug_crane` Rotate | this content: the line, the swing and `slug_crane_turn`; at stage 10 the scene |
| damp sticks on broken glass | this content: no messages, anim/spot/synth at 15 ([552]) |
| dry sticks Rub-together | this content ([579], [693], [706]) |
| torch Extinguish | generic, was unbound: "You extinguish the torch." ([702]) |
| tinderbox on logs / logs on tinderbox on the platform | "It's too damp for you to light a fire here." ([518], [588], [689]), was a fire |
| tinderbox on the unlit torch on the platform | "Your tinderbox is damp from the sea crossing. It won't light here." ([541]) |
| rope, damp sticks, broken glass Take | generic ground items |
| rope on `seaslug_table_ice`, logs/tinderbox on `fire_remains`, logs on glass, tinderbox on damp sticks, dry sticks on torch | engine/default "Nothing interesting happens." |
| `fire_remains` Examine | cache examine |
| `slug2_village_poordoor`, `fai_barbarian_poordoor(open)` | generic doors |
| quest list, journal, quest scroll close | engine; the journal below |
| bank, GE, stamina potions, Varrock/Ardougne tablets, logout | the recorder's kit / generic (tablets audited 2026-10-29) |

## Teleports

| tick | from -> to | cause | ours |
|---|---|---|---|
| [64] / [81] | Witchaven -> copy -> 2720,3302 | the repair scene | `~seaslug_repair_scene` |
| [92] [135] [663] | Witchaven -> 2782,3273 | the crossing | `~seaslug_sail_to_platform` |
| [111] [617] [969] | platform -> 2722,3305 | the crossing | `~seaslug_sail_to_witchaven` |
| [376] | platform -> 2800,3320 | the crossing | `~seaslug_sail_to_island` |
| [470] | island -> 2782,3273 | the crossing | `~seaslug_sail_island_to_platform` |
| [292] [710] / [357] [948] | ladder up / down | ladders | as recorded |
| [910] / [931] | crane deck -> copy -> 2772,3291,1 | the crane scene | `~seaslug_crane_scene` |
| 4405 [2237] [2296] | tablets | recorder's kit | generic |

## The crossing (seven times, all the same)

| tick | packets | ours |
|---|---|---|
| T (click or op) | fade out, the page closed | `~seaslug_fade_out` |
| T+2 | `minimap_state` 2, teleport, song 147 cut, jingle `sailing_journey` (172), `toplevel_mainmodal_open(3750175, 42)`, `seaslug_boat_travel` (461) with this route's layer unhidden, `minimap_toggle 2` | `~seaslug_sail` |
| T+8 / T+11 / T+9 | `minimap_state` 0, the arrival box, fade in, the boat closed, `minimap_toggle 0` (to the platform / Witchaven / the island) | |
| the box's click | the box and the fade overlay closed | |

The lit torch survives a crossing ([706] lit, [969] still lit at Witchaven).
Checked on a `TORIRSSERVER_TRACE_OUT=1` run: the same packet order.

## The boat-repair scene ([61]..[84])

| tick | R | ours |
|---|---|---|
| 61 | line, paste gone, stage 3, fade out, page closed | matched |
| 64 | lock, copy of square 42_51 at (28,36), `holgartinwater` at (30,36) facing south | matched |
| 65 | npc glide to (31,36), `seaslug_boat_repair` delay 45, facing reset, say "Right me hearty. Let's get this boat fixed.", cam_moveto (23,28) 500 100/100, cam_lookat (29,35) 200 100/100, fade in, `slug_smear_paste` delay 45 | matched (tick log: stage+4) |
| 74 | say "Huzzah! We've fixed the leaky old tub.", transform `holgartnolegs` | matched (stage+13) |
| 78 / 81 | fade out / unlock at 2720,3302 | matched |
| 82 / 84 | fade in / overlay closed | matched |

## Kent ([420]..[458])

| tick | R | ours |
|---|---|---|
| 436 | the click: stage 6, page closed | matched |
| 439 | "<displayname> wait!" (chatshock1) | matched (was "Traveller wait!") |
| 441 / 442 | click closes; Kent `seaslug_grab_n_destroy`, spot `seaslug_slug_squashed`, synth `slug_scoop_slug` delay 10 | matched (click+1) |
| 444 / 446 | "*slooop*" / "He pulls a sea slug from under your top." | matched (LostCity's extra "Kent reaches behind your neck." deleted) |
| 448 | `seasluginv` on the tile, private, despawn 200; his page | matched (anim+6) |

## The crane scene ([906]..[934], raw copy origin 12800,10240)

| tick | R | ours |
|---|---|---|
| 907 | the op: face, fade out | matched |
| 910 | no-continue box "Kennith scrambles through the broken wall...", lock, copy of 43_51 at (20,27,1) | matched |
| 912 | `kennith_platform` at (15,25), cam_lookat (16,24) 100, cam_moveto (20,33) 900, fade in | matched |
| 913 | glide to (16,25), cameras at rate 3 | matched |
| 915 | `seaslug_controls_right`, crane `seaslug_crane_right`, `slug_crane_turn` delay 10, cam_moveto (13,24) | matched |
| 920 / 921 | glide (18,25) + `kennith_cutscene` / `seaslug_controls_left`, cast gone, `seaslug_crane2`, `slug_crane_turn_and_lower` | matched (+5, +1) |
| 928 / 931 | stage 11 + fade out / unlock at 2772,3291,1 | matched (+7) |
| 932 / 934 | fade in + "Down below, you see Holgart collect the boy..." / closed | matched |

## Other interactions

| interaction | R | ours |
|---|---|---|
| sea slug pickup [106] | 3 lines + "Ouch!" + `leech_suck` on the op, hit 3 at +1 | fixed (synth, the delay) |
| sticks drying [552] | dry sticks at once, `seaslug_player_drying_sticks` + `seaslug_drying_sticks` + `slug_dry_sticks` at 15, no message | fixed |
| torch lit [693] | three lines, `slug_torch_lit` | fixed (synth) |
| ladder with torch [707] | face L+1, anim, teleport + "The fishermen seem afraid of your torch." | fixed (the line's tick) |
| panel [805] | line, kick seq + synth; +3, +6 | fixed seq and synth; timing matched |
| crane at 7 [717] [726] | line, swing, `slug_crane_turn`, ~5 ticks busy | fixed (the distance gate kept) |
| fishermen [163]..[319] | "Hello there.", box "His eyes are staring vacantly into space.", the first npc page 3 ticks after the box's click | fixed text; timing matched |
| completion [994] [996] | stage+pearls+xp; "Thanks!"; "Thank you. Take care of yourself <displayname>."; on its click the line, jingle 154, scroll: "1 Quest Point", "7,175 Fishing XP", "Fishing Platform Access", "Some Oyster Pearls", model `seasluginv` | fixed; jingle 154 added to the recorded table |

## The journal

Recorded at stages 1 (4405 [2222]), 4, 6, 8, 9 and 11; every row of those is
ours (`seaslug_journal.rs2`). Older format: blank rows between paragraphs, no
leading blank. Jagex's quirks kept: stage 8 already strikes "I've created an
opening to let Kennith escape." and 9 reads as 8. 0, 2, 3, 5, 7, 10, 12:
LostCity's text in the recorded shape, [unverified].

## Open

- Unrecorded pages, all LostCity's [unverified]: Caroline's "No" and her
  in-progress and post-quest talks; Holgart without paste (stage 1/2), "I'm
  keeping away", "Okay, let's go back", stage 5 at Witchaven, post-quest;
  Bailey at stages 7..12; Kennith at 10 and 11; Kent at other stages; the
  other three fishermen themes (wiki transcript); the ladder smack without a
  torch.
- The quest point goes out with the scroll, two ticks after the stage
  (Jagex sends it with the stage at [994]); `~quest_complete_rewards` awards it.
- The rub's failure roll (`stat_random(firemaking, 64, 512)`) is LostCity's:
  2 of 2 recorded rubs at Firemaking 30+ lit.
- The damp-platform area is fitted to four recorded refusals (square 43_51,
  z <= 3304); tiles at its edges are unverified.
- The C selftest `::seaslugrun` was adjusted (the crane waits for the stage
  over 40 ticks, then closes the scene's box) but not run: no stanza filter
  exists and a full selftest sweep is out.
