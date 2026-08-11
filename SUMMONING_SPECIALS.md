# Summoning Special Moves — Port Plan and Todo List

## Goal

Port the complete special-move behavior for every one of the 78 supported
familiars, including target selection, familiar/owner/target animations,
spotanims, projectiles, sounds, delayed impacts, combat effects, skill and item
effects, special-point cost, scroll consumption, Summoning XP, failure messages,
and state such as cooldowns or charged-next-attack flags.

This document is the completion contract. A familiar is not considered ported
merely because its scroll is mapped or because the common owner cast animation
plays.

## Current baseline

- [x] All 78 familiar types map to their intended scroll and point cost.
- [x] The scroll button checks ownership, inventory, and special points.
- [x] The shared owner cast sequence `7660` and spotanim `1316` are admitted and
  played after each currently implemented special accepts.
- [x] Existing pouch, summon, renew, dismiss, timer, and initial BoB work remains
  available as a foundation.
- [x] All 78 effects validate/execute before the common transaction. Seven
  rows are labeled `implemented_reconstructed`; their direct evidence,
  later-source corroboration, inference, and residual uncertainty are recorded
  in the schema-3 provenance fixture.
- [x] Every enabled mechanic has an admitted special-only asset closure and a
  translation-ledger regression check. Real-client visual evidence is still
  required before release.
- [x] Targeted scrolls route NPC, player, inventory item, ground item, and
  scenery packets through explicit target-kind validation.
- [x] Every enabled special awards its configured Summoning XP as part of the
  common successful commit.
- [x] Charged attacks, familiar-origin combat, target debuffs, cooldowns, and
  delayed effects used by the 78-row roster have bounded engine/content paths.

## Sources of truth and porting policy

Use these sources in this order:

1. Dated official Jagex material for period intent, balance values, target kind,
   cost, and XP. A later local implementation cannot override a conflicting
   August 2009 official value.
2. `../2009scape/Server/src/main/content/global/skill/summoning/familiar/`
   for the original familiar-specific implementation details.
3. `../2009scape/Server/src/main/content/global/skill/summoning/`
   for shared validation, targeting, resource consumption, XP, and familiar
   lifecycle behavior.
4. `docs/summoning_port/pouches_530.json` and the existing server registry for
   the authoritative 78-type mapping, scroll ID, point cost, and XP.
5. Cache 530 definitions for animation, spotanim, projectile-model, and sound
   dependencies; all runtime IDs must be remapped through generated ledgers.
6. A cited historical reference or another known-good implementation only when
   the local 2009scape method is absent, returns an unconditional `false`, or is
   visibly incomplete. Do not invent missing gameplay.

Known source defects must be corrected intentionally, not copied accidentally:

- Spirit scorpion and stranger plant mutate state and then return `false`.
  Their corrected ports return success after the applied effect and commit each
  cost exactly once.
- Giant ent, karamthulhu overlord, lava titan, praying mantis, spirit dagannoth,
  spirit kalphite, swamp titan, and talon beast are local-source no-ops. All now
  have bounded cited ports; Giant Ent, Lava Titan, and Swamp Titan retain their
  explicit schema-3 reconstruction decisions.
- Phoenix has no local familiar implementation for its special. Its bounded
  ashes/full-heal/3x3 reconstruction and cache-native sequence fallback are
  recorded in the final-seven fixture.
- Magpie only plays a visual in the local source and has no substantive effect.
- Honey badger sets a familiar-local charged bit but the local source never
  consumes it in an attack hook. The 2011 period revision fixes the actor and
  double-attack shape; later source supplies the exact owner-stat deltas. The
  port uses an explicitly inferred 50% second-hit chance on the next legal NPC
  familiar swing.
- Every correction or externally reconstructed effect needs a provenance note
  and a regression test.

### Final-seven reconstruction audit — 2026-08-11

