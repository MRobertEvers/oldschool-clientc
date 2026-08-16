# Phase 5c: corpus familiar/pouch cohort — Luna handoff

**Status:** planned. This is the next Summoning slice; it authorizes no import or runtime change by itself.

“Luna” is the implementing agent in this handoff, not the unrelated diary-rabbit NPC named Luna. The project has no existing “Phase 5.6” label, so this document uses the next queue label, **5c**, to avoid conflating an agent/model version with a porting phase.

## Outcome and scope contract

Phase 5c completes the **baseline familiar corpus**: a fixed 45-pair subset of the remaining active 530 familiar/pouch inventory. Each admitted familiar will be a correctly named, owned, summonable follower with its actual pouch interaction, body and chathead model, ready and walk animation, dynamic Summoning sidebar entry, points/lifetime lifecycle, Call, Dismiss, and logout/relog reconstruction.

It does **not** claim original-RS special moves, scrolls, combat, Beast-of-Burden storage, foraging, boosts, healing, target-picking, teleports, item generation, wilderness forms, tertiary recipe resolution, potions, pets, or per-familiar audio. Those features must either be implemented in separately named later slices or remain absent from the visible client surface. Do not expose an action merely because the source NPC has one.

The goal is deliberately a complete common lifecycle/render corpus, not a promotion of the old generated roster. The preserved summoning_roster_530 experiment remains evidence only.

## Fixed membership

The source inventory has 82 pouch records: 78 active familiar/pouch pairs and four Sacred Clay records with slot=-1. Spirit wolf and Dreadfowl are already accepted reference slices. The Phase-5c set is the 45 active records below; its pair order also defines the stable new familiar type allocation 3..47.

| Source NPC / pouch | Familiar |
| --- | --- |
| 6841 / 12059 | Spirit spider |
| 7331 / 12778 | Spirit mosquito |
| 6837 / 12055 | Spirit scorpion |
| 7361 / 12808 | Spirit Tz-Kih |
| 7353 / 12800 | Giant chinchompa |
| 6835 / 12053 | Vampire bat |
| 6845 / 12065 | Honey badger |
| 7333 / 12780 | Void spinner |
| 7351 / 12798 | Void torcher |
| 7367 / 12814 | Void shifter |
| 6853 / 12073 | Bronze minotaur |
| 6855 / 12075 | Iron minotaur |
| 6857 / 12077 | Steel minotaur |
| 6859 / 12079 | Mithril minotaur |
| 6861 / 12081 | Adamant minotaur |
| 6863 / 12083 | Rune minotaur |
| 7377 / 12816 | Pyrelord |
| 6843 / 12061 | Bloated leech |
| 6992 / 12027 | Spirit jelly |
| 7365 / 12812 | Spirit kyatt |
| 7337 / 12784 | Spirit larupia |
| 7363 / 12810 | Spirit graahk |
| 6809 / 12023 | Karamthulhu |
| 6865 / 12085 | Smoke devil |
| 6802 / 12015 | Spirit cobra |
| 6889 / 12123 | Barker toad |
| 6813 / 12029 | Bunyip |
| 7372 / 12820 | Ravenous locust |
| 6839 / 12057 | Arctic bear |
| 7345 / 12792 | Obsidian golem |
| 6798 / 12011 | Praying mantis |
| 7335 / 12782 | Forge regent beast |
| 7347 / 12794 | Talon beast |
| 6811 / 12025 | Hydra |
| 6804 / 12017 | Spirit dagannoth |
| 6822 / 12039 | Unicorn stallion |
| 6869 / 12089 | Wolpertinger |
| 7355 / 12802 | Fire titan |
| 7357 / 12804 | Moss titan |
| 7359 / 12806 | Ice titan |
| 7341 / 12788 | Lava titan |
| 7329 / 12776 | Swamp titan |
| 7339 / 12786 | Geyser titan |
| 7375 / 12822 | Iron titan |
| 7343 / 12790 | Steel titan |

The accounting is intentional:

