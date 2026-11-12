# Gemstone Crab (Tlati Rainforest world boss) from RSProx

Content: `OSRS-Content/osrs-content/server/scripts/areas/area_varlamore/` —
`scripts/gemstone_crab.rs2` (the world cycle, the crab, its strike, the burrow,
the shell and its mining), `scripts/gemstone_crab_cave.rs2` (the three caves),
`scripts/gemstone_crab_merald.rs2` (Ru Merald), `configs/gemstone_crab.{npc,varp,
vars,constant,spawn}`. Tests: `test/varlamore/gemstone_crab.lua` (one player: a
whole life, the shell, the caves, Ru Merald) and `test/varlamore/
gemstone_crab_party.lua` (six players: the strike rule and the ranking).

## Sources

| session | rev | what it holds |
|---|---|---|
| index 4509 | 242.1 w496 | the fight: 3 cave crawls, 48 attacks, 2 burrows, an examine; `--all-players --bas` transcribed |
| index 4508 | 242.1 w496 | same recorder: 3 lives (N, S, N), 39 attacks, the shell refused twice, 2 crawls, the shell crumbling; `--all-players --bas` |
| index 1095 | 237.1 Leagues VI | eligible: the green line, 2 shells mined (6 gems), 3 burrows |
| index 3258 | 234.1 Grid Master | eligible: a shell mined (3 gems) and "You have mined all you can"; a shell crumbling |
| 945, 1099, 1103, 3055, 3056, 3063 | 231..237 | passes; spawn tiles and angles; the kill-count varps at login (1099, 1103) |

`rsprox_index.py search npc 14779` (10 sessions), `loc 57631` (4), `npc 14780` (5);
message search found the eligible branch only in 1095 and 3258. The wiki pages
read end to end: Gemstone Crab, Gemstone Crab/Strategies, Cave (Gemstone Crab),
Transcript:Gemstone Crab, Transcript:Cave (Gemstone Crab), Ru Merald,
Transcript:Ru Merald. Where the wiki and a recording disagree the recording wins.

## Action inventory (rsprox_actions.py)

