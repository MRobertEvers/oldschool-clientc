import os,sys
W="/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid"
OUT=W+"/docs/minigames/theater_of_blood/encounters/sotetseg.tsv"
BS="sources/blert_api/spec_pass_sotetseg/blert_sote_summary.txt"
OM="sources/blert_api/spec_pass_sotetseg/ours_measurements.txt"
CS="sources/blert_api/spec_pass_sotetseg/cache_seq_lengths.txt"
WIKI="sources/wiki_Sotetseg.wikitext"
STRAT="sources/wiki_Theatre_of_Blood_Strategies.wikitext"
GUIDE="sources/wiki_Guide_Advanced_Theatre_of_Blood.wikitext"
ENTRY="sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext"
HARD="sources/wiki_Theatre_of_Blood_Hard_Mode.wikitext"
NP="sources/newsposts/"
rows=[]
def r(mid,qty,val,unit,tags,grade,refs,closes,tol): rows.append([mid,qty,val,unit,tags,grade,"; ".join(refs),closes,tol])

# ---- cadence and openings
r("sotetseg.cadence","his attack period, every mode (ticks between consecutive attack animations, melee or ball, outside a death ball and a maze)","5","ticks","[cache][blert][wiki][plugin]","A",
  ["sources/cache_npc_attackrates.txt:695","sources/cache_npc_attackrates.txt:716","sources/cache_npc_attackrates.txt:737",CS+":attack_melee",BS+":SUMMARY","sources/blert_plugin/SotetsegDataTracker.java:58",WIKI+":29",OM+":SOLO_NORMAL_CADENCE"],"-","exact")
r("sotetseg.first_attack","first attack after the fight starts (blert tick 0 = room start; ours is counted from ::tobgo = the room start, which reads 5)","6","ticks","[blert]","B",
  [BS+":SUMMARY",OM+":SOLO_NORMAL_CADENCE","TOB_RESEARCH.md:332"],"-","+-1")
r("sotetseg.first_attack_entry","first attack after the fight starts, Entry mode (one recorded raid, scale 4: tick 7)","7","ticks","[blert]","D",
  [BS+":SUMMARY"],"-","+-1")
r("sotetseg.post_death_ball_gap","ticks from the death-ball attack to his NEXT attack (one whole 5-tick slot is skipped)","10","ticks","[blert]","B",
  [BS+":SUMMARY",OM+":SOLO_NORMAL_CADENCE"],"-","exact")
r("sotetseg.magic_per_ball","ordinary ball (magic) attacks that precede the death-ball attack; melee attacks are not counted and do not reset it","10","count","[blert][wiki]","B",
  [BS+":SUMMARY",WIKI+":95",ENTRY+":191",OM+":SOLO_NORMAL_CADENCE",OM+":TOBRUN"],"-","exact")
r("sotetseg.melee_roll_adjacent","P(melee | target adjacent at T-1, non-death-ball attack tick); every melee had an adjacent target, none with a far one","483","permille","[blert][nr]","B",
  [BS+":MELEE_ROLL",OM+":ADJACENT_ROLL","THEATRE_OF_BLOOD_PLAN.md:1324"],"-","range")
r("sotetseg.melee_range","target distance (tiles from his footprint) at which melee is possible at all","1","tiles","[blert][wiki]","B",
  [BS+":MELEE_ROLL",WIKI+":92",OM+":SOLO_NORMAL_CADENCE"],"-","exact")
r("sotetseg.melee_hit_delay","melee hitsplat lands this many ticks after the attack animation (not tick-eatable)","1","ticks","[blert][wiki]","B",
  [BS+":MELEE_DELAY",WIKI+":92",OM+":ADJACENT_ROLL"],"-","exact")
r("sotetseg.melee_max","melee max hit, Normal and Hard unprayed (prayed 22)","45","hp","[wiki][guide]","C",
  [WIKI+":24",STRAT+":787","sources/transcripts/yt_4i4lv-srJkw.md:93"],"-","range")
r("sotetseg.melee_max_prayed","melee max hit with Protect from Melee (halved, not blocked)","22","hp","[wiki]","D",
  [WIKI+":92",STRAT+":787"],"-","range")
