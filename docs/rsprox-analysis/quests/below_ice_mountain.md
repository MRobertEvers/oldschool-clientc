# Below Ice Mountain — RSProx analysis

Source: **RSProx index 4374** (2026-10-25, rev 241.1, world 631): the whole quest, start to
finish, in one session (ticks 0..1937). Transcripts: `rsprox_index.py get 4374` (default),
`--raw` (instance tiles), `--bas`. Every bracketed tick below is 4374's.

Ours: `OSRS-Content/osrs-content/server/scripts/quests/quest_belowicemountain/`
(`belowicemountain.rs2` the talks, `bim_cutscenes.rs2` the scenes, the guardian and the end),
test `test/quests/belowicemountain.lua`.

Before this rebuild the quest was built from the wiki transcript and Quest Helper's step list:
no scenes at all (each was "dialogue as chat"), the guardian fought in the public hall, a bag
the player had to click, the stage writes on the wrong pages, every page neutral, and several
branches the recording contradicts (below).

## Stage map (as written, on the page it is written on)

| varbit | value | tick | where |
|---|---|---|---|
| bim 12063 | 0→5 | [400] | Willow: after "Yes." to *Start the Below Ice Mountain quest?*, before "A chance to uncover..." |
| | 5→7 | [404] | before Willow's "We had a falling out after a, uhm... dig, went wrong." |
| | 7→10 | [415] | before the player's "You have a rather interesting group of friends..." |
| | 10→15 | [831] | Checkal recruited (with checkal 40) |
| | 15→20 | [1322] | Burntof recruited, the last of three (with burntof 40) |
| | 20→25 | [1503] | Willow at the entrance, before "Are you ready? Who knows what's inside the ruins?" |
| | 25→30 | [1624] | in the tripwire copy, Marley's "That got it!" clicked (all three sides 40→45) |
| | 30→35 | [1726] | the guardian awake in the reveal copy (all three sides 45→50) |
| | 35→40 | [1822] | the guardian's despawn |
| | 40→120 | [1875] | the scroll (with bim_claimed_reward 0→1, the 2,000 coins) |
| bim_checkal 12065 | 0→5 | [479] | after "If you can survive his rigorous training program..." |
| | 5→10 | [538] | before Atlas's "No price is too much for a good workout..." |
| | 10→15 | [750] | in the workout scene, with **emote_flex 12062 0→1** |
| | 15→20 | [799] | after "Now, show me your muscles...", before the mesbox |
| | 20→40 | [831] | the departure frame (C+3 after the last click) |
| bim_marley 12064 | 0→5 | [916] | before "Why don't you go and get one then?" |
| | 5→10 | [1087] | the cook: before "And hey presto..." |
| | 10→35 | [1163] | "Pass it over." clicked: the objbox |
| | 35→40 | [1182] | the departure frame (C+3) |
| bim_burntof 12066 | 0→5 | [1206] | after "Are you Burntof?" |
| | 5→10 | [1234] | the drink USED on him (opnpct), the objbox |
| | 10→15 | [1259] | after "Hmmm... *hic*", before the game is offered |
| | 15→40 | [1322] | "Are you okay Burntof? ..." clicked |
| camdozaal_ramarno_intro 12068 | 0→1 | [1922] | Ramarno leaves under a fade |
| musicmulti_21 2950 | bit 2 | [544] | Barbarian Workout, on the workout's lock tick |
| | bit 0 | [1629] | The Ruins of Camdozaal, entering the reveal copy |

## The talks: what the recording says that the wiki build had wrong

- **Moods**: every page's `if_setanim` (`rsprox_pages.py`), copied page by page.
- **Menu headers**: "Start the Below Ice Mountain quest?", "Would you like to start your
  training session?", "Head into the ruins?" (each `chatbox_multi_init`), not "Select an option".
- **Checkal before the quest** [320]: mesbox "Checkal stares at you intensely.", "You best not be
  here to start any trouble, sir.", "Wouldn't dream of it." (ours said "Busy training.").
- Checkal "That would be me, sir." / "Stop right there, sir." (commas), "help anyone who shows that
  much dedication" (not "a man who"); Marley "Sorry, pal," and "Yes sir I am."; the player's
  "Well, Ramarno, I'm <name>, and you have me...".
- **Checkal flexes every 20 ticks** on a global clock ([316], [336], [456], [476], [496], [796],
  [816]) and faces west on every add (`[facing] coord=` 3086,3415). His answer to the player's
  flex comes 3 ticks after it starts ([817] → [820]).