| session | tick, count | action | verdict |
|---|---|---|---|
| 4509 | [5] x1 | welcome_screen:play | generic (login) |
| 4509 | [7] x6 | oploc1 cave_rock02_entrance01_gemstone | **this content** (caves; 3 crawls, 3 repeat clicks) |
| 4509 | [17] x48 | opnpc2 gemstone_crab | **this content** (the fight) |
| 4509 | [296],[300],[1165],[1169] | strength/attack potions (4 and 3 dose) | generic drinking; bound in our tree (not audited here) |
| 4509 | [1172] x1 | opnpc6 gemstone_crab | **this content**: examine. Recorded `npc_examine` "The shiniest crustacean this side of the shoreline."; ours answers examine from the record, so `desc=` is set (client bake needed, Open) |
| 4509 | [2583] x1 | Varrock teleport tablet | generic (`teleport_tablet.rs2`, §2b; not audited here) |
| 4509 | [2600],[2602] | stats:ranged skill guide, close | generic UI |
| 4509 | [2625] | opnpc3 ge_clerk_1, banker1_east | generic (GE, bank) |
| 4509 | [2633] | logout | generic |
| 4508 | [11..731] | bank chest, bank tabs/items/quantities, GE offers and collection, item sets, equips (snakeskin, hides, glory, rune scimitar), prayer orb, skill guides, objdialog searches | generic, the recorder's preparation; bound in our tree, not audited here |
| 4508 | [728] | Fortis teleport tablet | generic (§2b; not audited here) |
| 4508 | [742],[750],[771],[774] | quetzal keeper Fortis / Varrock quetzal / quetzal menu | generic (Quetzal Transport System; bound, not audited here) |
| 4508 | [825],[832] | world map open/close | generic |
| 4508 | [839] x39 | opnpc2 gemstone_crab | **this content** |
| 4508 | [843],[846],[1506],[1509],[2480],[2483],[3564],[3568] | potions | generic drinking |
| 4508 | [997] | combat_interface:retaliate | generic |
| 4508 | [1005..1095] | Evil Bob random event (talk, net, fishing spot, uncook pot, portal) | generic random event (macro_*; another session's area) |
| 4508 | [1671] x2 | opnpc1 gemstone_crab_remains | **this content** (Mine, refused) |
| 4508 | [1675] x4 | oploc1 cave | **this content** (2 crawls) |
| 4508 | [3132] | stats:attack | generic ("You can't do that right now." while busy) |

## Teleport inventory

| session | tick | from -> to | cause | verdict |
|---|---|---|---|---|
| 4509 | [13] | 1277,3169 -> 1352,3122 | north cave, crab at the east nest | **matched** (east landing) |
| 4509 | [666] | 1352,3122 -> 1246,3038 | east cave, crab burrowed, rising south | **matched** (south landing, next nest) |
| 4509 | [1754] | 1246,3038 -> 1352,3122 | south cave, crab burrowed, rising east | **matched** |
| 4509 | [2587] | 1350,3112 -> 3214,3424 | Varrock tablet | generic (§2b) |
| 4508 | [75] | 2443,3083 -> 3214,3425 | Varrock tablet | generic |
| 4508 | [732] | -> 1682,3134 | Fortis tablet | generic |
| 4508 | [747],[754],[778] | quetzal hops to Varrock, back, to 1226,3091 | quetzal transport | generic |
| 4508 | [1012],[1107] | Evil Bob's island and back | random event | generic |
| 4508 | [1680] | 1277,3169 -> 1246,3038 | north cave, crab at the south nest | **matched** |
| 4508 | [2750] | 1246,3038 -> 1277,3168 | south cave, crab rising north | **matched** (north landing) |

## The nests, the crab and its cycle

Crab south-west tiles (every add, all sessions): north 1271,3171; east 1351,3110;
south 1238,3041. D = the death seq tick, S = the spawn tick, R = the shell's tick.

| rule | recorded | ours (scriptrun tick log / wire log) | verdict |
|---|---|---|---|
| spawn S: `crab_boss_spawn` + `vfx_crab_boss_death` on the add | 4/4 (4509 [681],[1773], 4508 [2755], 1095 [8579]) | S=16: seq 12481, spot 3454 | matched |
| spawn facing | north/east: spawnangle south, no face (5/5); south: spawnangle east + `[facing] angle=1536` (4509 [681]) | south: spawnangle 1536 on a later add (gc4 [254]); north: 0 | matched |
| next crab at D+29 | 5/5 (681=652+29, 1773, 2755, 1677 inferred, 1095 8579) | D=88, next spawn 117 | matched |
| next nest | 6/6 moved to another nest (N->E, E->S, S->E, N->S, S->N, N->E) | random of the other two | matched; the wiki's "occasionally the same location" not modelled (0/6) |
| life length = HUD basehp | 948, 967, 1026, 1047, 1061, 1077, 1084, 1099 | 900 + random(201) | fitted |
| D = S + 2 + life | 3/3 (681+2+1061=1744, 2755+2+1077=3834, 1677+2+1047=2726) | D = 16+2+70 = 88 | matched |
| HUD: hp counts down a tick at a time from life-1 at the first show; 9 at D-9, 5 at D-8, 0 at D-4 | 3/3 lives | 69/70 at first show, ..., 9 at 78, 5 at 79, 0 at 83 (wire) | matched |
| overhead bar `healthbar_standard_120` (20), fill from the timer, not damage | 36,935 bars | headbar 20, fill falls with time only | matched |
| first target S+2, first strike +3, then every 7 | 683/686, 2757/2760 (1776 -> 1787/1790: first target out of reach) | 18/21, 21, 28 ... 84 (cadence 7x9) | matched |
| strike phase constant per life | 764/764 strikes | yes | matched |
| retarget only on a 6-tick clock from S, never within 24 ticks, ~half the checks | 161/161 switches on the clock; intervals 24 70x, 30 38x, 36 27x, 42 10x, 48 8x, 54 6x, 72, 90 (geometric p 0.44) | 6-player run: S+2, S+48, S+96 | fitted |
| targets: players in reach | 178/181 adjacent, 3 on its SW tile under it; never further | adjacent or under, not the middle, not the outer corners | fitted (see Open) |
| D: death seq `crab_boss_death` + `vfx_crab_boss_death` + `[facing] angle=` the cardinal toward the last target | 9/9 (5 rev 242 + 4 older) | D=88: 12482, 3454, face angle 0 (target due south) | matched |
| no splat on the crab from D, attackers stop | 4509 [652..655], [1744..1747]: 0 | `npc_die` | matched (engine sends `enabled_ops 0b00000`, see Open) |
| D+2: two lines, blocking loc `invisible_type8_blocking_size5` shape 10 rot 0 on its tile | 8/8 loc adds, 7/7 message pairs | 90: loc 32740 shape 10; both lines | matched |
| D+2: the crab glides +4,+2 (`teleport`) | 3 of 9 burrows (4509 [1746], 4508 [3836], 1095 [8552],[26781..2]) | not sent | Open (no rule found) |
| D+4: crab removed, shell added on its tile, spawnangle = face angle = the death angle, `crab_boss_remains_idle_spot` | 5/5 | 92: free 14779, spawn 14780, spot 3452, face 0 | matched |
| shell sparkle every 14 from R+13 | 4508 [3851..3977] 10/10 | 105..231 10/10 | matched |
| R+150 `crab_boss_remains_disintegrate` + `vfx_crab_boss_death`; loc gone R+153; npc gone R+156 | 4508 [3988],[3991],[3994]; 3258 [7763],[7766] (del +5) | 242, 245, 248 | matched |
| boss HUD `hp_hud_fade_in` x4 from D | 5/5 | not sent (engine) | Open |

## The strike

All six lives, every player on an `--all-players` transcript (4509, 4508): on a
`crab_boss_attack` tick every player in the crab's reach within Chebyshev 2 of
its target takes a splat on that tick.

| rule | recorded | ours | verdict |
|---|---|---|---|
| struck = in reach and within 2 of the target | 10,009 of 10,015 player-ticks (6 misses: two players, one stretch each); 0 of 2,000+ others; adjacent but 3+ from the target: 0/1,149 | 6-player test: 0 wrong of 17 strikes (pairs 2 apart struck together, 3 apart never) | matched |
| players 2+ tiles from the crab never struck, even within 2 of the target | 0 of 764 | reach test | matched |
| the middle of the 5x5 | no data; the wiki: safe from all sides | never targeted or struck (0/17 with a player there) | wiki |
| damage | 104 ones of 10,009; recorder 7 of 505 | `~npc_meleeattack` (attack 1, crush, max 1) | matched shape; rate Open |
| every recorder splat has a cause (§13b) | 505/505 on strike ticks (232 + 273) | — | matched |
| sounds | none from the crab; the struck player's armour block / `human_hit` synth, delay 20 | engine's block/hit synths | matched (generic) |
| the struck player's defend seq | yes (human_sword_def) | `anim(%com_defendanim)` | matched |

Accuracy: the recorder (defence 37..40, defensive style, snakeskin + glory, crush
+49) is predicted 14..15 ones in 505 by the standard roll with attack 1 / bonus 0;
7 were recorded. A negative crush attack bonus (about -32) would fit; not
changed without more players' gear (Open).

## Hits on the crab

| rule | recorded | ours | verdict |
|---|---|---|---|
| splat + headbar on every hit, the bar from the timer | yes | `npc_hitmark` + the reserve | matched |
| hit synth to the dealer, a 0 included | synth 10527 (`gem_stone_crab_hit_01`), 616/616 | `sound_synth(synth_10527)` on the splat tick | matched |
| no defend seq | none in 772 crab seqs | defend_anim null | matched |
| xp 87.5% | wiki | `combat_xp_multiplier` 875 | wiki (rev 242 stat packets transcribe garbled) |
| varbit 5934 +1 with chance min(d,10)/10 per hit | 595 increments over 1,453 recorder hits: 22/259, 20/116, 29/96, 46/122, 68/143, 58/95, 86/117, 86/111, 110/129, 62/62, 8/8 for d=1..11; per-login, never reset | same rule, at the landing; varp 1653 sent (9 points in the solo run) | matched, one tick late for melee (Open) |
| ruby bolts (e) treat it as 300 hp (60 dmg) | wiki | our ruby proc reads current hp (timer + reserve) | Open |

## Messages

| line | type | when | recorded | ours |
|---|---|---|---|---|
| The gemstone crab burrows away, leaving a piece of its shell behind. | game | D+2, near the nest | 7/7 | yes |
| The top three crab crushers were A, B, & C! | game | D+2 | 7/7 | yes (6-player test) |
| The top crab crusher was X! | game | D+2, under five dealers | wiki | yes (solo test) |
| `<col=005f00>`You gained enough understanding of the crab to mine from its remains. | game | D+4, top 16 | 1095 [8554],[9501]; 3258 [8724] | yes |
| Your understanding of the gemstone crab is not great enough to mine its shell. | game | on the op | 4508 [1671],[1672] | yes |
| You swing your pick at the crab shell. | spam | O | 1095, 3258 | yes |
| You mine an uncut <gem> from the crab shell. | spam | O+2, O+4, O+6 | 6 + 3 | yes |
| You have mined all you can from the shell. | game | re-click | 3258 [8763] | yes |
| You need a pickaxe to mine the shell. You do not have a pickaxe which you have the Mining level to use. | mesbox | no pickaxe | wiki | yes (UNVERIFIED) |
| You look inside the cave, but see a blockage further in. It would take something very big to clear it. | mesbox | its own nest's cave | wiki | yes (UNVERIFIED) |
| The burrowing crab forces you out of its way. | game | D+2, under it | wiki | yes (UNVERIFIED; the push tile is ours) |
| Your inventory is too full to hold any more gems. | mesbox | full pack | nothing | ours (UNVERIFIED) |

## Mining the shell

| rule | recorded | ours | verdict |
|---|---|---|---|
| the top sixteen by damage may mine; others refused on the op | 4508 refused x2; 1095, 3258 mined | ranking at D+2 by damage this life | matched (ranking: wiki) |
| O swing line + the pickaxe's mining seq; gems O+2/O+4/O+6 with synth `mine_ore` (3600); seq stopped with the third | 1095 [8557..8563], 3258 [8742..8748] | 94 seq 625, 96/98/100 synth 3600, seq -1 at 100 | matched |
| `total_gemstone_crab_kills` (4825) +1 with the eligibility line; `varlamore_gemstone_crab_reward_lifetime` (4796) +1 with the third gem; both sent and saved | 1095, 3258; at login in 1099/1103 | varp 4825=1, 4796=1 on the wire; perm | matched |
| gems | ruby/opal/opal, opal/opal/emerald, jade x3 | the wiki's table, 3 rolls | wiki |

## The caves

| rule | recorded (5 crawls) | ours (wire log) | verdict |
|---|---|---|---|
| L+1 face the cave (3x4) | 5/5 | 118 face_coord 1278,3167 3x4 | matched |
| L+2 `human_longcrawl_priority`, synth `climbing_loop` x3 delay 4, fade out (message "", overlay 174, 948(0,255,0,0,50), hp hud hidden + cc_deleteall) | 5/5 | 119, all of it | matched |
| L+3 minimap_state 2 + minimap_toggle 2 | 5/5 | 120 | matched |
| L+4 teleport, seq -1, face the tile beyond (N west, E south, S north) | 5/5 | 121 face_coord 1246,3039 | matched |
| L+5 fade in (0,0,0,255,50), minimap 0, busy 0 | 5/5 | 122 | matched |
| L+7 overlay closed | 3/3 | 124 | matched |
| destination: the crab's nest, or the next one while it is burrowed | 5/5 | test: next nest | matched |
| landings | E 1352,3122 x2, S 1246,3038 x2, N 1277,3168 x1 | the same tiles | matched |

## Npc info, block by block (npc_block_inventory.py, ours = solo run)

| npc | block | recorded | ours | verdict |
|---|---|---|---|---|
| gemstone_crab | sequence | 772 (attack, spawn, death) | 13 | matched kinds |
| gemstone_crab | spotanim | 3454 x8 (own); the rest players' spell impacts | 3454 | matched |
| gemstone_crab | facing | 189: player= targets, angle= at spawn (south) and death | player= and angle= | matched kinds; Jagex re-sends a held face when the target moves (fallbackangle), ours does not (engine) |
| gemstone_crab | hit / headbar | 37,937 / 36,935 | per hit | matched |
| gemstone_crab | enabled_ops | none | 1 (at D, `npc_die`) | Open (engine) |
| gemstone_crab | movement | idle; `teleport` +4,+2 at 3 burrows | idle | Open |
| gemstone_crab_remains | spotanim, facing, sequence | 19, 5, 1 | 12, 1, 1 | matched; "kinds recorded and never sent: 0" |
| Ru Merald x3 | walk, wander 1 tile | 825 steps in 9 tiles each | north and south seen in their 3x3s (solo run) | matched |

Multinpc: none of these is a multinpc.

## Vars on the wire

| var | recorded | ours |
|---|---|---|
| varp 1683 / varbits 6099, 6100, 12401 (boss HUD) | per tick | engine HUD, per tick (`param_509` 2) |
| varbit 5934 (varp 1653) | per point | sent (generated `transmit=no` corrected) |
| varp 4825, 4796 | on change and at login | sent, perm (generated `temp, transmit=no` corrected) |
| varbit 6719 minimap_state (varp 1021) | crawls | sent |
| varbit 12393 busy | crawls, mining | engine |
| varp 5991 hpbar_hud_show_extra_bar -1 -> 0 | once a session, the first fade anywhere (4336, 4500..4502 too) | not this content |
| varp 4514 tracking_damage_dealt_to_npcs | per hit | not modelled (generic tracking) |

## Ru Merald

No recording talks to him. Talk-to and Directions are the wiki transcript
(UNVERIFIED, moods neutral); the first conversation sets varbit 17223
`gemstone_crab_intro` (its base varp 4434 made perm, still not sent). Directions
use an 8-point compass from him to the nest: the three nests give exactly the
six directions the transcript names.


## Re-checked on index 4517, 4545..4554 (2026-11-12)

Eleven more of the owner's sessions (rev 242.1, w496/w378, melee and ranged):
31 burrows, 35 cave crawls, 3 eligible lines, no shell mined. Every cycle rule
above held; one new divergence.

| rule | recorded (new sessions) | verdict |
|---|---|---|
| next crab at D+29 | 14/14 where the recorder saw the add (4 others +43..53: arrived later) | matched |
| next nest never the same | 18/18 (N->E 6, E->S 5, E->N 3, S->N 2, S->E 2) | matched |
| full life S..D | 911..1095 (ours 902..1102) | matched |
| cave landings | north 1277,3168 (9), east 1352,3122 (15), south 1246,3038 (11): 35/35 on the test's tiles | matched |
| D+2 glide +4,+2 | 4 of 31 burrows (4547 [596..597] twice, +8,+4; [2527]; 4550 [1011]; 4552 [1374]) | Open 3, still no rule: E twice, S, N; the recorder adjacent each time |
| D face | `angle=` the cardinal of the last target 19/31, `[facing] type=reset` 12/31 | NEW Open 14 |

## How it was verified

- Private build `make -C src OPT=1 PLATFORM_OBJ_BASE=build_gemcrab sscompile
  torirsserver`; scripts compiled from a symlink mirror with HEAD's copies of two
  other sessions' half-done units (quest_animalmagnetism, macro_schedule; the
  untracked macro_certer left out); server pack from `cachepack pack --server-only`.
