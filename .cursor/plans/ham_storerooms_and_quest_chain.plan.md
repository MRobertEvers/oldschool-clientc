---
name: H.A.M. quest chain and storerooms
overview: Finish and verify the H.A.M.-related quest chain, then implement the post-Death-to-the-Dorgeshuun H.A.M. Storerooms thieving activity with OSRS-accurate guards, keys, doors, chests, rewards, and tests.
todos:
  - id: quest-audit
    content: Fix quest requirements and blockers, including ordinary H.A.M. pickpocketing, and prove all three H.A.M.-related quests are completable end to end
    status: completed
  - id: storeroom-state
    content: Add the post-quest storeroom module and correct quest/post-quest guard and access state across login, reset, and completion
    status: completed
  - id: guard-pickpocket
    content: Implement storeroom guard pickpocketing, key rolls, ordinary loot, failure behavior, thieving modifiers, and Rogue outfit doubling
    status: completed
  - id: doors-and-detection
    content: Implement the four pick-lock doors, lockpick modifiers, XP, guard sight/detection, and cracked-wall routes
    status: completed
  - id: keyed-chests
    content: Implement all six keyed chests with atomic key consumption, exact 150-roll rewards, animations, sound, and reset behavior
    status: completed
  - id: verify
    content: Add deterministic content self-tests, compile all scripts, run mock-server tests, and complete clean-save quest/minigame playthroughs
    status: in_progress
isProject: false
---

# H.A.M. quests and Storerooms implementation plan

## Goal and unlock rule

Implement the H.A.M. Storerooms as the repeatable thieving activity described by the OSRS Wiki, while first making the quests that lead to it genuinely playable.

The canonical unlock is **completion of Death to the Dorgeshuun**, not completion of Another Slice of H.A.M. The latter remains a downstream quest that must also be finished and verified, but it must not be added as a Storerooms access requirement.

Authoritative references:

- [H.A.M. Storerooms](https://oldschool.runescape.wiki/w/H.A.M._Storerooms)
- [The Lost Tribe](https://oldschool.runescape.wiki/w/The_Lost_Tribe)
- [Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun)
- [Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.)
- [H.A.M. Member](https://oldschool.runescape.wiki/w/H.A.M._Member)
- [Storeroom guard](https://oldschool.runescape.wiki/w/Guard_(H.A.M._Storerooms))
- [Storeroom door](https://oldschool.runescape.wiki/w/Door_(H.A.M._Storerooms))
- [Small chest](https://oldschool.runescape.wiki/w/Small_chest)

## Current-state audit

The repository already contains substantial scripts for all three quests, but it does not yet have a usable post-quest Storerooms activity. The existing quest scripts must be treated as a starting point, not evidence of completion.

| Area | Existing implementation | Required correction |
|---|---|---|
| The Lost Tribe | Full state machine under `server/scripts/quests/quest_losttribe` | Change Mining from 13 to 17; add the missing 13 Agility requirement; retain 13 Thieving; use current levels for boostable checks; verify Rune Mysteries and Goblin Diplomacy gates and every progression path |
| Ordinary H.A.M. members | NPCs exist, but no H.A.M.-specific pickpocket handler was found | Implement level-15 pickpocketing, 22.2 XP, 1–3 failure damage, concussion/ejection or jail behavior, normal loot, and the Death-to-the-Dorgeshuun robe-rate override. This is a quest blocker because two full robe sets are required |
| Death to the Dorgeshuun | Full state machine and reward exist | Use current level checks for boostable 23 Agility/Thieving requirements; remove quest-guard/post-quest-guard overlap; verify both robe hand-ins, all state transitions, logout recovery, combat, and reward/unlock |
| Another Slice of H.A.M. | Full state machine exists | Add hard prerequisites for completed Death to the Dorgeshuun, The Giant Dwarf, and The Dig Site; retain non-boostable base checks for 15 Attack and 25 Prayer; update stale comments and verify the complete quest rather than relying on narrated soft-skips |
| Storerooms | Map, five post-quest guards, four keys, door/chest locs, and ladder assets exist | Add interaction scripts, lifecycle gating, rewards, detection, and tests |

Also smoke-test the already-present prerequisite quests—Rune Mysteries, Goblin Diplomacy, The Giant Dwarf, and The Dig Site—so the chain can be completed from a clean player save.

## Phase 1 — finish the quest chain

### 1. Correct quest requirements

- In `quest_losttribe/configs/losttribe.constant` and its start/action gates:
  - require 17 Mining, 13 Agility, and 13 Thieving;
  - use `stat(...)` where the Wiki permits boosts;
  - keep completed Rune Mysteries and Goblin Diplomacy as hard prerequisites.
- In `quest_deathtothedorgeshuun/scripts/dttd_shared.rs2`, change the 23 Agility and 23 Thieving checks from base levels to current levels, while retaining The Lost Tribe completion gate.
- In `quest_anothersliceofham/scripts/slice_urtag.rs2`, prevent start unless:
  - `%dttd_main >= ^dttd_complete`;
  - `%giantdwarf_quest >= ^gdwarf_complete`;
  - `~itexam_progress() >= ^itexam_complete`;
  - base Attack is at least 15 and base Prayer is at least 25.
- Update Another Slice's constants/comments now that The Giant Dwarf and The Dig Site exist in the repository.

### 2. Implement ordinary H.A.M. member pickpocketing

Add custom `opnpc1` bindings for the male and female pickpocketable H.A.M. members rather than changing the global `opnpc3` pickpocket convention.

- Level 15 Thieving, 22.2 XP per success, shared success curve for both sexes.
- Use the shared thieving helpers for stun, dodgy necklace, Gloves of Silence, inventory checks, and XP where their behavior matches.
- Reproduce the H.A.M. loot table, including robe pieces and easy clues. During the robe-gathering stage of Death to the Dorgeshuun, apply the Wiki's special robe path: after the clue roll, a 1/5 special-table roll awards one of the seven pieces; otherwise use the normal 1/100-per-piece path.
- Track consecutive full concussions separately from ordinary stun. Agility and each equipped H.A.M. piece reduce the chance that a failure counts toward the three-concussion removal threshold; neither changes pickpocket success.
- On the third counted concussion, send the player either outside the hideout or to Jimmy the Chisel's cell. Reset the counter on leaving/re-entering, quest reset, and appropriate login recovery.

### 3. Close quest fidelity gaps

For each of the three quests, walk every state transition and classify existing shortcuts:

- Replace any narrated/automatic shortcut that skips a required puzzle, combat condition, item exchange, follower state, or failure/retry path.
- Cosmetic-only gaps such as unsupported camera choreography may remain only if the underlying interaction and state transition match OSRS and the exception is documented next to the script.
- Make debug commands set up internally consistent inventory, NPC, follower, and var state; they must not be the only way to advance.
- Verify quest journal states, completion counters, XP/reward grants, replay prevention, death handling, logout/login resumption, and reset cleanup.

Phase 1 is complete only when a new player can finish all three quests through normal interactions.

## Phase 2 — create the Storerooms content module and state model

Create `server/scripts/minigames/minigame_ham_storerooms/` with `configs/` and `scripts/` for the post-quest activity. Keep quest dialogue/cutscene logic in `quest_deathtothedorgeshuun`; the activity may read `%dttd_main` but must not own quest progression.

- Gate post-quest guard interactions, doors, and chests on `%dttd_main >= ^dttd_complete`.
- Fix `dttd_ham_guards_login` so temporary quest guards exist only during the infiltration stage and never coexist with the five cached `_postquest` guards after completion.
- Make login, logout, quest reset, completion, and server reload converge on the same NPC state without duplicate spawns.
- Keep the existing trapdoor unlock and ladder routing, but verify both lead to the correct map level after completion.
- Confirm the two cracked-wall shortcuts on the north and south sides of the south-west room and all room collision from each side; add explicit squeeze-through scripts where generic loc routing is insufficient.
- Add a single helper for “wearing all seven H.A.M. pieces” for use by guard dialogue. Talking without the full outfit makes the guard attack; with it, use the non-hostile dialogue path except for the deliberately provocative response.

## Phase 3 — storeroom guard pickpocketing

Bind `opnpc1` for all five `dttd_ham_guard_*_postquest` NPCs.

- Require 20 Thieving and grant 22.2 XP on success.
- Roll keys independently at 1/10 each: bronze, iron, silver, and steel. A success can therefore yield more than one key.
- Roll the guard's ordinary H.A.M.-style loot using the Wiki's published tables after the key rolls, including its separately weighted herb entries.
- Full Rogue equipment doubles keys and ordinary loot according to the common thieving reward rules, but never doubles XP or clue boxes.
- Reuse dodgy-necklace and Gloves-of-Silence behavior from the shared framework.
- On failure, stun and deal a random 1–3 damage, but never eject or jail the player.
- A failed attempt stops that guard's patrol and sends it back to its starting tile. Preserve correct multiplayer ownership: do not store world-NPC movement state in a player var.
- Preserve attack/talk options and combat behavior after adding pickpocket.

Because the Wiki has no published guard success chart, use the cache-era/shared level-20 pickpocket curve already used by this content revision, isolate it in named constants, and cover it with a statistical test rather than inventing a Wiki rate.

## Phase 4 — doors, lockpicks, and guard detection

Implement `oploc4` Pick-lock for the four locked door placements at `(2567,5198)`, `(2576,5198)`, `(2567,5192)`, and `(2576,5192)`.

- Require 23 Thieving.
- Award 3.8 XP on success and 0.5 XP on failure.
- Use OSRS low/high success parameters 10/254 normally and 60/300 while a lockpick is present. Do not consume the lockpick.
- Open the door through the standard paired-door helper, with collision and automatic close behavior intact.
- On each attempt, determine whether a guard has unobstructed line of sight **and is facing the player**. If seen, interrupt the attempt and make that guard attack.
- If Serverscript cannot query an NPC's facing/front arc, add one minimal host opcode/proc and corresponding compiler/VM/mock-server coverage. Do not silently substitute radius-only detection.
- Once a guard has stopped patrolling because of a failed pickpocket, it must no longer police door attempts, matching the activity's intended safe setup loop.

## Phase 5 — keyed chests and exact rewards

Implement the six `Small chest` placements by coordinate so duplicate loc types cannot receive the wrong key:

| Chest location | Required key |
|---|---|
| North-west room `(2570,5192)` | Silver |
| North-east room `(2573,5193)` | Iron |
| South-west room `(2569,5199)` | Bronze |
| South-east room `(2573,5198)` | Steel |
| North room `(2577,5209)` | Steel |
| North room `(2577,5208)` | Steel |

On successful open:

1. Select one outcome from the Wiki's exact 150-weight table.
2. Calculate required inventory space for the complete bundle, accounting for existing stacks.
3. If the entire bundle will not fit, show an inventory-space message and leave both chest and key untouched.
4. Otherwise remove exactly one matching key, add the entire bundle atomically, play `human_openchest` and `chest_open`, temporarily swap to `dttd_ham_chest_open`, then restore the closed chest.
5. State that the key snaps/breaks in the lock; there is no reusable-key path.

Encode the reward selection as a pure helper taking a roll in `0..149`, with data rows for the bundle contents. The 20 weights must sum to 150:

`75, 6, 2, 3, 5, 6, 4, 6, 2, 5, 2, 5, 4, 4, 6, 2, 3, 3, 3, 4`.

The corresponding bundles, in order, are:

1. 0–99 coins
2. sapphire necklace
3. diamond + sapphire amulet + sapphire necklace + sapphire ring
4. ruby necklace
5. gold ring
6. sapphire amulet + sapphire necklace + sapphire ring
7. emerald ring + sapphire amulet + sapphire necklace
8. sapphire amulet + sapphire necklace
9. diamond necklace
10. gold amulet
11. diamond ring + sapphire amulet + sapphire necklace
12. sapphire + sapphire amulet + sapphire necklace + sapphire ring
13. emerald necklace
14. emerald amulet + sapphire necklace
15. gold necklace
16. diamond amulet + sapphire necklace
17. ruby + sapphire amulet + sapphire necklace + sapphire ring
18. ruby ring + sapphire amulet + sapphire necklace
19. ruby amulet + sapphire necklace
20. emerald + sapphire amulet + sapphire necklace + sapphire ring

## Verification plan

### Deterministic automated coverage

- Add a content self-test/debug proc that supplies explicit pickpocket and chest rolls rather than depending on live RNG.
- Test every quest start gate at one below, exact, boosted, and completed values as appropriate.
- Test all 20 chest outcomes and all roll boundaries from 0 through 149; assert total weight 150.
- Test each of the six chest coordinates, correct and incorrect keys, missing key, single-slot/full inventory, existing coin stack, four-item bundle, and rollback on insufficient space.
- Test independent key rolls, multiple keys on one pickpocket, full Rogue doubling, clue non-doubling, XP, stun damage bounds, dodgy necklace, Gloves of Silence, and no storeroom ejection.
- Test door level gating, lockpick/non-lockpick curves, both XP amounts, guard front-arc plus line-of-sight detection, stopped-guard behavior, door collision, and auto-close.
- Test quest guard/post-quest guard exclusivity on fresh login, mid-quest login, completion, logout/login, reset, and reload.

### Build and runtime gates

- Compile the complete content set with `make -C src mock230-scripts` (or the repository's current equivalent if the target has moved).
- Run `make -C src test-mock230-dev` and any quest/content self-test target added by the implementation.
- Start from a clean save and complete, without debug advancement:
  1. prerequisite quests;
  2. The Lost Tribe;
  3. Death to the Dorgeshuun, including obtaining both H.A.M. sets through pickpocketing;
  4. one full Storerooms loop using every key/room and at least one cracked-wall route;
  5. Another Slice of H.A.M.
- Repeat the Storerooms loop after relogging, dying, and filling the inventory to verify persistence and rollback.

## Definition of done

- All three H.A.M.-related quests are startable only with the correct prerequisites and can be completed normally from a clean save.
- Storerooms access activates immediately upon Death to the Dorgeshuun completion and does not depend on Another Slice of H.A.M.
- Five guards, four locked rooms, four key types, six chests, alternate wall routes, rewards, dialogue, detection, failure, and thieving modifiers match the cited Wiki behavior.
- No debug-only progression, duplicate guard spawns, partial reward grants, consumed keys on failed transactions, or known quest soft-locks remain.
- Script compilation, mock-server tests, deterministic content tests, and the clean-save playthrough all pass.