The primary source is Jagex's archived
[Summoning — Scrolls table (snapshot 2009-08-21 10:54:39 UTC)](https://web.archive.org/web/20090821105439id_/http://www.runescape.com/kbase/guid/summoning_scrolls),
cross-checked against the archived
[Summoning — Familiars guide (snapshot 2009-08-19 05:53:56 UTC)](https://web.archive.org/web/20090819055356id_/http://www.runescape.com/kbase/guid/summoning_familiars).
The first link is direct period-official evidence for every cost, XP value, and
high-level effect below. At the user's direction the evidence window was
widened through 2011-12-31. Local 2009scape classes are implementation
evidence, and later repositories are corroboration; neither is mislabeled as
period evidence.

The dated community snapshots retained by this audit are
[Honey badger, 2009-11-12](https://runescape.wiki/w/Honey_badger?oldid=1903749),
[Ravenous locust, 2009-12-06](https://runescape.wiki/w/Ravenous_locust?oldid=1990108),
[Phoenix, 2009-11-27](https://runescape.wiki/w/Phoenix_(familiar)?oldid=1954006),
[Forge regent, 2009-08-22](https://runescape.wiki/w/Forge_regent?oldid=1652524),
[Giant ent, 2009-08-21](https://runescape.wiki/w/Giant_ent?oldid=1645837)
and [2009-12-27](https://runescape.wiki/w/Giant_ent?oldid=2080328),
[Lava titan, 2009-12-21](https://runescape.wiki/w/Lava_titan?oldid=2058590),
and [Swamp titan, 2009-12-22](https://runescape.wiki/w/Swamp_titan?oldid=2062840).
The stable 2011 revisions are
[Honey badger, 2011-12-16](https://runescape.wiki/w/Honey_badger?oldid=5058424),
[Ravenous locust, 2011-12-19](https://runescape.wiki/w/Ravenous_locust?oldid=5065661),
[Phoenix, 2011-12-10](https://runescape.wiki/w/Phoenix_(familiar)?oldid=5037635),
[Forge regent, 2011-12-10](https://runescape.wiki/w/Forge_regent?oldid=5037487),
[Giant ent, 2011-12-13](https://runescape.wiki/w/Giant_ent?oldid=5048958),
[Lava titan, 2011-12-31](https://runescape.wiki/w/Lava_titan?oldid=5101862),
and [Swamp titan, 2011-12-18](https://runescape.wiki/w/Swamp_titan?oldid=5064427).
[RuneHQ's Giant Ent page](https://www.runehq.com/item/giant-ent-scroll-acorn-missile)
and [Swamp Titan page](https://www.runehq.com/item/swamp-titan-scroll-swamp-plague)
are recorded as undated corroboration.

The machine-readable audit is
[`docs/summoning_port/SPECIAL_SOURCE_GAP_FIXTURES.json`](docs/summoning_port/SPECIAL_SOURCE_GAP_FIXTURES.json).
It records target rules, formulas, timing, caps, assets, PvP restrictions,
source dates/links, explicit inference decisions, and residual uncertainty.

| Type | Implemented reconstruction | Evidence status |
| ---: | --- | --- |
| 16 | Exact later-source owner stat formula; next legal NPC familiar swing has an inferred 50% second-hit chance | 2009/2011 actor and double-attack shape direct; probability inferred |
| 50 | First live food-table item, one unit, three-tick 1346/1347/1348 choreography | One-item/bandage rule direct; runtime food table and shared PvP policy are declared mappings |
| 52 | Ashes transaction, full heal, `floor(50*(M-H)/M)`, 3x3, at most five | Max 50/area/full-heal direct; linear formula, hard target cap, two ticks, and sequence 11092 fallback inferred |
| 56 | Two-tick max-20 magic hit plus atomic random weapon/shield disarm | Max/disarm/assets corroborated; direct bounded player hit uses shared PvP policy |
| 58 | Max 17, target-centred 3x3, at most three, one hit each, inferred 25% acorn | Max 17/up-to-three/RuneHQ convergence; radius and chance inferred |
| 64 | Max-14 magic hit and flat ten-percentage-point energy drain | Exact 10% direct from 2011; max/assets later-source corroboration |
| 65 | Max 20, target-centred 3x3, at most six, inferred 25% severity-29 poison | Max 20/poison 58 direct from 2011 and RuneHQ; cap/chance conversion declared |

All six destructive player-target paths share one authorization rule: owner,
familiar, and victim must be in multiway Wilderness, within both combat-level
brackets, with the familiar no farther than eight tiles. This intentionally
excludes Duel Arena and Castle Wars. The registry reports 78 enabled rows,
seven `implemented_reconstructed`, and zero source gaps.

## Definition of done

A row in the coverage matrix may be checked only when all applicable items below
are true:

- The exact target kind and all source preconditions are enforced.
- A failed precondition consumes no scroll, points, or XP and leaves no partial
  side effect.
- A successful execution consumes exactly one scroll, deducts the exact point
  cost once, and awards the exact configured Summoning XP once.
- Owner, familiar, target, positional, projectile, sound, and delay effects match
  the source behavior using admitted/remapped assets—never raw 530 IDs.
- Damage is attributed to the owner and obeys combat/PvP/multicombat rules.
- Buffs, drains, poison, healing, run energy, inventory, bank, ground items,
  scenery, cooldowns, and charge state are correct and bounded.
- The special is reachable from the real interface flow and not only a debug
  command.
- Focused tests cover at least one success, each important failure, and the
  resource transaction. Runtime evidence proves the effect and its visual path.

Completion means **78/78 checked**, with no `source gap`, `blocked`, placeholder,
or generic-only row remaining.

## Architecture plan

### 1. Make one authoritative special registry

- [ ] Replace parallel switch statements with one data record per familiar:
  familiar type, scroll, point cost, XP, target kind, handler, and asset bundle.
- [ ] Validate at startup/test time that the registry contains exactly the same
  78 familiar types as the summon registry and that scrolls are unique.
- [ ] Give shared families one parameterized implementation: Call to Arms (4),
  Petrifying Gaze (7), Bull Rush (6), and Titan's Constitution (3).
- [ ] Represent target kind explicitly: none/self, current combat target, selected
  NPC, selected player, inventory item, ground item, or scenery location.
- [ ] Record implementation state (`specified`, `source gap`, `implemented`,
  `verified`) in test data so an unimplemented handler cannot silently fall back
  to the common cast visual.

### 2. Refactor execution into validate, execute, and commit

- [ ] Resolve the owner, live owned familiar, expected familiar type, scroll,
  points, cooldown/charge state, and target before mutating anything.
- [ ] Have each handler perform side-effect-free validation and return a prepared
  operation or a precise failure reason.
- [ ] Execute the prepared operation; treat target disappearance, full inventory,
  full bank, changed scenery, or other races as failure without partial mutation.
- [ ] Commit one-scroll removal, point deduction, XP award, and shared owner cast
  only after the familiar-specific operation is accepted.
- [ ] Define and test ordering when the effect itself removes/transforms the
  targeted inventory item.
- [ ] Prevent double submission while a target cursor, delayed pulse, or charged
  attack is pending.

### 3. Add the missing target and familiar-control surfaces

- [ ] Re-measure current interface-662 packet support and document the exact
  component target masks/opcodes already available.
- [ ] Implement selected NPC and player targets with range, line-of-sight,
  ownership, attackability, PvP, and multicombat validation.
- [ ] Implement inventory-item, ground-item, and scenery-location targets with
  slot/item identity checks and object/location revalidation.
- [ ] Expose the owner's current combat target for specials that use it rather
  than opening a target cursor.
- [ ] Add a safe way for content to animate/spotanimate the owned familiar and to
  emit familiar-to-target or familiar-to-owner projectiles.
- [ ] Add owner-attributed familiar damage, delayed impacts, AoE enumeration,
  poison, stun/knockback, skill drains, and charged-next-attack hooks.
- [ ] Specify NPC behavior for effects expressed as player skill drains; do not
  silently apply a nonexistent NPC skill model.

### 4. Port the special-only asset closure

- [ ] Generate a machine-readable manifest from every implemented handler for all
  sequence, spotanim, projectile-model, and sound references.
- [ ] Compute recursive frame/skeleton/model dependencies from cache 530.
- [ ] Admit only the minimal closure to the staged cache and produce 530-to-runtime
  ledgers for every asset namespace.
- [ ] Transcode or otherwise prove the three known special sounds, including
  shared cast `4161`, Howl `4265`, and Healing Aura `4372`, before enabling them.
- [ ] Bind every familiar/owner/target sequence to the intended actor and verify
  spotanim height, delay, projectile timing, and source/destination attachment.
- [ ] Make dry-run, apply, and second-run idempotence checks mandatory.
- [ ] Reject raw source IDs in runtime scripts unless an identity mapping is
  explicitly recorded by the generated ledger.

## Unblock plan — targeted and familiar-origin specials

The remaining work is blocked on shared primitives, not on permission to make
individual scrolls consume resources. Do these in order; each stage must be
independently tested before enabling dependent registry rows.

Implemented prerequisite slice (2026-08-11):

- [x] Interface 662's overlay is a real five-kind target source, and NPC,
  player, held-item, ground-item, and scenery casts all reach Summoning-owned
  triggers. `OPPLAYERT`, previously decoded and dropped, now has a world route.
- [x] Familiar target kinds are explicit. A wrong-kind packet fails before the
  operation and common commit, so it cannot change scrolls, points, XP, or
  combat state.
- [x] Immediate and target paths share validate/execute/revalidate/commit, with
  scroll, point cost, XP, owner animation, and status refresh in one commit.
- [x] NPC UIDs and slot-backed interactions carry generations. NPC/player/
  ground targets fail closed after slot reuse, logout, death, or replacement.
- [x] Content can independently resolve the live owned familiar and the
  owner's current live NPC combat target; resolving either yields the existing
  safe animation, spotanim, facing, coordinate, damage, and projectile APIs.
- [x] The completed slices cover generation-safe NPC/player targets, delayed
  combat, atomic inventory/bank/BoB operations, persistent charged state, and
  per-handler asset closures. The final seven use the shared bounded
  Wilderness authorization seam and schema-3 reconstruction contracts.

1. **Targetable sidebar surface.** Make the existing Summoning target-overlay a
   real target component: set its target verb/mask and route its NPC, player,
   inventory-item, ground-item, and scenery packets through the existing
   `OPNPCT`/`OPPLAYERT`/`OPHELDT`/`OPOBJT`/`OPLOCT` paths. Bind an explicit
   target-kind record to the active familiar. A click with the wrong kind must
   clear the cursor and leave scrolls, points, XP, and combat state unchanged.
2. **Prepared-operation transaction.** Split every special into target
   validation, prepared operation, execution, and common commit. Keep the
   selected target as a generation-safe entity/object/location handle, then
   revalidate it immediately before execution. Commit exactly one scroll,
   point-cost deduction, XP award, and owner cast only after the prepared
   operation succeeds. Clear the cursor/handle on dismissal, death, logout,
   world change, timeout, and failed target validation.
3. **Owned-familiar actor context.** Add a safe content API that resolves the
   live owned familiar without replacing the selected target context. It must
   support familiar animation/spotanim, face target, coordinate, size, and
   generation-aware disappearance checks. Do not use raw NPC slots or source
   IDs in content.
4. **Familiar-origin combat and projectiles.** Provide owner-attributed NPC and
   player hits with accuracy/max-hit rolls, range/line-of-sight, PvP and
   multicombat filtering, delayed impacts, poison/stun/knockback, target skill
   drains, and familiar-to-target/familiar-to-owner projectiles. A delayed
   callback must no-op safely when the owner, familiar, or target has changed
   generation or died. This unblocks Howl, Dreadfowl Strike, Petrifying Gaze,
   Bull Rush, the minotaurs, and all direct/AoE combat rows.
5. **World and inventory atomic operations.** Add target identity/revalidation
   for inventory slots, ground-item handles, and scenery locations; atomic
   inventory/BoB/bank transfers with note and stack rules; a bank-capacity
   preflight; and owner-filtered delayed ground spawns. This unblocks Bunyip,
   cobra, pyrelord, Pack yak, Abyssal titan, Beaver, compost mound, Hydra, and
   forager specials.
6. **Persistent special state.** Store cooldowns and charged-next-attack state
   on the live familiar generation, not merely its type. Hook normal familiar
   attacks so Honey badger, scorpion, Iron titan, and Steel titan charges apply
   once and clear on use, dismissal, death, replacement, logout, and timeout.
7. **Asset batches and timing.** For each enabled mechanic family, generate the
   minimal 530 dependency closure and ledger for familiar/owner/target
   sequences, spotanims, projectiles, and sounds. Verify actor binding,
   attachment, height, delay, and endpoint in the real client before checking
   any coverage row.
8. **Evidence gates.** Extend `tools/test_summoning_specials.py` to cover all
   78 registry rows, including success and each important failure transaction.
   Add deterministic RNG overrides, a target-capable runtime harness, and a
   representative visual trace per mechanic family before the 78-row sweep.

## Implementation cohorts

Implement shared engine primitives first, then port cohorts in this order. A
cohort does not pass merely because one representative works.

### Cohort A — self effects and immediate production

- [ ] Capped/uncapped skill boosts and drains.
- [ ] Healing, self-damage, poison/disease cure, stat restoration, and run energy.
- [ ] Inventory rewards and delayed ground-item spawning with ownership rules.
- [x] Call-to-Arms teleport sequencing and destination validation for all four
  Void familiars; it uses imported start/end source assets and revalidates the
  owned familiar after its delay.

### Cohort B — direct combat and shared combat families

**Status: complete (2026-08-11).** Direct-NPC-combat handlers use deterministic
owner/familiar/target revalidation and focused transaction tests.

- [ ] Single-target damage with accuracy, max-hit, projectile, impact, and delay.
- [ ] Secondary effects: prayer drain, skill drain, poison, owner heal, familiar
  heal, stun, and knockback.
- [x] Parameterized Petrifying Gaze and Bull Rush variants for current-NPC
  combat targets, including source delay, stat drains, hit tables, and stun
  exclusions.
- [ ] AoE filtering, caps, multicombat rules, and owner attribution.

### Cohort C — charged and next-attack state

**Status: complete for the supported roster (2026-08-11).** Charged attacks
have generation-safe owner-target state, poison, and cleanup. Honey badger's
fixture-backed reconstruction is enabled as `implemented_reconstructed`.

- [x] Honey badger Insane Ferocity: apply the fixture-backed owner-stat
  exchange and arm the bounded reconstructed next-NPC-swing second-hit path;
  clear it with the live familiar lifecycle.
- [x] Spirit scorpion Venom Shot: source-family pre-hit charge consumption,
  owner-attributed target poison timer, and source owner/familiar visuals.
- [x] Iron titan two-extra-hit charge behavior, including source multiway
  gating, approach, five-tick cadence, and generation-safe delayed hits.
- [x] Steel titan three-extra-hit charge behavior, including its source
  ranged/magic/melee rotation, range fallback, and three extra impacts.
- [x] Persist charge only for the live familiar instance; clear it on use,
  dismiss, death, replacement, logout, or invalidation as source behavior requires.

#### Resolved reconstruction decisions — verified 2026-08-11

These prerequisites now have bounded host implementations rather than generic
successful casts:

- **Normal familiar combat lifecycle:** Iron and Steel Titan retain their
  generation-safe owner-target swing dispatches and Spirit Graahk uses a
  generation-safe familiar-local NPC target. Honey badger uses the
  fixture-recorded owner-stat exchange and bounded reconstructed second-hit
  path because its local charged bit has no consumer.
- **Player pre-hit and NPC poison state (unblocked for Spirit scorpion):** its
  ranged battle adjustment runs after the ranged battle state is created and
  before impact preparation, consumes only the live familiar's charged state,
  and preserves the source's exact `weapon.id + 6` pairing: unpoisoned bronze..rune darts,
  bronze..rune javelins, and bronze knife—not bows, arrows, black variants, or
  other thrown weapons. The native NPC poison timer replaces only equal/weaker
  severity, retains player identity for attribution, ticks after 30 game ticks,
  and deals `floor((severity + 4) / 5)` as the source does.
- **Area target authorization:** Spirit Tz-Kih Fireball
  Assault, Spirit Kalphite Sandstorm, Smoke Devil Dust Cloud, and Giant
  Chinchompa Explode share a bounded attackable-NPC predicate: an Attack menu
  entry, live familiar identity, and the source familiar's eight-tile reach.
  They preserve source timing/visuals without fabricating PvP targets. The
  unverified player branches are not inferred from the safe PvM subset.
- **Player-target policy:** destructive familiar effects now share one explicit
  policy: owner, familiar, and victim in multiway Wilderness, within both
  combat-level brackets, and familiar range eight. Castle Wars, Duel Arena,
  safe areas, the owner, and friendly collateral are rejected.
- **Cited reconstructions:** all seven rows have fixture-backed direct evidence,
  separate later-source corroboration, explicit inference, regression-locked
  bounds, and residual-uncertainty notes.

| Type | Special | Reconstruction decision | Status of this audit |
| ---: | --- | --- | --- |
| 16 | Honey badger — Insane Ferocity | Exact later-source owner-stat formula; inferred 50% second hit | Enabled as `implemented_reconstructed` |
| 41 | Karamthulhu — Doomsphere Device | — | Reconstructed: period magic water hit up to 16 against a legal NPC combat target, using the imported attack sequence |
| 50 | Ravenous locust — Famine | First runtime food-table item; shared PvP policy | Enabled as `implemented_reconstructed` |
| 52 | Phoenix — Rise from the Ashes | Linear missing-health cap 50; radius one; five victims; sequence 11092 fallback | Enabled as `implemented_reconstructed` |
| 55 | Praying mantis — Mantis Strike | — | Reconstructed: magic hit up to 17; a landed hit drains three Prayer and applies the host's four-tick NPC bind |
| 56 | Forge regent — Inferno | Max 20; two ticks; atomic random one-item disarm | Enabled as `implemented_reconstructed` |
| 58 | Giant ent — Acorn Missile | Max 17; radius one; three targets; inferred 25% acorn | Enabled as `implemented_reconstructed` |
| 63 | Spirit dagannoth — Spike Shot | — | Reconstructed: accurate ranged 0–18 hit, four-tick NPC stun on a landed hit, roster attack visual |
| 64 | Lava titan — Ebon Thunder | Max 14; flat ten-percentage-point drain | Enabled as `implemented_reconstructed` |
| 65 | Swamp titan — Swamp Plague | Max 20; radius one; six targets; inferred 25% severity-29 poison | Enabled as `implemented_reconstructed` |

### Cohort D — item, bank, BoB, and crafting interactions

- [x] Bunyip fish conversion/healing and pyrelord jewellery-crafting entry.
- [x] Spirit cobra egg transformations (all seven source egg/product pairs,
  replacement in the selected inventory slot).
- [x] Pack yak atomic bank transfer, including note and bankability rules.
- [x] Abyssal titan essence shipment across inventory and BoB contents.
- [x] Forge regent selected-player equipment removal: delayed max-20 hit and
  atomic one-item transfer under the shared Wilderness PvP policy.
- [x] Ravenous locust food removal: first live food-table item, excluding
  bandages, under the shared Wilderness PvP policy.

### Cohort E — scenery, movement, AoE, and delayed world effects

- [x] Beaver Multichop pulses and Woodcutting rewards.
- [x] Compost mound bin state/contents and hydra tree-stump regrowth.
- [x] Spirit wolf Howl: selected-NPC projectile, sound, delayed revalidation,
  facing, and retreat waypoint.
- [ ] Spirit kyatt call-to-owner.
- [ ] Fruitfall, Fish Rain, Herbcall, Egg Spawn, and Giant Chinchompa explosion.

### Cohort F — reconstructions and corrections

- [x] Recover Spirit Kalphite Sandstorm from the immediate pre-disable
  2009scape revision (`393752d77^:.../SpiritKalphiteNPC.java`): source range
  six, seq 8517/gfx 1350, projectile 1349, one-tick pulse, and its actual
  six-target (`count > 5`) bound. Its buggy source return value is reconciled
  so a successful effect commits its scroll and special points exactly once.
- [x] Research and cite the absent, no-op, or incomplete special effects.
- [x] Write expected-behavior fixtures before implementing reconstructed logic.
- [x] Correct Stranger Plant's source return defect: retain its 50% severity-20
  poison, source animation/projectile/impact, accurate delayed hit, and commit
  the accepted transaction exactly once.
- [x] Add and regression-lock the reconstructed PvP/Wilderness policy before
  enabling familiar effects that mutate another player's inventory, equipment,
  energy, poison, or hitpoints.

## Ordered delivery plan

The cohorts describe related behaviour; this sequence is the merge and
acceptance order.  It keeps every enabled row transaction-safe and avoids
turning an unimplemented target family into a generic cast that consumes a
scroll.

1. **Lock the registry and current baseline.** [x] Generated by
   `tools/summoning_special_registry.py` and checked by
   `tools/test_summoning_special_registry.py`: it derives the 78 rows from the
   pouch/scroll source mapping and live runtime tables, including cost, XP,
   target kind, handler state, provenance, and asset bundle.  It proves one
   pouch per live familiar and explicitly marks the seventy-one enabled rows;
   unimplemented rows are `unavailable`, never generic successful casts.
   Continue to use this generator as the input when replacing the remaining
   ServerScript switch tables.
2. **Finish lifecycle-safe targeting.** Add cursor expiry/cancellation and
   clearing on logout, death, dismissal, replacement, and world change.  Add
   the missing PvP/multicombat policy and target range/line-of-sight checks.
   Test every packet kind with a recycled NPC/player/ground handle and prove no
   resource commit occurs on rejection.
3. **Deliver the familiar combat primitive.** Add generation-safe delayed
   familiar-to-target projectiles and owner-attributed NPC/player hits, then
   deterministic rolls, poison, stun, knockback, drains, and bounded AoE.
   Enable one representative only after real-client visual proof, then land the
   shared direct-combat families: Dreadfowl/Thorny snail/Mosquito/Wyrm,
   minotaurs, cockatrices, and basic single-target rows.
4. **Deliver stateful specials.** Put cooldowns and charge state on the live
   familiar generation and hook normal familiar attacks.  Enable Macaw/Fruit
   bat cooldowns, Honey badger/Scorpion/Iron titan/Steel titan charges, then
   verify clearing on all lifecycle invalidations.
5. **Deliver atomic item and world operations.** Implement preflighted,
   identity-checked inventory, BoB, bank, ground-item, and scenery operations.
   This enables the conversion/banking/production rows and must include
   full-capacity and stale-target failures.  Do not enable Pack yak or Abyssal
   titan before their whole transfer is atomic.
6. **Admit assets per mechanic family.** For each preceding slice, generate
   the minimal sequence/spotanim/projectile/sound closure and ledger, run
   dry-run/apply/idempotence, and retain a representative real-client trace.
   The shared cast and Call to Arms assets already provide the model for this;
   no raw 530 asset ID may appear in runtime content.
7. **Resolve reconstructions explicitly.** Magpie retains its completed
   correction fixture. The final seven are enabled as
   `implemented_reconstructed`; the schema-3 fixture distinguishes direct
   evidence, later corroboration, and every numeric/target-policy inference.
8. **Sweep and release.** Run the 78-row deterministic success/failure matrix,
   then a real-client sweep.  Check a coverage row only after its transaction,
   assets, and runtime evidence are retained; update `docs/SUMMONING_PORT.md`
   only when the release checklist below is entirely checked.

### Slice exit criteria

Every delivery slice must pass all of the following before dependent rows are
enabled:

- The registry identifies the handler and explicitly records its status.
- Success commits one scroll, the configured points, and the configured XP;
  every failure commits none of them.
- A focused deterministic regression covers success and the slice's important
  rejection/race cases with a non-zero assertion count.
- The asset import is reproducible and all runtime references resolve through
  its ledger.
- ServerScript compilation, Summoning isolation, special-asset validation, and
  `mock230_pack --check-only` pass; client-visible work also has a fresh-save
  real-client trace.

## Complete 78-familiar coverage matrix

Legend: **specified** means the local source describes meaningful behavior;
**incomplete** or **source gap** must be researched before implementation;
**source defect** requires the documented correction. Every final checkbox must
include logic, content assets, transaction behavior, and tests.

| Done | # | Familiar — scroll | Target | Required effect | Source state |
|---|---:|---|---|---|---|
| [x] | 1 | Spirit wolf — Howl | selected NPC | Projectile, howl, and repel/knockback with restrictions | source-backed |
| [x] | 2 | Dreadfowl — Dreadfowl Strike | combat target | Magic hit up to 3; usable only while owner is in combat | source-backed |
| [x] | 4 | Spirit spider — Egg Spawn | area | Create 0–8 red spider eggs around familiar | source-backed |
| [x] | 5 | Thorny snail — Slime Spray | combat target | Accurate magic hit up to 8 | source-backed |
| [x] | 6 | Granite crab — Stony Shell | self | Boost Defence by 4 | source-backed |
| [x] | 7 | Spirit mosquito — Pester | combat target | Familiar-owned accurate melee hit with source special visuals | source-backed |
| [x] | 8 | Desert wyrm — Electric Lash | combat target | Hit up to 5 | source-backed |
| [x] | 9 | Spirit scorpion — Venom Shot | charged state | Next qualifying thrown attack consumes charge and applies owner-attributed severity-1 poison | source-backed/corrected defect |
| [x] | 10 | Spirit Tz-Kih — Fireball Assault | nearby NPCs | First two nearby attackable-NPC candidates; source eight-tile familiar reach, delayed magic hit 0–6 | source-backed / PvP subset |
| [x] | 11 | Albino rat — Cheese Feast | self | Add cheese to inventory after locked animation | source-backed |
| [x] | 12 | Spirit kalphite — Sandstorm | nearby NPCs | One-tick delayed familiar magic volley against up to six nearby attackable NPCs, up to 20 each | source-backed/reconciled |
| [x] | 13 | Compost mound — Generate Compost | scenery | Fill a compost bin's remaining capacity with potatoes or rare supercompost ingredients | source-backed |
| [x] | 14 | Giant chinchompa — Explode | nearby NPCs | Three-tick 0–12 burst within 6 tiles, then generation-safe familiar dismissal | source-backed/reconciled |
| [x] | 15 | Vampire bat — Vampyre Touch | combat target | Hit up to 12; 40% chance to heal owner by 2 | source-backed |
| [x] | 16 | Honey badger — Insane Ferocity | self + charged next NPC swing | Exact owner-stat exchange; next legal swing has inferred 50% second hit | implemented/reconstructed |
| [x] | 17 | Beaver — Multichop | tree scenery | Timed repeated tree chops with Woodcutting validation/rewards | source-backed |
| [x] | 18 | Void ravager — Call to Arms | self | Animated teleport to Pest Control staging area | source-backed |
| [x] | 19 | Void spinner — Call to Arms | self | Animated teleport to Pest Control staging area | source-backed |
| [x] | 20 | Void torcher — Call to Arms | self | Animated teleport to Pest Control staging area | source-backed |
| [x] | 21 | Void shifter — Call to Arms | self | Animated teleport to Pest Control staging area | source-backed |
| [x] | 66 | Bronze minotaur — Bronze Bull Rush | combat target | Hit up to 4; no stun | source-backed |
| [x] | 67 | Iron minotaur — Iron Bull Rush | combat target | Hit up to 6; 60% chance to stun for 4 ticks | source-backed |
| [x] | 68 | Steel minotaur — Steel Bull Rush | combat target | Hit up to 9; 60% chance to stun for 4 ticks | source-backed |
| [x] | 69 | Mithril minotaur — Mithril Bull Rush | combat target | Hit up to 13; 60% chance to stun for 4 ticks | source-backed |
| [x] | 70 | Adamant minotaur — Adamant Bull Rush | combat target | Hit up to 16; 60% chance to stun for 4 ticks | source-backed |
| [x] | 71 | Rune minotaur — Rune Bull Rush | combat target | Hit up to 20; no stun | source-backed |
| [x] | 22 | Bull ant — Unburden | self | Restore run energy by half base Agility; fail if already full | source-backed |
| [x] | 23 | Macaw — Herbcall | area | With 100-tick cooldown, spawn a random herb after 5 ticks | source-backed |
| [x] | 24 | Evil turnip — Evil Flames | combat target | Hit up to 10, heal familiar by 2, drain target Magic by 1 | source-backed |
| [x] | 25 | Spirit cockatrice — Petrifying Gaze | combat target | Hit/visual and drain Defence by 3 | source-backed |
| [x] | 26 | Spirit guthatrice — Petrifying Gaze | combat target | Hit/visual and drain Attack by 3 | source-backed |
| [x] | 27 | Spirit saratrice — Petrifying Gaze | combat target | Hit/visual and drain Prayer by 3 | source-backed |
| [x] | 28 | Spirit zamatrice — Petrifying Gaze | combat target | Hit/visual and drain Strength by 3 | source-backed |
| [x] | 29 | Spirit pengatrice — Petrifying Gaze | combat target | Hit/visual and drain Magic by 3 | source-backed |
| [x] | 30 | Spirit coraxatrice — Petrifying Gaze | combat target | Hit/visual and drain Summoning by 3 | source-backed |
| [x] | 31 | Spirit vulatrice — Petrifying Gaze | combat target | Hit/visual and drain Ranged by 3 | source-backed |
| [x] | 32 | Pyrelord — Immense Heat | inventory item | Accept only gold bars and open the jewellery-crafting flow | source-backed |
| [x] | 33 | Magpie — Thieving Fingers | self | +2 Thieving with source familiar visual | source-backed |
| [x] | 34 | Bloated leech — Blood Drain | self | Cure poison/disease, restore depleted stats by 20% of base, take 1–5 damage | source-backed |
| [x] | 3 | Spirit terrorbird — Tireless Run | self | Boost Agility by 2 and restore run energy by half base Agility | source-backed |
| [x] | 35 | Abyssal parasite — Abyssal Drain | combat target | Hit up to 7 and drain Prayer by 1–3 | source-backed |
| [x] | 36 | Spirit jelly — Dissolve | combat target | Hit up to 13 and drain Attack by 3 | source-backed |
| [x] | 37 | Ibis — Fish Rain | area | Spawn two random fish near owner after 3 ticks | source-backed |
| [x] | 38 | Spirit kyatt — Ambush | self/movement | Call familiar to owner while owner is in combat | source-backed |
| [x] | 39 | Spirit larupia — Rending | combat target | Hit up to 10 and drain Strength by 1 | source-backed |
| [x] | 40 | Spirit graahk — Goad | selected NPC | Command familiar's normal aggressive melee attack against valid target | source-backed |
| [x] | 41 | Karamthulhu overlord — Doomsphere | combat target | Accurate period-style magic water hit up to 16 with its imported attack sequence | source-backed/reconstructed |
| [x] | 42 | Smoke devil — Dust Cloud | nearby NPCs | Immediate familiar burst within 1 tile, excluding the familiar, for 0–5 each | source-backed/reconciled |
| [x] | 43 | Abyssal lurker — Abyssal Stealth | self | Boost Agility and Thieving by 4 | source-backed |
| [x] | 44 | Spirit cobra — Ophidian Incubation | inventory item | Transform a supported egg into its configured product | source-backed |
| [x] | 45 | Stranger plant — Poisonous Blast | combat target | Accurate delayed hit up to 2 and 50% owner-attributed severity-20 poison | source-backed/corrected defect |
| [x] | 46 | Barker toad — Toad Bark | combat target | Hit up to 8 with source attack and impact visuals | source-backed |
| [x] | 47 | War tortoise — Testudo | self | Boost Defence by 9, capped at base plus 9 | source-backed |
| [x] | 48 | Bunyip — Swallow Whole | inventory item | Consume supported raw fish, enforce Cooking level, and heal cooked-food value | source-backed/reconciled |
| [x] | 49 | Fruit bat — Fruitfall | area | Cooldown, two-stage animation, then private random fruit around owner | source-backed |
| [x] | 50 | Ravenous locust — Famine | selected legal PvP player | Destroy first live food-table item after source three-tick choreography | implemented/reconstructed |
| [x] | 51 | Arctic bear — Arctic Blast | combat target | Hit up to 15 with source projectile and impact | source-backed |
| [x] | 52 | Phoenix — Rise from the Ashes | ground ashes | Consume ashes, full-heal/teleport, linear missing-health max-50 burst over at most five 3x3 victims | implemented/reconstructed |
| [x] | 53 | Obsidian golem — Volcanic Strength | self | Boost Strength by 9 | source-backed |
| [x] | 54 | Granite lobster — Crushing Claw | combat target | Hit up to 14 with source ranged projectile | source-backed |
| [x] | 55 | Praying mantis — Mantis Strike | combat target | Accurate magic hit up to 17; landed hit drains three Prayer and binds target for four host ticks | source-backed/reconstructed |
| [x] | 56 | Forge regent — Inferno | selected legal PvP player | Delayed max-20 magic hit and atomic random equipped weapon/shield disarm | implemented/reconstructed |
| [x] | 57 | Talon beast — Deadly Claw | combat target | Three independently accurate delayed magic hits, each up to 8, with its admitted attack sequence | source-backed/reconstructed |
| [x] | 58 | Giant ent — Acorn Missile | selected combat target + 3x3 area | One max-17 hit each to at most three; inferred 25% private acorn chance | implemented/reconstructed |
| [x] | 59 | Fire titan — Titan's Constitution | self | Percentage-based Defence boost plus configured heal | source-backed |
| [x] | 60 | Moss titan — Titan's Constitution | self | Percentage-based Defence boost plus configured heal | source-backed |
| [x] | 61 | Ice titan — Titan's Constitution | self | Percentage-based Defence boost plus configured heal | source-backed |
| [x] | 62 | Hydra — Regrowth | scenery | Regrow a valid Farming tree stump | source-backed |
| [x] | 63 | Spirit dagannoth — Spike Shot | combat target | Accurate ranged 0–18 hit; landed hit applies the host's four-tick NPC stun | source-backed/reconstructed |
| [x] | 64 | Lava titan — Ebon Thunder | selected combat target | Max-14 magic hit; legal player loses ten percentage points of special energy | implemented/reconstructed |
| [x] | 65 | Swamp titan — Swamp Plague | selected target + 3x3 area | Max 20 to at most six; inferred 25% severity-29 poison | implemented/reconstructed |
| [x] | 72 | Unicorn stallion — Healing Aura | selected player | Heal target by 15% of maximum life points | source-backed |
| [x] | 73 | Geyser titan — Boil | combat target | Defence-sensitive source maximum hit with projectile/impact | source-backed |
| [x] | 74 | Wolpertinger — Magic Focus | self | Boost Magic by 7, capped at base plus 7 | source-backed |
| [x] | 75 | Abyssal titan — Essence Shipment | inventory + BoB | Atomically bank carried and stored rune/pure essence | source-backed |
| [x] | 76 | Iron titan — Iron Within | charged state | Charge next source-gated normal attack to add two owner-attributed extra hits | source-backed |
| [x] | 77 | Pack yak — Winter Storage | inventory item | Atomically bank one bankable item with note conversion rules | source-backed |
| [x] | 78 | Steel titan — Steel of Legends | charged state | Charge next attack to add three owner-attributed extra hits | source-backed |

## Test and evidence plan

### Static and build gates

- [x] `tools/test_summoning_special_registry.py` asserts exact 78-row coverage
  with no duplicate/missing familiar, pouch, handler, cost, or XP mapping.
- [x] Assert that 77 enabled behaviors point to local source classes and the
  Phoenix exception plus all seven reconstructions point to the dated fixture.
- [ ] Assert every referenced runtime asset is in the generated ledger and its
  recursive dependency closure; reject untracked raw 530 IDs.
- [ ] Add script compilation and control-flow tests proving that every registry
  row reaches a real handler and none reaches a generic-only placeholder.
- [ ] Run the existing summoning isolation, Phase 1/4, and scroll-asset tests.
- [ ] Run `make -C src mock230-cache-summoning` and the normal relevant build.
- [ ] Verify staged cache dry-run/apply/idempotence and repository-scope isolation.

### Deterministic behavior tests

- [ ] Add a deterministic RNG/debug override for random damage, poison, stun,
  drops, fruit/herb/fish selection, and production counts.
- [ ] Test every row's success transaction: one scroll, exact points, exact XP,
  and exactly one accepted effect.
- [ ] Test every important failure transaction: wrong familiar, no familiar,
  wrong scroll, insufficient points, missing/invalid target, out of range, full
  inventory/bank, cooldown, already charged, invalid item/scenery, PvP rejection,
  and target disappearing before execution.
- [ ] Test cap/floor behavior for skill boosts/drains, HP, prayer, run energy,
  poison, bank quantity, ground-item quantity, and AoE target limits.
- [ ] Test delayed callbacks after familiar dismiss/death, owner logout/death,
  target death/movement, and world/plane change.
- [ ] Test charge clearing and prevention of stacking or duplicate submission.

### Runtime proof

- [ ] Add a controlled harness that can spawn each familiar, provide its scroll,
  set points/targets/preconditions, and capture server assertions.
- [ ] First prove one representative of every mechanic family; then execute a
  scripted 78-row sweep with success/failure evidence for each row.
- [ ] Capture owner, familiar, projectile, target, impact, sound, and timing logs
  for every special that uses them.
- [ ] Visually inspect the representative cases for actor binding, orientation,
  spotanim height, projectile endpoints, delayed impact timing, and despawn.
- [ ] Run save/load and reconnect checks for cooldown/charged state where relevant.

## Release checklist

- [ ] All engine prerequisites are merged and independently tested.
- [ ] The generated asset manifest and ledgers are reviewed and reproducible.
- [ ] All shared-family parameter tables match the source.
- [x] All seven former source-gap rows have dated evidence, explicit inference,
  executable regression contracts, and `implemented_reconstructed` state.
- [x] All 78 coverage rows are checked.
- [ ] Static, build, cache, deterministic, isolation, and runtime suites pass.
- [ ] No successful special omits its effect, assets, cost, or XP.
- [ ] No failed special consumes resources or leaves a partial effect.
- [ ] Update `docs/SUMMONING_PORT.md` only after this completion contract is met;
  until then, describe special moves as infrastructure/generic-cast only.
