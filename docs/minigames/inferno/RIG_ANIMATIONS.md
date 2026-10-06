# Inferno: which animations each monster can play (rig pass)

Rig pass `matthew-mbp-m4-waves-b1-rig-inferno`, closed 2026-10-03. Method: `docs/minigames/waves_loop/RIG_PASS.md`
(the npc record binds ready/walk; their framemap closes the candidate set; the Jagex name classifies within it).
Tables: `sources/rig/<unit>.tsv` (139 rows over 9 units) and `sources/rig/<unit>.md`.
Tiers: `bound` (npc record names it), `rig+name`, `rig+sound`, `rig` (role unknown), `name` (no rig join).
No Inferno sequence on any of these rigs carries an in-band frame sound, so no row is `rig+sound`.

**Blert** marks an id that the Blert plugin's attack table names for that npc
(`sources/blert/plugin/src/main/java/io/blert/challenges/inferno/InfernoNpc.java:33-76`, read by the closer).
It is corroboration from a load-bearing plugin constant, not a tier; the tables keep the method's tiers.

**What compiles.** Every combat npc's generated ledger (`OSRS-Content/osrs239-content/npc_combat/i/inferno_*.combat`)
says `NOT COMPILED`: the authored blocks in `MI/configs/inferno.npc` win (MI = `server/scripts/minigames/minigame_inferno/`).
A ledger disagreement below is a defect in the ledger, not in what the game plays today; the authored value is given beside it.

## Jal-Nib (nibbler, 7691) -- rig 1688, closed, 5 seqs
- ready 7573 `jalnib_ready`, walk 7572 (bound). attack 7574 `jalnib_attack` (rig+name, 2.0 ticks), defend 7575, death 7576 (rig+name).
- Spawn, special: none on the rig. No spotanim shares a name word. Blert lists no nibbler attack. Ledger and authored: agree.

## Jal-MejRah (bat, 7692) -- rig 1695, closed, 4 seqs
- ready = walk 7577 (bound). attack 7578 `jalmejrah_attack` (rig+name, Blert BAT_AUTO), defend 7579, death 7580 (rig+name).
- Spotanim 1382 `inferno_harpie_proj` (name). 541 `slayer_harpie_splat` shares only "harpie": listed, not a candidate.
- Spawn, special: none. Ledger and authored: agree.

## Jal-Ak and the Jal-AkRek bloblets (7693-7696) -- rig 1687, closed, 7 seqs, shared by all four
- ready 7586, walk 7587 (bound); attacks 7581 `jalak_attack_magic`, 7582 `_melee`, 7583 `_ranged`; death 7584 (1.5 ticks); defend 7585 (all rig+name).
- Blert: Jal-Ak 7581/7582/7583; Jal-AkRek-Mej (7694) 7581; -Xil (7695) 7583; -Ket (7696) 7582.
- Spotanims (name): 1380 `inferno_splitter_mage`, 1381 `inferno_babysplitter_mage`, 1378 `inferno_splitter_range`,
  1379 `inferno_babysplitter_range`. Which npc fires which is unsettled (closer corrected two notes that assigned them).
- Spawn (the split): no sequence on the rig names one. Special: none.
- **Ledger:** all four get `attack_anim = jalak_attack_magic` (a3 rank). Wrong for 7696 and 7695 (RIG-5, RIG-6).
  Authored (inferno.npc:666/696/718/743): 7693 melee, 7696 melee, 7694 magic, 7695 ranged; agrees with Blert.

## Jal-ImKot (meleer, 7697; 12594 the small copy) -- rig 1690, closed, 7 seqs
- ready 7595, walk 7596 (bound). attack 7597 `jalimkot_attack` (rig+name, Blert MELEER_AUTO), defend 7598, death 7599 (4.6 ticks).
- 7600 `jalimkot_digdown` (3.7 ticks) and 7601 `jalimkot_digup` (3.5 ticks): transition, rig+name; Blert names 7600 MELEER_DIG.
  7601 is the only "spawn-like" candidate: the emerge after a dig. Not called spawn: the name says dig up.
- Ledger and authored: agree.