| Bucket | Count | Disposition |
| --- | ---: | --- |
| Already accepted | 2 | Spirit wolf (6829/12047) and Dreadfowl (6825/12043) stay unchanged regressions. |
| Phase-5c baseline corpus | 45 | The exact allowlist above; neither a heuristic nor a prefix scan can widen it. |
| Beast-of-Burden / forager | 30 | Deferred to Phase 6 because their container, interface, and forager behavior is outside the baseline corpus. Phase 6a supplies only the private cache-backed inventory foundation. |
| Phoenix | 1 | Deferred to Phase 7 with pets/incubator; it lacks the normal Familiar lifecycle contract. |
| Sacred Clay | 4 | Remain source-inventory slot=-1 deferred roots; do not add a fifth clay record. |

The 30 Phase-6 roots are 6794, 6796, 6800, 6806, 6808, 6815, 6817, 6818, 6820, 6824, 6827, 6831, 6833, 6847, 6849, 6851, 6867, 6871, 6873, 6875, 6877, 6879, 6881, 6883, 6885, 6887, 6991, 6994, 7349, and 7370. Do not make them appear functional by importing a follower model while silently omitting its storage/forager behavior.

Void torcher, Void shifter, and Rune minotaur remain in the 45. Their unusual source lifetimes (9400, 9400, and 15100 ticks) are a **source-model gate**, not permission to round, clamp, or replace them. They enter only after the lifecycle oracle below proves their exact schedule.

## Invariants Luna must preserve

1. Leave the review-only summoning_roster_530 experiment byte-for-byte intact: 630 source files, 2,175 pack references, 1,365 ledger rows, and source fingerprint 2774863acae92958c352660f598bc0dbcd0379dc7b4fcc708c682fa81634df43. Its archived broad CSV/INI remain under docs/summoning_port/review_only/, and feature-on staging must still withhold it.

2. Leave the Dreadfowl nine-row ledger, target IDs/names, ifop4 route, and phase-5b tests intact. Generalize tests around it; do not weaken them from “exact Dreadfowl” to “some cohort exists.”

3. Use a new prefix, summoning_cohort_corpus, a new dedicated ledger, and target ranges that do not overlap the primary ledger or Dreadfowl’s reservations. Source-ID reuse with the preserved review experiment is expected; destination-ID or destination-name reuse is not.

4. Do not accept pets, scrolls, tertiary inputs, potions, source synths, source audio closure, combat attack/defend/death animation roles, locs, spotanims, wilderness forms, or a bare summoning generated name. The manifest uses npc_sounds=no and exports only the selected NPC and pouch roots.

5. Work only in the separately staged Summoning lane and its matching asset roots. Never move the review roster to another live lane, park it under a suffix, or make the ordinary flag-off cache see it.

6. Do not run ASAN on this Mac. Every client acceptance run uses a fresh MOCK230_SAVES=$(mktemp -d) and a normal embedded build.

## Deliverables

| Artifact | Purpose |
| --- | --- |
| docs/summoning_port/corpus_cohort_530.json | Checked source-policy catalog: exact 45 rows, stable type IDs, source fields, deferred capabilities, and the expected imported closure. |
| docs/summoning_port/corpus_cohort_530.ini | Generated narrow importer manifest. It exports only the 45 NPC and 45 pouch roots with npc_sounds=no. |
| OSRS-Content/osrs239-content/port/summoning_corpus_530.map | Separate exact corpus closure ledger; every row is minted/unreviewed until a distinct human material review. |
| docs/summoning_port/roster_boundary_530.json | Adds the corpus prefix, all 90 roots, one dedicated ledger record, and measured non-overlapping reservations; keeps Dreadfowl and review-only data unchanged. |
| cohort-named lane files/assets | summoning_cohort_corpus.{npc,obj,seq,...}, cohort-named model/animset/framemap files, and cohort-only pack/allocation rows. |
| source-catalog generator/checker | Rebuilds the catalog/INI deterministically and rejects any source-set drift before import. |
| corpus structural and runtime tests | New phase-5c tests; the existing phase-5a and phase-5b tests remain regressions. |

The catalog is the authority for membership and runtime profile data. It must contain, at minimum, the source NPC/pouch/name/slot, target type, source level/cost/summon XP, canonical display name, lifetime, exact point-drain model, target NPC/pouch IDs, body/head/pouch models, ready/walk sequences, animation archive/framemap, and a deferred_capabilities list. A field derived from the 530 source must cite the source file/line or the checked extraction input; hand-copied values are not enough.

