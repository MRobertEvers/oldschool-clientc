# Quest modernization plan

Status: proposed
Created: 2026-08-16
Scope: `OSRS-Content/osrs239-content/server/scripts/quests` plus every area,
skill, minigame, object, NPC, and interface trigger used by those quests.

## 1. Outcome

Every quest implementation in this tree should be playable from its real start
condition through its permanent post-quest state, match the current OSRS Wiki,
and use the rev-230/rev-239 content and interface machinery already present in
this repository. A quest is not complete merely because a debug command can set
its end-state or because a journal can display `QUEST COMPLETE!`.

The work produces four durable artifacts:

1. A machine-readable quest manifest generated from real completion call sites,
   partial quest roots, quest dbrows, and journal dispatch—not a hand-maintained
   count.
2. One audit record per implementation unit, with pinned OSRS Wiki revision IDs,
   discovered gaps, modernization work, and verification evidence.
3. Modernized RuneScript/config content, with engine changes only where the
   script VM genuinely cannot express required OSRS behavior.
4. Automated invariant and end-to-end tests that keep completed quests from
   silently regressing.

## 2. Baseline and scope correction

The current tree has **174 distinct calls to `~quest_complete_rewards`**:

- 161 normal quest dbrows;
- 3 miniquest dbrows; and
- 10 Recipe for Disaster subquest dbrows.

The comment and dispatch table in `quests/scripts/quest_cheat.rs2` claim 166.
That registry is missing eight real completion paths: **Biohazard, Eadgar's
Ruse, One Small Favour, Regicide, Sea Slug, Tree Gnome Village, Underground
Pass, and Witch's House**. This is an existing correctness defect, not just a
documentation discrepancy: `::complete` cannot prepare these prerequisites and
the self-test cannot exercise their state adapters.

There are also **14 partial quest roots** with journals/config/scripts but no
call to the shared completion path. They remain in scope and are the first
gameplay wave. Altogether this plan tracks **188 implementation units** (174
completable units plus 14 partial roots). Recipe for Disaster is deliberately
tracked as ten subquest units because each has an independent state machine and
completion reward.

Inventory rules:

- A completable unit is discovered from a real
  `~quest_complete_rewards(<dbrow>, ...)` call anywhere under `server/scripts`.
- A partial unit is a quest root with meaningful gameplay/journal/config content
  but no shared completion call.
- Quest-list dbrows alone are not implementations.
- A directory name alone is not proof of a separate quest; aliases and shared
  area triggers are folded into the canonical dbrow.
- Adding or removing a completion call must update the generated manifest and
  its invariant tests in the same change.

Initial risk signals justify a fresh audit instead of trusting the prior queue:

- seven legacy modal-open call sites remain across five quest roots (six
  `if_openmain` and one `if_openoverlay`);
- 102 quest roots contain the phrase `soft-skip` somewhere in active scripts or
  configs, although each occurrence still needs classification because some
  comments describe a removed skip;
- completion calls are distributed outside quest roots, which is how eight
  quests escaped the current completion registry; and
- the 14 partial roots can present journals or isolated interactions without a
  valid path to the shared completion lifecycle.

## 3. What “modern engine” means here

The target is the machinery already established by
`UI_ERA_PORTING_GUIDE.md`, `PORTING_GUIDE.md`, and the shared quest scripts:

- Native cache dbrows are the source for display name, quest points,
  difficulty, requirements, start location, and reward metadata where present.
- Native cache varps/varbits hold progress; parallel authored progress variables
  are allowed only when the cache has no suitable state.
- Completion goes through `~quest_complete_rewards`, the modern completion
  scroll, table-derived quest points, completed-count update, and jingle policy.
- Journals open from the dynamic quest list by dbrow and render through
  `~quest_journal`; no IF1 per-quest quest-list component or hand-enumerated
  quest-point policy remains.
- Modern panels mount with `if_opensub` into a named parent slot, run their
  cache-authored clientscript, and re-arm server-handled ops on every mount.
  Legacy `if_openmain`/`if_openoverlay` is not retained for a quest panel just
  because an old port used it.
- Dialogue choices use the modern `chatmenu` helpers and `last_slot`; old IF1
  component-selection logic is removed.
- Gameplay policy remains RuneScript/config data. C changes are reserved for
  missing general VM or protocol capability, never a quest-specific shortcut.
- Entity/config references are symbolic pack names. Numeric IDs copied from
  LostCity, 2009Scape, Quest Helper, or the Wiki are rejected.
- Player queues, NPC queues/timers, instances, map/loc transforms, and modal
  lifetimes use the current engine's ownership and protection rules.

This is not a mandate to force every quest into one generic state machine.
Quest-specific narrative logic remains quest-specific; the shared lifecycle,
UI, metadata, completion, persistence, and test contracts become uniform.

## 4. Per-quest audit record

Before changing a quest, create its row in the machine-readable manifest with:

- canonical dbrow, implementation root, and all external trigger files;
- state carrier(s), start state, every meaningful intermediate state, and end
  state;
- OSRS Wiki article, quick guide, transcript, journal section, and linked
  NPC/item dialogue pages, recording page revision IDs and retrieval date;
- Quest Helper directory when one exists, used only as a state/test guide;
- prerequisites, required/boostable stats, required items, quest points,
  experience, item rewards, unlocks, and post-quest effects;
- current status: `partial`, `audit-pending`, `in-progress`, `blocked`, or
  `verified-modern`;
- every known simplification, soft-skip, stale comment, raw ID, legacy UI open,
  unverified spawn, or engine fallback; and
- tests and live-client captures that prove the final result.

The Wiki article/quick guide defines mechanics, requirements, and rewards. The
transcript defines accept/refuse branches, re-talks, lost-item replacements,
busy/inventory-full behavior, and post-quest dialogue. Quest Helper may guide
state transitions but never overrides the Wiki or the osrs239 cache.

## 5. Per-quest modernization checklist

Complete these gates in order for every inventory row.

### Gate A — discover the entire implementation

1. Find the quest root, journal arm, `::complete` arm, start NPC/loc, completion
   call, and every cross-directory trigger touching its state variables.
2. Resolve all NPCs, locs, objs, interfaces, jingles, animations, maps, and
   dbrows against the osrs239 pack.
3. Build a state-transition table from Wiki/Transcript/Quest Helper and compare
   it with every read and write of the quest's state.
4. Reconcile contradictory old queue documents against the live tree; a `done`
   label is evidence to inspect, not acceptance.

### Gate B — remove old engine assumptions

1. Replace IF1 quest-list/completion remnants with the shared dbrow journal and
   completion APIs.
2. Replace legacy panel opens with named modern mounts; decompile the panel's
   onload clientscript, supply the vars it reads, arm every server op, and
   re-arm after remount.
3. Replace old dialogue component routing with `~p_choice*`/`last_slot`.
4. Replace raw IDs, cache-era aliases, hand-painted modern panels, and
   quest-specific C routing with symbolic config and RuneScript.
5. Audit every queue/timer for player/NPC subject, protected context, logout,
   death, region change, and modal-busy behavior.
6. Audit scripted NPC/loc additions for ownership, uniqueness, respawn,
   visibility, instance isolation, and cleanup. Prefer real map/world spawns
   when the cache/world data already supplies them.

### Gate C — close gameplay and narrative oversights

1. Enforce quest and skill prerequisites exactly, including boostability and
   quest-point gates.
2. Implement start refusal, re-talk at every stage, alternate valid choices,
   post-quest dialogue, and all reachable transcript side branches.
3. Handle missing/full inventory, duplicate quest items, item loss, replacement,
   death, logout/relogin, and reconnect in every relevant state.
4. Make item removal and reward granting atomic and idempotent. Repeated clicks,
   double resumes, and duplicate timer delivery must not duplicate rewards or
   skip state.
5. Verify all NPC/loc/object menu ops and use-on directions. No required action
   may exist only as an unused label or debug trigger.
6. Implement actual combat, puzzles, cutscenes, travel, instances, transforms,
   and failure/reset paths. A message saying “soft-skip” is a failing critical
   path, not a finished implementation.
7. Verify reward quantities/XP, quest points, unlock gates, transports, shops,
   spell access, music/jingles, and world changes against the Wiki.
8. Make the journal correct at every state, including partial item collection
   and branching routes, and make the end state agree with the dbrow or document
   the necessary native-state exception.

### Gate D — verify before marking modern

1. Run the quest-specific static audit and ensure no unresolved symbolic names,
   duplicate triggers, unregistered completion row, missing journal arm, or
   undisclosed soft-skip remains.