r("sotetseg.melee_max_entry","melee max hit, Entry mode (infobox max hit1)","20","hp","[wiki]","D",
  [WIKI+":23"],"-","range")
r("sotetseg.ball_max","small ball and ricochet max hit when not prayed, Normal and Hard (ranged and magic)","50","hp","[wiki][guide]","C",
  [WIKI+":24",STRAT+":789","sources/transcripts/yt_KF9y2GYTJ-A.md:151"],"-","range")
r("sotetseg.ball_max_entry","small ball and ricochet max hit when not prayed, Entry mode (infobox max hit1 ranged and magic)","22","hp","[wiki]","D",
  [WIKI+":23"],"-","range")
r("sotetseg.prayer_disable","ticks the victim's three protection prayers are blocked after an unprayed ball hit (Retribution, Smite, Redemption unaffected); a 2022 note says the duration was reduced, without a figure","5","ticks","[wiki][guide][jagex]","C",
  [WIKI+":92",NP+"wiki_Update_Tournament_World_Mobile_Enhancements_and_Theatre_of_Blood_Tweaks.wikitext:90","sources/transcripts/yt_M1t2qWMbzEs.md:55",OM+":PRAYER_BLOCK"],"-","exact")
r("sotetseg.ricochet_count","projectiles a ball splits into at the impact: one red (magic, 1606) and one grey (ranged, 1607), aimed at OTHER players and never both on one player in one cycle","2","count","[wiki][cache][jagex]","C",
  [WIKI+":92","sources/cache_spotanim.txt:58","sources/cache_spotanim.txt:59",NP+"wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:74"],"-","exact")
r("sotetseg.ball_flight_by_distance","ordinary ball flight time as a function of the target's distance from him (teams stand far to lengthen it; the plugin shows remaining cycles/30 per orb); no figure in any source","?","ticks","[wiki][M100]","E",
  [STRAT+":791","sources/openosrs_theatre/SotetsegHandler.java:127"],"M100","approx")
# ---- death ball
r("sotetseg.death_ball_flight","ticks from the death-ball attack event to the tick the recorder's hitpoints drop (5 of 5 recorder hits, targets 3 to 5 tiles from him); blert's own comment: the ball takes 15 ticks to land","16","ticks","[blert][plugin]","B",
  [BS+":DEATH_BALL_FLIGHT","sources/blert_plugin/SotetsegDataTracker.java:258",OM+":SOLO_NORMAL_CADENCE"],"M29","+-1")
r("sotetseg.death_ball_flight_distance_independent","spread of the death ball's impact tick across target distances (Jagex: its timing is the same regardless of distance; two newsposts ask for a longer minimum flight)","0","ticks","[jagex][blert]","A",
  [NP+"wiki_Update_Theatre_of_Blood_Changes_Deadman_Summer_Finals.wikitext:72",NP+"wiki_Update_Theatre_of_Blood_Feedback_Tweaks.wikitext:31",WIKI+":135",BS+":DEATH_BALL_FLIGHT",OM+":SOLO_NORMAL_CADENCE"],"M29","exact")
r("sotetseg.death_ball_split_radius","damage is divided among every player inside the 3x3 around the target (radius in tiles)","1","tiles","[wiki][guide][nr]","C",
  [WIKI+":95",STRAT+":794",GUIDE+":171"],"-","exact")
r("sotetseg.death_ball_floor","guaranteed-kill figure with two or more players alive and nobody sharing","121","hp","[wiki]","D",
  [STRAT+":794"],"M44","range")
r("sotetseg.death_ball_max_5","death ball ceiling at party size 5 (unshared)","188","hp","[wiki]","D",
  [WIKI+":95",WIKI+":24"],"M44","range")
r("sotetseg.death_ball_max_4","death ball ceiling at party size 4: 155 is interpolated, no source states it","155","hp","[wiki][M44]","E",
  [WIKI+":95"],"M44","approx")
r("sotetseg.death_ball_max_3","death ball ceiling at party size 3 and below: 121 is the wiki's floor, not a stated ceiling","121","hp","[wiki][M44]","E",
  [STRAT+":794"],"M44","approx")
r("sotetseg.death_ball_hit_entry_solo","death ball hit in Entry mode, solo","15","hp","[wiki]","D",
  [WIKI+":105","sources/transcripts/yt_B_gjVdmfOrY.md:93"],"-","range")
