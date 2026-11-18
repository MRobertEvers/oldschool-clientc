# Halloween 2026, "All Trick, No Treats" (Rimmington)

Source: RSProx index 4584 (rev 242.1, w407, 2026-11-17, "Halloween 2026"), the
whole event in one sitting, start to reward scroll. `--raw` for the scene's copy.
Branches no session reaches come from the wiki's `Transcript:2026 Halloween
event` and `2026 Halloween event` pages and are marked *wiki* below.

Content: `OSRS-Content/osrs-content/server/scripts/seasonal/halloween2026/`.
Test: `test/seasonal/halloween2026.lua` (scriptrun, 79/79 rows, 883 ticks).

## The switches: a lane, then a world event

**RETIRED 2026-11-18**: the lane is `default=off`. The cache taken that day
(OpenRS2 #2772) deleted the 32 trick-or-treater npc types and took the
jack-o-lanterns and the onion crate out of Rimmington's map. The event still
runs when its lane is on: the 32 npcs are the tree's own records now
(27410..27441, `server/scripts/seasonal/configs/halloween2026_cast.npc`,
identical to Jagex's field for field in a baked cache) and `~hw26_loc_sync`
places the locs.

Whether the event is in the build is its lane,
`OSRS-Content/osrs-content/ported/halloween2026/lane.ini`: `default=on` puts
its scripts, records and spawn rows in every build, `default=off` leaves all
three out (`server/scripts/seasonal/README.md`). The base tree's three calls
into it (`chemist.rs2`, `cooking.rs2` twice) answer "not mine" without it
(`lane_seams/halloween2026_seams.rs2`).

In a build that has the event, the world decides whether it runs:

| what | how |
|---|---|
| at boot | `TORIRSSERVER_EVENTS=halloween2026` (comma-separated list) |
| live | `::event halloween2026 on` / `off` (a cheat: staff-only on any password-checking world) |
| script | `world_event("halloween2026")` (opcode 11138), `world_event_set(name, on)` (11139) |
| npcs | `.spawn` rows under `==== NPC event=halloween2026 ====` stand only while it is on; a switch re-runs the static roster that tick |
| map locs | the cache of the recording placed the 8 jack-o-lanterns and the onion crate (#2772 does not); `~hw26_loc_sync` deletes them while off and puts them back while on, as players come within 40 tiles (`[mapzone,0_46_50]` + a 4-tick softtimer) |

Henja and Angel (`hw18_boy`/`hw18_girl`) were in the generated square file all
year; the event's `.spawn` suppresses those rows and stands them under the event
header. A `::event` switch lasts until restart; set the environment variable in
the world's unit to keep it.

## Action inventory (`rsprox_actions.py`), a verdict per row

| tick | action | verdict |
|---|---|---|
| 2 | welcome screen play | generic |
| 5 | Talk-to hw18_boy x8, hw18_girl x3 | **this content** (one conversation, fixed speakers) |
| 33..44 | Spook child 2, adults 3, 1 before stage 15 | **this content**: the two refusal lines |
| 46..162 | Smash jack-o-lantern 1..8 | **this content** |
| 107 | poordoor (2964,3206) | generic door; the route to jack-o-lantern 3 |
| 206 | worn glory Rub (Draynor) | generic jewellery |
| 229..273 | Diango Talk-to, banana hat, holiday items, trade | Diango's own content: **open** (no holiday retrieval in our tree) |
| 269..271 | wear the slime ghost set | generic equip |
| 284 | destroy banana hat | generic |
| 292 | Teleport to House | generic spell |
| 299..365 | Spook children 1..5, adults 3, 1, 4 | **this content** |
| 430..465 | Take-from crate of onions x12 | **this content** |
| 460 | poshdoor (2932,3214) | generic door |
| 464..552 | Chemist, Halloween menu, riddle 22 then 92 | **this content** |
| 559 | sugar on onion | **this content** ("I should melt it first.") |
| 585 | sugar on rimmington_poor_range | **this content** (cooking.rs2 hands it over) |
| 591..600 | molten sugar on onion x7 | **this content** |
| 604 | drop candy onion | generic |
| 613..712 | candy onion on adults 3, 1, 4, 2, 5 | **this content** |
| 737..771 | Talk-to, the scene | **this content** |
| 775..792 | Talk-to, the reward, event scroll close | **this content** |
| 797..806 | wear the vampyre set | generic equip (stance not checked: open) |

Teleports: glory to Draynor (generic), house (generic), the scene's copy in and
out (this content).

## Stages: `%varb16579_hw26_main` on varp 5996

Both hw26 varps are now `scope=perm, transmit=yes` (`halloween2026.varp`); every
hw26 varbit sits on them and the multilocs and multinpcs are picked on the client.

| value | written | recorded tick |
|---|---|---|
| 5 | the click on "I'll see what I can do." | [22] |
| 10 | the last jack-o-lantern's varbit, with the player's page | [170] |
| 15 | the click on Henja's Diango page, with the unlocks | [197] |
| 20 | the last child's flag, with the page | [383] |
| 25 | the click on Angel's "Oh come on!" | [423] |
| 30 | the riddle's count, with "Well done!" | [551] |
| 35 | the last adult's flag, with the page | [705] |
| 40 | inside the scene, with "Yeah..." | [761] |
| 45 | the click on Henja's reward page | [776] |

## Sections

### Jack-o-lanterns ([46..170])

O = the face tick (L+1); O+1 `human_unarmedkick`, map anim
`barbassault_egg_01_stomp_spotanim` (873) at the loc, delay 22, synth
`mory_bloodveld_splat` (4393) delay 22; O+2 the varbit. 8/8 recorded. Ours: the
same three rows on the kick tick and the varbit a tick later, 8/8 (tick log).
Before stage 5: "That wouldn't be very Halloween-y of me." *(wiki)*.

### Spooking ([299..383])

| | recorded | ours |
|---|---|---|
| refusals before stage 15 | mes on the op tick, children/people lines | same |
| the scare page | O: `emote_trick` (10503) + player page, continue cleared; line one of WoooOOOOooo! (chat_trance), Boo! (chatshock1), AAAAAAAA! (chat_baffled) | same, rolled |
| adult | O+1 "Grow up." (chatbored1, no continue); O+3 chat shut + "The adult doesn't seem particularly phased by your antics." | same |
| child | O+1 flag, chat shut, running child added | same |
| running child | `_running` npc: `human_running_60`, transparency 0->255 over cycles 60..120, a say, an exact move 5 tiles over 60 cycles, gone at +4 | same; lines: 3 recorded + 2 *wiki* |
| costume | the recorder wore 5 slime ghost pieces | three Halloween items worn *(wiki)*; refusal page *(wiki, mood unverified)* |

The running child's start is 4..5 tiles from the child, PAST the player on the
line child -> player, and it slides 5 more that way (5/5 scares, one session). Ours
rounds that line to the eight directions; the recorded angles 872/1616 come out
768/1536.

### Onions and sugar ([430..605])

Crate: face, then `human_pickuptable` + `hw26_onion` + synth `pick2` on O+1, one
per click (12 clicks, 7 onions: clicks inside one tick merge). Full backpack:
mesbox *(wiki)*. Range: O+1 "You melt the sugar into a sticky liquid." +
`human_firecooking`, the sugar turned molten in its slot. Molten sugar on an
onion: the line, the onion's slot becomes `hw26_candy_onion`, synth
`barbassault_egg_dipping` (3299); the molten sugar is kept. All matched.

### The Chemist ([464..552])

"Halloween. | Something else." under "What do you want to talk about?"; ten
pages; on the riddle's last click meslayer 108 "What is your answer?" and the
chat shut; the answer's page on the count's tick; the sugar, synth 12845
(`hw26_sugar_inv_add`) with the click on "Well done!". 22 -> "That's not
right! No sugar for you!" (chatlaugh1). The full-backpack and more-sugar
branches are *wiki*.

### Candy onions ([613..722])

Player line (3 recorded, 2 more *wiki*), "Oh, yes please!" (the adult turns to
the player a tick later), and on its click: the flag, the onion gone,
`sound_area eat` range 5 size 3 at the adult, the sick double added on the
adult's tile with `human_eat` and an instant face to the player; +3
`myq6_veliaf_throw_up_full_quick` + `vfx_veliaf_sick_01` at 32 + transparency
over 180..240; gone at +11. Ours: 5/5, the same ticks.

### The scene ([741..771])

| offset | recorded | ours (wire log) |
|---|---|---|
| T | fade out, blank messagebox | 843 |
| T+2 | lock (fov_clamp 3, rooftops held), into the copy at (40,13), Henja (20,10) angle 768, Angel (21,11) angle 512 | 845 |
| T+3 | Angel 2 west, camera (26,11)/300 at (11,11)/100, 232/100 | 846 |
| T+4 | camera to (23,11) at 2/2, fade in | 847 |
| T+7 | "They're all gone! <displayname>, you've saved our Halloween!" no continue | 850 |
| T+12 | camera (12,10)/300 at (11,6)/1 | 855 |
| T+16 | `toa_zebak_vomit02` at (16,11); camera (14,9)/200 at (16,11)/1 | 859 |
| T+20 | stage 40, `emote_think`, camera (19,10)->(21,10) at 1/1, "Yeah..." (chat_concerned) | 863 |
| T+24 | fade out, blank messagebox | 867 |
| T+26 | unlock, out to (2964,3213) | 869 |
| T+28 | fade in | 871 |

### The reward ([775..792])

Henja's page; on its click four "You've received a holiday item" lines, stage
45, the four vampyre pieces, 2 each of noted pumpkin and red/green/blue h'ween
mask, jingle `quest_complete_1`, eventscroll 807 with the recorded title,
models (candy onion 400, pumpkin lantern 600), rewards and farewell. Not enough
room: mesbox *(wiki)*. After: "Thanks again for your help! You're such a nice
stranger." *(wiki)*.

## Open

- **Earlier Halloweens' rewards at stage 15** ([197]): 61 "collect it from
  Diango" holiday items, the Deathly Mansion wallkit, four spider-hat variants
  and six music tracks need Diango's holiday-item retrieval (interface 78,
  `rareitems_diango`), which this tree does not have. Only the emotes (Scared,
  Zombie Hand, Hypermobile Drinker, Trick) and the skin colours are given.
- Diango's banana-hat gift and his "holiday item back" menu ([229..268]).
- The vampyre set's own stance (`[bas]`), never read from a `--bas` transcript.
- npc ids 16772+ are above 16383 and reach the client through CHANGE_TYPE; not
  photographed on the live client.
- "snuck right up behind them": no direction rule is recorded (scares from
  every side worked); none is coded.
- One session: every rolled line and the running geometry are one roll each.
