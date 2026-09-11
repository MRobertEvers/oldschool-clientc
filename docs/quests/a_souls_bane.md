# A Soul's Bane modernization audit

Status: `audit-pending` — the native journal and shared completion lifecycle
exist, but the normal quest stalls in the Rage room and later room state,
coordinates, combat, instance, and post-quest contracts are incomplete.

Audited: 2026-08-16

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies that plan's Gates A–D to the quest gauntlet, its five encounter
rooms, and the permanent Tolna's rift unlock. It is an implementation
specification, not completion evidence.

## 1. Authoritative references

These revisions are pinned so later implementation and review use the same
requirements, route, dialogue, encounter, reset, reward, and post-quest rules.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [A Soul's Bane](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane?oldid=15292369) | 15292369, 2026-08-10 | Requirements, instancing, all rooms, combat mechanics, rewards, and diary dependency |
| [A Soul's Bane/Quick guide](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane/Quick_guide?oldid=15123298) | 15123298, 2026-02-07 | Ordered actions, checkpoint/relog behavior, room counts, final fight, and finish |
| [Transcript:A Soul's Bane](https://oldschool.runescape.wiki/w/Transcript:A_Soul%27s_Bane?oldid=15263337) | 15263337, 2026-07-14 | Acceptance/refusal, room voices, re-talks, cutscenes, kill feedback, Brana, reunion, and completion dialogue |
| [Tolna's rift](https://oldschool.runescape.wiki/w/Tolna%27s_rift?oldid=15278818) | 15278818, 2026-07-28 | Post-quest lobby, room layout, permanent monster variants, drops, levels, Hitpoints, and music |
| [Rift](https://oldschool.runescape.wiki/w/Rift_%28Tolna%27s_rift%29?oldid=15014225) | 15014225, 2025-11-03 | Exact rope-point and wrong-rift entry messages |
| [Confusion beast](https://oldschool.runescape.wiki/w/Confusion_beast?oldid=15199269) | 15199269, 2026-04-28 | Real/illusion, ranged, poison, combat-XP, and post-quest differences |
| [Hopeless creature](https://oldschool.runescape.wiki/w/Hopeless_creature?oldid=15199413) | 15199413, 2026-04-28 | Three forms, eating heal, poison reset, ranged fallback, and permanent variant |
| [Warning sign](https://oldschool.runescape.wiki/w/Warning_sign_%28Tolna%27s_rift%29?oldid=14877431) | 14877431, 2025-04-05 | Five post-quest world transforms and warning purpose |
| [Transcript:Warning sign](https://oldschool.runescape.wiki/w/Transcript%3AWarning_sign_%28Tolna%27s_rift%29?oldid=14377195) | 14377195, 2023-02-15 | Exact post-quest Read text |
| [Transcript:Tolna](https://oldschool.runescape.wiki/w/Transcript%3ATolna?oldid=15101640) | 15101640, 2026-01-08 | Post-quest training-area conversation |
| [Varrock Diary](https://oldschool.runescape.wiki/w/Varrock_Diary?oldid=15293707) | 15293707, 2026-08-12 | Medium task for entering the Tolna dungeon after completion |

Transition aid only: Quest Helper's
[`ASoulsBane.java`](https://github.com/Zoinkwiz/quest-helper/blob/241eaec29b19243bda7e88e99d5c16568c0776a6/src/main/java/com/questhelper/helpers/quests/asoulsbane/ASoulsBane.java)
at commit `241eaec29b19243bda7e88e99d5c16568c0776a6` (2025-08-27) confirms the
native 0–12 state bands and current quest-room world zones. It does not override
the Wiki. No Quest Helper checkout or extracted fixture is present locally.

The source comments also name 2009scape dialogue/plugin material, but no pinned
2009scape source or fixture is present in this workspace. Those comments are
provenance hints, not authoritative requirements.

## 2. Native quest identity and player contract

The native `quest_soulsbane` dbrow and pinned Wiki define this contract:

| Field | Native value / expected behavior |
| --- | --- |
| Quest ID | 108 |
| Type | Members' quest |
| Difficulty / length | Intermediate / short |
| Release date | 3 April 2006 |
| Start | Talk to Launa east of Varrock, south of the Earth Altar |
| End state | `%soulbane_prog = 13` |
| Quest points | 1 |
| Prerequisites / required levels | None; the player must be able to defeat level-45+ enemies |
| Required items | One rope plus ordinary combat equipment/weapon for the rooms after Rage |
| Recommended support | 30 Combat, food, armour, antipoison, and ranged or magic capability |
| Mandatory Rage work | Kill roughly 7–8 angry creatures with the four matching anger weapons; this grants 40 Attack XP and no XP from the creatures themselves |
| Mandatory Fear work | Find and kill five level-42 fear reapers |
| Mandatory Confusion work | Repeatedly find the one real level-43 beast among four illusions until only one of six doors remains |
| Mandatory Hopelessness work | Fully defeat five level-40 hopeless creatures through three forms each |
| Mandatory final fight | Defeat all three level-46 parts of Tolna at once |
| Direct rewards | 1 quest point, 500 Defence XP, 500 Hitpoints XP, and 500 coins |
| Unlock | Access to the permanent Tolna's rift combat-training dungeon |
| Downstream effect | Entering the post-quest dungeon completes a Medium Varrock Diary task |

`stat_advance` receives tenths of an XP. The local constants therefore encode
40 Attack XP as `400`, and both 500-XP completion rewards as `5000`; those
quantities are correct.

The quest is an instance. Logging out or leaving ejects the player, preserves
completed-room checkpoints, and restarts only the incomplete room. Death places
the player's grave outside the rift; items deliberately left on the instance
floor are lost.

## 3. Implementation surface

Paths below are relative to `OSRS-Content/osrs239-content/`.

### Quest-owned files

| Path | Present responsibility | Audit result |
| --- | --- | --- |
| `server/scripts/quests/quest_soulsbane/configs/soulsbane.constant` | Primary milestones, room counts, rewards, and absolute destinations | Direct rewards are correct; milestones collapse native states 7–11 and three later destinations disagree with current cache-era zones |
| `server/scripts/quests/quest_soulsbane/configs/soulsbane.varp` | Redeclares four native state carriers | All four are permanent, including attempt scratch state that must reset on logout/leave/death |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane.rs2` | Launa, rope/rift, journal, shared debug setup, and room debug entry | Start/rope foundation is usable; initial cutscene, instance ownership, checkpoint routing, post-quest branch, and normal Rage spawns are absent |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane_anger.rs2` | Rack, weapon matching, Rage damage/counter, 40 Attack XP, and east exit | Active legacy overlay, no normal spawn/respawn, pre-queue lethal check cannot record kills, and all cutscenes are skipped |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane_fear.rs2` | Dark holes, reaper spawn/count, lit exit | Random coin-flip approximation, shared NPC ownership, persistent partial attempt, and skipped room/cutscene dialogue |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane_confu.rs2` | Real/fake beasts, hit counters, disappearing doors, and final door | Shared static spawns, melee-only hooks, pre-queue lethal check, fixed layout, missing ranged/poison mechanics, wrong destination, and skipped cutscene |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane_hope.rs2` | Five three-form creatures, kill count, bridge bit, and exit | Basic form chaining exists; eating heal, ranged fallback, owned spawns, bridge presentation, correct state band, destination, and cutscene do not |
| `server/scripts/quests/quest_soulsbane/scripts/soulsbane_tolna.rs2` | Three head bits, human Tolna, surface transfer, and completion | Brana/final cutscene and head mechanics are absent; NPCs are globally added, state 11 is skipped, surface visibility leaks, and full inventory can lose coins |

The header in `soulsbane.rs2` still says the Fear, Confusion, Hope, and Tolna
rooms are deferred even though separate scaffold files now exist. The live code
and this audit supersede that stale summary; the scaffolds remain incomplete.

### Mandatory shared and cross-directory files

| Path | Relationship | Audit result / required ownership |
| --- | --- | --- |
| `server/scripts/skill_combat/combat_stats.rs2` | Hard-codes calls into Rage and Confusion from `player_melee_swing` | Quest policy is embedded in one generic attack-style path; ranged/magic bypass both mechanics, and callbacks run before queued damage lands |
| `server/scripts/ladders_stairs/configs/ladders.loc` | Categorizes `soulbane_rope_up` and `soulbane_rope_down` | Quest overrides climb-up; verify generic post-quest/lobby climb-down rather than relying on coordinate inference |
| `server/scripts/interface_questjournal/scripts/quest_journal.rs2` | Dispatches the native dbrow | Correct modern dispatch; quest text follows the collapsed state model and lacks side-state detail |
| `server/scripts/quests/scripts/quest_cheat.rs2` | Idempotently sets `%soulbane_prog` to 13 | Does not establish Tolna/Launa/sign/rope post-quest transforms or prove the unlocked dungeon |
| `server/scripts/areas/world/configs/m51_53.spawn` | Spawns surface `soulbane_launa_multi` and `soulbane_tolna_multi` | Correct cache-native multi-NPC bases; quest code adds a second raw Tolna when its exact-type lookup misses the multi |
| `server/scripts/interface_music/scripts/music.rs2` | Reads unlock bits and plays unlocked music | No shared setter or quest integration was found for the four Tolna's rift tracks |
| `server/scripts/interface_diaries/` | Owns diary areas/tier counts | The generic Varrock counters exist, but no per-task Tolna-dungeon entry hook/state was found |
| `server/scripts/player/scripts/gravestone.rs2` | Shared grave lifecycle | Quest has no owned-instance death adapter that moves the grave outside the rift |
| `server/scripts/skill_combat/configs/bas/attack_sounds.obj` | Anger weapon attack sounds | Symbolic support exists and should remain shared data |
| `server/scripts/npc/configs/npc_anims.generated.npc` | Generated animations for quest and training variants | Cache support exists; keep generated output generated and implement policy in owned content |

No quest-room NPC spawn files exist for the relevant map squares. The only
quest Rage monsters are the four `npc_add` calls in `::soulsbaneanger`; normal
rift entry creates none. The permanent stronger monster variants likewise have
no post-quest spawns or scripted creation.

### Cache-native content already available

The osrs239 cache exposes substantially more intended content than the scripts
use:

- `%soulbane_prog` (cache varbit 2011), the rope and all room counters/door/head
  bits, `soulbane_launa_pres`, `soulbane_anger_flamepres`,
  `soulbane_warning_pres`, `soulbane_hope_monmes`, and
  `soulbane_watchedcutscene`;
- young Launa, young Tolna, Brana, human Tolna, three Tolna heads, quest
  creature variants, and stronger permanent training variants;
- the full Rage/Fear/Confusion/Hopelessness room loc set, firewall, exits,
  six doors, bridge pieces, entrance rope, grass/sign transforms, and rack;
- all four anger weapons and the correct cache-owned weapon/rack visuals;
- `soulbane_hope_*`, `soulbane_conf_*`, and `soulbane_tolna_*` ranged
  spotanimations;
- `bane_attach_rope`, door, fire, fear-reaper, and Tolna synth effects;
- the cache-authored `soulbane_angerbar` interface; and
- music rows for `Wrath and Ruin`, `Fear and Loathing`, `Method of Madness`,
  and `No Way Out`, each with native unlock-bit metadata.

Modernization should connect these assets to owned current-engine systems
before authoring substitutes. The exact cache scene-copy/instance source maps
and every animation/jingle sequence still require a real-client trace.

## 4. Native state model and current reachability

Quest Helper groups the current primary state into the following native bands.
The exact before/after-cutscene meaning within each two-value band should be
confirmed with a live var trace, but the missing local bands are unambiguous.

| Value(s) | Expected phase | Current writer / defect |
| ---: | --- | --- |
| 0 | Not started | Default/reset; correct |
| 1 | Accepted; attach rope / enter first room | Launa acceptance; broadly correct |
| 2 | Rage room entry/attempt | First rift entry writes 2 without entry camera/voice/cutscene or monsters |
| 3–4 | Rage resolved and Fear room/checkpoint sequence | Local code writes 3 only after an unreachable Rage counter, then 4 after five reapers; pair semantics/cutscene checkpoints are collapsed |
| 5–6 | Confusion room/checkpoint sequence | Local code writes 5 when five doors vanish, then incorrectly reuses 6 as Hopelessness complete |
| 7–8 | Hopelessness room/checkpoint sequence | No local writer, constant, or journal branch |
| 9–10 | Brana confrontation and three-part Tolna fight | No local writer, constant, or journal branch |
| 11 | Tolna restored in the final room; speak to him | No local writer; the code jumps directly from 6 to 12 when all heads die |
| 12 | Tolna returned to the surface; claim completion | Local code first uses 12 for human Tolna still inside, so the journal points to the surface too early |
| 13 | Complete | Surface Tolna writes state before granting coins/XP/shared completion |

Normal progression stops at state 2 for two independent reasons:

1. normal entry does not create any angry creatures; and
2. even the debug-created creatures cannot increment Rage as written.

The melee swing queues damage for a later NPC phase, then immediately invokes
`~soulsbane_anger_on_damage`. That proc requires the NPC's current Hitpoints to
already be zero, which cannot be true on the lethal swing before the queued
damage lands. `~soulsbane_confu_on_real_damage` has the same ordering defect.
Neither room has an authoritative death callback that performs the missing
transition.

The per-room debug commands inject primary states and isolated entities. They
are useful positioning adapters but cannot prove the real state machine:

- `::soulsbaneanger` creates only one of each of four creatures, below the
  required 7–8 kills, and inherits the broken lethal callback;
- `::soulsbanefear` bypasses Rage and can exercise the approximate hole loop;
- `::soulsbaneconfu` bypasses Fear but inherits the broken real-death callback;
- `::soulsbanehope` injects state 5 and can exercise form chaining; and
- `::soulsbanetolna` injects state 6, skipping native states 7–10.

## 5. Room coordinates and instance ownership

The first two authored destinations agree with the cache-era Quest Helper
zones. The next three do not.

| Phase | Current authored destination | Current reference location | Audit result |
| --- | --- | --- | --- |
| Rage | `(3015, 5244, 0)` | Zone `(3010,5217)`–`(3038,5246)` | Inside expected Rage room |
| Fear | `(3051, 5240, 0)` | Zone `(3044,5218)`–`(3071,5247)` | Inside expected Fear room |
| Confusion | `(2970, 5208, 0)` | Beast step near `(3055, 5199, 0)` | Wrong map square; near the final-room x/y range on the wrong plane |
| Hopelessness | `(2928, 5208, 0)` | Creature step near `(3087, 5198, 0)` | Wrong map square |
| Tolna | `(2900, 5224, 0)` | Final room near `(2984, 5212, 1)` | Wrong map square and plane |
| Post-quest lobby | No branch or destination | Tolna's rift centered near `(3104, 5280, 0)` | Entire permanent lobby/training route absent |

The quest uses fixed public coordinates, broad `npc_findallany`/`npc_del`
searches, and unowned `npc_add`. One player's entry can therefore clear,
replace, attack, or advance entities belonging to another player. The surface
fallback additionally sets the per-player Tolna transform and then searches for
the concrete transformed NPC type; because the world spawn is the base multi,
the lookup can miss and add a globally visible second Tolna.

The Wiki requires a private instance. Required lifecycle behavior is:

```text
persistent completed-room checkpoint
             |
             v
new player-owned quest instance + fresh current-room attempt
             |
       +-----+------+----------------+
       |            |                |
   room clear   voluntary exit   logout/death
       |            |                |
save checkpoint   discard attempt   discard attempt
       |            |                +--> grave outside on death
       v            +--> surface
next owned room
```

Current room counters and entity-presence fields live in permanent varps and no
logout/leave/death cleanup exists. Partial Rage, Fear, Confusion, Hope, and head
progress can therefore persist when the Wiki says the incomplete room restarts.

## 6. Current versus required playable route

### Stage 1 — Launa, rope, and entry

Required behavior:

1. Launa explains Tolna and Brana, then offers the transcript's accept/refuse
   branch. Refusal remains at 0; acceptance reaches state 1.
2. Re-talk before entry reminds the player to search the rift rather than using
   a generic warning at every later milestone.
3. A rope is used specifically on the central rope point. Only that transformed
   rift may be entered; other fissures report that the player must climb at the
   rope.
4. Entry creates a private instance at the correct saved checkpoint, removes
   any stale current-room attempt, and runs the room camera/Tolna voice.
5. Cutscenes own input so an unrelated ground click does not eject the player;
   logout/disconnect has an explicit safe cleanup/resume outcome.

Current acceptance/refusal and single rope consumption are a reasonable base.
All twelve fissure locs share the same use-on and Enter handlers, however, so a
rope used on any segment globally enables entry from any segment. Entry always
teleports to the public Rage coordinates regardless of checkpoint or completion,
does not create an instance, and omits the entry camera, voice, audio, and NPCs.

### Stage 2 — Rage

Required behavior:

1. The Rage overlay mounts on room entry using the current named parent slot,
   updates from authoritative damage/kill progress, and closes on clear, exit,
   death, logout, and disconnect.
2. The rack offers sword, spear, mace, battleaxe, and Nothing. Only one anger
   weapon may be held; full inventory and worn weapon/shield cases do not lose
   items or desynchronise the rack transform.
3. Sword/unicorn, spear/bear, mace/rat, and battleaxe/goblin are enforced for
   melee, ranged, magic, specials, and multi-hit attacks. Correct hits deal ten
   times normal damage and grant no per-hit creature XP.
4. The room supplies/respawns enough owned angry creatures for 7–8 kills.
5. Maximum rage triggers the yell/spin/remaining-monster clear, exactly 40
   Attack XP, the complete Tolna-leaves-home cutscene, and the saved Fear
   checkpoint once.
6. Early exit asks for confirmation, discards current-room progress, removes
   all anger weapons, and returns to the surface.

Current rack selection uses modern `~p_choice4` but omits Nothing and clears the
old quest weapon before proving capacity for the new one. A player can leave
with anger weapons. Rage monsters exist only in a debug command; its four
one-shot additions are insufficient. Weapon policy is injected only into the
generic melee swing, so ranged/magic bypass it. The lethal counter cannot fire,
the overlay opens only from that unreachable counter, uses legacy
`if_openoverlay`, and is never explicitly closed. The completion cutscene is a
message saying it was soft-skipped.

### Stage 3 — Fear

Required behavior:

1. Entry runs the room camera, Tolna's fear lines, and player observation.
2. Exactly one eligible dark hole is the hidden reaper location for a round.
   The player cannot search the same hole repeatedly; after a kill a new hidden
   hole is chosen.
3. Owned level-42 melee reapers use correct animations/sounds. Each authoritative
   player kill advances one of the five transcript feedback lines.
4. The fifth kill runs the Tolna-falling memory, lights the west exit, records
   the Fear checkpoint, and cleans all attempt state/entities.
5. Leaving/logging/death before completion resets reaper count and hidden-hole
   state; re-entry after a completed Rage room starts a fresh Fear attempt.

Current code flips a coin on each new-hole click rather than choosing one
stable hidden hole for the round. It globally adds a reaper at the clicked loc,
uses generic combat, and increments permanent state without encounter ownership.
The per-kill transcript lines, camera/voice, memory, audio, and cleanup are
missing. The fifth kill sets state 4 and directly teleports through a soft exit.

### Stage 4 — Confusion

Required behavior:

1. Entry runs Tolna's confusion voice/camera and creates five owned beasts: one
   real and four indistinguishable illusions.
2. Illusions always take zero damage, still grant the correct combat experience,
   and vanish after eight zeroes regardless of melee/ranged/magic attack path.
3. The real beast uses its ranged attack; melee-range attacks can poison. A real
   death removes exactly one of five false doors, respawns a fresh wave, and
   leaves one final door after five authoritative kills.
4. Spawn placement/order does not reveal the real beast trivially, while the
   current client identity behavior remains consistent across waves.
5. The final wave writes the correct state in the 5–6 band, runs Tolna's
   self-argument cutscene, and opens the real final door to Hopelessness.

Current entry always places the concrete real type at the same central
coordinate and four fake types at fixed offsets. `combat_stats.rs2` calls the
illusion and real handlers only from melee; ranged and magic directly queue
normal damage. The real handler checks Hitpoints before the lethal queued damage
lands, so no melee death removes a door either. Illusion XP is a fixed tiny
placeholder, ranged/poison mechanics are absent, global cleanup can delete
another player's wave, state 6 is skipped, and the exit points to `(2970,5208)`
instead of the current Confusion room.

### Stage 5 — Hopelessness

Required behavior:

1. Entry runs Tolna's hopelessness lines and creates five owned creatures.
2. Each creature must be killed through all three forms. Eating heals active
   creatures substantially; phase changes clear poison and preserve the correct
   melee/ranged fallback behavior.
3. Each permanent kill emits the appropriate transcript feedback. The fifth
   runs the bridge effect, records the state-7/8 checkpoint, and makes a
   traversable Bridge of Hope.
4. Crossing the bridge reaches the final-room staging area at cache-correct
   coordinates. Leaving/logging/death early resets the whole current attempt.

The three-form death chain and five-kill counter are useful scaffolding. All
five NPCs are nevertheless globally spawned, eating has no interaction with
their Hitpoints, ranged fallback/relative strength is absent, and per-kill
dialogue is replaced by a counter message. The source explicitly marks bridge
multilocs as soft-skipped. It writes state 6 instead of the native 7–8 band and
uses `(2928,5208)` as the room destination.

### Stage 6 — Brana, three-headed Tolna, and reunion

Required behavior:

1. Entering the final room at `(2984,5212,1)` completes the Brana/Tolna
   confrontation before the heads become attackable. Brana remains available
   for the transcript re-talk.
2. All three owned heads attack in multicombat. They use ranged attacks at
   distance, the northern head's rapid melee up close, and poison as defined by
   the current encounter.
3. Death/logout/leave before all three die resets the current final-room attempt;
   individual head bits do not become permanent completion shortcuts.
4. Defeating all heads moves to state 11 and produces human Tolna. Talking to
   him runs the complete Tolna/Brana reconciliation and then moves to state 12
   on the surface.
5. Launa hides and surface Tolna appears through the native multi-NPC fields;
   no globally added fallback NPC leaks to other players.

Current entry prints two soft-skip messages, never creates Brana, and spawns
three generic-combat heads at `(2900,5224,0)`. Cache attack rates exist, but no
quest code uses the available ranged spotanimations or implements poison/range
selection. Each death persists a permanent bit. All three jump from state 6 to
12, spawn a global human Tolna, and reduce the reunion to three lines. The
surface transfer then likely adds a concrete global Tolna alongside the native
per-player multi because it searches for the transformed type rather than the
spawned base multi. `soulbane_launa_pres` is never written, so Launa remains.

### Stage 7 — completion and permanent unlock

Required behavior:

1. Surface Tolna delivers the full history/reward/training dialogue and grants
   500 coins, 500 Defence XP, 500 Hitpoints XP, and one quest point exactly once.
2. A full inventory without an existing coin stack remains claimable; state 13
   is not committed before all non-idempotent rewards are secured.
3. Five grass transforms become readable warning signs, Tolna remains, Launa
   is absent, and post-quest dialogue explains the training dungeon.
4. Re-entering routes to the permanent lobby near `(3104,5280)`, not the quest
   Rage room. All four rooms contain the stronger cache variants at their Wiki
   levels/Hitpoints and current drop behavior.
5. The four Tolna's rift music tracks unlock/play through shared music policy,
   and the first eligible post-quest entry completes the Medium Varrock Diary
   task idempotently.

Current direct XP and numeric reward quantities are correct, and shared
completion supplies the quest point lifecycle. The script sets state 13 before
`inv_add`; with 28 occupied slots and no coin stack, the 500 coins can fail and
never be reclaimed. Completion does not set Launa/warning transforms. Post-
quest entry still teleports to the empty quest Rage room; the cache's stronger
variants are unused, no lobby exists, warning signs have no Read trigger, four
music bits are never set, and the diary has no task hook.

## 7. Gap and oversight register

| Priority | Area | Current defect | Required correction |
| --- | --- | --- | --- |
| P0 | End-to-end reachability | Normal Rage entry spawns no monsters, so state 2 cannot advance. | Create a complete owned Rage population/respawn lifecycle during real entry and prove the normal Launa-to-Tolna route. |
| P0 | Rage/Confusion kill authority | Both callbacks test current NPC HP before their queued lethal damage lands, so Rage never counts and real Confusion deaths never remove doors. | Advance from authoritative post-damage/death events exactly once, with killer/instance eligibility and duplicate-event guards. |
| P0 | Attack-style parity | Rage and Confusion policy is hard-coded only into `player_melee_swing`; ranged/magic bypass weapon/illusion rules. | Move target policy behind a reusable attack-style-independent prepare/death hook; remove quest-specific calls from the melee funnel. |
| P0 | Later room destinations | Confusion, Hopelessness, and Tolna constants disagree with current Quest Helper zones; Tolna also uses the wrong plane. | Resolve cache source/instance maps and use owned local destinations/transitions, then verify every portal in-client. |
| P0 | Instance isolation | Fixed public maps plus unowned global add/find/delete let players interfere with every encounter and surface fallback. | Use a player-owned instance and entities with explicit spawn, visibility, killer, reconnect, and cleanup contracts. |
| P0 | Checkpoint/reset lifecycle | All attempt fields are permanent; leave/logout/death does not reset the incomplete room or eject reliably. | Separate durable completed-room checkpoints from current-attempt state; centralize voluntary exit, logout, disconnect, death, and retry cleanup. |
| P0 | Native state | States 7–11 are never written; state 6 and 12 represent the wrong phases. | Reconstruct exact 0–13 semantics from live tracing/cache/Helper and preserve every cutscene/human/surface checkpoint. |
| P0 | Completion coins | State 13 is committed before a possibly failing 500-coin add. | Keep a durable reward-claim state or guarantee delivery before completion; test full inventory with and without a coin stack. |
| P0 | Post-quest reward | “Access to Tolna's rift” routes to an empty quest room; the lobby and all permanent monsters are absent. | Implement the separate permanent lobby, room spawns, stronger variants, drops, exits, and safe repeat entry. |
| P1 | Legacy Rage UI | Active `if_openoverlay(soulbane_angerbar)` opens only after an unreachable kill and is never closed. | Mount the cache panel with a named modern subinterface, run its onload/client vars, update authoritatively, and own every close path. |
| P1 | Anger weapons | Rack lacks Nothing, clears before capacity proof, can desynchronise on full inventory, and early exit leaks weapons. | Implement atomic one-at-a-time rack exchange, wield/inventory handling, transcript menu, exit/death/logout cleanup, and no outside use. |
| P1 | Fear selection | Each new-hole click independently flips a coin instead of modeling one hidden hole per round. | Select one eligible owned hole per round, prevent immediate repeat, rotate after death, and implement exact kill feedback. |
| P1 | Confusion mechanics | Fixed real spawn, approximate fake XP, no ranged/poison AI, and global wave cleanup diverge from the encounter. | Implement owned randomized waves, style-independent zero/XP behavior, real beast ranged/poison attacks, and five-door lifecycle. |
| P1 | Hopeless mechanics | Eating heal and ranged fallback are absent; phase/kill presentation is reduced to messages. | Connect player food consumption to owned creatures, implement phase combat/poison rules, transcript feedback, and bridge effect/collision. |
| P1 | Final encounter | Brana and confrontation/reunion are absent; heads use default combat and persist partial deaths. | Implement Brana, gated cutscene, three concurrent head styles/poison, attempt reset, state 11, and complete reconciliation. |
| P1 | Cutscenes/audio | Every room voice/camera and all Tolna memories are absent or explicitly soft-skipped; cache synth/spotanim assets are unused. | Build input-owned restart-safe cutscenes with stable checkpoints and reconcile animations, cameras, effects, jingles, and music. |
| P1 | Surface transforms | `soulbane_launa_pres` and `soulbane_warning_pres` are unused; a concrete Tolna can be globally added beside the native multi. | Use only per-player native multi state, remove fallback entity creation, show five signs, and implement their Read transcript. |
| P1 | Dialogue | Launa gives one generic line for almost every room; Brana, full human-Tolna, completion, and post-quest dialogue are abridged/absent. | Implement all reachable pinned transcript branches and milestone re-talks with modern choices. |
| P1 | Rift operations | Any of twelve fissures can accept the rope and any can enter once the global bit is set. | Restrict attachment/entry to the cache rope point and implement exact not-started/no-rope/wrong-fissure feedback. |
| P1 | Grave/ground items | No instance death path relocates the grave outside or discards instance-floor items. | Integrate shared grave ownership with an outside destination and instance teardown semantics. |
| P1 | Journal | Text follows collapsed states, points to the surface while Tolna is still below, and omits room counters/items/heads/reward claim. | Render exact primary and side-state objectives, including current attempt versus saved checkpoint. |
| P1 | Music | Four native unlock rows exist, but no setter/quest route was found. | Add/use a reusable music-unlock service and unlock each track at its authoritative room/location. |
| P1 | Varrock Diary | Medium task exists in current OSRS but the generic diary scaffold has no Tolna entry integration. | Publish an idempotent post-quest lobby-entry event to the authoritative per-task diary state. |
| P1 | Cheat adapter | `::complete` sets only primary 13, leaving Launa/Tolna/sign/rope/post-quest world state incoherent. | Make the adapter establish all derived permanent transforms/unlocks while shared cheat policy accounts for points exactly once. |
| P1 | Test validity | Five room debug commands inject states and substitute isolated soft entities for the route. | Drive real loc/NPC/combat/instance callbacks in automated tests; retain direct state setters only for isolated adapter assertions. |
| P2 | Presentation | Exact camera paths, overhead timing, remaining-monster Rage flourish, bridge construction, attack sounds, and lighting are unaudited. | Reconcile cache assets after critical ownership/mechanics work; document only genuinely cosmetic deviations. |

## 8. Modern-engine assessment

Parts to retain:

- native `%soulbane_prog` and cache-native room/world side bits;
- symbolic NPC, loc, object, animation, interface, and dbrow names rather than
  raw IDs;
- modern `~p_choice2` quest acceptance and `~p_choice4` as a rack-menu base;
- cache-driven surface/rack/door transforms;
- dbrow journal dispatch and shared `~quest_complete_rewards`; and
- the exact local reward constants and matching-weapon table.

Parts that are old or structurally unsafe:

- active legacy `if_openoverlay` for the Rage meter;
- quest-specific Rage/Confusion calls embedded directly in the shared melee
  attack routine;
- fixed public-room teleports in place of current instance ownership;
- global radius deletion and fallback NPC creation;
- permanent storage for incomplete-room scratch state; and
- soft messages/direct teleports standing in for cutscenes, room transitions,
  bridge construction, and the post-quest dungeon.

The target architecture should be:

```text
native durable quest checkpoint + world transforms
                    |
                    v
          player-owned rift instance
                    |
        +-----------+-----------+
        |                       |
room controller            lifecycle controller
(entities/puzzle/UI)        (exit/logout/death/reconnect)
        |
        v
style-independent authoritative hit/death events
        |
        v
cutscene + durable next-room checkpoint
                    |
                    v
       atomic completion and separate permanent lobby
```

Do not add quest-specific C routing. If the repository genuinely lacks a
general attack-style-independent target hook, instance-owned NPC visibility, or
food-consumption observer after repository-wide proof, add the smallest reusable
engine/VM capability with general tests. All room, weapon, illusion, dialogue,
and reward policy remains RuneScript/config content.

## 9. Implementation sequence

### ASB-1 — formalize state, map, and lifecycle contracts

- Add the quest and all external files above to the generated manifest.
- Live-trace `%soulbane_prog` values 0–13 and every side bit across room entry,
  clear, cutscene, leave, logout, death, human Tolna, surface, and completion.
- Resolve quest instance source maps, all portal coordinates/planes, post-quest
  lobby map, loc transforms, and native interface/client scripts.
- Classify each field as durable checkpoint, derived world state, or disposable
  current-attempt state.

Acceptance: every value/entity/loc has one owner, every coordinate resolves to
the expected cache room, and a written reset table covers every terminal path.

### ASB-2 — establish owned instance and shared combat events

- Implement one player-owned quest instance controller with room-local entity,
  ground-item, cutscene, UI, logout, reconnect, death, and exit cleanup.
- Publish style-independent pre-hit/post-hit/death hooks that melee, ranged,
  magic, specials, poison, recoil, and multi-hit paths all respect.
- Remove direct Soul's Bane policy calls from `player_melee_swing` after the
  generic routing is proven.
- Route deaths to a grave outside and discard unclaimed instance-floor items.

Acceptance: two simultaneous players cannot see/delete/hit/advance one another's
content; every attack style reaches the same target policy exactly once.

### ASB-3 — implement start and Rage

- Correct the one rope point, exact fissure messages, checkpoint-aware entry,
  Rage population/respawn, and entry voice/camera.
- Modernize the Rage panel mount/update/close lifecycle.
- Implement atomic five-option rack exchange, exact weapon matching across all
  attack paths, no monster XP, maximum-rage flourish, 40 Attack XP, memory, and
  early-exit reset/confirmation.

Acceptance: states 0–4 are reached through real actions; 7–8 valid kills fill
the visible meter and every invalid weapon/style remains incapable of bypass.

### ASB-4 — implement Fear and Confusion

- Implement one hidden Fear hole per round, owned reapers, exact overhead lines,
  final memory, and lit exit.
- Implement owned Confusion waves, style-independent illusions/XP, real ranged
  and poison behavior, five door removals, final cutscene, and correct state 5–6
  transition.
- Exercise voluntary exit/logout/death from every partial counter/door case.

Acceptance: five real reaper kills and five real beast kills are authoritative;
no fake, other player, wrong style path, stale NPC, or repeated event advances.

### ASB-5 — implement Hopelessness and final Tolna room

- Implement correct Hopelessness/bridge zones, eating heal, three forms,
  ranged/poison transitions, feedback, bridge effect/collision, and states 7–8.
- Implement Brana confrontation/re-talks, correct plane/room, three concurrent
  head mechanics, poison, partial-attempt reset, state 9–11 progression, human
  Tolna reunion, and surface transfer to 12.

Acceptance: all fifteen creature forms and all three heads must be defeated in
their owned current attempt; every cutscene/checkpoint resumes or resets safely.

### ASB-6 — make completion atomic and world state exact

- Implement the complete surface dialogue and a durable full-inventory reward
  claim for 500 coins, both 500-XP awards, and one quest point.
- Set/hide Launa, show native Tolna, transform five signs, implement Read text,
  and remove every concrete fallback entity.
- Make `::complete` establish the same derived permanent world state without
  granting direct gameplay rewards twice.

Acceptance: state 13 and every world transform become visible together; zero-
slot/no-coin-stack completion loses nothing and repeated claims are no-ops.

### ASB-7 — implement the permanent training dungeon

- Route completed players to the real lobby and four freely selectable rooms.
- Populate the correct level-47/55/63/71 variants with Wiki Hitpoints, combat,
  respawn, drop/no-drop, leave, death, logout, and concurrency behavior.
- Implement full post-quest Tolna dialogue, all four music unlocks, and the
  Medium Varrock Diary entry task.

Acceptance: the advertised reward is playable repeatedly and independently of
the quest instance; diary/music unlock once; the angry rat alone drops bones as
specified by the pinned rift reference.

### ASB-8 — verify and remove scaffolding

- Replace the five direct-state debug room commands with real-trigger test
  orchestration or retire them after equivalent automated coverage exists.
- Remove every critical `soft`, stale `deferred`, fixed-coordinate shortcut,
  legacy overlay open, global entity fallback, and quest-specific melee hook.
- Compile, pack, run transition/instance/combat/reward/diary/music tests, and
  capture a real-client smoke through every room and the permanent lobby.

Acceptance: all Gates A–D pass and this status can change from `audit-pending`
to `verified-modern`.

## 10. Verification matrix

| Scenario | Required assertions |
| --- | --- |
| Start | Refusal remains 0; acceptance reaches 1; every pre-entry re-talk matches transcript; no requirement is invented |
| Rope/rift | One rope is consumed at the correct point; wrong fissures/no rope/not started give exact feedback; repeat use consumes nothing |
| Instance entry | Correct checkpoint room and entry cutscene load; simultaneous players receive disjoint maps/entities/ground items |
| Attempt reset | Early exit, logout, disconnect, and death restart only the incomplete room and preserve all completed-room checkpoints |
| Grave/items | Death creates the grave outside; protected/unprotected item policy is normal; deliberately dropped instance items disappear on teardown |
| Rage rack | All four weapons plus Nothing; zero-slot/worn/shield/repeated exchange cannot lose, duplicate, or leak weapons outside |
| Rage combat | Four exact mappings, x10 damage, no creature XP, melee/ranged/magic/special/multi-hit parity, enough respawns, 7–8-kill meter |
| Rage clear | Yell/spin clears remaining owned monsters, grants exactly 40 Attack XP once, closes UI, runs full memory, and reaches correct Fear checkpoint |
| Fear | Stable hidden hole per round, no same-hole spam, five owned reapers, four intermediate lines plus final line/memory, west exit lights once |
| Confusion illusions | Every attack style deals zero, grants exact intended XP, and removes an illusion after eight zeroes without affecting a door |
| Confusion real | Ranged/poison mechanics work; exactly five authoritative real deaths remove five doors/waves and expose only the valid final door |
| Hopelessness | Five owned creatures, three forms each, eating heal, poison reset, melee/ranged behavior, kill feedback, bridge construction/collision |
| Final room | Brana cutscene finishes before attack; three heads attack concurrently with correct styles/rates/poison; partial deaths reset on attempt loss |
| Human Tolna | All heads lead to state 11, complete Brana/Tolna dialogue, then state 12 on surface; no global NPC appears for other players |
| Completion capacity | 0–28 used slots, with/without coin stack, never lose 500 coins; 500 Defence XP, 500 Hitpoints XP, and one quest point award once |
| World transforms | Launa hides, Tolna appears, five signs replace grass and Read correctly; relog and repeated interaction remain coherent |
| Post-quest lobby | Completed entry reaches lobby, not quest Rage; all four stronger populations/levels/HP/combat/drops/exits work concurrently |
| Music | Wrath and Ruin, Fear and Loathing, Method of Madness, and No Way Out unlock at their intended locations exactly once |
| Varrock Diary | Pre-quest entry cannot count; first post-quest dungeon entry completes the exact Medium task once; later entries do not duplicate totals |
| Journal | Every value 0–13 and partial room/reward state reports the correct saved checkpoint and current objective |
| Cheat adapter | First `::complete quest_soulsbane` establishes 13, points, and derived world/unlock state; second invocation is a no-op |

Minimum repository checks after implementation:

```sh
tools/questhelper_extract.py asoulsbane --check
make -C src torirsserver-scripts
ToriRSServer_Pack --check-only
```

The Quest Helper command is conditional on adding a pinned local fixture/source.
Also record automated instance/transition/combat/UI/reward/music/diary suites and
real-client packet/screenshot captures; compilation alone cannot prove a
private five-room gauntlet or permanent training dungeon.

## 11. Definition of done

A Soul's Bane may be marked `verified-modern` only when:

- the real Launa start reaches state 13 without a direct state setter or soft
  transition, using exact native values 0–13;
- rope entry, all four emotional rooms, Brana, every Tolna head, human/surface
  Tolna, every cutscene/re-talk, and all leave/logout/death/retry paths match the
  pinned Wiki and transcript;
- the quest is player-owned and instance-safe, with correct outside graves,
  ground-item cleanup, room checkpoints, and current-attempt resets;
- Rage/Confusion mechanics are style-independent and no quest policy remains
  hard-coded in the generic melee routine;
- the Rage meter is a correctly mounted/closed modern panel, and no legacy
  `if_openoverlay` remains;
- direct and intermediate XP, coins, quest points, world transforms, music,
  signs, post-quest dialogue, and Varrock Diary integration are exact,
  capacity-safe, durable, and idempotent;
- the permanent lobby and all four stronger training populations are playable,
  concurrent, and distinct from the quest instance;
- no active critical soft-skip, collapsed native state, guessed coordinate,
  global entity fallback, stale deferred marker, raw-ID workaround, or quest-
  specific engine shortcut remains; and
- script compilation, cache packing, automated state/instance/combat/UI/reward/
  music/diary coverage, real-client smoke evidence, and idempotent cheat
  evidence are recorded in this file.