- **Burntof before the quest** [153], [176]: "Buy ush a drink." / "I think you have had enough." /
  "Pleash, buy ush a drink.". **A drink is used on him** (opnpct): before the quest he drinks it
  anyway ([182] objbox with the ale's model, "Ah, cheersh friend."); at burntof 5 it is the
  quest's drink ([1234] mind bomb: "No messhing around with you! Only the strong stuff, I like
  it!"). Ours gave the drink by talking.
- **The cook** opens straight on the menu (no "What do you want? I'm busy!"); "Can you sell me any
  food?" sells a cabbage for 1 coin ([966]..[971]); "free food" is "Can you give me any free
  money?" ([1022]); "horrible kitchen" is "How dare you? ..." ([1034]). Ours had invented
  one-liners for all three.
- **The sandwich** [1133]→[1134]: one tick after knife-on-bread, `bim_sandwich_make` (8938), the
  meat and bread out, the sandwich in, "You combine the cooked meat and bread to make a Steak
  sandwich!".
- **Marley eats** [1164], [1166], [1168]: `human_eat` + synth `eat` (2393) three times, two ticks
  apart, under a blank chatbox; the next page six ticks after the objbox click.
- **Recruit departures** are fade frames. Checkal [828]→[836]: click C, C+1 black + chat closed,
  C+3 the stage (his multinpc goes invisible), C+5 the black lifted, C+6 the player's line, its
  click +1 the overlay shut. Marley [1179]→[1186]: the black ON the click, C+3 the stage and the
  lift, C+5 the line.
- **RPS signs** (one roll each): round 1 Burntof shows the sign that BEATS the player's and claims
  he showed the one it beats (paper → "Burntof chose scissors." → "I put out *hic* rock"); round 2
  he shows the one the player's beats and claims they showed the one his beats (scissors → paper
  → "you chose rock"); round 3 he shows the winner and the player's hand changes to the sign that
  beats his (rock → paper → scissors). Ours had round 1 backwards. After each menu click: the
  player `say("Rock, paper, scissors!")` + `rps_human`, Burntof `dwarf_rps` + "Rock, paper, uhm...
  scissors!", a blank chatbox, the result mesbox 5 ticks on ([1273]→[1278]).
- **No bag to click**: the bag is a scene after the guardian ([1835]..[1852]), and the quest
  completes on the scene's last tick ([1875]); the scroll's model is the steak sandwich (zoom 400,
  78,128) and its rows "2,000 Coins" / "Access to the Ruins of" / "Camdozaal".
- **Ramarno** is walked to and talked to as the scroll is shut ([1880]: the player runs to him,
  facing him); after his last page a mesbox with nothing to click under a fade [1920], his
  intro varbit [1922] (the multinpc goes), the black lifted with the box again [1923], its click
  closes both [1925].

## The scenes

All five lock with the frame `bim_cutscenes.rs2`'s header describes (identical to Misthalin
Mystery's: fov_clamp, cutscene_status, ops hidden, minimap 2, gravestone, area sounds 25, draw
distance; cam_shake ×5 zero, xp drops and sides 0–6, 11, 12 closed, `highlighting_off`,
`minimap_toggle 2`). Jagex also re-sends the friends/account/logout/music panels that are already
open (side 8, 9, 10, 13); ours does not (engine-generic, harmless).

| scene | ticks | copy | notes |
|---|---|---|---|
| Atlas's workout | [541]..[775] | one 13×16-zone copy: Barbarian Village (template x 3000..3079, z 3408..3439) unturned on zones (3..12, 0..3); Varrock (x 3200..3255, z 3392..3471) **turned a quarter clockwise** on zones (0..9, 9..15) | six timed pages (no continue, `if_setevents -1..-1 []`) under a montage: jog, star jumps, run, a tomato, push-ups 1..9, ten punches, a long run, push-ups 91..100, a sprint to the guards, a white fade; then four clickable pages and the flex |
| Rock paper scissors | [1262]..[1349] | the Rising Sun's square (46,52), upstairs | Burntof and Tina cast; three rounds |
| The excavation | [1513]..[1600] | the entrance's square (46,54) | the blast (three `barbassault_egg_explosion` locs, 2 ticks each), the rubble, Checkal's three loads |
| The tripwire | [1600]..[1626] | the hall's square (46,90), first copy | Marley's lines, the player steps over the wire (`regicide_stepover` + exactmove) |
| The ruins revealed | [1628]..[1747] | the hall again, a **fresh** copy | every page is sent twice: first with nothing to click, then clickable two ticks later; the quake; the guardian wakes; the crew runs |
| (the fight) | [1750]..[1822] | the same copy | see below |
| The bag and Ramarno | [1828]..[1872] | the same copy | then out to the real hall (2950,5769) |

