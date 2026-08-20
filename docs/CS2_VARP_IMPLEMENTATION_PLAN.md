# CS2 varp investigation and implementation plan

## Execution status (2026-08-09)

The plan has been executed for the full 105-row baseline inventory. The checked
audit now reports **0 current transmission gaps** across the 175 varps shared by
CS2 and executable server scripts.

| Wave | Rows | Result |
|---|---:|---|
| Existing quest/progression declarations | 34 | Implemented |
| Activity, reward, and completion counters | 12 | Implemented |
| Undeclared quest/progression state | 25 | Implemented |
| Combat and player status | 5 | Implemented |
| Make-X interface state | 1 | Implemented |
| Music playback mode | 1 | Implemented |
| Music unlock words | 27 | Storage/transmission implemented; unlock production blocked |

The 27 music unlock words have permanent server storage and login/change
transmission, but remain `blocked` in the review ledger because this repository
has no region-to-music table or other authoritative exploration-unlock
producer. That missing producer was already acknowledged in
`torirs_server_world.c`; inventing unlocks or setting every bit would be incorrect.

Implementation also corrected two contracts discovered during review:

- Mage Arena god-spell completion now uses the client-observed `1000` sentinel
  instead of the old `99` value.
- A manually selected music track now sets playback mode `2` (Single), not
  mode `0` (Area).

The canonical per-varp result and evidence is
`OSRS-Content/osrs239-content/port/cs2_varps.map`.

### Verification result

Passed:

- `python3 tools/cs2_varp_audit.py --check` (105 ledger rows, 0 gaps)
- server-script compilation (12,603 scripts)
- server-only content pack (3,137 records, 0 unresolved names)
- CS2 transmit-pump tests
- mock varp protocol tests
- content register, server codec, and live symbol-table tests
- in-process login save/load assertions
- root and content `git diff --check`

Baseline/environment failures kept outside this change:

- `test-port` stops in the pre-existing name parity ledgers (1,775 name-diff
  problems; the independently run var ledger has 417 problems).
- `test-ss-corpus` cannot read the optional sibling
  `../LostCity_Server/engine/data/pack/server/script.dat`.
- `test-torirsserver-embed` reaches and passes its login save/load assertions, then
  fails existing sound/entity/social wire assertions unrelated to varps.

## Goal

Understand and correctly implement every entry in `docs/CS2_UNIMPLEMENTED_VARPS.md` without turning the current audit into a blanket `transmit=yes` change.

The baseline queue contained 105 varps referenced by both client CS2 and executable server scripts but not made client-visible:

- 71 have no server `.varp` declaration.
- 34 have a declaration but do not enable `transmit=yes`.
- The 105 references occur across 47 CS2 scripts.

The work is complete only when every row has an evidence-backed disposition, an implementation where one is required, and an end-to-end test—or is removed from the implementation queue with proof that it is client-local or otherwise should not be transmitted.

## Non-negotiable rules

1. Do not add `transmit=yes` solely because a CS2 script mentions a varp.
2. Treat `configs/all.varbit` as the authority for shared carrier varps. Never introduce a whole-varp write that can clobber packed varbits.
3. Do not assume equal names or equal IDs mean equal semantics across revisions. Confirm the value from both the CS2 consumer and the server producer.
4. Route server mutations through the normal varp setter and transmission path. Do not add direct `player->varps[id]` writes.
5. Decide initialization, update timing, and persistence together. A varp that updates correctly but is missing on login is still incomplete.
6. Keep generated facts separate from human conclusions. The scanner may find references; a reviewed ledger records what they mean.

## Phase 0: Make the audit reproducible

Before behavior changes, turn the one-off scan into a checked tool and a human review ledger.

### Automated inventory

Add a tool that records, for every CS2 varp reference:

- varp ID and cache symbol;
- CS2 scripts and source lines;
- whether each occurrence reads, writes, or subscribes to the varp;
- calling scripts and interface hook/component entry points where recoverable;
- server `.rs2` references and mutation sites;
- existing `.varp` definition and `transmit`, `scope`, and `protect` fields;
- base-var carrier information from `configs/all.varbit`;
- cache `clientcode`, if any;
- direct C references and direct array writes.

The tool should support `--check` so CI fails when source changes make the committed ledger stale.

### Human ledger

Create one row per varp with these reviewed fields:

