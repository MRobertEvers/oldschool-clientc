# Rig pass: glyph_and_pillars (Ancestral Glyph and the Rocky support pillars)

Method: docs/minigames/waves_loop/RIG_PASS.md. Documents only; nothing run or rewritten.

## The npcs (cache_npc.txt)
- 7707 inferno_moving_safespot, Ancestral Glyph, size 3, model 33036 (line 536). readyanim and walkanim both moving_safe_spot_ready.
- 7709 inferno_invisible_3x3, Rocky support, size 3, model 32999 (line 571). No animation field at all.
- 7710 inferno_safespot_dying, Rocky support, size 3, model 33046 (line 582). No animation field at all.

## The rig
- Glyph: one framemap, 1697, with exactly 3 sequences, all moving_safe_spot_*. Closed; no name narrowing.
- 7709 and 7710 have no readyanim, so no rig can be derived (the ledger says "no rig" too).
- Off-rig candidate: safe_spot_distructible_pillar_collapse 7561 (22 frames, 2.0 ticks) is alone on framemap 1700. Offered by name words only (safe_spot, pillar); nothing in the cache ties it to an npc.

## Candidates: 6 rows in glyph_and_pillars.tsv (bound 2, rig+name 1, rig 1, name 2)
- ready 7567 and walk 7567 (same sequence, 3.0 ticks): bound.
- death 7569 moving_safe_spot_death (42 frames, 4.0 ticks, forcedpriority 10): rig+name.
- 7568 moving_safe_spot_hit (20 frames, 2.0 ticks, forcedpriority 6): role unknown, tier rig. Jagex's name says hit, not attack or defend.
- 7561 pillar collapse: 7710 death (name), 7709 unknown (name).
- No attack, special or spawn candidate. No frame sound on any sequence.

## Graphics
- No spotanim in cache_spotanim.txt shares a name word with the glyph or the pillars (checked glyph, safespot, rocky, pillar).

## Unknown
- 7568 moving_safe_spot_hit: attack, defend or a hit reaction.
- Whether 7709 plays anything; whether 7561 belongs to 7709, 7710, or a loc.

## Ledger (OSRS-Content/osrs239-content/npc_combat/i/*.combat)
- inferno_moving_safespot: death moving_safe_spot_death (right), attack moving_safe_spot_hit (a4, only forcedpriority 6 seq), defend null, sounds "-".
- Disagreement 1: attack_anim = moving_safe_spot_hit although the cache names it neither attack nor defend; the role stays unknown (closer: the earlier guess that it is the reaction to a Jad strike rests on no cache evidence).
- inferno_invisible_3x3 and inferno_safespot_dying: all "-"; no disagreement. Gap: 7710 has no death although 7561 exists (INF-AV-003).

## Only a recording, plugin constant or picture can settle
- What 7568 depicts, and that 7561 is the pillar's collapse. Blert and plugin constants carry no row for either.