r("sotetseg.death_ball_hit_entry_group","death ball hit in Entry mode in groups (wiki: around 80; the Entry page says 70+)","80","hp","[wiki]","D",
  [WIKI+":105",ENTRY+":191"],"-","range")
r("sotetseg.death_ball_count_hard","death balls per cast in Hard Mode (two targets)","2","count","[wiki][guide]","C",
  [WIKI+":102",HARD+":344","sources/transcripts/yt_9coCVByPHCw.md:37"],"-","exact")
r("sotetseg.death_ball_after_maze","re-activations whose very first attack is a death ball (Jagex fixed it: the big bomb should no longer happen immediately after the maze; 0 of 44 recorded, the earliest is the 2nd attack)","0","count","[jagex][blert]","B",
  [NP+"wiki_Update_Theatre_of_Blood_New_Modes.wikitext:101",BS+":SUMMARY",BS+":DEATH_AFTER_MAZE"],"-","exact")
# ---- hitpoints and stats
r("sotetseg.hp_normal","hitpoints by party size 3, 4, 5 (3 covers 1 to 3 players); cache states the 5-man base only","3000,3500,4000","hp","[cache][plugin][blert][wiki]","C",
  ["sources/cache_npc_sotetseg.txt:15","sources/blert_plugin/TobNpc.java:115",WIKI+":31",BS+":SUMMARY",OM+":HITPOINTS"],"-","exact")
r("sotetseg.hp_hard","hitpoints by party size 3, 4, 5 in Hard Mode (cache stat4 4000; scaling is blert's table)","3000,3500,4000","hp","[cache][plugin][blert][wiki]","C",
  ["sources/cache_npc_attackrates.txt:737","sources/blert_plugin/TobNpc.java:116",WIKI+":32",BS+":SUMMARY",OM+":HITPOINTS"],"-","exact")
r("sotetseg.hp_entry_per_player","Entry hitpoints per player (cache stat4 on the _story records; four players = the wiki's 2240)","560","hp","[cache][wiki][plugin]","A",
  ["sources/cache_npc_sotetseg.txt:94",WIKI+":30","sources/blert_plugin/TobNpc.java:114",OM+":HITPOINTS"],"M19","exact")
r("sotetseg.defence_level","defence level Normal, Entry, Hard (cache stat2); ours spawns tob_sotetseg_combat in every mode so Entry reads 200","200,150,200","count","[cache][wiki]","A",
  ["sources/cache_npc_sotetseg.txt:15","sources/cache_npc_sotetseg.txt:94",WIKI+":41",OM+":HITPOINTS"],"-","exact")
r("sotetseg.attack_level","attack level Normal, Entry, Hard (cache stat1): Hard hits harder than Normal; ours uses the Normal record in every mode","250,180,350","count","[cache][wiki]","A",
  ["sources/cache_npc_sotetseg.txt:15","sources/cache_npc_sotetseg.txt:94",WIKI+":34"],"-","exact")
r("sotetseg.defence_floor","defence can be drained no lower than this; Jagex confirms a floor exists at Sotetseg, the number is the wiki's; ours does not enforce it","100","count","[wiki][jagex]","D",
  [WIKI+":99",NP+"wiki_Update_Equipment_Rebalance_Ranged_Meta.wikitext:216"],"-","exact")
r("sotetseg.defence_restore","defence is restored to full after each maze","1","count","[wiki][guide]","C",
  [WIKI+":99",STRAT+":807",GUIDE+":343"],"-","exact")
# ---- maze triggers and shape
r("sotetseg.maze_trigger_hp","maze procs when hitpoints first reach these percents of the SCALED pool (blert's last hitpoints reading before each proc, 22 mazes: 66.7 to 68.2 and 33.3 to 34.1 percent, i.e. the crossing hit lands on the proc tick)","66.6,33.3","percent","[wiki][blert][plugin]","C",
  [WIKI+":97","sources/blert_plugin/SotetsegDataTracker.java:311",BS+":SUMMARY"],"-","range")
r("sotetseg.maze_boss_idle_at_proc","tick offset between the maze-proc event and his switch to the idle form 8387/10864/10867","0","ticks","[blert]","B",
  [BS+":SUMMARY",OM+":MAZE_PROC"],"-","exact")
