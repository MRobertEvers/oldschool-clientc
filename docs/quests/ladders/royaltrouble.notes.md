# Royal Trouble -- what the ladder cannot know (driven 2026-10-02, vm-parity1)

Where things really stand
- Sailor: the guide says 2578,3845; he stands at 2581,3847 (areas/world/configs/m40_60.spawn:12).
- Ladder down: stand on 2509,3847 (maplink row src); it lands on 2509,10245 in the dwarf village.
- Armod and the four others wander around 2571-2573,10277-10278: walk near, then talk.
- Giant Sea Snake: the guide's tile 2615,10280 is inside the pool, which the cache flags blocked
  (maps/m40_160.jm2 `f1`, the real quest instances it). Port: body on the shore, 2613..2617 x
  10276..10280 (configs/royaltrouble.constant ^royal_snake_coord). Melee works from the shingle
  (2610-2618 x 10274-10277); the heavy box drops on 2613,10276 and lasts 5 minutes.

Barriers between steps
- Village -> lift room: crevice 2505,10281 needs the mining prop USED on it (royal_dungeon.rs2:202),
  then Squeeze-through (Agility 40, 1 xp). Back is the crevice at 2505,10282, no level.
- Lift: Use-Lift only at varb2146=7; up lands on plane 1 east of the platform, down on plane 0.
- Plank room -> path: tunnel 2511,10287 plane 1 (Agility 40). Beams and pulley beams are thrown
  away at the hole (royal_dungeon.rs2:467) -- finish the lift, do not carry spares through.
- Rope swing: the rope goes on the rock at 2539,10299 (royal_ropeswing_multiloc), not on the static
  royal_ropeswing_mid at 2539,10296; swing lands near 2543,10299 (skill_agility/rope_swings.rs2).
- Slippery rocks 2548,10288 / 2545,10287 / 2542,10287 / 2539,10286: stand beside, use the plank;
  stepping on one costs 8 every 2 ticks. Plank is kept.
- Crevice 2585,10260 (to the kids): needs all five pages found AND the diary read (varb2153).
- Crevice 2617,10272 (to the snake): Agility 40. Rope home 2618,10265 only once the snake is dead.

Dialogue choices that gate
- Ghrim: "Has anything been happening in the kingdom recently?" then "Yes."; later "King Vargas asked me to talk to you."
- Vargas: "Right away, your Majesty." Sigrid: "Of course, it's my duty." Sailor: "I'm looking for a sailor...".
- Donal: "Of course. Dealing with monsters is what I do best!". Any one citizen per island completes an interview.
- Sigrid with a lost box sends you to the guard by the surface hole (2619,3865), who returns it.

Hazards (royal_cave.rs2, timer armed by [mapzone] on map squares 39_160 and 40_160)
- Steam vents block walking in the cache, so they cannot be stood on: the plume hits the four
  tiles beside one (royal_cave.rs2:113). Falling rocks: within 1 tile of royal_rocks_1 at
  2563,10254 2567,10248 2570,10257 (royal_cave.rs2:133, the guide gives no tiles). Both cost one
  tenth of base Hitpoints per hit (the wiki says "depending on level"); the rocks cost 8.

Differs from the guide
- Fight: no instance; one snake for everyone (royal_shared.rs2 royal_spawn_snake).
- Brand/Astrid scene and the kids' fire scene play as plain pages, no camera (cutscene spec pending).
- No friend-vs-romance flag exists in the pack: lines say "dear".
