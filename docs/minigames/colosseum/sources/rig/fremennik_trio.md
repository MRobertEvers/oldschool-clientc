# Rig pass: fremennik_trio (colosseum)

Npcs (cache_npc.txt): colosseum_warbander_ranged_female 12814 (line 219, "Fremennik warband archer"),
colosseum_warbander_mage_male 12815 (line 256), colosseum_warbander_melee_male 12816 (line 293),
colosseum_human_gib 12828 (line 590; model 51247 only, no animation fields, interactable=no).

## Rig
All three warbanders sit on framemap 0, the shared human rig (3905 sequences): the join does NOT
close the set, so every non-bound candidate is tier `name`, matched by the words warband, warbander,
fremennik, archer/mage/melee, colosseum. Mage is bound to human_staffready (ready) and
human_halberdwalk_f/b/l/r; melee and ranged to human_ready and human_walk_f/b/l/r.
The gib has no ready or walk animation, so it has no rig at all.

## Candidates: 32 rows in fremennik_trio.tsv
bound 15 (5 per warbander), name 17.

## Attack, defend, death (all tier `name`, on rig 0)
- ranged attack1 10850 npc_fremennik_warbander_archer_att_colosseum (58 ticks, frame 6 arrow_launch);
  defend 10851; death 10852 (10 frames, then a 20000-tick final hold, sound human_death).
- mage attack1 10853 npc_fremennik_warbander_mage_zaros_vertical_casting_walkmerge (66 ticks, frame 1
  sound varlamore_fremennik_mage_fire_cast_01; a walkmerge, so it casts while walking); defend 10854; death 10855.
- melee attack1 10856 npc_fremennik_warbander_melee_human_sword_stab (39 ticks, frame 2 stabsword_stab);
  defend 10857; death 10858.
Only one attack per monster was found; no special, spawn or transition candidate.

## Graphics
No spotanim name shares warband, warbander or fremennik. For the gib, the eight
vfx_colosseum_human_explosion_01..08 (2713-2720) share its words "colosseum" and "human"; all use
model 46421 and anim vfx_scarab_explosion01 (18 frames, 2 game ticks). Tier `name`, role `other`:
the cache does not tie them to the gib. Four sibling gibs exist (manticore, minotaur, colossi, final boss).

## Unknown
Everything else on rig 0 stays `unknown` and is not listed. The gib has nothing on any rig.

## Ledger (OSRS-Content/osrs239-content/npc_combat/c/*.combat)
All four files have death, attack and defend `-` and sounds `-`. No disagreement: the ledger names no
animation, while three name candidates per warbander exist (it is silent, not wrong).

## Needs outside evidence
Blert or plugin attack animation ids for the three warbanders (confirm 10850/10853/10856), the
projectile graphic for the archer and mage (none by name), whether the gib plays any animation or
only a spotanim explosion (a recording), and the picture that promotes the death/defend names.