- `test/varlamore/gemstone_crab.lua`: SCRIPTRUN PASS (20 checks), tick log and
  `TORIRS_SCRIPTRUN_WIRELOG` read against the tables above.
- `test/varlamore/gemstone_crab_party.lua` (6 bots): SCRIPTRUN PASS, 0 wrong of
  17 strikes, the middle never struck, the top-three line.

## Open

1. `npc_die` sends `enabled_ops 0b00000` at D; Jagex sends none for the crab.
   Removing it needs an engine death that keeps the menu (engine, not content).
2. The boss HUD's `hp_hud_fade_in` x4 at the death is not sent (torirs_server_hpbar.c
   says it is not modelled for any boss).
3. The crab's `teleport` +4,+2 at D+2 (3 of 9 burrows) is not modelled: no rule found.
4. Varbit 5934 is counted at the landing, one tick after Jagex's for melee; moving
   it to the swing needs a hook in combat_stats.rs2 (another session's file).
5. Ruby bolts (e) and any effect reading the crab's current hitpoints see the
   timer plus the reserve (player_hit_npc_prepare.rs2 / combat procs, another
   session's files); the wiki says Jagex treats it as 300 hp.
6. Accuracy: the recorder's 1-damage rate is half the standard roll's prediction;
   a crush attack bonus is not fitted.
7. Targets under the crab other than its SW tile, and its outer diagonal corners,
   have no data; the middle is excluded from the wiki line.
8. Jagex re-sends a held entity face whenever the target moves (its fallback
   angle); our engine sends a face only when the target changes.
9. The 1776 life: its first target stood 2 tiles off and the first strike waited
   for a second target; ours only targets players in reach.
10. Examine texts (`desc=`) reach our client only through a client cache bake;
    the cave's examine stays "I can go through here." (the wiki says "I wonder
    where this could lead to..."; examine.loc is generated, not edited here).
11. Unrecorded: the no-pickaxe, full-pack and gem-bag branches, the shove, the
    single-crusher line, Ru Merald's dialogue, the gem table.
12. The Tlati nests have no multi-combat zone in `maps/multiway.csv` (2004 data):
    the crab fights multi through `forcemulti`, but the multiway indicator is not
    shown there; and the Tlati wildlife the recordings show (flies, parrots,
    skinks, monkeys, jaguars, toucans) is not spawned in our world.
13. The tests shorten one life (`::gemcrab_here`); the world's own first crab
    (spawned at boot by the south Ru Merald's timer) is replaced at tick 16, so a
    full 900..1100-tick life has not been run end to end.
14. The death face is `[facing] type=reset` on 12 of 31 burrows in 4517, 4545..4554
    (every nest, every direction; in 4548 --all-players the last target was in
    reach for the resets too), where ours always sends the cardinal angle. No
    rule found; the shell's spawnangle follows the angle when one is sent.