## Jal-Xil (ranger, 7698; 7702 the Zuk-wave copy) -- rig 1693, closed, 6 seqs
- ready 7602, walk 7603 (bound). attacks 7604 `jalxil_attack_melee` (2.0 ticks), 7605 `jalxil_attack_ranged` (3.0 ticks);
  death 7606, defend 7607 (rig+name). Blert: 7605 RANGER_AUTO, 7604 RANGER_MELEE.
- Spotanim 1377 `inferno_xil_projectile` (name; static model, no anim).
- **Ledger:** `attack_anim = jalxil_attack_melee` (a3 rank) for both copies; the auto attack is 7605 (RIG-7, RIG-8).
  Authored inferno.npc:289/500 states the same melee default; whether the scripts play 7605 per ranged attack is not checked here.

## Jal-Zek (mager, 7699; 7703 the Zuk-wave copy) -- rig 1686, closed, 6 seqs
- walk 7608, ready 7609 (bound). attacks 7610 `jalakxil_attack_magic`, 7612 `jalakxil_attack_melee`; death 7613 (rig+name).
- Special 7611 `jalakxil_resurrect` (72 frames, 6.0 ticks, rig+name; Blert MAGER_RESURRECT).
- Defend: the rig has none (ledger and authored both `null`). Blert: 7610 MAGER_AUTO, 7612 MAGER_MELEE.
- Spotanim 1376 `inferno_zek_projectile` (name; its anim is `zuk_proj`).
- Ledger agrees (magic). Authored inferno.npc:323/521 defaults to the melee variant 7612; not checked against the scripts.

## JalTok-Jad (7700; 7704 the Zuk-wave copy) -- rig 1692, closed, 9 seqs
- walk 7588, ready 7589 (bound). attacks 7590 `_attack_melee` (2.4 ticks), 7592 `_attack_magic` (5.3 ticks), 7593 `_attack_ranged` (1.8 ticks);
  defend 7591; death 7594 (5.1 ticks) (all rig+name). Blert names all three attacks.
- Unknown: 8857 `jaltokjad_walk_pet`, 8858 `jaltokjad_chathead_pet` (tier rig; the name points at a pet, not this npc).
- Graphics: no spotanim shares a Jad name word. The code's fire-spit set (447-451, 157; AV_INVENTORY.md:128) is not a rig candidate.
- Ledger: `attack_anim = jaltokjad_attack_melee` (a3). Not counted as a disagreement: Jad's name states no style and the
  single slot cannot hold three attacks. Authored agrees with the ledger.

## Yt-HurKot (Jad healer, 7701; 7705) -- rig 163 (`lizard_cleric_*`), closed, 6 seqs
- walk 2634, ready 2636 (bound). attack 2637 (Blert JAD_HEALER_AUTO), defend 2635, death 2638, heal 2639 (other, rig+name).
- 2638's last frame is held 20,000 client ticks (all.seq:47653): 3.0 ticks of motion, then a corpse hold. Closer corrected the table.
- Ledger and authored: agree. The rig is shared with the lizard clerics by its own record, not by resemblance.

## TzKal-Zuk (7706) -- rig 1691, closed, 6 seqs
- ready 7564 `zuk_idle` (bound; no walkanim). attack 7566 `zuk_attack` (3.0 ticks, Blert ZUK_AUTO), defend 7565, death 7562 (5.0 ticks).
- Spawn 7563 `zuk_spawn` and 13717 `zuk_spawn_no_rock` (both 7.0 ticks, rig+name). Which plays when: unknown.
- One attack sequence only: the rig cannot separate Zuk's attack styles.
- Spotanims (name): 1375 `inferno_zuk_projectile`, 2261 `_small`, 2381 `_mid`, 2382 `_mid_short`, 3294 `_gigantic`; purpose of the last four unknown.
- Ledger and authored: agree.

## Jal-MejJak (Zuk healer, 7708) -- rig 19 (`dagannoth_water_creature_*`), closed, 7 seqs
- walk 2863, ready 2867 (bound). attack 2868, defend 2869, death 2866 (7.9 ticks) (rig+name).
- Transitions 2864 `_spring_up` (1.4 ticks) and 2865 `_go_down` (2.9 ticks): the spawn and exit candidates. Blert lists no attack.
- No spotanim shares a name word.
- Ledger agrees with the rig. **Authored** inferno.npc:440 sets `death_anim` to 2865 `_go_down`, leaving the named death 2866 unplayed (RIG-9);
  inferno.npc:439 defend = walk is INF-AV-004; no spawn animation is INF-AV-006.