**The turned copy.** The default transcript's tile translation inside the turned Varrock zones is
not rigid (two npcs one tile apart on the wire land 13 tiles apart in the template), but the zone
→ template-zone pairing is (every observed zone, scratch `zonemap.py` pairing the line-aligned raw
and default transcripts). A rigid layout needs the client's rotation 1, `^map_instance_turn_cw`
here (`ToriRSServer_MapInstanceRotateToDst` rotation 1 = (z, 7 − x), the client's). So the scene is
written in RAW copy offsets (`rsprox_cutscene.py --origin 10688,13504` on the `--raw` transcript)
and only the zone layout comes from the template.

**The real hall is the finished state.** The map ships the hall with the big doors open, the four
pillars broken and Willow's bag by the passage, and 4374 never stands in it before the scroll.
Every step before 120 is in a copy; the reveal's copy is the one the guardian is fought in
([1628]..[1872] on raw 10304..10319,14848..14871).

## The Ancient Guardian ([1750]..[1822])

- Added on the rubble's tile (2952,5778 template) facing south [1750]; walks straight for the
  player from [1751] at nine tiles (no hunt range: `npc_setmode(opplayer2)`).
- Swings every **6** ticks, 10 of 10 ([1759], [1765], [1771], [1777], [1783], [1789], [1795],
  [1801], [1808], [1814]): `ancient_guardian_attack_melee` (8942), synth 4989
  `bim_ancient_guardian_melee_attack`; the player's block 511 / hit 521 (delay 20) and
  `human_sword_def` are the engine's. Our block said 5 (from the wiki); the cache's 6 stands.
- One unprayed hit, 7 ([1771]); five zeros unprayed, then Protect from Melee. Max-hit check: the
  cache's 33 Strength, +50 → 7. (n=6 unprayed: not enough to fit accuracy.)
- **The pillars** (4 of 4 mined, no player hit on it at all): the swing on the op (spam message
  "You swing your pick at the pillar." + the pickaxe seq; on [1759] the guardian's swing the same
  tick replaced the seq with the defend), the pillar down **7** ticks after the swing ([1759]→
  [1766], [1794]→[1801], [1807]→[1814]; 1 of 4 re-clicked in place after a lobster took 8: [1777]→
  [1785]); on that tick the player's seq cleared, `rockfall` (60) on the guardian (height 10,
  delay 20), the loc swapped to `bim_boss_rock_mined`, `sound_area roof_collapse` (1384, delay 35,
  range 10) at the guardian's tile, `cam_shake` axes 0..2 with random 5..8 (8/6/5, 7/8/8, 5/6/8,
  5/5/8); +1 the guardian `say "Grrr!"`; +2 its hit, from nobody (`hitsplat 17` damage_other,
  `sound_area 4971` defend, range 10): 10, 12, 12, then the 6 it had left (40 hp: the bar
  30→22→14→5→0).
- Death [1817]: `ancient_guardian_death` (8943) the tick after the last hit, `type=reset`,
  `enabled_ops 0`, `sound_area 4985` (death, range 10); despawn [1822] (5 ticks).

## Wire audit

Ours is `test/quests/belowicemountain.lua`'s tick log (run on private packs; the scratch differ
`scene_diff.py` matches each recorded event — player/npc seq with delay, npc say, spotanims with
height and delay, map anims, synths and area sounds with delay and range, loc changes,
projectiles, songs, the quest's varbits — to the nearest of ours with the same signature, ticks
relative to each section's anchor). A dialogue-paced section drifts by however much faster our
driver clicks than the recorder; the drift is only ever between a page's click and its answer.