2. Run `tools/questhelper_extract.py <helper> --check` where applicable.
3. Run `make -C src mock230-scripts` and `mock230_pack --check-only` against the
   intended cache.
4. Run automated transition tests from not-started through complete, plus
   relog/reconnect, inventory-full, item-loss/replacement, repeated-action, and
   death/reset cases appropriate to the quest.
5. Run a real-client headless smoke from the real start trigger through the
   reward scroll and post-quest interaction. Capture packets/screenshots for
   every modern modal or dynamic interface touched.
6. Verify `::complete <dbrow>` twice: the first sets the correct permanent state
   and points; the second is a no-op.
7. Record the Wiki revision IDs, commands, captures, remaining non-critical
   deviations, and final status in the manifest.

`verified-modern` requires every critical path to be playable. A cosmetic
deviation may remain only with a precise issue, source citation, player impact,
and regression test around the simplified behavior.

## 6. Delivery sequence

### Phase 0 — make the inventory enforceable

- Add `tools/quest_audit.py` (or an equivalently scoped tool) to generate the
  manifest from completion call sites, quest roots, dbrows, journals, and cheat
  arms.
- Fail CI when the completion set, journal set, cheat set, and manifest differ.
- Add the eight missing cheat arms and assert end-state/idempotence for every
  completable row.
- Add lint findings for legacy interface opens, raw numeric entity/interface
  IDs, `soft-skip`/`deferred` markers, missing real start triggers, and quest
  state writes outside the recorded ownership surface.

### Phase 1 — finish the 14 partial roots

Work in dependency order, starting with short prerequisites and quests that
gate many already-completable descendants. Each partial quest must reach the
shared completion API and pass all four gates before moving on. Do not hide a
partial implementation by adding only a cheat arm.

Suggested order:

1. Scorpion Catcher, Fight Arena, Family Crest, The Tourist Trap, Lost City.
2. Big Chompy Bird Hunting, Elemental Workshop I, Horror from the Deep,
   Shades of Mort'ton, Temple of Ikov.
3. Troll Stronghold, In Search of the Myreque, Shilo Village, Tribal Totem.

Recompute the dependency graph from `quest:requirement_quests` before starting;
the order above is a seed, not a substitute for the cache data.

### Phase 2 — remove shared legacy machinery once

- Convert the remaining quest-specific `if_openmain`/`if_openoverlay` sites to
  modern named mounts and add shared open/arm helpers where multiple quests use
  the same panel shape.
- Centralize repeated, correct quest lifecycle operations only after at least
  two audited quests prove the abstraction.
- Add any generally required VM/protocol feature in its own tested change, then
  resume the quest that exposed it. No quest-specific C strings, IDs, or state
  transitions are accepted.

### Phase 3 — audit all 174 completable units

- Process prerequisite chains from roots to descendants so later quests reuse
  verified gates, travel, unlocks, and world state.
- Within a dependency layer, prioritize disclosed critical-path soft-skips,
  then legacy UI/queue/entity lifetime risks, then quests with no automated
  transition coverage, then cosmetic fidelity.
- Treat each RFD subquest as a separate row but run a final aggregate RFD test
  covering feast progression, access tiers, and the Culinaromancer finale.
- Keep one quest (or one inseparable quest-series slice) per reviewable change.
  Shared engine changes land separately before dependent content changes.

### Phase 4 — whole-system regression

- Run every not-started/start/in-progress/complete journal state.
- Run all completion arms and quest-point totals from a clean account.
- Run topological prerequisite completion and verify every downstream gate.
- Run logout/death/region-change fuzzing while quest queues, instances,
  temporary NPCs, loc transforms, and modals are active.
- Compare the generated manifest to the live Wiki page revisions; changed pages
  return their quests to `audit-pending`.

## 7. Review and completion policy

A quest change is accepted only when the review contains:

- Wiki article/quick-guide/transcript revision links;
- before/after state-transition table;
- list of removed legacy machinery and fixed oversights;
- compile/pack/test commands and results;
- live-client evidence for interfaces and critical interactions; and
- an explicit statement of any remaining deviation.

Progress is reported as counts of `partial`, `audit-pending`, `in-progress`,
`blocked`, and `verified-modern` rows. The old queue's broad `done` labels are
not carried forward.

## 8. Inventory

Wiki links below were resolved through the OSRS Wiki API on 2026-08-16. `Guide`
is the quest/section quick guide and `Transcript` is the full dialogue source.
The implementation path is the primary quest root; Gate A must still discover
external area/skill/minigame triggers before work begins.

### 8.1 Completable implementation units (174)