| Field | Meaning |
|---|---|
| `owner` | Quest, minigame, combat, music, interface, or client-only subsystem |
| `value_domain` | Boolean, enum meanings, counter range, sentinel values, or packed layout |
| `lifetime` | Interface session, login session, temporary player state, or permanent player state |
| `producer` | The authoritative server/client code that changes it |
| `consumer` | The CS2 behavior driven by it |
| `timing` | Login sync, before interface open, on mutation, or client-only |
| `disposition` | `transmit-varp`, `transmit-varbit`, `derived`, `client-local`, `obsolete`, or `blocked` |
| `evidence` | Exact CS2/RS2/config/runtime locations supporting the decision |
| `status` | `unreviewed`, `understood`, `implemented`, or `verified` |

No row advances to implementation without `value_domain`, `producer`, `consumer`, `lifetime`, and `disposition` filled in.

## Per-varp investigation procedure

Apply this procedure to every row, working a whole subsystem at a time.

1. Read every CS2 occurrence in context.
   - Identify the containing procedure, arguments, return values, comparisons, sentinels, formatting, and writes.
   - Trace one level of callers and callees.
   - Identify the interface/component or hook that invokes it.
2. Read every server reference in context.
   - Identify all assignments, increments, resets, tests, login initialization, logout cleanup, and reward transitions.
   - Search C-side symbol lookups and direct varp array access as well as `.rs2` sources.
3. Reconstruct the value contract.
   - Enumerate observed values and ranges.
   - Name each state where possible.
   - Record whether zero is a real state, an unset state, or just the default.
4. Establish ownership and lifetime.
   - Decide whether the server, CS2, or both may mutate the value.
   - Decide `scope=temp` versus `scope=perm` from gameplay semantics and save behavior.
5. Check representation safety.
   - Confirm whether the ID carries any varbits.
   - Cross-check `port/vars.map` for `clean-varp`, `carrier`, `false-friend`, or other existing evidence.
   - If the concept is a varbit in this revision, transmit the carrier but mutate only the correct bit range.
6. Confirm timing.
   - Determine whether the client needs the value during login, before an interface opens, or only after a later mutation.
   - Confirm packet ordering relative to `IF_OPEN*`, `RUNCLIENTSCRIPT`, and transmit hooks.
7. Capture a runtime trace for ambiguous cases.
   - Log server mutations, emitted VARP packets, CS2 reads/writes, and the active script/component.
   - Reproduce the UI or gameplay flow and compare the observed values with the inferred contract.
8. Record the disposition and evidence before editing behavior.

## Workstreams and implementation order

The 105 entries form six coherent groups. The counts below cover the entire queue exactly.

### Wave 1: 34 already-declared quest/progression varps

Examples: `grail`, `cookquest`, `runemysteries`, `dragonquest`, `vampire`, and `princequest`.

Why first: server implementations and `.varp` files already exist. Most client consumers are quest-list/status scripts such as 1901, 1969, 2352, 2664, 3809, 4024, 7856, and 9104. This wave tests the transmission and login-sync path with minimal new gameplay logic.

For each varp:

- verify the quest stages used by CS2 match the stages written by the server scripts;
- verify `scope=perm` where quest progress must survive logout;
- add `transmit=yes` only after that stage mapping is confirmed;
- test not-started, in-progress, and complete values;
- verify the correct quest-list color/status and prerequisite behavior after login and immediately after a stage change.

### Wave 2: 12 activity, reward, and completion counters

Varps: `journey_number`, `pilot_journey`, `nzone_rewardpoints`, the six wilderness-boss counters, `total_wintertodt_kills`, and the two Gauntlet completion counters.

Why second: these are narrow integer contracts with visible formatting scripts and clear permanent/session ownership.

For each counter:

- establish exact units, bounds, and any `-1`/unset sentinel;
- find the authoritative increment/reset path;
- declare `scope=perm` for account totals and `scope=temp` for journey/session state;
- initialize from saved state before the relevant UI opens;
- test zero, one, a normal value, and the largest formatting boundary used by CS2.

### Wave 3: 25 undeclared quest/progression varps

Examples: `mcannon`, `cogquest`, `arenaquest`, `treequest`, `grandtree`, `magearena`, `mm_main`, and `routequest`.

Why third: these resemble Wave 1 but require new server declarations and may expose incomplete quest ports.

For each varp:

- locate the owning quest directory and add the declaration there, rather than to a global catch-all;
- prove the cache symbol still represents the same quest state;
- identify every state transition currently implemented by server scripts;
- add missing initialization/mutations only where the existing quest logic proves them;
- set `transmit`, `scope`, and `protect` explicitly;
- test the quest status UI plus at least one real stage transition.

Do not mark a partially ported quest “implemented” merely because its current stages transmit. Record unsupported stages in the ledger.

