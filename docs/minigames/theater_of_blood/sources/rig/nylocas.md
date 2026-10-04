# Nylocas rig join (rig worker, 2026-10-03)

Built by build/rig_state/matthew-mbp-m4-raid-b1-rig-tob/rig_nylocas.py (imports tools/gen_npc_combat.py loaders, writes nothing else).
Rows: nylocas.tsv (one row per candidate per npc, 1477 rows; spotanims once under npc "(room)").

## NPCs (55, three modes)
Normal 8342-8358, Entry "_story" 10774-10790, Hard "_hard" 10791-10811. Per mode: 12 wave npcs
(small/big x incoming/fighting x melee/ranged/magic), 4 Vasilias (spawning, melee, magic, ranged; size 4),
1 support (size 3); Hard adds 4 Prinkipas (10803-10806, size 3). Prinkipas is NOT in
cache_npc_nylocas.txt (ids from configs/all.npc and npc_rigs.csv). Sizes: small 1, big 2.
Ready/walk are always the form's own top_spider_<style>_idle/_walk: both on one framemap per npc.

## Rigs
- 1800 melee form (also Vasilias spawning form, Prinkipas spawning/melee): 38 sequences, 9 are top_spider_*.
  The other 29 belong to other monsters by name (Araxxor araxytes 17, Hosdun spider boss 6, Sotetseg gauntlet spider 4,
  crystal spider 2): tier "rig", role unknown, NOT candidates. Shared rig, so narrowed by the ready stem.
- 1799 magic form: 9 sequences, all top_spider_magic_*. 1801 ranged form: 9, all top_spider_ranged_*. Both closed.
- Support 8358/10790/10811: no readyanim, no walkanim, no rig, no candidate. Its pillar seqs 8073 and 8074
  (nylocas_pillar_*, framemap 1832, loc animations) and the Nylocas Queen seqs 14381-14386 (framemap 1810,
  another monster) match by name word only: tier "name".
Candidates: 56 seq on the three rigs (27 top_spider_*, 29 other-monster), 8 name-only seqs, 10 spotanims.

## Candidates by role (all "rig+name" unless noted; the cache name states the role)
- ready/walk (bound): 8002/8003 melee, 7988/7987 magic, 7993/7997 ranged.
- attack: 8004 melee (18f, 2t, sound 3932@f6), 7989 magic (19f, 2t, 4006@f6 and f9), 7999 ranged (15f, 2t, 3953@f2, 3966@f10).
  Also meleeattack 7990 magic, 8001 ranged (the ranged/magic forms' own melee swing; sounds 3982, 3993) and 5 "_quiet" attack variants
  (14387, 14390, 14391, 14392, 14393) with synth_ sound ids that have no pack name: "quiet" does not state a role beyond attack.
- special: death_detonate 8006 melee, 8000 ranged, 7992 magic (47/47/50 frames, 2.0 ticks, sounds 3294, 3967, 3550). Name says death_detonate;
  "special" here means self-destruct, a role the tier cannot say more about.
- spawn: 8075 top_spider_melee_spawn and 9030 _spawn_noloop (16f, 2t, 3546 web_spawn). Melee rig only; no magic/ranged spawn exists.
- death: 8005, 7998, 7991 (27/27/30 frames, 4.3 ticks, three named sounds each); quiet deaths 14388, 14389, 14394.
- defend: NONE on any of the three rigs. Unknown/other roles: the 29 other-monster seqs.
Tier "rig+sound": none needed (all sounds are pack-named but add no role beyond the seq name).

## Graphics (tier name)
1562-1564 death_standard -> anim 8005/7998/7991; 1565-1567 death_detonate -> 8006/8000/7992 (all on the right rig).
1559-1561 rangedprojectile size1/mid/size2: no anim field (static model). 1558 tob_nylocas_shielded: anim is druidicspirit_druidsshield_spotanim
(seq 1102, framemap 607): another monster's animation by name, so the immune graphic is borrowed art, not Nylocas art.

## Ledger (npc_combat .combat, 55 files checked, including miniboss_hard)
No disagreement. Every attack_anim, death_anim is top_spider_<own style>_{attack,death} and on the npc's rig; defend_anim is null everywhere
(correct: nothing on the rigs); the 3 supports are all "-" (correct). attack/death sound rows are "-" with "carried in-band": consistent with the seqs.
Not a disagreement but worth knowing: a small's ledger death (4.3 ticks) is not what the spec says it plays (one tick, then the graphic).

## Spec (encounters/nylocas.tsv, 30 .av. rows)
All seq ids named by the rows are on the rig of the npc that plays them, and each quoted frame sound matches the cache (3932; 3953/3966; 4006x2; 3294/3967/3550;
death sounds 3980/4011/3992, 3978/3232/3943, 4010/3289/3984). Grades fit tier: idle/walk A (bound), gfx_seq A (spotanim record's own anim),
attack/death/detonate/land C or D (name tier, measured on tick log). No row claims above its tier. Spec disagreements: none.
One watch item: wave_attack.proj and wrong_style.gfx are D by scale match / name, and 1558's anim is Druidic Spirit's shield.

## Unused by any spec row or room script
Name-stated roles nobody uses: 7990 magic_meleeattack, 8001 ranged_meleeattack, and the 8 "_quiet" seqs (14387-14394).
Unreferenced by script name but in spec: 8005/7998/7991 (ledger-driven). Used: 8004 7999 7989 8006 8000 7992 8075 9030.

## What only a recording or a plugin constant can settle
- Whether a Nylocas ever plays the meleeattack seq (an adjacent ranged/magic thrower?) and what "_quiet" means (Hard? Entry? no-sound?).
- Whether Prinkipas and Vasilias play 8075 or 9030 on landing in a given mode; whether a big one has a different death.
- Which of the three death-shaped animations plays when a nylocas is hit to 0 versus the turn animation blert describes.
- Role of the 29 shared-rig other-monster seqs: they are certainly not Nylocas.

## Closer audit (rig-tob closer, 2026-10-03)
(1) The supports have no row in npc_rigs.csv, and tob_nylocas_support.combat:15-17 are all "-": holds (seam8's human-default fault is gone). (2) 1558's anim 1102 is druidicspirit_druidsshield_spotanim on framemap 607: holds. (3) The Vasilias and wave ledgers (n/nylocas_boss_*.combat, t/tob_nylocas_big_fighting_magic.combat:15-16) name top_spider_<own style>_{attack,death}, and npc_rigs.csv gives each one rig: holds. No strikes. "No ledger disagreement" stands.
