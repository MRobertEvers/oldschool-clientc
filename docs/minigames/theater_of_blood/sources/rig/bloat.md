# Bloat rig join (ToB, rig pass 2026-10-03)

Rows: sources/rig/bloat.tsv (41 rows, written by build/rig_state/.../bloat_rig.py, which imports tools/gen_npc_combat.py as a module).

## NPCs (cache_npc_bloat.txt)
tob_bloat 8359, tob_bloat_story 10812, tob_bloat_hard 10813. All size 5, readyanim tob_bloat_ready (8080), walkanim tob_bloat_walk (8081). No other animation field. All three sit on ONE framemap, 1823 (npc_rigs.csv:7738, 9420, 9421). Ready and walk share it. Not a shared rig: only Bloat uses 1823.

## Candidates
Rig 1823 holds 5 sequences (per npc, 15 rows over the three records):
- 8080 tob_bloat_ready: ready, bound. 58 cycles, no sound.
- 8081 tob_bloat_walk: walk, bound. 2.0 ticks, footstep sounds 3931 f7, 3998 f16.
- 8082 tob_bloat_sleep: special, rig+name. 124 frames, 33.0 ticks, forcedpriority 8. Frame sounds: 3998 f7, snore 3976 on f26..f66, foot_unstick 3935 f89, shout 3545 f101, footstep 3931 f103. The Jagex name says "sleep"; that it is the whole down (lie, stomp, rise) rests on our own tick log and the spec pass, not on a Jagex name. Tier for "down" is rig+name at most.
- 8085 tob_bloat_death: death, rig+name. 25 frames, 3.0 ticks, no frame sound.
- 9031 tob_bloat_walk_pet: walk, rig+name (pet variant; no npc record names it).
Attack: NONE on the rig. No sequence on 1823 is named attack, and the fly attack is a projectile spotanim, not an npc animation. Spawn: none. Defend: none (the ledger agrees: null).
Off-rig Bloat-named sequences (tier name, listed once per npc): flies_large 8083 and flies_small 8084 (framemap 1815, shared with the Nightmare flies and Colosseum bees), chamber_idle 8086 (1827), swinging_chain 8087 (1826), falling_flesh 8088 (1828), stunned_short 8089 (framemap 905, 12 sequences).
Spotanims (tier name, with anim): 1568 flies_large (8083, 2.0 ticks), 1569 flies_small (8084), 1570-1573 falling_flesh1-4 (8088, 5.6 ticks), 1575 stunned (8089, 3.07 ticks), 1576 blood_splat (anim failedspell 653, 0.63 ticks, a HUMAN-rig seq; the name binds only the graphic).

## Stays unknown
Nothing on the rig is unknown: all five are classified. Unknown is whether the real Bloat plays 8082 only on the down or also when stunned/hit, and what animation, if any, the real client shows on a hit (no defend seq exists).

## Ledger disagreements (npc_combat/t/tob_bloat{,_hard,_story}.combat; compiled in npc_anims.generated.npc:44391)
- attack_anim = tob_bloat_sleep (all three): comment says "rig's only forcedpriority=6 seq"; its forcedpriority is 8 (ready 1, walk 5, death 11). The comment's evidence is wrong, and 8082 is the 33-tick down sequence, not an attack. It is on the rig but its role is not attack. Harmless only because retaliate=no and every Bloat attack is scripted; any generic attack path would play the down sequence.
- defend_anim null: agrees with the rig.
- death_anim tob_bloat_death: agrees (rig+name).
- Ledger headline "5 sequences" agrees with the join. The .combat files carry a "NOT COMPILED" banner but the generated npc file does hold the three rows, so the banner is stale. [closer: confirmed -- the server overlays every repeated [gameval] block onto one record in directory order (torirs_server_content.c:2030-2041), and minigames/ sorts before npc/, so the generated attack_anim (npc_anims.generated.npc:44393, combat_stats.generated.npc:27273) lands on top of tob.npc:686, which states no anim. The banner's "an authored block always wins" describes an older loader.]
No row names an animation off the rig or one owned by another monster.

## Spec disagreements (encounters/bloat.tsv .av. rows)
- bloat.av.down.seq 8082, grade C [plugin][cache]: on rig 1823; role "down" is not stated by the Jagex name (sleep); the 33-tick fit is our tick log. Tier deserved: rig+name for "sleep", role down measured only. C is defensible; do not raise.
- bloat.av.idle.seq 8080 A, walk.seq 8081 A: bound, correct. death.seq 8085 C: rig+name, correct.
- bloat.av.hand.shadow_seq 8088 A: framemap 1828, not Bloat's rig; the bind is the spotanim record, so A rests on that record, not on the npc join. Fine, but it is not an npc sequence.
- bloat.av.room.chamber_anim 8086 A and chain_anim 8087 A: bound by loc records, off the npc rig, correct.
- fly.proj/fly.gfx 1568/1569 D and stun_gfx 1575 D: tier name, correct (graphics have their own rigs).
No spec row names a sequence an npc plays that is off that npc's rig.

## Unused candidates
Only 9031 tob_bloat_walk_pet (rig+name, the pet's walk): no spec row and no script uses it, correct for the room. 8082, 8085, 8080, 8081 are all used by the spec and tob_bloat.rs2 (npc_anim(tob_bloat_sleep) at line 706).

## Only a recording or plugin could settle
Whether the live down plays 8082 exactly once with no cut, whether the hit reaction has an animation (none on the rig), whether the real death plays 8085 on the kill tick or the tick after (ours +1), and the sound rows 3976/3935/3545/3931 as client-side frame sounds.

## Closer audit (rig-tob closer, 2026-10-03)
(1) 8082 is on 1823 with 124 frames, and tob_bloat.combat:20 names it as the attack: holds. (2) The tsv note gives forcedpriority=8, and the generator's a4 string is hardcoded to "forcedpriority=6" for the whole FP_LIVE set {6,7,8} (tools/gen_npc_combat.py:129,832): holds, and this is a generator wording bug. (3) The banner is stale: confirmed above. No strikes.
