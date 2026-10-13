# ToB room transitions: every raider walks

Owner, 2026-10-12: "Why do the non-leader seats idle in the raid room after each room? They
should not - they need to run to the next room and click the door, or go to the chest then click
the door. They should not just appear in the next room - that is not how real tob works."

## What was wrong

- `tob_relay.lua`: only seat 1 walked the gate and the passage. Seats 2..5 went through the gate
  only at Bloat and Sotetseg (the chest), then waited at party barriers.
- `tob_raid.rs2`: the passage (`~tob_exit_walked` / `~tob_exit_passage` -> `~tob_advance_room`)
  built the next room, and `~tob_carry_party` teleported every other raider still in the old room
  into it, with the room's title card and music. So the members arrived in rooms they never
  walked into.
- A second bug showed up once members walked themselves. The raider whose room watchdog declared
  the boss dead was never armed again. That raider's walk into the passage did nothing, and the
  relay's fallback click, 40 ticks later, was what took them through. This hit the leader after
  every room except the Maiden, before and after this change: baseline `vf19_rl` f1 had
  `bloat.barrier.chest` at t1059 and `bloat.passage` at t1100.

## The real rules (sources under `sources/` unless noted)

| Question | What the sources say |
|---|---|
| How raiders get between rooms | Each raider goes through the way onward themselves. Jagex: "the area you click to enter the next challenge room is rather large and can often be misclicked" (`newsposts/wiki_Update_Theatre_of_Blood_Feedback_Tweaks.wikitext:33`), and "The click area of the challenge exit has been reduced in size" (`newsposts/wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:68`). Guides: "do not click the doorway there's a big chest here" (`transcripts/yt_9MPHZy4sjmM.md:44`). The vtob bot clicks "Formidable passage" op 1 after each room (`vtob/TheMaidenofSugadinti.java:41`, `vtob/PestilentBloat.java:70`). |
| Raiders left behind | They stay in the old room: "If you're attempting to start a Theatre of Blood wave, but some of your party are still in the previous wave's arena, the warning screen will now make this more explicit" (`newsposts/wiki_Update_Phosani_s_Nightmare_and_HiScores.wikitext:84`; `wiki_Theatre_of_Blood.wikitext:378`). Blert: "Players may leave between rooms" (`blert_plugin/RoomDataTracker.java:88`). Only outside spectators are moved for you: they "will be teleported from room to room as the party advances" (`newsposts/wiki_Update_The_Theatre_of_Blood.wikitext:68`). |
| Rooms are separate regions | Maiden 12613, Bloat 13125, Nylocas 13122, Sotetseg 13123, Xarpus 12612, Verzik 12611 (`blert_plugin/Location.java:48-61`, `advancedraidtracker/TobIDs.java:32-39`). A passage is a region change per player. Blert sets up a room tracker per client "When entering a new instance for the first time" (`blert_plugin/TheatreChallenge.java:313`). |
| The barrier starts the room | "In all of the rooms before Verzik, a barrier separates the team from the boss, and the fight only begins once a player has crossed the barrier" (`wiki_Theatre_of_Blood_Strategies.wikitext:536`). Clicking it opens a start prompt: "click the barrier to bring up the start menu, then click to enter" (`blert_guides/tob_plugins_content.mdx:49-52`). vtob clicks barrier 32755, waits for the dialogue, then presses 1 (`vtob/TheMaidenofSugadinti.java:37-40`). |
| Entering after the start | Allowed, at least at Bloat. "To start the fight have one raider enter the room" and "As soon as Bloat deactivates, the rest of the team should enter" (`wiki_Theatre_of_Blood_Strategies.wikitext:695,697`). "Player A has entered the room late" fails a task; it does not lock anyone out (`wiki_Personal_Space.wikitext:21`). The sources show no lockout. |
| A boss acts before the start | Bloat walks his laps before anyone crosses: "To start the fight have one raider enter the room when Bloat is on the opposite side of the pillar" (`wiki_Theatre_of_Blood_Strategies.wikitext:695`), and vtob presses "1" only when Bloat is in the south area (`vtob/PestilentBloat.java:81-84`). Advanced Raid Tracker flags an entry "inaccurate" if varbit 6447 is already non-zero when Bloat spawns (`advancedraidtracker/BloatHandler.java:219`). |
| Chests | "Accessible twice per raid — once after killing the Pestilent Bloat, and once after killing Sotetseg" (`wiki_Chest__Theatre_of_Blood_.wikitext:20`). Points are per player. Each raider walks there: "pass through the barrier to the west and open the treasure chest by the entrance" (`transcripts/yt_M1t2qWMbzEs.md:43`); "run back south through the barrier and open the treasure chest" (`:61`). |
| Xarpus | He has a barrier with the same start menu (`blert_guides/tob_plugins_content.mdx:49-52`; Strategies:536 covers every room before Verzik). Advanced Raid Tracker: "Xarpus timer starts when player crosses into region" (`advancedraidtracker/XarpusHandler.java:144-147`). |
| Verzik | "To initiate the fight, a player must first talk to Verzik Vitur" (`wiki_Verzik_Vitur.wikitext:388`). "Search the skeleton by the door to pick up the Dawnbringer... Then enter the boss room. The fight doesn't start until you speak to Verzik" (`transcripts/yt_M1t2qWMbzEs.md:69`). |
| Dead raiders | "placed outside the fighting area, and can only proceed if other team members succeed" (`wiki_Theatre_of_Blood.wikitext:75`). This tree releases them at the room's entry (`tob_spectate.rs2 ~tob_spectate_tick`), and they walk out with the party. |