## Execution plan

### 0. Establish a no-mutation baseline

Before adding any corpus file, record git status --short for the root and both content/client submodules. Preserve unrelated QBD/client work. Run the existing preservation regressions and save their output with the work log:

    python3 tools/port_summoning_ids.py --check
    python3 tools/test_summoning_phase5a.py --tree OSRS-Content/osrs239-content
    make -C src test-summoning-phase5b

Do not use git stash, reset, clean, or a bulk importer apply at this stage. A failure in the review-only fingerprint/count test is a stop condition, not something to repair by regenerating the old roster.

### 1. Build and freeze the source-policy catalog

Add a small deterministic generator (and --check mode) that reads pouches_530.json plus a checked source-behavior extraction. Its input policy is the exact 45-pair table above—not an “all non-pet” or slot >= 0 predicate evaluated during staging. It must prove all of the following:

- active source pairs minus the two accepted pairs equal the 76 raw remaining pairs;
- 45 are in the corpus, 30 have a Phase-6 container/forager reason, and Phoenix has a Phase-7 lifecycle reason;
- all four Sacred Clay pairs are still explicitly deferred;
- no pair appears twice, no half-pair is admitted, and no admitted source overlaps Spirit wolf or Dreadfowl;
- every assigned familiar type is unique, stable, and exactly 3..47 in the catalog order;
- every profile has a finite source-derived lifetime and a deterministic exact drain schedule.

Model point drain with an executable source oracle. The oracle must reproduce the reference Familiar scheduling arithmetic for every row, including the three long-lived exceptions, and emit a profile representation that survives relog based solely on the persisted type and remaining ticks. Do not apply Dreadfowl's 100-tick interval to every familiar, and do not introduce a new persisted field merely to hide a rounding problem. Preserve 0=none, 1=Spirit wolf, and 2=Dreadfowl forever.

The first new test should mutate an included row, a deferred row, a type ID, a lifetime, each exceptional lifetime, and a drain boundary. All mutations must fail. Only then check in the generated catalog and INI.

### 2. Generalize admission without weakening existing proof

The current boundary/parser/stager deliberately knows only Phase 5a/5b and a sole Dreadfowl cohort. Make the following compatible change before adding the corpus entry:

1. Permit the next queue state (5c) in both tools/port_summoning_ids.py and tools/stage_summoning_overlay.py; retain schema-1 parsing unless a tested schema migration is genuinely necessary.

2. Refactor phase-5a assertions from “exactly one cohort ledger” to “the Dreadfowl ledger exists and is exactly its nine rows, plus every admitted cohort has its own exact ledger.”

3. Keep tools/test_summoning_phase5b.py as Dreadfowl-specific proof. Change only its boundary expectation so it finds the Dreadfowl entry instead of requiring it to be the only entry.

4. Add the corpus prefix and ledger only after the catalog validates. The boundary must list both halves of all 45 pairs, must reserve every derived kind, and must retain all review-only fields unchanged.

5. Add mutation coverage for an unadmitted prefix, a review-only marker, a partial pair, a widened corpus root set, a duplicate target range, a primary-ledger collision, and an unsafe synth or npc_sounds=yes line.

The staging rule remains fail-closed: cohort-named configs and assets enter only when their exact prefix is admitted. Review-only lines may be held out from line-oriented .alloc, .client, and .pack files, but a review token in a structured config/script/interface file must fail unless that file is entirely isolated and held out.

### 3. Measure a fresh import, then reserve exact ranges

Create the narrow importer manifest with:

    prefix=summoning_cohort_corpus
    ledger=port/summoning_corpus_530.map
    npc_sounds=no

It contains the 45 NPC roots and 45 pouch roots only. It must not point at port/summoning_530.map, Dreadfowl's map, or any review-only output.

Run cachepack import --manifest … without --apply first. Hash the source tree, primary ledger, Dreadfowl ledger, review archive, and stage inputs before and after the dry run. From the dry-run result, write the expected closure into the catalog: exact source IDs/counts for model, sequence, frame archive, and framemap, plus every target name that will be minted.