## Ancestral Glyph (7707) -- rig 1697, closed, 3 seqs
- ready = walk 7567 (bound). death 7569 `moving_safe_spot_death` (4.0 ticks, rig+name).
- 7568 `moving_safe_spot_hit` (2.0 ticks, forcedpriority 6): role unknown, tier rig. Blert lists no attack for 7707.
- **Ledger:** `attack_anim = moving_safe_spot_hit` (a4, forcedpriority rule) claims a role the cache does not state (RIG-10).
  Authored inferno.npc:45/47 uses it for both attack and defend.

## Rocky supports (7709 `inferno_invisible_3x3`, 7710 `inferno_safespot_dying`) -- no rig
- Neither record has a readyanim. 7561 `safe_spot_distructible_pillar_collapse` (2.0 ticks) sits alone on framemap 1700:
  tier name for both (death candidate for 7710). Nothing in the cache ties it to either npc. See INF-AV-002, INF-AV-003.

## Unknown, across the pass
- Which spawn sequence Zuk plays when; which projectile spotanim each Zuk, bloblet and Jal-Ak attack launches.
- Whether Jal-MejJak plays `_spring_up`/`_go_down` on entry and exit; what `moving_safe_spot_hit` is for.
- The jad pet sequences 8857/8858 on the combat rig (left `unknown`; nothing says combat plays them).
- No sounds: every candidate's frame-sound field is empty; Inferno combat sounds are not in-band.

## Ledger disagreements (one row each in waves_loop/CONTENT_BUGS.md)
| id | ledger file:line | ledger says | rig evidence |
|---|---|---|---|
| RIG-5 | npc_combat/i/inferno_creature_splitter_melee.combat:20 | jalak_attack_magic (a3) | 7582 jalak_attack_melee on rig 1687; npc word "melee"; Blert BLOBLET_MELEE_AUTO |
| RIG-6 | npc_combat/i/inferno_creature_splitter_range.combat:20 | jalak_attack_magic (a3) | 7583 jalak_attack_ranged on rig 1687; Blert BLOBLET_RANGED_AUTO |
| RIG-7 | npc_combat/i/inferno_creature_ranger.combat:20 | jalxil_attack_melee (a3) | 7605 jalxil_attack_ranged on rig 1693; Blert RANGER_AUTO |
| RIG-8 | npc_combat/i/inferno_ranger_finalwave.combat:20 | jalxil_attack_melee (a3) | as RIG-7, Blert ZUK_RANGER |
| RIG-10 | npc_combat/i/inferno_moving_safespot.combat:20 | moving_safe_spot_hit as attack (a4) | name says hit; role unknown; Blert has no attack for 7707 |

RIG-9 is against the authored block, not a ledger. No ledger names an off-rig sequence or another monster's
animation; the Colosseum-style failure (RIG-1..4) does not occur here.

## Sampling by the closer
45 rows opened (five per unit) against `cache_npc.txt`, `cache_seq.txt`/`configs/all.seq`, `cache_spotanim.txt` and the
framemap catalog `out/osrs239_anims` (script `build/rig_state/matthew-mbp-m4-waves-b1-rig-inferno/close_sample.py`):
ids, names, frame counts, lengths and rig membership all held. Corrected: two blob spotanim notes (npc attribution above
evidence), two Jad-healer death lengths (hold frame), one glyph md guess. 541 is absent from the dump but present in all.spotanim.

## What promotes a candidate
- Blert's attack tables and plugin constants: landed for attacks (InfernoNpc.java above). Still needed from the corpus:
  spawn, death and projectile ids (Blert's event protos, `sources/blert_api/observed_npc_events.tsv`, the other plugins under `sources/`).
- A recorded fight (Blert challenge JSON) naming the animation id per tick, for Zuk's spawn variants and the healer transitions.
- A picture taken by the test driver of each candidate playing on its npc, for every `rig` and `name` row.
- For the defaults in `inferno.npc` (ranger and mager melee): a read of the attack scripts showing which id each attack plays.