| Quest | Type | Dbrow | Implementation | OSRS Wiki |
|---|---|---|---|---|
| A Kingdom Divided | Quest | `quest_kingdomdivided` | [`quest_kingdomdivided`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_kingdomdivided/) | [Article](https://oldschool.runescape.wiki/w/A_Kingdom_Divided) · [Guide](https://oldschool.runescape.wiki/w/A_Kingdom_Divided/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Kingdom_Divided) · [Audit](quests/a_kingdom_divided.md) |
| A Night at the Theatre | Quest | `quest_nightatthetheatre` | [`quest_nightatthetheatre`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_nightatthetheatre/) | [Article](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre) · [Guide](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Night_at_the_Theatre) · [Audit](quests/a_night_at_the_theatre.md) |
| A Porcine of Interest | Quest | `quest_porcineofinterest` | [`quest_porcineofinterest`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_porcineofinterest/) | [Article](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest) · [Guide](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Porcine_of_Interest) · [Audit](quests/a_porcine_of_interest.md) |
| A Soul's Bane | Quest | `quest_soulsbane` | [`quest_soulsbane`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_soulsbane/) | [Article](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane) · [Guide](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Soul%27s_Bane) · [Audit](quests/a_souls_bane.md) |
| A Tail of Two Cats | Quest | `quest_tailoftwocats` | [`quest_atailoftwocats`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_atailoftwocats/) | [Article](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats) · [Guide](https://oldschool.runescape.wiki/w/A_Tail_of_Two_Cats/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Tail_of_Two_Cats) · [Audit](quests/a_tail_of_two_cats.md) |
| A Taste of Hope | Quest | `quest_tasteofhope` | [`quest_tasteofhope`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_tasteofhope/) | [Article](https://oldschool.runescape.wiki/w/A_Taste_of_Hope) · [Guide](https://oldschool.runescape.wiki/w/A_Taste_of_Hope/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AA_Taste_of_Hope) · [Audit](quests/a_taste_of_hope.md) |
| Animal Magnetism | Quest | `quest_animalmagnetism` | [`quest_animalmagnetism`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_animalmagnetism/) | [Article](https://oldschool.runescape.wiki/w/Animal_Magnetism) · [Guide](https://oldschool.runescape.wiki/w/Animal_Magnetism/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AAnimal_Magnetism) · [Audit](quests/animal_magnetism.md) |
| Another Slice of H.A.M. | Quest | `quest_anothersliceofham` | [`quest_anothersliceofham`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_anothersliceofham/) | [Article](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.) · [Guide](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M./Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AAnother_Slice_of_H.A.M.) · [Audit](quests/another_slice_of_ham.md) |
| At First Light | Quest | `quest_atfirstlight` | [`quest_atfirstlight`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_atfirstlight/) | [Article](https://oldschool.runescape.wiki/w/At_First_Light) · [Guide](https://oldschool.runescape.wiki/w/At_First_Light/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AAt_First_Light) · [Audit](quests/at_first_light.md) |
| Bear Your Soul | Miniquest | `miniquest_bearyoursoul` | [`quest_bearyoursoul`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_bearyoursoul/) | [Article](https://oldschool.runescape.wiki/w/Bear_Your_Soul) · [Guide](https://oldschool.runescape.wiki/w/Bear_Your_Soul/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABear_Your_Soul) · [Audit](quests/bear_your_soul.md) |
| Below Ice Mountain | Quest | `quest_belowicemountain` | [`quest_belowicemountain`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_belowicemountain/) | [Article](https://oldschool.runescape.wiki/w/Below_Ice_Mountain) · [Guide](https://oldschool.runescape.wiki/w/Below_Ice_Mountain/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABelow_Ice_Mountain) · [Audit](quests/below_ice_mountain.md) |
| Beneath Cursed Sands | Quest | `quest_beneathcursedsands` | [`quest_beneathcursedsands`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_beneathcursedsands/) | [Article](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands) · [Guide](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABeneath_Cursed_Sands) · [Audit](quests/beneath_cursed_sands.md) |
| Between a Rock... | Quest | `quest_betweenarock` | [`quest_betweenarock`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_betweenarock/) | [Article](https://oldschool.runescape.wiki/w/Between_a_Rock...) · [Guide](https://oldschool.runescape.wiki/w/Between_a_Rock.../Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABetween_a_Rock...) · [Audit](quests/between_a_rock.md) |
| Biohazard | Quest | `quest_biohazard` | [`quest_biohazard`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_biohazard/) | [Article](https://oldschool.runescape.wiki/w/Biohazard) · [Guide](https://oldschool.runescape.wiki/w/Biohazard/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABiohazard) |
| Black Knights' Fortress | Quest | `quest_blackknightsfortress` | [`quest_blackknight`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_blackknight/) | [Article](https://oldschool.runescape.wiki/w/Black_Knights%27_Fortress) · [Guide](https://oldschool.runescape.wiki/w/Black_Knights%27_Fortress/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABlack_Knights%27_Fortress) |
| Bone Voyage | Quest | `quest_bonevoyage` | [`quest_bonevoyage`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_bonevoyage/) | [Article](https://oldschool.runescape.wiki/w/Bone_Voyage) · [Guide](https://oldschool.runescape.wiki/w/Bone_Voyage/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ABone_Voyage) |
| Cabin Fever | Quest | `quest_cabinfever` | [`quest_cabinfever`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_cabinfever/) | [Article](https://oldschool.runescape.wiki/w/Cabin_Fever) · [Guide](https://oldschool.runescape.wiki/w/Cabin_Fever/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ACabin_Fever) |
| Children of the Sun | Quest | `quest_childrenofthesun` | [`quest_childrenofthesun`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_childrenofthesun/) | [Article](https://oldschool.runescape.wiki/w/Children_of_the_Sun) · [Guide](https://oldschool.runescape.wiki/w/Children_of_the_Sun/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AChildren_of_the_Sun) |
| Client of Kourend | Quest | `quest_clientofkourend` | [`quest_clientofkourend`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_clientofkourend/) | [Article](https://oldschool.runescape.wiki/w/Client_of_Kourend) · [Guide](https://oldschool.runescape.wiki/w/Client_of_Kourend/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AClient_of_Kourend) |
| Clock Tower | Quest | `quest_clocktower` | [`quest_cog`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_cog/) | [Article](https://oldschool.runescape.wiki/w/Clock_Tower) · [Guide](https://oldschool.runescape.wiki/w/Clock_Tower/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AClock_Tower) |
| Cold War | Quest | `quest_coldwar` | [`quest_coldwar`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_coldwar/) | [Article](https://oldschool.runescape.wiki/w/Cold_War) · [Guide](https://oldschool.runescape.wiki/w/Cold_War/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ACold_War) |
| Contact! | Quest | `quest_contact` | [`quest_contact`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_contact/) | [Article](https://oldschool.runescape.wiki/w/Contact%21) · [Guide](https://oldschool.runescape.wiki/w/Contact%21/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AContact%21) |
| Cook's Assistant | Quest | `quest_cooksassistant` | [`quest_cook`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_cook/) | [Article](https://oldschool.runescape.wiki/w/Cook%27s_Assistant) · [Guide](https://oldschool.runescape.wiki/w/Cook%27s_Assistant/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ACook%27s_Assistant) |
| Creature of Fenkenstrain | Quest | `quest_creatureoffenkenstrain` | [`quest_fenkenstrain`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_fenkenstrain/) | [Article](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain) · [Guide](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ACreature_of_Fenkenstrain) |
| Current Affairs | Quest | `quest_currentaffairs` | [`quest_currentaffairs`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/) | [Article](https://oldschool.runescape.wiki/w/Current_Affairs) · [Guide](https://oldschool.runescape.wiki/w/Current_Affairs/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ACurrent_Affairs) |
| Darkness of Hallowvale | Quest | `quest_darknessofhallowvale` | [`quest_darknessofhallowvale`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_darknessofhallowvale/) | [Article](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale) · [Guide](https://oldschool.runescape.wiki/w/Darkness_of_Hallowvale/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADarkness_of_Hallowvale) |
| Death Plateau | Quest | `quest_deathplateau` | [`quest_death`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_death/) | [Article](https://oldschool.runescape.wiki/w/Death_Plateau) · [Guide](https://oldschool.runescape.wiki/w/Death_Plateau/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADeath_Plateau) |
| Death on the Isle | Quest | `quest_deathontheisle` | [`quest_deathontheisle`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_deathontheisle/) | [Article](https://oldschool.runescape.wiki/w/Death_on_the_Isle) · [Guide](https://oldschool.runescape.wiki/w/Death_on_the_Isle/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADeath_on_the_Isle) |
| Death to the Dorgeshuun | Quest | `quest_deathtothedorgeshuun` | [`quest_deathtothedorgeshuun`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_deathtothedorgeshuun/) | [Article](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun) · [Guide](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADeath_to_the_Dorgeshuun) |
| Defender of Varrock | Quest | `quest_defenderofvarrock` | [`quest_defenderofvarrock`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_defenderofvarrock/) | [Article](https://oldschool.runescape.wiki/w/Defender_of_Varrock) · [Guide](https://oldschool.runescape.wiki/w/Defender_of_Varrock/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADefender_of_Varrock) |
| Demon Slayer | Quest | `quest_demonslayer` | [`quest_demon`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_demon/) | [Article](https://oldschool.runescape.wiki/w/Demon_Slayer) · [Guide](https://oldschool.runescape.wiki/w/Demon_Slayer/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADemon_Slayer) |
| Desert Treasure I | Quest | `quest_deserttreasure` | [`quest_deserttreasure`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_deserttreasure/) | [Article](https://oldschool.runescape.wiki/w/Desert_Treasure_I) · [Guide](https://oldschool.runescape.wiki/w/Desert_Treasure_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADesert_Treasure_I) |
| Desert Treasure II - The Fallen Empire | Quest | `quest_deserttreasure2` | [`quest_deserttreasureii`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_deserttreasureii/) | [Article](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire) · [Guide](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADesert_Treasure_II_-_The_Fallen_Empire) |
| Devious Minds | Quest | `quest_deviousminds` | [`quest_deviousminds`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_deviousminds/) | [Article](https://oldschool.runescape.wiki/w/Devious_Minds) · [Guide](https://oldschool.runescape.wiki/w/Devious_Minds/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADevious_Minds) |
| Doric's Quest | Quest | `quest_dorics` | [`quest_doric`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_doric/) | [Article](https://oldschool.runescape.wiki/w/Doric%27s_Quest) · [Guide](https://oldschool.runescape.wiki/w/Doric%27s_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADoric%27s_Quest) |
| Dragon Slayer I | Quest | `quest_dragonslayer1` | [`quest_dragon`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_dragon/) | [Article](https://oldschool.runescape.wiki/w/Dragon_Slayer_I) · [Guide](https://oldschool.runescape.wiki/w/Dragon_Slayer_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADragon_Slayer_I) |
| Dragon Slayer II | Quest | `quest_dragonslayer2` | [`quest_dragonslayer2`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_dragonslayer2/) | [Article](https://oldschool.runescape.wiki/w/Dragon_Slayer_II) · [Guide](https://oldschool.runescape.wiki/w/Dragon_Slayer_II/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADragon_Slayer_II) |
| Dream Mentor | Quest | `quest_dreammentor` | [`quest_dreammentor`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_dreammentor/) | [Article](https://oldschool.runescape.wiki/w/Dream_Mentor) · [Guide](https://oldschool.runescape.wiki/w/Dream_Mentor/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADream_Mentor) |
| Druidic Ritual | Quest | `quest_druidicritual` | [`quest_druid`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_druid/) | [Article](https://oldschool.runescape.wiki/w/Druidic_Ritual) · [Guide](https://oldschool.runescape.wiki/w/Druidic_Ritual/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADruidic_Ritual) |
| Dwarf Cannon | Quest | `quest_dwarfcannon` | [`quest_mcannon`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mcannon/) | [Article](https://oldschool.runescape.wiki/w/Dwarf_Cannon) · [Guide](https://oldschool.runescape.wiki/w/Dwarf_Cannon/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ADwarf_Cannon) |
| Eadgar's Ruse | Quest | `quest_eadgarsruse` | [`quest_eadgar`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_eadgar/) | [Article](https://oldschool.runescape.wiki/w/Eadgar%27s_Ruse) · [Guide](https://oldschool.runescape.wiki/w/Eadgar%27s_Ruse/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEadgar%27s_Ruse) |
| Eagles' Peak | Quest | `quest_eaglespeak` | [`quest_eaglepeak`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_eaglepeak/) | [Article](https://oldschool.runescape.wiki/w/Eagles%27_Peak) · [Guide](https://oldschool.runescape.wiki/w/Eagles%27_Peak/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEagles%27_Peak) |
| Elemental Workshop II | Quest | `quest_elementalworkshop2` | [`quest_elementalworkshopii`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_elementalworkshopii/) | [Article](https://oldschool.runescape.wiki/w/Elemental_Workshop_II) · [Guide](https://oldschool.runescape.wiki/w/Elemental_Workshop_II/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AElemental_Workshop_II) |
| Enakhra's Lament | Quest | `quest_enakhraslament` | [`quest_enakhraslament`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_enakhraslament/) | [Article](https://oldschool.runescape.wiki/w/Enakhra%27s_Lament) · [Guide](https://oldschool.runescape.wiki/w/Enakhra%27s_Lament/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEnakhra%27s_Lament) |
| Enlightened Journey | Quest | `quest_enlightenedjourney` | [`quest_enlightenedjourney`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_enlightenedjourney/) | [Article](https://oldschool.runescape.wiki/w/Enlightened_Journey) · [Guide](https://oldschool.runescape.wiki/w/Enlightened_Journey/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEnlightened_Journey) |
| Enter the Abyss | Miniquest | `miniquest_entertheabyss` | [`quest_entertheabyss`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_entertheabyss/) | [Article](https://oldschool.runescape.wiki/w/Enter_the_Abyss) · [Guide](https://oldschool.runescape.wiki/w/Enter_the_Abyss/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEnter_the_Abyss) |
| Ernest the Chicken | Quest | `quest_ernestthechicken` | [`quest_haunted`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_haunted/) | [Article](https://oldschool.runescape.wiki/w/Ernest_the_Chicken) · [Guide](https://oldschool.runescape.wiki/w/Ernest_the_Chicken/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AErnest_the_Chicken) |
| Ethically Acquired Antiquities | Quest | `quest_ethicallyacquiredantiquities` | [`quest_ethicallyacquiredantiquities`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ethicallyacquiredantiquities/) | [Article](https://oldschool.runescape.wiki/w/Ethically_Acquired_Antiquities) · [Guide](https://oldschool.runescape.wiki/w/Ethically_Acquired_Antiquities/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AEthically_Acquired_Antiquities) |
| Fishing Contest | Quest | `quest_fishingcontest` | [`quest_fishingcompo`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_fishingcompo/) | [Article](https://oldschool.runescape.wiki/w/Fishing_Contest) · [Guide](https://oldschool.runescape.wiki/w/Fishing_Contest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AFishing_Contest) |
| Forgettable Tale... | Quest | `quest_forgettabletale` | [`quest_forgettabletale`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_forgettabletale/) | [Article](https://oldschool.runescape.wiki/w/Forgettable_Tale...) · [Guide](https://oldschool.runescape.wiki/w/Forgettable_Tale.../Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AForgettable_Tale...) |
| Garden of Tranquillity | Quest | `quest_gardenoftranquillity` | [`quest_gardenoftranquility`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_gardenoftranquility/) | [Article](https://oldschool.runescape.wiki/w/Garden_of_Tranquillity) · [Guide](https://oldschool.runescape.wiki/w/Garden_of_Tranquillity/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGarden_of_Tranquillity) |
| Gertrude's Cat | Quest | `quest_gertrudescat` | [`quest_fluffs`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_fluffs/) | [Article](https://oldschool.runescape.wiki/w/Gertrude%27s_Cat) · [Guide](https://oldschool.runescape.wiki/w/Gertrude%27s_Cat/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGertrude%27s_Cat) |
| Getting Ahead | Quest | `quest_gettingahead` | [`quest_gettingahead`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_gettingahead/) | [Article](https://oldschool.runescape.wiki/w/Getting_Ahead) · [Guide](https://oldschool.runescape.wiki/w/Getting_Ahead/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGetting_Ahead) |
| Ghosts Ahoy | Quest | `quest_ghostsahoy` | [`quest_ghostsahoy`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ghostsahoy/) | [Article](https://oldschool.runescape.wiki/w/Ghosts_Ahoy) · [Guide](https://oldschool.runescape.wiki/w/Ghosts_Ahoy/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGhosts_Ahoy) |
| Goblin Diplomacy | Quest | `quest_goblindiplomacy` | [`quest_gobdip`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_gobdip/) | [Article](https://oldschool.runescape.wiki/w/Goblin_Diplomacy) · [Guide](https://oldschool.runescape.wiki/w/Goblin_Diplomacy/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGoblin_Diplomacy) |
| Grim Tales | Quest | `quest_grimtales` | [`quest_grimtales`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_grimtales/) | [Article](https://oldschool.runescape.wiki/w/Grim_Tales) · [Guide](https://oldschool.runescape.wiki/w/Grim_Tales/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AGrim_Tales) |
| Haunted Mine | Quest | `quest_hauntedmine` | [`quest_hauntedmine`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_hauntedmine/) | [Article](https://oldschool.runescape.wiki/w/Haunted_Mine) · [Guide](https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AHaunted_Mine) |
| Hazeel Cult | Quest | `quest_hazeelcult` | [`quest_hazeelcult`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_hazeelcult/) | [Article](https://oldschool.runescape.wiki/w/Hazeel_Cult) · [Guide](https://oldschool.runescape.wiki/w/Hazeel_Cult/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AHazeel_Cult) |
| Heroes' Quest | Quest | `quest_heroes` | [`quest_hero`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_hero/) | [Article](https://oldschool.runescape.wiki/w/Heroes%27_Quest) · [Guide](https://oldschool.runescape.wiki/w/Heroes%27_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AHeroes%27_Quest) |
| Holy Grail | Quest | `quest_holygrail` | [`quest_grail`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_grail/) | [Article](https://oldschool.runescape.wiki/w/Holy_Grail) · [Guide](https://oldschool.runescape.wiki/w/Holy_Grail/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AHoly_Grail) |
| Icthlarin's Little Helper | Quest | `quest_icthlarinslittlehelper` | [`quest_icthlarin`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_icthlarin/) | [Article](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper) · [Guide](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AIcthlarin%27s_Little_Helper) |
| Imp Catcher | Quest | `quest_impcatcher` | [`quest_imp`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_imp/) | [Article](https://oldschool.runescape.wiki/w/Imp_Catcher) · [Guide](https://oldschool.runescape.wiki/w/Imp_Catcher/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AImp_Catcher) |
| In Aid of the Myreque | Quest | `quest_inaidofthemyreque` | [`quest_inaidofthemyreque`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_inaidofthemyreque/) | [Article](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque) · [Guide](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AIn_Aid_of_the_Myreque) |
| In Search of Knowledge | Miniquest | `miniquest_insearchofknowledge` | [`quest_insearchofknowledge`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_insearchofknowledge/) | [Article](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge) · [Guide](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AIn_Search_of_Knowledge) |
| Jungle Potion | Quest | `quest_junglepotion` | [`quest_junglepotion`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_junglepotion/) | [Article](https://oldschool.runescape.wiki/w/Jungle_Potion) · [Guide](https://oldschool.runescape.wiki/w/Jungle_Potion/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AJungle_Potion) |
| King's Ransom | Quest | `quest_kingsransom` | [`quest_kingsransom`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_kingsransom/) | [Article](https://oldschool.runescape.wiki/w/King%27s_Ransom) · [Guide](https://oldschool.runescape.wiki/w/King%27s_Ransom/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AKing%27s_Ransom) |
| Land of the Goblins | Quest | `quest_landofthegoblins` | [`quest_landofthegoblins`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_landofthegoblins/) | [Article](https://oldschool.runescape.wiki/w/Land_of_the_Goblins) · [Guide](https://oldschool.runescape.wiki/w/Land_of_the_Goblins/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ALand_of_the_Goblins) |
| Legends' Quest | Quest | `quest_legends` | [`quest_legends`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/) | [Article](https://oldschool.runescape.wiki/w/Legends%27_Quest) · [Guide](https://oldschool.runescape.wiki/w/Legends%27_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ALegends%27_Quest) |
| Lunar Diplomacy | Quest | `quest_lunardiplomacy` | [`quest_lunardiplomacy`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_lunardiplomacy/) | [Article](https://oldschool.runescape.wiki/w/Lunar_Diplomacy) · [Guide](https://oldschool.runescape.wiki/w/Lunar_Diplomacy/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ALunar_Diplomacy) |
| Making Friends with My Arm | Quest | `quest_makingfriendswithmyarm` | [`quest_makingfriendswithmyarm`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_makingfriendswithmyarm/) | [Article](https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm) · [Guide](https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMaking_Friends_with_My_Arm) |
| Making History | Quest | `quest_makinghistory` | [`quest_makinghistory`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_makinghistory/) | [Article](https://oldschool.runescape.wiki/w/Making_History) · [Guide](https://oldschool.runescape.wiki/w/Making_History/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMaking_History) |
| Meat and Greet | Quest | `quest_meatandgreet` | [`quest_meatandgreet`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_meatandgreet/) | [Article](https://oldschool.runescape.wiki/w/Meat_and_Greet) · [Guide](https://oldschool.runescape.wiki/w/Meat_and_Greet/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMeat_and_Greet) |
| Merlin's Crystal | Quest | `quest_merlinscrystal` | [`quest_arthur`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_arthur/) | [Article](https://oldschool.runescape.wiki/w/Merlin%27s_Crystal) · [Guide](https://oldschool.runescape.wiki/w/Merlin%27s_Crystal/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMerlin%27s_Crystal) |
| Misthalin Mystery | Quest | `quest_misthalinmystery` | [`quest_misthalinmystery`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_misthalinmystery/) | [Article](https://oldschool.runescape.wiki/w/Misthalin_Mystery) · [Guide](https://oldschool.runescape.wiki/w/Misthalin_Mystery/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMisthalin_Mystery) |
| Monk's Friend | Quest | `quest_monksfriend` | [`quest_drunkmonk`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_drunkmonk/) | [Article](https://oldschool.runescape.wiki/w/Monk%27s_Friend) · [Guide](https://oldschool.runescape.wiki/w/Monk%27s_Friend/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMonk%27s_Friend) |
| Monkey Madness I | Quest | `quest_monkeymadness1` | [`quest_mm`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mm/) | [Article](https://oldschool.runescape.wiki/w/Monkey_Madness_I) · [Guide](https://oldschool.runescape.wiki/w/Monkey_Madness_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMonkey_Madness_I) |
| Monkey Madness II | Quest | `quest_monkeymadness2` | [`quest_monkeymadnessii`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_monkeymadnessii/) | [Article](https://oldschool.runescape.wiki/w/Monkey_Madness_II) · [Guide](https://oldschool.runescape.wiki/w/Monkey_Madness_II/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMonkey_Madness_II) |
| Mountain Daughter | Quest | `quest_mountaindaughter` | [`quest_mountaindaughter`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mountaindaughter/) | [Article](https://oldschool.runescape.wiki/w/Mountain_Daughter) · [Guide](https://oldschool.runescape.wiki/w/Mountain_Daughter/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMountain_Daughter) |
| Mourning's End Part I | Quest | `quest_mourningsendpart1` | [`quest_mourningsendparti`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mourningsendparti/) | [Article](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I) · [Guide](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMourning%27s_End_Part_I) |
| Mourning's End Part II | Quest | `quest_mourningsendpart2` | [`quest_mourningsendpartii`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mourningsendpartii/) | [Article](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_II) · [Guide](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_II/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMourning%27s_End_Part_II) |
| Murder Mystery | Quest | `quest_murdermystery` | [`quest_murder`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_murder/) | [Article](https://oldschool.runescape.wiki/w/Murder_Mystery) · [Guide](https://oldschool.runescape.wiki/w/Murder_Mystery/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMurder_Mystery) |
| My Arm's Big Adventure | Quest | `quest_myarmsbigadventure` | [`quest_myarmsbigadventure`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_myarmsbigadventure/) | [Article](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure) · [Guide](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AMy_Arm%27s_Big_Adventure) |
| Nature Spirit | Quest | `quest_naturespirit` | [`quest_druidspirit`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_druidspirit/) | [Article](https://oldschool.runescape.wiki/w/Nature_Spirit) · [Guide](https://oldschool.runescape.wiki/w/Nature_Spirit/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ANature_Spirit) |
| Observatory Quest | Quest | `quest_observatory` | [`quest_itgronigen`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_itgronigen/) | [Article](https://oldschool.runescape.wiki/w/Observatory_Quest) · [Guide](https://oldschool.runescape.wiki/w/Observatory_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AObservatory_Quest) |
| Olaf's Quest | Quest | `quest_olafs` | [`quest_olafsquest`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_olafsquest/) | [Article](https://oldschool.runescape.wiki/w/Olaf%27s_Quest) · [Guide](https://oldschool.runescape.wiki/w/Olaf%27s_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AOlaf%27s_Quest) |
| One Small Favour | Quest | `quest_onesmallfavour` | [`quest_onesmallfavour`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_onesmallfavour/) | [Article](https://oldschool.runescape.wiki/w/One_Small_Favour) · [Guide](https://oldschool.runescape.wiki/w/One_Small_Favour/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AOne_Small_Favour) |
| Pandemonium | Quest | `quest_pandemonium` | [`quest_pandemonium`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_pandemonium/) | [Article](https://oldschool.runescape.wiki/w/Pandemonium) · [Guide](https://oldschool.runescape.wiki/w/Pandemonium/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APandemonium) |
| Perilous Moons | Quest | `quest_perilousmoons` | [`quest_perilousmoons`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_perilousmoons/) | [Article](https://oldschool.runescape.wiki/w/Perilous_Moons) · [Guide](https://oldschool.runescape.wiki/w/Perilous_Moons/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APerilous_Moons) |
| Pirate's Treasure | Quest | `quest_piratestreasure` | [`quest_hunt`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_hunt/) | [Article](https://oldschool.runescape.wiki/w/Pirate%27s_Treasure) · [Guide](https://oldschool.runescape.wiki/w/Pirate%27s_Treasure/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APirate%27s_Treasure) |
| Plague City | Quest | `quest_plaguecity` | [`quest_elena`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_elena/) | [Article](https://oldschool.runescape.wiki/w/Plague_City) · [Guide](https://oldschool.runescape.wiki/w/Plague_City/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APlague_City) |
| Priest in Peril | Quest | `quest_priestinperil` | [`quest_priestperil`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_priestperil/) | [Article](https://oldschool.runescape.wiki/w/Priest_in_Peril) · [Guide](https://oldschool.runescape.wiki/w/Priest_in_Peril/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APriest_in_Peril) |
| Prince Ali Rescue | Quest | `quest_princealirescue` | [`quest_prince`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_prince/) | [Article](https://oldschool.runescape.wiki/w/Prince_Ali_Rescue) · [Guide](https://oldschool.runescape.wiki/w/Prince_Ali_Rescue/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APrince_Ali_Rescue) |
| Prying Times | Quest | `quest_pryingtimes` | [`quest_pryingtimes`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_pryingtimes/) | [Article](https://oldschool.runescape.wiki/w/Prying_Times) · [Guide](https://oldschool.runescape.wiki/w/Prying_Times/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3APrying_Times) |
| Rag and Bone Man I | Quest | `quest_ragandboneman1` | [`quest_ragandboneman`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ragandboneman/) | [Article](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_I) · [Guide](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARag_and_Bone_Man_I) |
| Ratcatchers | Quest | `quest_ratcatchers` | [`quest_ratcatchers`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ratcatchers/) | [Article](https://oldschool.runescape.wiki/w/Ratcatchers) · [Guide](https://oldschool.runescape.wiki/w/Ratcatchers/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARatcatchers) |
| Recipe for Disaster - Another Cook's Quest | RFD subquest | `subquest_rfd_intro` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Another_Cook%27s_Quest) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Another_Cook%27s_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Another_Cook%27s_Quest) |
| Recipe for Disaster - Culinaromancer | RFD subquest | `subquest_rfd_finale` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Defeating_the_Culinaromancer) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Defeating_the_Culinaromancer) |
| Recipe for Disaster - Evil Dave | RFD subquest | `subquest_rfd_evildave` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Evil_Dave) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Evil_Dave/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_Evil_Dave) |
| Recipe for Disaster - King Awowogei | RFD subquest | `subquest_rfd_monkey` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_King_Awowogei) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_King_Awowogei/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_King_Awowogei) |
| Recipe for Disaster - Lumbridge Guide | RFD subquest | `subquest_rfd_lumbridgeguide` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Lumbridge_Guide) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Lumbridge_Guide/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_the_Lumbridge_Guide) |
| Recipe for Disaster - Mountain Dwarf | RFD subquest | `subquest_rfd_dwarf` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Mountain_Dwarf) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Mountain_Dwarf/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_the_Mountain_Dwarf) |
| Recipe for Disaster - Pirate Pete | RFD subquest | `subquest_rfd_pirate` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Pirate_Pete) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Pirate_Pete/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_Pirate_Pete) |
| Recipe for Disaster - Sir Amik Varze | RFD subquest | `subquest_rfd_amikvarze` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Sir_Amik_Varze) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Sir_Amik_Varze/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_Sir_Amik_Varze) |
| Recipe for Disaster - Skrach Uglogwee | RFD subquest | `subquest_rfd_ogre` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Skrach_Uglogwee) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Skrach_Uglogwee/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_Skrach_Uglogwee) |
| Recipe for Disaster - Wartface & Bentnoze | RFD subquest | `subquest_rfd_goblins` | [`quest_recipefordisaster`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recipefordisaster/) | [Article](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Goblin_generals) · [Guide](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Goblin_generals/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecipe_for_Disaster/Freeing_the_Goblin_generals) |
| Recruitment Drive | Quest | `quest_recruitmentdrive` | [`quest_recruitmentdrive`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_recruitmentdrive/) | [Article](https://oldschool.runescape.wiki/w/Recruitment_Drive) · [Guide](https://oldschool.runescape.wiki/w/Recruitment_Drive/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARecruitment_Drive) |
| Regicide | Quest | `quest_regicide` | [`quest_regicide`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_regicide/) | [Article](https://oldschool.runescape.wiki/w/Regicide) · [Guide](https://oldschool.runescape.wiki/w/Regicide/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARegicide) |
| Romeo & Juliet | Quest | `quest_romeoandjuliet` | [`quest_romeojuliet`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_romeojuliet/) | [Article](https://oldschool.runescape.wiki/w/Romeo_%26_Juliet) · [Guide](https://oldschool.runescape.wiki/w/Romeo_%26_Juliet/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARomeo_%26_Juliet) |
| Roving Elves | Quest | `quest_rovingelves` | [`quest_rovingelves`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_rovingelves/) | [Article](https://oldschool.runescape.wiki/w/Roving_Elves) · [Guide](https://oldschool.runescape.wiki/w/Roving_Elves/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARoving_Elves) |
| Royal Trouble | Quest | `quest_royaltrouble` | [`quest_royaltrouble`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_royaltrouble/) | [Article](https://oldschool.runescape.wiki/w/Royal_Trouble) · [Guide](https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARoyal_Trouble) |
| Rum Deal | Quest | `quest_rumdeal` | [`quest_rumdeal`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_rumdeal/) | [Article](https://oldschool.runescape.wiki/w/Rum_Deal) · [Guide](https://oldschool.runescape.wiki/w/Rum_Deal/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARum_Deal) |
| Rune Mysteries | Quest | `quest_runemysteries` | [`quest_runemysteries`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_runemysteries/) | [Article](https://oldschool.runescape.wiki/w/Rune_Mysteries) · [Guide](https://oldschool.runescape.wiki/w/Rune_Mysteries/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ARune_Mysteries) |
| Scrambled! | Quest | `quest_scrambled` | [`quest_scrambled`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_scrambled/) | [Article](https://oldschool.runescape.wiki/w/Scrambled%21) · [Guide](https://oldschool.runescape.wiki/w/Scrambled%21/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AScrambled%21) |
| Sea Slug | Quest | `quest_seaslug` | [`quest_seaslug`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_seaslug/) | [Article](https://oldschool.runescape.wiki/w/Sea_Slug) · [Guide](https://oldschool.runescape.wiki/w/Sea_Slug/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASea_Slug) |
| Secrets of the North | Quest | `quest_secretsofthenorth` | [`quest_secretsofthenorth`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_secretsofthenorth/) | [Article](https://oldschool.runescape.wiki/w/Secrets_of_the_North) · [Guide](https://oldschool.runescape.wiki/w/Secrets_of_the_North/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASecrets_of_the_North) |
| Shadow of the Storm | Quest | `quest_shadowofthestorm` | [`quest_shadowstorm`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_shadowstorm/) | [Article](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm) · [Guide](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AShadow_of_the_Storm) |
| Shadows of Custodia | Quest | `quest_shadowsofcustodia` | [`quest_shadowsofcustodia`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_shadowsofcustodia/) | [Article](https://oldschool.runescape.wiki/w/Shadows_of_Custodia) · [Guide](https://oldschool.runescape.wiki/w/Shadows_of_Custodia/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AShadows_of_Custodia) |
| Sheep Herder | Quest | `quest_sheepherder` | [`quest_sheepherder`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_sheepherder/) | [Article](https://oldschool.runescape.wiki/w/Sheep_Herder) · [Guide](https://oldschool.runescape.wiki/w/Sheep_Herder/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASheep_Herder) |
| Sheep Shearer | Quest | `quest_sheepshearer` | [`quest_sheep`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_sheep/) | [Article](https://oldschool.runescape.wiki/w/Sheep_Shearer) · [Guide](https://oldschool.runescape.wiki/w/Sheep_Shearer/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASheep_Shearer) |
| Shield of Arrav | Quest | `quest_shieldofarrav` | [`quest_blackarmgang`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_blackarmgang/) | [Article](https://oldschool.runescape.wiki/w/Shield_of_Arrav) · [Guide](https://oldschool.runescape.wiki/w/Shield_of_Arrav/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AShield_of_Arrav) |
| Sins of the Father | Quest | `quest_sinsofthefather` | [`quest_sinsofthefather`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_sinsofthefather/) | [Article](https://oldschool.runescape.wiki/w/Sins_of_the_Father) · [Guide](https://oldschool.runescape.wiki/w/Sins_of_the_Father/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASins_of_the_Father) |
| Sleeping Giants | Quest | `quest_sleepinggiants` | [`quest_sleepinggiants`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_sleepinggiants/) | [Article](https://oldschool.runescape.wiki/w/Sleeping_Giants) · [Guide](https://oldschool.runescape.wiki/w/Sleeping_Giants/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASleeping_Giants) |
| Song of the Elves | Quest | `quest_songoftheelves` | [`quest_songoftheelves`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_songoftheelves/) | [Article](https://oldschool.runescape.wiki/w/Song_of_the_Elves) · [Guide](https://oldschool.runescape.wiki/w/Song_of_the_Elves/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASong_of_the_Elves) |
| Spirits of the Elid | Quest | `quest_spiritsoftheelid` | [`quest_spiritsoftheelid`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_spiritsoftheelid/) | [Article](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid) · [Guide](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASpirits_of_the_Elid) |
| Swan Song | Quest | `quest_swansong` | [`quest_swansong`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_swansong/) | [Article](https://oldschool.runescape.wiki/w/Swan_Song) · [Guide](https://oldschool.runescape.wiki/w/Swan_Song/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ASwan_Song) |
| Tai Bwo Wannai Trio | Quest | `quest_taibwowannaitrio` | [`quest_tbwt`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_tbwt/) | [Article](https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio) · [Guide](https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATai_Bwo_Wannai_Trio) |
| Tale of the Righteous | Quest | `quest_taleoftherighteous` | [`quest_taleoftherighteous`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_taleoftherighteous/) | [Article](https://oldschool.runescape.wiki/w/Tale_of_the_Righteous) · [Guide](https://oldschool.runescape.wiki/w/Tale_of_the_Righteous/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATale_of_the_Righteous) |
| Tears of Guthix | Quest | `quest_tearsofguthix` | [`quest_tearsofguthix`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_tearsofguthix/) | [Article](https://oldschool.runescape.wiki/w/Tears_of_Guthix) · [Guide](https://oldschool.runescape.wiki/w/Tears_of_Guthix/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATears_of_Guthix) |
| Temple of the Eye | Quest | `quest_templeoftheeye` | [`quest_templeoftheeye`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_templeoftheeye/) | [Article](https://oldschool.runescape.wiki/w/Temple_of_the_Eye) · [Guide](https://oldschool.runescape.wiki/w/Temple_of_the_Eye/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATemple_of_the_Eye) |
| The Ascent of Arceuus | Quest | `quest_ascentofarceuus` | [`quest_ascentofarceuus`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ascentofarceuus/) | [Article](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus) · [Guide](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Ascent_of_Arceuus) |
| The Corsair Curse | Quest | `quest_corsaircurse` | [`quest_corsaircurse`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_corsaircurse/) | [Article](https://oldschool.runescape.wiki/w/The_Corsair_Curse) · [Guide](https://oldschool.runescape.wiki/w/The_Corsair_Curse/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Corsair_Curse) |
| The Curse of Arrav | Quest | `quest_curseofarrav` | [`quest_curseofarrav`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_curseofarrav/) | [Article](https://oldschool.runescape.wiki/w/The_Curse_of_Arrav) · [Guide](https://oldschool.runescape.wiki/w/The_Curse_of_Arrav/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Curse_of_Arrav) |
| The Depths of Despair | Quest | `quest_depthsofdespair` | [`quest_depthsofdespair`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_depthsofdespair/) | [Article](https://oldschool.runescape.wiki/w/The_Depths_of_Despair) · [Guide](https://oldschool.runescape.wiki/w/The_Depths_of_Despair/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Depths_of_Despair) |
| The Dig Site | Quest | `quest_digsite` | [`quest_itexam`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_itexam/) | [Article](https://oldschool.runescape.wiki/w/The_Dig_Site) · [Guide](https://oldschool.runescape.wiki/w/The_Dig_Site/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Dig_Site) |
| The Eyes of Glouphrie | Quest | `quest_eyesofglouphrie` | [`quest_theeyesofglouphrie`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_theeyesofglouphrie/) | [Article](https://oldschool.runescape.wiki/w/The_Eyes_of_Glouphrie) · [Guide](https://oldschool.runescape.wiki/w/The_Eyes_of_Glouphrie/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Eyes_of_Glouphrie) |
| The Feud | Quest | `quest_feud` | [`quest_thefeud`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_thefeud/) | [Article](https://oldschool.runescape.wiki/w/The_Feud) · [Guide](https://oldschool.runescape.wiki/w/The_Feud/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Feud) |
| The Final Dawn | Quest | `quest_finaldawn` | [`quest_finaldawn`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_finaldawn/) | [Article](https://oldschool.runescape.wiki/w/The_Final_Dawn) · [Guide](https://oldschool.runescape.wiki/w/The_Final_Dawn/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Final_Dawn) |
| The Forsaken Tower | Quest | `quest_forsakentower` | [`quest_forsakentower`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_forsakentower/) | [Article](https://oldschool.runescape.wiki/w/The_Forsaken_Tower) · [Guide](https://oldschool.runescape.wiki/w/The_Forsaken_Tower/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Forsaken_Tower) |
| The Fremennik Exiles | Quest | `quest_fremennikexiles` | [`quest_fremennikexiles`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_fremennikexiles/) | [Article](https://oldschool.runescape.wiki/w/The_Fremennik_Exiles) · [Guide](https://oldschool.runescape.wiki/w/The_Fremennik_Exiles/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Fremennik_Exiles) |
| The Fremennik Isles | Quest | `quest_fremennikisles` | [`quest_thefremennikisles`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_thefremennikisles/) | [Article](https://oldschool.runescape.wiki/w/The_Fremennik_Isles) · [Guide](https://oldschool.runescape.wiki/w/The_Fremennik_Isles/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Fremennik_Isles) |
| The Fremennik Trials | Quest | `quest_fremenniktrials` | [`quest_viking`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_viking/) | [Article](https://oldschool.runescape.wiki/w/The_Fremennik_Trials) · [Guide](https://oldschool.runescape.wiki/w/The_Fremennik_Trials/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Fremennik_Trials) |
| The Garden of Death | Quest | `quest_gardenofdeath` | [`quest_gardenofdeath`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_gardenofdeath/) | [Article](https://oldschool.runescape.wiki/w/The_Garden_of_Death) · [Guide](https://oldschool.runescape.wiki/w/The_Garden_of_Death/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Garden_of_Death) |
| The Giant Dwarf | Quest | `quest_giantdwarf` | [`quest_giantdwarf`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_giantdwarf/) | [Article](https://oldschool.runescape.wiki/w/The_Giant_Dwarf) · [Guide](https://oldschool.runescape.wiki/w/The_Giant_Dwarf/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Giant_Dwarf) |
| The Golem | Quest | `quest_golem` | [`quest_golem`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_golem/) | [Article](https://oldschool.runescape.wiki/w/The_Golem) · [Guide](https://oldschool.runescape.wiki/w/The_Golem/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Golem) |
| The Grand Tree | Quest | `quest_grandtree` | [`quest_grandtree`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_grandtree/) | [Article](https://oldschool.runescape.wiki/w/The_Grand_Tree) · [Guide](https://oldschool.runescape.wiki/w/The_Grand_Tree/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Grand_Tree) |
| The Great Brain Robbery | Quest | `quest_greatbrainrobbery` | [`quest_thegreatbrainrobbery`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_thegreatbrainrobbery/) | [Article](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery) · [Guide](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Great_Brain_Robbery) |
| The Hand in the Sand | Quest | `quest_handinthesand` | [`quest_handinthesand`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_handinthesand/) | [Article](https://oldschool.runescape.wiki/w/The_Hand_in_the_Sand) · [Guide](https://oldschool.runescape.wiki/w/The_Hand_in_the_Sand/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Hand_in_the_Sand) |
| The Heart of Darkness | Quest | `quest_heartofdarkness` | [`quest_heartofdarkness`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_heartofdarkness/) | [Article](https://oldschool.runescape.wiki/w/The_Heart_of_Darkness) · [Guide](https://oldschool.runescape.wiki/w/The_Heart_of_Darkness/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Heart_of_Darkness) |
| The Ides of Milk | Quest | `quest_idesofmilk` | [`quest_idesofmilk`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_idesofmilk/) | [Article](https://oldschool.runescape.wiki/w/The_Ides_of_Milk) · [Guide](https://oldschool.runescape.wiki/w/The_Ides_of_Milk/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Ides_of_Milk) |
| The Knight's Sword | Quest | `quest_knightssword` | [`quest_squire`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_squire/) | [Article](https://oldschool.runescape.wiki/w/The_Knight%27s_Sword) · [Guide](https://oldschool.runescape.wiki/w/The_Knight%27s_Sword/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Knight%27s_Sword) |
| The Lost Tribe | Quest | `quest_losttribe` | [`quest_losttribe`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_losttribe/) | [Article](https://oldschool.runescape.wiki/w/The_Lost_Tribe) · [Guide](https://oldschool.runescape.wiki/w/The_Lost_Tribe/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Lost_Tribe) |
| The Path of Glouphrie | Quest | `quest_pathofglouphrie` | [`quest_pathofglouphrie`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_pathofglouphrie/) | [Article](https://oldschool.runescape.wiki/w/The_Path_of_Glouphrie) · [Guide](https://oldschool.runescape.wiki/w/The_Path_of_Glouphrie/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Path_of_Glouphrie) |
| The Queen of Thieves | Quest | `quest_queenofthieves` | [`quest_queenofthieves`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_queenofthieves/) | [Article](https://oldschool.runescape.wiki/w/The_Queen_of_Thieves) · [Guide](https://oldschool.runescape.wiki/w/The_Queen_of_Thieves/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Queen_of_Thieves) |
| The Red Reef | Quest | `quest_redreef` | [`quest_redreef`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_redreef/) | [Article](https://oldschool.runescape.wiki/w/The_Red_Reef) · [Guide](https://oldschool.runescape.wiki/w/The_Red_Reef/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Red_Reef) |
| The Restless Ghost | Quest | `quest_restlessghost` | [`quest_priest`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_priest/) | [Article](https://oldschool.runescape.wiki/w/The_Restless_Ghost) · [Guide](https://oldschool.runescape.wiki/w/The_Restless_Ghost/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Restless_Ghost) |
| The Ribbiting Tale of a Lily Pad Labour Dispute | Quest | `quest_ribbitingtale` | [`quest_ribbitingtale`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ribbitingtale/) | [Article](https://oldschool.runescape.wiki/w/The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute) · [Guide](https://oldschool.runescape.wiki/w/The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute) |
| The Slug Menace | Quest | `quest_slugmenace` | [`quest_theslugmenace`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_theslugmenace/) | [Article](https://oldschool.runescape.wiki/w/The_Slug_Menace) · [Guide](https://oldschool.runescape.wiki/w/The_Slug_Menace/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThe_Slug_Menace) |
| Throne of Miscellania | Quest | `quest_throneofmiscellania` | [`quest_misc`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_misc/) | [Article](https://oldschool.runescape.wiki/w/Throne_of_Miscellania) · [Guide](https://oldschool.runescape.wiki/w/Throne_of_Miscellania/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AThrone_of_Miscellania) |
| Tower of Life | Quest | `quest_toweroflife` | [`quest_toweroflife`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_toweroflife/) | [Article](https://oldschool.runescape.wiki/w/Tower_of_Life) · [Guide](https://oldschool.runescape.wiki/w/Tower_of_Life/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATower_of_Life) |
| Tree Gnome Village | Quest | `quest_treegnomevillage` | [`quest_tree`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_tree/) | [Article](https://oldschool.runescape.wiki/w/Tree_Gnome_Village) · [Guide](https://oldschool.runescape.wiki/w/Tree_Gnome_Village/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATree_Gnome_Village) |
| Troll Romance | Quest | `quest_trollromance` | [`quest_troll_love`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_troll_love/) | [Article](https://oldschool.runescape.wiki/w/Troll_Romance) · [Guide](https://oldschool.runescape.wiki/w/Troll_Romance/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATroll_Romance) |
| Troubled Tortugans | Quest | `quest_troubledtortugans` | [`quest_troubledtortugans`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_troubledtortugans/) | [Article](https://oldschool.runescape.wiki/w/Troubled_Tortugans) · [Guide](https://oldschool.runescape.wiki/w/Troubled_Tortugans/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATroubled_Tortugans) |
| Twilight's Promise | Quest | `quest_twilightspromise` | [`quest_twilightspromise`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_twilightspromise/) | [Article](https://oldschool.runescape.wiki/w/Twilight%27s_Promise) · [Guide](https://oldschool.runescape.wiki/w/Twilight%27s_Promise/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3ATwilight%27s_Promise) |
| Underground Pass | Quest | `quest_undergroundpass` | [`quest_upass`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_upass/) | [Article](https://oldschool.runescape.wiki/w/Underground_Pass) · [Guide](https://oldschool.runescape.wiki/w/Underground_Pass/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AUnderground_Pass) |
| Vampyre Slayer | Quest | `quest_vampyreslayer` | [`quest_vampire`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_vampire/) | [Article](https://oldschool.runescape.wiki/w/Vampyre_Slayer) · [Guide](https://oldschool.runescape.wiki/w/Vampyre_Slayer/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AVampyre_Slayer) |
| Wanted! | Quest | `quest_wanted` | [`quest_wanted`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_wanted/) | [Article](https://oldschool.runescape.wiki/w/Wanted%21) · [Guide](https://oldschool.runescape.wiki/w/Wanted%21/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWanted%21) |
| Watchtower | Quest | `quest_watchtower` | [`quest_itwatchtower`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_itwatchtower/) | [Article](https://oldschool.runescape.wiki/w/Watchtower) · [Guide](https://oldschool.runescape.wiki/w/Watchtower/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWatchtower) |
| Waterfall Quest | Quest | `quest_waterfall` | [`quest_waterfall`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_waterfall/) | [Article](https://oldschool.runescape.wiki/w/Waterfall_Quest) · [Guide](https://oldschool.runescape.wiki/w/Waterfall_Quest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWaterfall_Quest) |
| What Lies Below | Quest | `quest_whatliesbelow` | [`quest_whatliesbelow`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_whatliesbelow/) | [Article](https://oldschool.runescape.wiki/w/What_Lies_Below) · [Guide](https://oldschool.runescape.wiki/w/What_Lies_Below/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWhat_Lies_Below) |
| While Guthix Sleeps | Quest | `quest_whileguthixsleeps` | [`quest_whileguthixsleeps`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_whileguthixsleeps/) | [Article](https://oldschool.runescape.wiki/w/While_Guthix_Sleeps) · [Guide](https://oldschool.runescape.wiki/w/While_Guthix_Sleeps/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWhile_Guthix_Sleeps) |
| Witch's House | Quest | `quest_witchshouse` | [`quest_ball`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ball/) | [Article](https://oldschool.runescape.wiki/w/Witch%27s_House) · [Guide](https://oldschool.runescape.wiki/w/Witch%27s_House/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWitch%27s_House) |
| Witch's Potion | Quest | `quest_witchspotion` | [`quest_hetty`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_hetty/) | [Article](https://oldschool.runescape.wiki/w/Witch%27s_Potion) · [Guide](https://oldschool.runescape.wiki/w/Witch%27s_Potion/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AWitch%27s_Potion) |
| X Marks the Spot | Quest | `quest_xmarksthespot` | [`quest_xmarksthespot`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_xmarksthespot/) | [Article](https://oldschool.runescape.wiki/w/X_Marks_the_Spot) · [Guide](https://oldschool.runescape.wiki/w/X_Marks_the_Spot/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AX_Marks_the_Spot) |
| Zogre Flesh Eaters | Quest | `quest_zogreflesheaters` | [`quest_zogreflesheaters`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_zogreflesheaters/) | [Article](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters) · [Guide](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript%3AZogre_Flesh_Eaters) |

### 8.2 Partial implementation roots (14)

| Quest | Dbrow | Implementation | OSRS Wiki |
|---|---|---|---|
| Big Chompy Bird Hunting | `quest_bigchompybirdhunting` | [`quest_chompybird`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_chompybird/) | [Article](https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting) · [Guide](https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Big_Chompy_Bird_Hunting) |
| Elemental Workshop I | `quest_elementalworkshop1` | [`quest_elemental_workshop`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_elemental_workshop/) | [Article](https://oldschool.runescape.wiki/w/Elemental_Workshop_I) · [Guide](https://oldschool.runescape.wiki/w/Elemental_Workshop_I/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Elemental_Workshop_I) |
| Family Crest | `quest_familycrest` | [`quest_crest`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_crest/) | [Article](https://oldschool.runescape.wiki/w/Family_Crest) · [Guide](https://oldschool.runescape.wiki/w/Family_Crest/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Family_Crest) |
| Fight Arena | `quest_fightarena` | [`quest_arena`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_arena/) | [Article](https://oldschool.runescape.wiki/w/Fight_Arena) · [Guide](https://oldschool.runescape.wiki/w/Fight_Arena/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Fight_Arena) |
| Horror from the Deep | `quest_horrorfromthedeep` | [`quest_horror`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_horror/) | [Article](https://oldschool.runescape.wiki/w/Horror_from_the_Deep) · [Guide](https://oldschool.runescape.wiki/w/Horror_from_the_Deep/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Horror_from_the_Deep) |
| In Search of the Myreque | `quest_insearchofthemyreque` | [`quest_routequest`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_routequest/) | [Article](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque) · [Guide](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:In_Search_of_the_Myreque) |
| Lost City | `quest_lostcity` | [`quest_zanaris`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_zanaris/) | [Article](https://oldschool.runescape.wiki/w/Lost_City) · [Guide](https://oldschool.runescape.wiki/w/Lost_City/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Lost_City) |
| Scorpion Catcher | `quest_scorpioncatcher` | [`quest_scorpcatcher`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_scorpcatcher/) | [Article](https://oldschool.runescape.wiki/w/Scorpion_Catcher) · [Guide](https://oldschool.runescape.wiki/w/Scorpion_Catcher/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Scorpion_Catcher) |
| Shades of Mort'ton | `quest_shadesofmortton` | [`quest_mortton`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_mortton/) | [Article](https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton) · [Guide](https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Shades_of_Mort%27ton) |
| Shilo Village | `quest_shilovillage` | [`quest_zombiequeen`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_zombiequeen/) | [Article](https://oldschool.runescape.wiki/w/Shilo_Village) · [Guide](https://oldschool.runescape.wiki/w/Shilo_Village/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Shilo_Village) |
| Temple of Ikov | `quest_templeofikov` | [`quest_ikov`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_ikov/) | [Article](https://oldschool.runescape.wiki/w/Temple_of_Ikov) · [Guide](https://oldschool.runescape.wiki/w/Temple_of_Ikov/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Temple_of_Ikov) |
| The Tourist Trap | `quest_touristtrap` | [`quest_desertrescue`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_desertrescue/) | [Article](https://oldschool.runescape.wiki/w/The_Tourist_Trap) · [Guide](https://oldschool.runescape.wiki/w/The_Tourist_Trap/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:The_Tourist_Trap) |
| Tribal Totem | `quest_tribaltotem` | [`quest_totem`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_totem/) | [Article](https://oldschool.runescape.wiki/w/Tribal_Totem) · [Guide](https://oldschool.runescape.wiki/w/Tribal_Totem/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Tribal_Totem) |
| Troll Stronghold | `quest_trollstronghold` | [`quest_troll`](../OSRS-Content/osrs239-content/server/scripts/quests/quest_troll/) | [Article](https://oldschool.runescape.wiki/w/Troll_Stronghold) · [Guide](https://oldschool.runescape.wiki/w/Troll_Stronghold/Quick_guide) · [Transcript](https://oldschool.runescape.wiki/w/Transcript:Troll_Stronghold) |