Do not guess a broad target window. Start the allocation survey above Dreadfowl's reservations (npc >26015, obj >46015, model >120031, seq/frame_archive >23031, framemap >10031), then pick final endpoints from the measured closure and a live allocation collision scan. The boundary ranges are hard reservations, not hints: every corpus row must land inside them, and no range may overlap Dreadfowl or the primary ledger for the same kind.

If a dry import discovers a pet, synth/audio, scroll, combat-only sequence, unsupported asset kind, or an unaccounted source closure, stop. Fix the explicit importer policy or defer the affected pair through a new documented slice; never delete rows from the output by hand and call the remaining set “complete.”

### 4. Apply only the measured closure and sanitize the visible surface

After the catalog, boundary, and dry-run closure agree, run the apply once into the cohort-named lane. Check that the new ledger has exactly the catalog's rows, each with an exact summoning_cohort_corpus_* destination name and minted/unreviewed disposition/signoff.

Audit generated NPC and pouch configs before staging:

- each pouch exposes exactly ifop4=Summon for this baseline;
- each NPC exposes only implemented follower interactions (ordinary interact/call and dismiss as appropriate);
- no Special, Store, Withdraw, Burrow, Despair, combat action, source special action, or hidden handler is left visible;
- each NPC binds its measured body/head plus ready/walk records; no attack/defend/death role is imported merely because it appeared in the old broad experiment;
- no direct synth, song, sample, patch, pet, scroll, loc, or spotanim asset is present.

Make sanitation deterministic and testable—prefer importer policy or a checked post-import normalizer that takes the catalog as input. Never hand-edit a generated config without updating the generator/checker that will reproduce it.

### 5. Generate a multi-familiar runtime registry

Refactor the two-familiar dispatch in ported/scape2009_summoning/scripts/summoning_spirit_wolf.rs2 into shared lifecycle code plus a checked generated corpus dispatch fragment. Do not create 45 divergent copies of the Dreadfowl script.

For each catalog row, generate:

- one [opheld4,<corpus pouch>] trigger that forwards its stable type to the shared summon proc;
- the pouch-to-type and type-to-NPC/level/cost/lifetime/name/drain functions;
- ordinary [opnpc1,<corpus NPC>] Call and [opnpc2,<corpus NPC>] Dismiss triggers;
- sidebar title/head selection through the existing generic if_setnpchead route;
- test-only pouch provisioning, never a direct test-only summon.

Retain the actual rev239 input route: authored ifop4=Summon appears through the dynamic backpack as IF_BUTTONX op=6, which the server maps to canonical OPHELD4. Do not “fix” it to wire-op 5, and do not change the legacy net_out_opheld() convention. Extend the inbound regression with corpus target IDs only where it provides a meaningful generated-table check.

The generic lifecycle must preserve current gates (feature enabled, level, points, one owned follower), consume the pouch only after those gates pass, set owner/follow mode, persist type and remaining ticks, clear safely on expiry/death/logout, reconstruct the selected type on login, and fail closed for unknown saved types. Keep the original wolf migration and Dreadfowl behavior exact. Do not add summoning XP, renewal semantics, special points, combat, or any other new gameplay claim unless it is separately source-specified and tested.

### 6. Add permanent structural gates

Add tools/test_summoning_phase5c_corpus.py and a test-summoning-phase5c-corpus Make target. Make mock230-cache-summoning and test-port depend on it, but keep client acceptance separate from ordinary flag-off cache construction.

The structural test must execute a non-zero number of checks and prove:

- exact 45-row catalog/INI roots and exact derived closure; no Dreadfowl, Spirit wolf, Phase-6, Phoenix, Sacred Clay, review-only, pet, scroll, tertiary, potion, synth, or sound source leaked in;
- all target IDs/names are unique, minted/unreviewed, prefix-owned, inside measured reservations, and disjoint from the primary/Dreadfowl ledgers;
- config action sanitation and ifop4=Summon for every pouch;
- source files/assets/pack rows are cohort-isolated and survive a disposable feature-on stage;
- the staged tree contains all expected corpus artifacts and zero summoning_roster_530 bytes or filenames; the review fingerprint, counts, and archived broad manifests are unchanged;
- positive and negative staging fixtures for an admitted corpus token, an unknown prefix, a pet, unsafe synth, review-only row, partial pair, extra closure row, wrong source/name, range escape, and non-minted row.

