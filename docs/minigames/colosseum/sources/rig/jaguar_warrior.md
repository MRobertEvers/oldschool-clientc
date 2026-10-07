# Rig pass: jaguar_warrior (colosseum)

npc `colosseum_jaguar_warrior`, id 12810, size 2, cache_npc.txt line 76.
Bound: readyanim human_ready (808), walkanim human_walk_f (819), walk_b (820), walk_l (821), walk_r (822).

## Rig
All five bound animations are on framemap 0, the shared human rig (3905 sequences).
The join does NOT close the set: every candidate below is tier `name`, matched by the
words "jaguar" (and colosseum, colossi, arena, fremennik, sunfire, minotaur were also tried).

## Candidates (11 rows in jaguar_warrior.tsv)
- bound 5: ready and four walks.
- name, on rig 0: attack1 npc_jaguar_ranger_claws_attack (10847, 63 client ticks, 3 swipe sounds named
  varlamore_jaguar_warrior_*), defend npc_jaguar_human_unarmed_def (10848), death
  npc_jaguar_human_death (10849; 47 ticks then a 20000-tick final hold).
- name, OFF-RIG (framemap 1969, quadruped jaguar family): 12498, 12499, 12491, 12492. Not this npc.
  Listed so nobody grabs them by name.
- No spawn, special or transition candidate was found.

## Unknown
Everything else on rig 0 (about 3,900 sequences) stays `unknown` and is not listed.
Whether 10847's name ("ranger") belongs to this warrior is unproven; only a recording or plugin
constant can settle it. Likewise the death timing and whether 10848 is the hit flinch.

## Graphics
No spotanim in cache_spotanim.txt has a name sharing "jaguar". None listed.

## Ledger
npc_combat/c/colosseum_jaguar_warrior.combat has death_anim, attack_anim and defend_anim all `-`
(rig shared, only a name could tell). No disagreement; the ledger is silent where a name candidate exists.

## Needs outside evidence
Blert or plugin attack animation ids for the Jaguar warrior; a picture to confirm 10847/10849 on this model
(models 50748, 53213, 53214).
