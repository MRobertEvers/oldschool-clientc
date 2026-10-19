# Lizardman shamans: tick spec and content audit

Room unit `shamans` (COX_FINISH_PLAN.md R). Content: the shaman procs in
`OSRS-Content/osrs239-content/server/scripts/minigames/minigame_cox/scripts/cox_minions.rs2`,
the shaman blocks of `configs/cox.constant`, `configs/cox.npc`, `configs/cox.varp`.
Written 2026-10-18 for the owner's order "match OSRS as close as possible".

## Sources (rank order: Jagex > recorded > wiki > plugins > Near-Reality > ours)

| tag | what | where |
|---|---|---|
| ASH | Mod Ash, Discord 9 Jul 2026: shamans regen 1 stat level per 20 ticks | quoted in `sources/wiki_Lizardman_shaman_Chambers_of_Xeric.wikitext` |
| CACHE | cache symbols (Jagex gamevals): seq 7152 `shayzien_lizard_boss_jump`, 6946 `_land`, 7157 `_minion_summon`, 7158 `_tail_swipe`, 7159 `_hands_to_head`, 7160 `_rise`, 7192 `shay_lizard_warrior_attack_melee`, 7193 `shay_lizard_warrior_attack_ranged`, 7194 `_defend`, 7196 `_death`; spotanim 1291 `lizardman_spit`, 1293 `lizardshaman_spit_acid`, 1294 `lizardshaman_acid_splash`, 1295 `lizardshaman_spawn_explode`; npc 7573/7574 `raids_lizardshaman_a/b`, 7575 `raids_lizardshaman_blocker`, 6768 `zeah_lizardshaman_spawn`; loc 29768 `raids_shaman_tendrils_entrance` (op1 Pass) | `configs/all.seq.compack`, `all.spotanim.compack`, `all.npc.compack`, `all.loc` |
| SYNQ | Synq, Remastered Solo Raids Guide 2026, "Shamans" 0:34:38-0:41:59 (frame-by-frame play with tick talk) | `synq_transcript.md` |
| VID | Tubby 0:20:18-0:21:57; TheEdB0ys CoX 0:35:41-0:37:17 and shaman guide 0:07:47-0:12:03; Extile 0:09:24; Gnomonkey 0:45:44; Beleti 0:14:06; PecanBread 0:22:21; The Academy (90 s) | `sources/transcript_*.md`, `sources/transcripts/yt_*.md` |
| WIKI-C | Lizardman shaman (Chambers of Xeric), infobox + text | `sources/wiki_Lizardman_shaman_Chambers_of_Xeric.wikitext` |
| WIKI-S | Lizardman shaman (surface): attacks section ("share the same attack set", WIKI-C) | `sources/wiki_Lizardman_shaman.wikitext` (fetched 2026-10-18) |
| WIKI-SS | Lizardman shaman/Strategies (surface) | `sources/wiki_Lizardman_shaman_Strategies.wikitext` (fetched 2026-10-18) |
| WIKI-SP | Spawn (lizardman shaman) | `sources/wiki_Spawn_lizardman_shaman.wikitext` (fetched 2026-10-18) |
| WIKI-T | Spirit tendrils | `sources/wiki_Spirit_tendrils.wikitext` (fetched 2026-10-18) |
| WIKI-CS | Chambers of Xeric/Strategies, "Lizardman Shamans" + count table | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:514-536` |
| WIKI-CA | Shayzien Specialist (Elite CA 314) | `sources/wiki_Shayzien_Specialist.wikitext` |
| PLUG | Lizardman Shaman Minion Alert (RuneLite hub): spawn npc id 6768 | `sources/lizardman_shaman_minion_alert/` |
| NR | Near-Reality: `chambersofxeric/npc/LizardmanShaman.java`, `room/LizardmanShamanRoom.java`, `plugins/object/SpiritTendrils.java`, `world/entity/npc/impl/LizardmanSpawn.java`, `cache/data/npcs/combat/7573.npc.json`, `drops/7573.drops.json`. NB `Utils.random(n)` is INCLUSIVE `[0,n]` (`cache/.../Utils.java:47`) | `~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main` |

## Tick conventions used below

Server tick T: npcs run before players. An npc decision on T reads raider tiles
at the END of T-1. "Moves on T" = the player's step during T (a click sent
during T-1). An npc queue armed on T with delay N (N >= 1) runs on T+N in the
npc phase and reads tiles at the end of T+N-1. A player queue `queue*(x, k)`
runs on T+k+1. `d` = Chebyshev distance from the shaman's 3x3 body
(`npc_range`, 0 under it).

## Round 4: RSProx (2026-10-18) -- supersedes the rows it names

Jagex's server, recorded (`docs/rsprox-analysis/cox/shamans.md`; 3282, 3389 in the
raid, 3276 the surface shamans that share the attack set). Where a row below
disagrees, this wins.

| row | recorded | was |
|---|---|---|
| S-ROLL | ranged 3, acid 2, jump 1, summon 1 (+ melee 3 in reach); a blocked jump or a refused summon rolls again | NR 4:1:1(+1) |
| S-RNG | lizardman_spit 420 -> 146 raw (105 -> 36), leaves 61 cycles in, ends 66 + 5 x gap, angle 5, progress 128; lands end / 30 | 40 -> 36, delay 32 |
| S-MEL | the tail swipe (7158), 4 of 5; stabsword_slash range 6 | 7192 |
| S-ACID-T | lands T + 2 + gap / 2 (30 of 30), and after its shaman has died; 1293 420 -> 5 raw, angle 30, progress 128 | 1 + (1 + d) / 3 on the shaman's queue |
| S-JUMP-L | lands T + 6 (land anim, teleport, moon_fall_land); next swing T + 9 | T + 4, T + 8 |
| S-SPAWN | a swing choice, refused while its last spawns live (summon to summon 17+); the spawns appear T + 2 in a chain from the target's tile; the target's camera shakes then (0, 4, 2, 0), reset + 3 | per-tick 1 in 61, 34 ticks, at once |
| S-SPAWN-X | bursts T + 10, + 12, + 14 in chain order; the tell 3 before, standing still; the 3x3; 5 / 7 / 10 (fitted) | fuses 10-20, the 5x5 |
| S-CLEAR | from the last deletion X: tendrils at X (3282; X + 3 in 3389), blockers' tell X + 3 (X + 2), burst 3 after | + 2 / + 6 / + 2 |
| S-DROP | the slain message on the deletion tick; seeds despawn 3000 | no message; 200 |
| sounds | spits creeper_spit range 10, acid splash dagganoth_splash 8, jump jump_no_land 8, landing moon_fall_land 8, spawns slug2_rumbling 8 / screech2 (delay 20) 5 / 100_cauldron_explodes 8, hit crocodile_hit, death crocodile_death | NR ids, synth to the room |

## The spec

| id | mechanic | decide tick / read | geometry | damage (solo) | prayer | tell (client) | source / ruling |
|---|---|---|---|---|---|---|---|
| S-COUNT | how many | room population | per-variant tiles (NR `lizardmanSpawnLocations`, decoded) | - | - | npcs 7573/7574 | WIKI-CS table: party 1-4 → 2, 5-9 → 3, 10-14 → 4, 15+ → 5. NR caps at 4 (`<4→2, ≤7→3, else 4`): wiki wins. Solo = **2**. Ids a/b rolled per spawn (NR) |
| S-STATS | stats | - | size 3 | HP 190 (CM 285), att/str/mag/rng 130, def 210; bonuses atk +58, str +52, rng atk +56, rng str +49; def stab 102 slash 160 crush 150 magic 160 ranged 0 (light -10, heavy 0) | - | - | WIKI-C infobox = NR json. HP scales with party via `~cox_scale_hp` (scaledhp=Yes) |
| S-REGEN | stat regen | every 20 ticks | - | +1 level, every stat incl. HP | - | - | ASH |
| S-AGGRO | aggression | npc hunt each tick | hunt range 8 | - | - | the npc turns and walks/attacks | WIKI-C "aggressive = Yes"; NR AGGRESSIVE, `aggressionDistance 8`; never tolerant (WIKI-S). WIKI-CA: a far shaman "may not aggro for a while" (consistent with a finite range) |
| S-RANGE | attack range / cadence | - | 8 tiles from the body | - | - | - | SYNQ 0:36:50 "attack range of eight", WIKI-SS "attack range of 8", NR 8. Swing every 4 ticks (WIKI-C); 8 after a jump (NR `return 8`) |
| S-ROLL | which attack | the swing tick T | adjacent = d ≤ 1 (diagonals count, WIKI-S) | - | - | see each style | NR inclusive roll. Not adjacent: ranged 4/6, acid 1/6, jump 1/6. Adjacent: ranged 4/7, acid 1/7, jump 1/7, melee 1/7. Jump not possible → re-roll one face smaller: not adjacent ranged 4/5, acid 1/5; adjacent ranged 4/6, acid 1/6, melee 1/6. "These attacks do not have a cooldown" (WIKI-S) |
| S-RNG | ranged auto | T (target tile end T-1) | single target, tracks | max **25** (stat formula: (130+9)·(49+64)/640; the same formula gives the surface shaman's published 21). NR flat 24 | Protect from Missiles zeroes it, read on T (engine: `~playerhit_n_ranged` at the swing) | anim 7193 on T; projectile 1291 | WIKI-SS, SYNQ 0:35:12, TheEdB0ys 0:07:47 ("100% block"). Accuracy roll vs ranged defence. **Poisons** on a landed roll even when prayed (WIKI-SS "This attack can poison"; TheEdB0ys CoX 0:36:12 "poisoned even if you are protecting from range"; Tubby "they will almost always poison you"): engine rule `~npc_poison_player`, severity 12 |
| S-MEL | melee | T | adjacent incl. diagonal | max 25 (stat formula, str +52); NR 24 | Protect from Melee | anim **7192** (WIKI-SS "same animation as the other lizardmen"; NR's 7158 outranked) | crush (NR, WIKI-S). Poisons like S-RNG |
| S-ACID | acid spit: target | T, the target's tile at the end of T-1 | 5x5 (radius 2) centred on that tile; every raider in it (CoX is multi) | 20-**39** typeless, green poison splat; Shayzien tier-5 −20% per piece | none ("unblockable", WIKI-SS) | anim 7193 on T (same as S-RNG: the swing itself is the tell; "if they throw their shoulders back run three tiles", Extile); projectile 1293 to the tile; splash 1294 on landing | SYNQ 0:34:39 "5x5", 0:37:25 "the area where it lands is determined right when [the animation] happens"; max 39 WIKI-C ("up to 40" WIKI-CS outranked); min 20 NR. Non-targets inside the room's NR safe polygon are exempt (NR) |
| S-ACID-T | acid spit: landing | lands on **T+L**, L = 1 + floor((1+d)/3); reads tiles at end of T+L-1 | - | - | - | - | **Ruling.** NR's flight (80+5·d_centre cycles − 1 tick) contradicts SYNQ 0:38:30-0:41:18; the magic hit-delay formula on d reproduces every SYNQ observation: d=1 (adjacent/corner) L=1 "instant", undodgeable ("be at least one tile away"); d=2..4 L=2: moving on T and T+1 (4 tiles) dodges, a reaction from T+1 (2 tiles) does not ("one tile away ... impossible"); d=5..7 L=3: a late dodge works ("at max blowpipe distance ... travel distance increased"); d=8 L=4. Dodge = Chebyshev ≥ 3 from the target tile at the read tick (SYNQ "three tiles") |
| S-ACID-P | acid poison | on landing | each raider hit | poison severity 12 at **1 in 4**, whether or not damage landed | - | "You have been poisoned!" | NR CoX (inclusive `random(3)==0`); WIKI-SS "negates all damage ... though you can still be poisoned"; severity 12 WIKI-C (NR's surface 10 is the surface value) |
| S-JUMP | jump: decide | T, target tile end T-1 | lands with its 3x3 centred on that tile; allowed only if all 9 tiles are open | - | - | anim **7152** on T | SYNQ 0:35:45 "registered the very moment the shaman jumps"; 0:36:17 the 3x3 must fit (any blocked tile in it, a corner included, prevents it: "that bottom right tile alone prevents the shamans from jumping"); WIKI-CA "3x3 AoE centred on the target"; NR `isFloorFree(target-(1,1), 3)` |
| S-JUMP-L | jump: landing | in the air T..T+3; lands **T+4** (teleport, anim 6946); slam reads tiles at end of T+3, splat on T+4 | 3x3 = the landed body | 20-25 typeless | none | anim 6946 on T+4, the npc reappears on the target tile | NR `schedule(..., 4)` (surface NR splats on landing; CoX NR one tick later, `ticks==1` — the surface form taken, the read tick is the same); damage WIKI-S "20-25 typeless". Dodge: Chebyshev ≥ 2 from the jump tile at end of T+3 (SYNQ "run at least two tiles away") |
| S-JUMP-A | airborne | T..T+4 | - | hits on it do nothing | - | - | "you also can't attack the shaman while it's in the air" (TheEdB0ys 0:10:22). It does not move in the air and re-targets by its aggression after landing (NR re-targets the jumper's target) |
| S-SPAWN | spawn summon | any tick with a target in reach (≤ 8) and in sight: 1 in 61 per tick, ≥ 34 ticks apart | 3 spawns: one ON the target's tile, two on open tiles next to it | - | - | shaman anim **7157** (`minion_summon`, "raising its hands"); camera shake; 3 npcs 6768 appear with anim 7160 | WIKI-SP ("screen shaking ... shaman raising its hands. Three spawns"); WIKI-SS ("two next to the player and one under the player"); cadence NR (`random(60)`, 20 s); never on a tick the swing is due (OURS: the timer runs before the swing, and one animation would replace the other, losing a tell); PLUG alert keys on 6768 |
| S-SPAWN-X | spawn burst | fuse 10-20 ticks each (NR), the one under the player shortest (WIKI-SS "always explode first"); the last 3 ticks it stands still | 5x5 around the spawn | 5 + min(5, floor(2·euclid)) = 5 on its tile .. 10 at the 5x5 corners | none | anim **7159** (`hands_to_head`) 3 ticks before; splash 1295 | WIKI-SP "5-10 damage per spawn within a 5x5 area"; formula NR (reproduces 5-10). WIKI-SS "8-10" outranked by the monster page. Spawns walk at the nearest raider (NR follows its target) and never attack |
| S-TEND | spirit tendrils (entrance) | latched the first tick any raider is within 6 of the anchor tile | 2 locs (4 in CCW) across the entrance | first pass free (2 in CCW), each later pass 15% max HP, green splat | - | locs 29768 appear | WIKI-T (Pass), WIKI-CS ("one or two sets of spirit tendrils ... to prevent players from safespotting them away from the chamber"); NR `SpiritTendrils`/`process()` |
| S-PASS | Pass op | on click | a 2-tile hop straight through, x axis first | as S-TEND | - | - | NR `SpiritTendrils.handleObjectAction`; after the room is clear: "The tendril is about to collapse!" and no hop |
| S-EXIT | exit blockers | spawned with the tendrils | 2 npcs 7575, `blockwalk=all`, at the exit | - | - | - | WIKI-CS "several spawns blocking the way forward"; NR `blockingExitLocations` |
| S-CLEAR | room clear | the last shaman's death | - | blockers: 1 dmg within 2 | - | tendrils removed on clear+2; blockers anim 7159 on clear+6, splash 1295 on clear+8, gone | NR `finish()`: wipe at 2, blockers at 6 then +2 |
| S-DROP | drop | each death | under the shaman | big bones; 5-10 each buchu, golpar, noxifer seeds; long bone 1/400; curved bone 1/5012.5 | - | ground items | WIKI-C drops; NR drops json; WIKI-CS |
| S-SAFE | safespots | - | a 10-range weapon outranges 8; long-range +2; the jump can be used to lure one into a trap spot | - | - | - | WIKI-CS, WIKI-SS, SYNQ 0:36:50, Tubby. Emergent from S-RANGE/S-JUMP, not authored |

### Shayzien
Tier-5 Shayzien pieces cut the acid by 20% each (5 = immune), helm/body/legs/
gloves/boots (NR `protectionModifier`; WIKI-S: "a full set ... completely
negate"). The Slayer helmet (i) with the hard Kourend diary counts as the helm
(WIKI-S) — not ported (no diary primitive).

## Audit (ours before 2026-10-18 / OSRS / verdict)

| mechanic | ours (before) | OSRS (spec row) | verdict → fix |
|---|---|---|---|
| count | wiki table | S-COUNT | OK |
| spawn tiles | NR per-variant, deterministic first n | NR random with replacement (can stack two) | OK (kept; stacking reads as an NR bug) |
| bonuses | the cache record carries str +52, rng atk +56, rng str (param 12) +49, the four defences; melee attack, magic and the ranged defences were 0 | S-STATS | **FIXED** cox.npc: the rest of the wiki bonuses, `damagetype` crush (`rangebonus` 0: param 12 is the +49) |
| aggression | `huntmode=none`, retaliate only | S-AGGRO | **FIXED** `huntmode=aggressive`, `huntrange` 8 |
| HP scaling | config 190 for any party | scaledhp | **FIXED** `~cox_scale_hp` in `[ai_spawn]` (no-op solo) |
| style roll | exclusive `random(5/6)`: no jump unless adjacent, melee only from a blocked jump, blocked jump 50/50 ranged/melee (OURS) | S-ROLL | **FIXED** inclusive roll, NR re-roll |
| adjacency | hunt radius 1 round the SW tile | d ≤ 1 to the 3x3 body | **FIXED** `npc_range(coord) <= 1` |
| ranged | raw damage, no roll, **no prayer**, no anim, max 24 | S-RNG | **FIXED** roll, prayer, poison, anim 7193, max from stats (25), shot from the body's centre. The baseline probe died in 36 ticks under Protect from Missiles |
| melee | raw damage, no roll, no prayer, no anim | S-MEL | **FIXED** shared melee funnel, crush, anim 7192 |
| acid target / size | target tile, 5x5 | S-ACID | OK |
| acid timing | player queue at `(32+5·d_SW)/30` +1 | S-ACID-T | **FIXED** L = 1+floor((1+d)/3), shaman's npc queue (dies with it) |
| acid tell | no anim (projectile only) | anim 7193 on T | **FIXED** |
| acid damage | 20-40, red splat | 20-39, green | **FIXED** |
| acid poison | 1 in 3 | 1 in 4 | **FIXED** |
| jump | `npc_walk` to one tile, no anim, **no damage**, only when adjacent | S-JUMP, S-JUMP-L, S-JUMP-A | **FIXED** 3x3 clear test, anim 7152, `npc_tele` on T+4 + anim 6946, 20-25 slam on T+5, hits ignored while airborne (`[ai_queue2]`), no swing and held in place while airborne (timer walks it to its own tile) |
| spawn cadence | per-tick 1/60 after 33, centre of the npc's hero | S-SPAWN | **FIXED** 1/61, 34, target in reach and sight, summon anim 7157, camera shake |
| spawn placement | NR cluster around a random centre | S-SPAWN (wiki) | **FIXED** one under, two adjacent; shortest fuse under the player |
| spawn burst | 5+min(5,2·Chebyshev): max 9 | 5-10 (euclid) | **FIXED** `~cox_shaman_spawn_damage_at` |
| spawn warn | none | anim 7159 3 ticks before, stand still | **FIXED** |
| tendrils | latched, locs placed, **no Pass handler** (room sealed once latched) | S-TEND, S-PASS | **FIXED** `[oploc1,raids_shaman_tendrils_entrance]` |
| clear | could never fire (the dying shaman still counted); player-queue chain with `sound_synth` (aborts with no player) | S-CLEAR | **FIXED** hp>0 filter; blocker npc queues; tendrils at +2 |
| drops | nothing (death_drop null, override dropped nothing) | S-DROP | **FIXED** `~cox_shaman_drops` |
| regen | 1/20 ticks | S-REGEN | OK |
| CM | max hit ×1.5 only | CM stats per infobox | open (solo normal first) |
| CA tracking | none | Shayzien Specialist | open (root: CA layer) |

## Selftest rows this changes (cox_selftest.rs2 is not this unit's)

The procs and constants the selftests read are all kept. Behaviour the
selftests pin that moved: none of `~cox_shaman_choose_style`'s asserted cases
change ((false,3) ranged, (false,4) acid, (true,3) ranged, (true,4) acid,
(true,5) jump all hold); `~cox_shaman_spawn_damage(int)` keeps its 5/9 form
(the burst now calls `~cox_shaman_spawn_damage_at`); the blob bounds 20..40
literal check still passes with max 39.

## Probes (facts this spec cannot settle; the solver's first run prints them)

1. The acid landing rule S-ACID-T (the one ruling made from a video, not a
   number): count acid hits by `d` at the swing and by the bot's movement on T
   and T+1.
2. Whether `map_blocked` counts the other shaman's body (NR ignores npcs).
3. The shaman's re-target after a jump (ours: aggression; NR: the jumper's
   target).
4. ~~Whether the shaman projectiles reach `api_drive.projectiles`~~ — they
   do (1291 and 1293 both read by the probe); the server's "osrs239 has no
   MAP_PROJANIM -- dropping it" line is some other send.

## What the probes measured (2026-10-18, scriptrun, `::coxgoto 8 0` seed 5 and `::coxtest 8 0/1/2`)

Probe Luas: the session scratchpad `shamans/probe2.lua`..`probe5.lua`. No
script aborts and no `no primary player bound` in any run.

- Acid: cast at d≈6 → lands T+3; cast at d≤1 → T+1 (projectile 1293
  duration 90 / 30 cycles); the 5x5 splash 1294 on the landing tick; damage
  20-39 green; poison ticks follow.
- Engine fact (round 1): an `npc_queue(q, arg, N)` armed from
  `[ai_opplayer2]` fired on T+N-1. Fixed in the engine (E1, 2026-10-18: the
  swing now runs after the npc's timers and queue, as `Npc.processNpc`); the
  two +1 compensations were removed, so N lands on T+N.
- Jump: 7152 on T, 6946 + teleport on T+4, slam 20-25 to a raider standing
  still. A first version that cleared the npc's mode (`npc_setmode(none)`)
  jumped again a tick later (aggression re-acquired and re-armed the swing);
  the swing is now guarded by the airborne clock instead.
- Ranged: 0 under Protect from Missiles; max 25 unprayed (a first cut that
  also stated `rangebonus 49` doubled the cache's own `rangebonus_ammo 49`
  and hit 35).
- Spawns: 7157 on the shaman, three 6768 with 7160, 7159 three ticks before
  the burst, 1295, 5/7 damage by distance.
- Melee 7192 when adjacent; the swing every 4 ticks (8 after a jump).
- Clear (`::kill` both): drops (big bones + 6-10 of each seed) two ticks
  after each death; tendrils gone; blockers 7159 then 1295 and freed; the
  finish ran twice when both died on one tick (now once, via the room
  register).
- Tendrils: latched as `::coxtest` stands the player at the entrance; Pass
  hops 2 tiles (6464 → 6461). Layout n=0 (CCW) has the second set inside the
  room that a raider must also pass (NR `extraSpiritTendrils`).

## Round 2 (2026-10-18)

- The tendril re-pass damage is applied from a player queue: dealt inline it
  was logged against the shaman the room check had left bound.
- Selftests: `::coxrun` OK, 178 checks (shaman rows 26, 74-87 included).
- The solver (`cox2_shamans.lua`) reads the acid's landing from the
  projectile's `duration` as sent (30 x L cycles) -- the content's arc ends on
  the landing tick, so the tell carries S-ACID-T exactly. `cycles_left` at
  first sight reads one tick short.
- Ruby bolts (e) in the kit: their Blood Forfeit (10% of my hp, on my swing)
  is logged in `hit_player` against the shaman I shot; the test separates it.

## Round 3 (2026-10-18)

- **Poison was a quarter strength.** poison.rs2 stores a severity that ticks
  `(severity + 4) / 5` (godwars.npc: K'ril's 16 is 76), and the wiki's
  "poisonous (12)" is the opening damage: 56, not 12 (`^cox_shaman_poison_severity`,
  both npc records' `poison_severity`, selftest #82 updated). Every probe and
  sweep before this ran 3-damage poison.
- **Live lane:** seed shamans0, live (embed `torirs_cox_shamans_opt`) against
  scriptrun: the same 2457 rows, identical once ticks are taken from each
  log's start; the only differences are absolute-clock stamps (npc_state
  field 7, varps 5732/7225, and the digests over them), shifted by the lanes'
  one-tick start offset. The gate's `::seedrandom` input row is live-only.
- **Tick-log labelling:** `damage` names as dealer whatever npc the calling
  script has bound (`torirs_server_scripts.c` SS_OP_DAMAGE,
  `TicklogSetDealerNpc(active_npc_slot)`), so the ruby bolt (e)'s self-hit
  (`enchanted_bolts.rs2` `~bolt_enchant_ruby`) is logged as the target's.