Keep the phase-5a audit and phase-5b Dreadfowl gate as prerequisites and make their check counts part of the report. A passing new test never substitutes for a passing preservation test.

### 7. Prove the full corpus in the normal client

Add tools/test_summoning_phase5c_corpus_runtime.py, modelled on the Dreadfowl real-client test, and a separate Make target. Build/run only with ENABLE_ASAN=0.

Shard the 45 cases across fresh temporary saves if needed, but do not replace the matrix with a small sample. For **every** pair, the harness must grant only that pouch and the needed test level, then use the actual inventory right-click menu to prove:

1. visible Summon → native dynamic action → IF_BUTTONX op=6 → canonical OPHELD4;
2. pouch consumption, not Drop/OPHELD5, and exactly one correct owned target NPC;
3. target body and head model load, type replacement, ready-sequence bind, and actual walk-sequence evidence after a real Call/reposition path;
4. correct dynamic sidebar title/head through normal-server IF_SETNPCHEAD, with a retained framebuffer/log artifact;
5. actual Call, Dismiss, expiry cleanup, and logout/relog reconstruction for the selected type.

Add or use a narrow general NPC animation trace before claiming walk acceptance; a static walkanim field or a ready-only seq_bind is insufficient. The lifecycle oracle validates every profile's exact lifetime/drain arithmetic. The live matrix must additionally exercise all schedule families and the three long-lived profiles at controlled drain boundaries without directly summoning an NPC. Keep the test's debug surface limited to inventory provisioning and controlled clock/setup hooks after the real pouch click.

Negative real-client cases must cover insufficient level/points, an existing follower, unknown persisted type, unsupported source action, no duplicate follower, and flag-off behavior. Retain the logs/framebuffers in a cohort-specific build directory until review is complete.

### 8. Bake, isolate, and close only on evidence

Run all feature-on outputs in disposable directories. The final evidence set is:

    python3 tools/port_summoning_ids.py --check
    python3 tools/test_summoning_phase5a.py --tree OSRS-Content/osrs239-content
    make -C src test-summoning-phase5b
    make -C src test-summoning-phase5c-corpus
    make -C src mock230-cache-summoning
    make -C src mock230-scripts-summoning
    make -C src test-summoning-phase5c-corpus-runtime
    make -C src test-summoning-isolation
    make -C src test-summoning-byte-identity \
      SUMMONING_CACHE_BEFORE=/absolute/temp/before \
      SUMMONING_CACHE_AFTER=/absolute/temp/after

Also require the feature cache's zero-error cachepack/CS2 checks and the applicable server-band mock230_pack --check-only check. If a whole-tree membership gate is red for pre-existing unrelated content, preserve its baseline diagnostic and show that the corpus-specific stage, cache, pack, and isolation gates are clean; do not suppress or relabel an unrelated failure as a corpus success.

The flag-off proof uses two distinct cache snapshots and must compare non-zero regular files byte-for-byte. It is invalid to compare a cache to itself or to use the feature-on staged tree as the ordinary input.

### 9. Documentation and queue close-out

Only after every gate passes, update SUMMONING_PORT.md, SUMMONING_PORT_QUEUE.md, and the loop prompt with the measured source/closure counts, exact accepted scope, runtime check count, cache figures, flag-off result, and artifact location. Keep the ledger's unreviewed signoff language: functional client acceptance is not a human material/texture signoff. Record the excluded Phase-6 and Phase-7 roots explicitly so later work cannot mistake them for omissions.

If any of the 45 cannot meet the exact asset, source-lifecycle, or real-client contract, leave Phase 5c pending and create a named exception slice. Do not silently shrink the catalog, reuse a review-only mapping, or mark the remainder as the completed corpus.

## Definition of done

Phase 5c is done only when all 45 exact pairs—not a sample—meet the baseline familiar contract, the source catalog/ledger/boundary match the measured closure, Dreadfowl and review-only evidence remain unchanged, feature-on cache and server checks are clean, and the ordinary flag-off cache is byte-identical. At that point the next work is an explicitly separate Phase-6 container cohort or a later per-mechanic/special-move slice, never an implicit widening of this one.