r("sotetseg.maze_no_attacks","attacks between the proc and the re-activation (none; 2 of 670 recorded attacks fall ON the re-activation tick)","0","count","[blert][wiki]","B",
  [BS+":MISC",WIKI+":97",OM+":MAZE_PROC"],"-","exact")
r("sotetseg.maze_teleport_delay","ticks from the proc event to every player's teleport (72 of 72 arena players, 6 of 6 runners)","3","ticks","[blert]","B",
  [BS+":STALL",OM+":MAZE_PROC"],"-","exact")
r("sotetseg.maze_first_move","earliest tick after the proc at which anyone moved (never earlier in 114 players); the 5-tick stall; cache: his portal seq is 5.33 ticks","5","ticks","[blert][plugin][cache][video]","B",
  [BS+":STALL","sources/advancedraidtracker/SotetsegHandler.java:122",CS+":shadow_portal","sources/transcripts/yt_EntowMeBPNg.md:22",OM+":MAZE_PROC"],"-","exact")
r("sotetseg.maze_cycle","the despawn check runs every this many ticks and ends the maze when nobody stood on the grid on the previous tick","4","ticks","[blert][guide][video][wiki]","B",
  [BS+":MISC",GUIDE+":185","sources/transcripts/yt_EntowMeBPNg.md:22",OM+":MAZE_CYCLE"],"-","exact")
r("sotetseg.maze_cycle_phase","the 4-tick cycle belongs to the room and is not restarted by a maze: re-activation tick mod 4 is the same for both mazes of a raid (19 of 22 raids; the other 3 differ by one tick); ours restarts it at each proc","global","text","[blert][video]","B",
  [BS+":CYCLE_PHASE","sources/transcripts/yt_EntowMeBPNg.md:22",OM+":MAZE_CYCLE"],"-","+-1")
r("sotetseg.maze_off_on_3","stepping off the grid on cycle tick 3 despawns the maze one tick later, on tick 0, and nulls rag damage (ticks from the step-off to the despawn)","1","ticks","[guide][video][wiki]","C",
  [GUIDE+":185","sources/transcripts/yt_EntowMeBPNg.md:22",OM+":MAZE_CYCLE"],"-","exact")
r("sotetseg.post_maze_first_attack","ticks from his re-activation to his first attack (27 of 28 recorded mazes; one was 0)","1","ticks","[blert]","B",
  [BS+":SUMMARY","TOB_RESEARCH.md:639",OM+":MAZE_PROC"],"M10","exact")
r("sotetseg.maze_grid_w","maze grid width in tiles","14","tiles","[cache][wiki][blert]","A",
  ["sources/cache_locs.txt:384",WIKI+":92","THEATRE_OF_BLOOD_PLAN.md:1069"],"M25","exact")
r("sotetseg.maze_grid_h","maze grid height in tiles (210 darktile decorations per square = 14 x 15)","15","tiles","[cache][wiki][blert]","A",
  ["sources/cache_locs.txt:384",WIKI+":92","THEATRE_OF_BLOOD_PLAN.md:1069"],"M25","exact")
r("sotetseg.maze_seeds","path seeds (single tiles on the 8 even rows, runs on the 7 odd rows between them)","8","count","[blert][tool]","B",
  ["sources/blert_api/sote_maze_paths.csv","TOB_RESEARCH.md:1594","THEATRE_OF_BLOOD_PLAN.md:1445"],"M11","exact")
r("sotetseg.maze_max_x_change","largest column change between consecutive seeds (max seen in 183 mazes)","5","tiles","[blert][tool]","B",
  ["sources/blert_api/sote_maze_paths.csv","TOB_RESEARCH.md:1594",OM+":MAZE_GENERATOR"],"M11","exact")
r("sotetseg.maze_start_column","start column range; column 0 never starts a maze in 183","1-13","tiles","[blert][tool]","B",
  ["sources/blert_api/sote_maze_paths.csv","TOB_RESEARCH.md:1594",OM+":MAZE_GENERATOR"],"M11","exact")