Not in the corpus:
- the warning screen's wording;
- whether only the leader may start (that is the owner's rule from 2026-10-06, in `tob_party.rs2 [oploc1,tob_arena_barrier]`);
- whether a raider outside Verzik's room can still get in after she starts;
- any loading screen per passage.

## Decision: does the barrier click start the room?

Yes, and it already did: the leader's barrier click and answer run `~tob_start_room`.

`::tobsyncroom` (and `::synctimers`) stays. It is not a start. It is a start-state sync behind the
`<room>_ready` party barrier, and every seat now reaches that barrier by walking to the room's
door. It is needed because a boss is live before the start in real ToB too: Bloat walks his laps
(vtob, ART above). The state he is in when the leader clicks therefore depends on how long the
party took to walk in, and that differs between lanes. Respawning him as the build made him, on
one tick, is what makes the click land on the same state on scriptrun and live (memory "sync the
room, never time the start").

## The change

Content (`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob`):
- `configs/tob.constant`: slot 87 was `^tob_var_transition`. It is now `^tob_var_next_handle`: the
  next room's instance, written on the room being LEFT.
- `scripts/tob_raid.rs2`:
  - `~tob_build_room` carries nobody. It counts who is still in the old room before its own
    arrival. If it is the last one out, it frees the old room. Otherwise it writes the new handle
    into the old room's `^tob_var_next_handle`.
  - `~tob_carry_party` is replaced by `~tob_follow_room`: one raider's own arrival in the built
    room (the arrival, the card, the music). The last raider out frees the old room.
  - `~tob_advance_room` is per raider. The guard is the raider's own pending
    `tob_transition_build`, and every raider gets their own blood spread.
  - `[queue,tob_transition_build]` builds the next room if nobody has yet, and follows otherwise.
    If the room it would follow into is gone, the raider leaves the raid.
  - `~tob_watch_room` re-arms the watchdog after `~tob_room_cleared` for rooms 1-5, so the raider
    who noticed the death can walk out.

Relay (`script/plugins/quest_driver/tob_relay.lua`):
- Every seat walks out: the gate, the chest after Bloat and Sotetseg, then the passage. Xarpus'
  north gate (34,43) is now in `GATE`/`OUT`.
- There is no `_chest` barrier. The next room's `_arrived` barrier gathers the party.
- `walk_to_door` takes every seat from the corridor's far end to the room's entry tile
  (`^tob_<room>_entry_lx/lz`) before `<room>_ready`.
- `into_arena`: a seat let out of Bloat's cage at x>40 first crosses back in.

Shared start helpers (`script/plugins/quest_driver/tob.lua`): the members' crossing and the
leader's barrier click now pin the copy of the barrier nearest to the raider
(`QD.raid._tob_barrier_copy`) instead of "the nearest copy of the id". With rooms coexisting,
Bloat's barriers stay in the scene window. The id-only pick prefers the walker's own level, and
the Nylocas barrier is a bridge loc (cache level 1, walked on plane 0). Every member's press went
to Bloat's barrier, 64 tiles away and already freed, and the server dropped it (probe `walk p1`
f1: "nylocas_v2_solve: never got inside the room in 300 ticks").

## Not done / open

- The start prompt's "some of your party are still in the previous wave's arena" warning. The
  wording is not in the corpus.
- `test/raids/_play_normal.lua` (NOT KEPT) still walks only its leader out, so its members now stay
  behind.