### Wave 4: 5 combat and player-status varps

Varps: `poison`, `saramage`, `guthmage`, `zamomage`, and `magearena_charge`.

Why fourth: these are transient gameplay state with timers, resets, death/logout behavior, and combat-visible UI.

For each varp:

- reconstruct timer units and sentinel values from both combat scripts and CS2;
- decide whether logout pauses, clears, or preserves the state;
- ensure mutation occurs through the world setter so changes transmit;
- test activation, countdown/change, expiry, death, logout/login, and cure/reset paths;
- verify no stale client value survives a server reset.

### Wave 5: 1 Make-X/interface-session varp

Varp: `makexcrafting`.

Why separate: scripts 2928, 2930, 3256, 3260, and 3262 suggest a temporary UI selection rather than durable player state. The main risk is assigning server ownership to a client-owned scratch varp.

Investigation must determine:

- whether CS2 or the server chooses the value;
- whether it is an object/category ID, quantity, or mode;
- which packet/script opens the interface and in what order the value is needed;
- whether the correct disposition is `client-local`, a pre-open transmitted temp varp, or a script argument replacing the varp dependency.

Test opening, changing selection, confirming, cancelling, reopening, and two simultaneous/queued production flows.

### Wave 6: 28 music state varps

Music varps: `musicplay` and `musicmulti_1` through `musicmulti_27`.

Why last: these form a wide, array-like contract and have the highest client-ownership false-positive risk.

For music:

- reconstruct scripts 315, 318, 3962, 3967, 7305, 7306, 9292, 9297, and 9630–9633 as one subsystem;
- determine whether `musicmulti_*` is a server-authored playlist array, client scratch space, unlock state, or a mixed contract;
- identify array ordering, empty sentinel, maximum length, and update trigger;
- prefer one subsystem-level implementation and test matrix over 28 independent declarations;
- test login, region change, manual track selection, shuffle/playlist changes, mute/unmute, and logout/login.

## Implementation pattern

Once a row is understood, implement it in this order:

1. Add or correct the owning `.varp` declaration with explicit `transmit`, `scope`, and `protect` fields.
2. Add or correct initialization in the player/login path.
3. Route every authoritative mutation through the normal setter.
4. Add persistence for permanent values and cleanup/reset behavior for temporary values.
5. Ensure initial transmission happens before the first dependent interface or client script.
6. Add focused tests before removing the row from the gap report.
7. Regenerate/check the audit and update the human ledger status to `verified`.

If investigation finds a client-local or obsolete varp, do not add a declaration. Record the evidence and change the audit disposition so it stops appearing as an implementation gap.

## Verification gates

Every implementation wave must pass all applicable gates:

### Static gates

- the CS2-varp audit and ledger `--check` mode;
- content symbol/name-resolution checks;
- carrier-varp and whole-write checks;
- no new direct writes to the varp array;
- server-script compilation and corpus tests.

### Packet/state gates

- login emits the correct initial value for transmitted varps;
- an authoritative mutation emits exactly one correctly sized VARP packet;
- unchanged values emit no redundant packet;
- reset/logout semantics clear or preserve the value as specified;
- packet ordering precedes the dependent interface/script execution.

### CS2/UI gates

- execute the named CS2 consumers with representative values;
- assert visible text, component state, colors, options, and hooks;
- test zero/unset, normal, boundary, and completion/expiry values;
- verify script-side writes do not create transmit loops.

### Regression gates

- mock server selftest;
- `make -C src test-port`;
- server-script compiler/corpus tests;
- CS2 VM and transmit-pump tests;
- save/load tests for permanent varps;
- a smoke run covering login, quest list, music settings, and one changed varp per completed wave.

## Commit strategy

Keep changes reviewable and bisectable:

1. Audit tool and ledger schema only.
2. One commit per coherent subsystem or small quest group.
3. Tests in the same commit as each behavior change.
4. Regenerated audit/ledger update in that commit.
5. No mixed “all remaining varps” commit.

## Definition of done

A varp is done only when:

- its producer, consumer, values, ownership, lifetime, representation, and timing are documented;
- its disposition is supported by exact source or runtime evidence;
- required server behavior, transmission, initialization, and persistence are implemented;
- focused packet/state and CS2 behavior tests pass;
- carrier safety and full regression gates pass;
- the checked audit no longer reports it as an unexplained gap.

The project is done when all 105 rows are `verified`, intentionally `client-local`/`obsolete` with evidence, or explicitly blocked by a named missing prerequisite. No row remains merely “probably transmit.”
