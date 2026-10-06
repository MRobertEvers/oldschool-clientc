# Rig pass: manticore (colosseum)

npcs: `colosseum_manticore` (id 12818, size 3, model 52612, cache_npc.txt line 367) and
`colosseum_manticore_gib` (id 12829, size 3, model 51233, line 596).
Bound (manticore): readyanim npc_manticore_01_idle (10863), walkanim npc_manticore_01_walk (10864, also _l and _r),
walkanim_b npc_manticore_01_backwards_walk (10865). The gib has NO animation fields.

## Rig
Manticore: framemap 2249, a private rig of 9 sequences, all named npc_manticore_01_*. The join closes the set:
every row is on the rig and its Jagex name belongs to this monster. Gib: no ready anim, so no rig.

## Candidates (17 rows in manticore.tsv)
- bound 3: ready, walk, backwards walk.
- rig+name 6: death 10866 and death_explode 10867 (both 120 client ticks, 4 game ticks, sounds
  manticore_death_roar02 and manticore_death_impact01..03); transition 10868 triple_charge (120, roar and
  projectile sounds; closer: was special, which nothing states; charge = windup by name, the projectile sound
  means it may itself release); attack1 10869 triple_throw (90, whoosh and projectile sounds); spawn 10870 and 10871 (30 each, silent).
- name 8: 7 spotanims (below) and 1 gib row (nothing to bind).
- No defend (flinch) candidate exists on the rig. Attack variants are only the triple pair.

## Graphics (tier name)
Spotanims 2681 magic, 2683 ranged, 2685 melee projectiles and 2682, 2684, 2686 impacts. Their anim fields are the
LEVIATHAN family (vfx_leviathan_01_projectile_*, 60 client ticks; impact 30), so they are shared art; which
projectile a given Manticore attack throws is not stated. 2721 vfx_colosseum_manticore_explosion_01 (60 ticks,
model 46421) probably pairs with death_explode and the gib npc. seq vfx_colosseum_manticore_gib_01 has 0 frames and no spotanim.

## Unknown
Which of the three styles (magic, ranged, melee) triple_throw and triple_charge fire, and the timing of the
projectile releases. Whether 10867 or 10866 is the death used in the arena. What the spawn pair means
(arena entry versus a post-gib respawn). Hit flinch: nothing.

## Ledger
npc_combat/c/colosseum_manticore.combat: death_anim npc_manticore_01_death (on rig, agrees); attack_anim and
defend_anim null (agrees: nothing states an attack, though 10868/10869 are now candidates); sounds `-` but death
carries in-band synth. colosseum_manticore_gib.combat: all `-`. Disagreements: 0.

## Needs outside evidence
Blert or plugin animation and projectile ids for the Manticore, a recording of its triple attack, and a picture
to confirm the death variant and the gib model on death.