**Atlas's workout** (recording [541]..[775], ours 102..336, our lock 105 = [544]): every event of
the montage on the recorded tick, +0: the cast's adds and dels (hobo, rat, Aubury, two men, the
woman, Baraek, the barbarians, Atlas ×3, the dog, three guards), `emote_run_on_spot` +8,
`emote_starjump_5` +12, the tomato +51 (launch spotanim h96, `human_throw`, "Catch!", `thrown`
r5, `rotten_tomato_travel`, `rotten_splash` d81, `grease_hit` d81 r5), push-ups +60, Atlas's
count 1..9 with a cheer every third +62..+78, ten punches +81..+99 (`human_unarmedblock`,
`human_unarmedpunch`, `2h_slash`), push-ups 91..100 with a nod every third +134..+155, the
guards, `emote_cheer` d2 +200, checkal 15 and `emote_flex` 1 +209. After the first clickable page
ours runs 3..5 ticks ahead (our clicks). Recording-only: `clock_tick_1` and
`tol_homonculus_crying` (the recorder's stamina, kit), the player's "Eugh..." (our tick log has
no player-say row; the script sends it), the long hall's own Atlas leaving view (zone), the
region track on the way out ([773], ours the same region change).

**Rock paper scissors** (recording [1262]..[1349], ours 482..545, lock 485 = [1265]): the lock,
Burntof's add, song 15; each round's `rps_human` + `dwarf_rps` + "Rock, paper, uhm...
scissors!"; Tina's add, `liquid`, the dels; recorded order kept, the deltas our menu clicks.
Recording-only: the player's say (no tick log row), a door's zone re-send on the way out.

**The excavation and the reveal** (recording [1513]..[1747], ours 558..740, lock 561 = [1516]):
91 matched, every recorded event present in order; up to the blast +0/−2, the reveal's tail
runs up to 40 ticks ahead (our driver clicks each of its doubled pages as soon as it can). The
blast exactly as sent: explosion loc +0, +2 the second with the first gone, +4 all three, +6
none ([1552]..[1558]); the door and rubble +10; Checkal's three `human_pickupfloor`; the tripwire
locs, `human_pickpocket`, `regicide_stepover` d20, the trap synths; the reveal's props, cast and
song 691; Burntof's four says; `lever`; the quake's ten rockfalls in three volleys (5 at the
anchor, 3 four ticks on, 2 after Willow's page, every delay as sent: 160/0/180/50/90, 0/50/90,
50/0); `ancient_guardian_awaken` with its synth; bim 35 and the three sides 50.

**The guardian** (recording [1750]..[1822], ours 743..834): see the section above; every rule
held on our wire — add, walk from the next tick, first swing +9, swings 6 apart, swing synth,
pillar +7 after the swing (3 of 3), rockfall spot h10 d20, mined loc, `roof_collapse` d35 r10 at
the guardian, "Grrr!" +1, the type-17 hit +2 with the defend area sound r10, the death seq +1
after the last hit with the death area sound r10. Ours-only: the test player's auto-retaliate
punches (the recorder never hit it; the account's setting). **Fixed**: the despawn was 2 ticks
after the seq (generated `death_delay=2`), recorded 5 — the ending started 3 ticks early.

**The bag and Ramarno** (recording [1822]..[1925], ours 831..940, anchored on bim 40): the
guardian gone with the stage, the lock +6, song 691, the exit's cutscene loc +7, `emote_think`,
"Hmm..." with `generic_look_and_search`, `human_pickupfloor`, the doors (right added, left
removed and re-added), `gate_open` + `bim_dungeon_door_open`, Ramarno's cutscene add and del,
the real hall's Ramarno in view, bim 120 with song 147 and the jingle; then the walk to Ramarno
and his talk (the test's `ramarno` leg: the intro varbit written).

**Every page.** The test dumps every dialogue echo and mesbox line per leg (`t.chat.lines`,
ledger rows `pages.*`); aligned with all 342 of 4374's from [380] on (scratch `page_diff.py`,
the recorder's name and ours both written `<name>`): **334 equal**, word for word and in order.
The other eight: the recorder clicked a locked emote ([808], the Halloween line: engine, not the
quest), bought a second drink ([1217]..[1226]) and broke off a talk with Willow to speak to Burntof
([1473]) — and Willow's [411] page, whose text is equal but whose row break is not: Jagex breaks
"...a real strongman<br>- someone with muscles", our shared `split_init` breaks after the " -"
(the echo shows it as a double space). That is the engine's wrap rule for every chat page, not
this quest's (open, below). Fixed on the way: the Rising Sun barmaids' sale
(`areas/falador/scripts/barmaid.rs2`, a LostCity port: "Hi, what ales are you serving?" / "That'll
be two gold.") is rev 241's now — "Heya! What can I get you?", "What ales are you serving?", "...all
for only 3 coins.", the coins and the drink on the choice's click with a spam line ("You buy an
Asgarnian Ale." / "You buy a Wizard's Mind Bomb."), "Thanks, <barmaid>." ([165]..[173],
[1212]..[1218], [1224]..[1229]).

**Npc blocks** (`npc_block_inventory.py --values`, every type the quest touches): every say,
seq, spotanim and `enabled_ops` value the recording sends is ours. Fixed: the montage's hobo and
man turn to angle 0 a tick after their add ([546]); Ramarno turns to the player on his first
page ([1886]). Explained: Checkal's 20-tick flex runs in ours world-wide (the recording sees it
only while he is in view), the guardian's two extra headbars and swings are the test player's
auto-retaliate. **Multinpc stances**: the six shells that state `human_ready` over a rung with
none (`multinpc_stance_audit.py`) stand in `human_ready` on our client, read with `t.npc.pose`
in the test (Willow ×2, Checkal ×2, Marley ×2).

**Movement** (`npc_movement_audit.py paths`, and the test's own per-tick player track through the
workout, ledger row `atlas-workout.path`):

- **The player's whole montage path is the recording's, tile for tile and tick for tick** (the
  walk to (10,84), the runs to (36,84) and (37,105), the run west (98,9) → (64,9) → (38,21), the
  sprint (7,116) → (75,115); copy offsets). Two fixes got it there. A route to a tile outside the
  104-tile scene the server built where the player landed finds no way (the scene re-centres only
  as the player nears its edge), so the two long sprints stopped dead at (64,9) and (25,116); and
  our pathfinder takes its straight step before its diagonal where Jagex's took the diagonal
  first. Each long run is now walked in legs ending on corners of the recorded path, every end
  inside the current scene.
- **Every scripted npc step on its recorded tick.** A scene script resumed by `p_delay` runs after
  the npcs have moved, so an `npc_walk` from it stepped a tick late (the rat [561] → ours 562,
  every one of the cast). Every such walk is now issued on the tick before its first step (38 of
  them); the rat's 13 steps and the woman's 2 land on exactly 4374's ticks.
- The three palace guards at the sprint's end did not move at all (stuck: their tiles lay outside
  the scene until the player reached them); now 4/4, 4/4 and 6/6 steps with the recorded gaps.
- Checkal (cutscene) 11 steps and Tina 4 with identical gap histograms; the rat 13/13; Burntof's 22
  the same count (the reveal's gaps follow our faster clicks); the guardian 25 of 27 with the same
  gaps (the rest is where our test player stood).
- The static world npcs are static now (Willow, Checkal, Burntof, Ramarno: 0 steps in 4374; the
  generated default would wander them). The crew at the entrance and Atlas wander, as recorded.

**Music.** Run 1 showed the region track starting over the scene's song as the player entered
each copy (`music 125 region` after `689 script`, `141` at the jump back to the village): fixed
with `midi_hold` for the length of each lock.

## Open

- **Cameras** are checked only by the test's `t.cutscene.await` rows (the first moveto/lookat
  heights and the reset of each scene); our tick log has no camera rows and the packet trace
  carries 48 raw bytes, so a per-tick camera diff is not done.
- **The player's says** ("Eugh...", "Rock, paper, scissors!", "Aaah!", "Hmm...") have no tick-log
  row; they are in the script on the recorded ticks but not seen on our wire.
- **The chat wrap** (engine, every quest): Jagex breaks "...strongman<br>- someone", our
  `split_init` breaks after the " -" ([411]).
- **Spawn angles** of the cast are not set where no face follows the add (the montage's
  pedestrians, Tina).
- **Dialogue-paced events after a click** (an npc walk on the click's next tick in the excavation,
  Tina's first step) follow the click, which the driver makes at its own pace; they are placed on
  the click tick or one after as the recording shows, not measured.
- **Re-entries and the logout backstop** (Willow's door at 30, 35 and 40 after a scene was cut
  short; `~bim_on_logout`) are this build's own; no recording shows them.
- **Atlas's paid repeat sessions**, the refusals, the post-quest crew in Nardah and the finished
  hall's bag are the wiki transcript or the earlier build (unverified, labelled in the script).
- **No photograph** of the scenes has been taken on our client (the turned Varrock zones above
  all): the copy's layout is inferred from the zone pairing and confirmed only by the player's
  and the cast's paths being walkable where the recording walked them.