r("sotetseg.maze_no_lateral_rate","share of turns that carry no lateral move (183 mazes, CI 17.8 to 22.2)","200","permille","[blert]","B",
  ["sources/blert_api/sote_maze_paths.csv","TOB_RESEARCH.md:1594",OM+":MAZE_GENERATOR"],"M11","range")
r("sotetseg.maze_players_hard","players sent to the shadow realm in Hard Mode: all but one, the maze divided among them (Normal and Entry: one)","1","count","[wiki][guide]","C",
  [WIKI+":102",HARD+":340","sources/transcripts/yt_9coCVByPHCw.md:37"],"-","exact")
# ---- maze hazards
r("sotetseg.maze_chip_interval","ticks between the 1 to 3 chip hits on a player in the shadow realm (29 of 29 recorded gaps)","7","ticks","[wiki][blert]","B",
  [BS+":CHIP",WIKI+":97",ENTRY+":193",OM+":RUNNER_TICK_PERIOD"],"-","exact")
r("sotetseg.maze_chip_damage","chip hit size (40 recorded hits: 1 x11, 2 x13, 3 x16)","1-3","hp","[wiki][blert]","B",
  [BS+":CHIP",WIKI+":97",OM+":RUNNER_TICK_PERIOD"],"-","range")
r("sotetseg.maze_first_chip","ticks from the proc to the runner's first chip (11 recorded runners: 6 to 12, median 8)","6-12","ticks","[blert]","B",
  [BS+":CHIP",OM+":RUNNER_TICK_PERIOD"],"-","range")
r("sotetseg.rag_interval","a wrong tile deals its damage every tick the player stands on it (recorded on consecutive ticks in 3 pairs)","1","ticks","[wiki][blert]","C",
  [WIKI+":97",ENTRY+":193",BS+":RAG_FORMULA",OM+":RUNNER_TICK_PERIOD"],"-","exact")
r("sotetseg.rag_percent","rag damage share of CURRENT hitpoints (6.67%)","66.7","permille","[wiki][blert]","C",
  [WIKI+":97",BS+":RAG_FORMULA"],"-","range")
r("sotetseg.rag_flat","rag damage flat part, Normal and Hard (recorder: 14 of 23 drops match 15 + floor(6.67% of hp) exactly, the rest are +1 to +2 or hp changed between samples)","15","hp","[wiki][blert]","C",
  [WIKI+":97",BS+":RAG_FORMULA"],"-","range")
r("sotetseg.rag_flat_entry","rag damage flat part, Entry mode","11","hp","[wiki]","D",
  [WIKI+":97",ENTRY+":193","sources/transcripts/yt_M1t2qWMbzEs.md:57"],"-","range")
r("sotetseg.rag_range","tiles around the wrong tile that take the rag damage (the wiki says 'the player'; Near-Reality uses 1)","1","tiles","[wiki][nr][M46]","E",
  [WIKI+":97","THEATRE_OF_BLOOD_PLAN.md:2247"],"M46","approx")
# ---- tornado
r("sotetseg.tornado_row","1-based grid row whose first step spawns the tornado (Hard Mode: also when the arena player passes the third row, in both worlds)","4","count","[guide][video][wiki]","C",
  [HARD+":340","sources/transcripts/yt_90957FaXfjM.md:22","sources/transcripts/yt_KDTlRVi6YTY.md:40",STRAT+":802",OM+":RUNNER_TICK_PERIOD"],"-","exact")
r("sotetseg.tornado_speed","tiles the tornado moves per tick along the path; no source states it","?","tiles","[M45]","E",
  [STRAT+":802","sources/transcripts/yt_90957FaXfjM.md:20"],"M45","approx")
r("sotetseg.tornado_damage","tornado hit per tick on a shared tile (reference server only; the wiki says only that it deals damage if intercepted)","35-45","hp","[nr][M45]","E",
  [WIKI+":97","THEATRE_OF_BLOOD_PLAN.md:2241"],"-","approx")
with open(OUT,"w",encoding="utf-8") as f:
    f.write("\t".join(["mechanic_id","quantity","spec_value","unit","tags","grade","source_ref","closes","tolerance"])+"\n")
    for row in rows:
        assert all("\t" not in c and "\n" not in c for c in row),row
        f.write("\t".join(row)+"\n")
print(len(rows),"rows")
