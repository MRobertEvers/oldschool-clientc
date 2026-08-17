# Server Queues

Research into authentic OSRS script queues, an audit of what this engine already implements, and
a plan for closing the remaining gaps.

> **Read this first.** The task that produced this document was framed as *"the engine needs a
> concept of queues"*. That premise is wrong. The engine has had a queue system for some time: five
> queue kinds, both timer types, 15 opcodes, an NPC queue, an engine/zone queue, a purpose-built
> selftest corpus, and a design chapter in
> [docs/osrs230_mockserver.md](osrs230_mockserver.md) §3.19 (lines 4080–4306). The 240 content
> files that call queue ops work today.
>
> What follows is therefore an **audit against the authentic model**, not a greenfield design.
> There are real gaps — the largest being SOFT and queue ordering — but they are refinements to a
> working subsystem.

Companion documents: [osrs230_mockserver.md](osrs230_mockserver.md) §3.19 is the implementation
record and stays authoritative for *what the code does*. This document is authoritative for *what
the game does*, and for the delta between the two.

---

## 1. What a queue is

A queue is a per-entity list of deferred script invocations. Each entry holds a script, its
arguments, a countdown in server ticks, and a **strength**. Once per tick, during that entity's
processing phase, the engine walks the list, decrements the countdowns, and runs whatever is due —
subject to rules about interfaces, delays, and cross-strength cancellation.

Almost every non-trivial OSRS behaviour is expressed through it:

| Behaviour | Mechanism |
| --- | --- |
| Make-X, fletching, smithing loops | weak queue, re-queuing itself each iteration |
| Damage from a spell already in flight | strong queue, delayed by the projectile's travel time |
| Death, teleports | strong queue (+ `p_delay`) |
| Level-up dialogue, XP drops, recoil damage | normal queue |
| Poison, antifire, stat drain/regen | timers (the sibling system, §4) |
| "You can't do that while you're busy" | falls out of the queue rules, not from ad-hoc checks |

---

## 2. Sources

In descending order of authority:

1. **osrs-docs** — [`Jakobzs/osrs-docs`](https://github.com/Jakobzs/osrs-docs):
   [`queues.md`](https://osrs-docs.com/docs/mechanics/queues/),
   [`timers.md`](https://osrs-docs.com/docs/mechanics/timers/),
   [`delays.md`](https://osrs-docs.com/docs/mechanics/delays/).
   Behaviour-verified against the live game with Mod Ash tweets cited as backing; the queue page
   carries a "Verified naming" label and includes Kotlin pseudocode of the processing loop.
   **This is the authority for the modern (post-2021) queue.**
2. **LostCity engine** — `2004Scape/Server` → `LostCityRS/Server`, and on disk at
   `/Users/matthewevers/Documents/git_repos/LostCity_Server` (branch `254_zuk`), which
   [PORTING_GUIDE.md:34](PORTING_GUIDE.md#L34) names as the primary reference. Key files:
   `src/engine/entity/PlayerQueueRequest.ts`, `NpcQueueRequest.ts`, `EntityTimer.ts`, `Player.ts`,
   `Npc.ts`, `World.ts`. **This is what our C code is a port of**, so it is authoritative for our
   opcode surface — but it is a *2004-era* engine and deliberately does not implement everything
   osrs-docs describes.
3. **`engine.rs2`** — [`LostCityRS/Content@274`](https://github.com/LostCityRS/Content/blob/274/scripts/engine.rs2),
   the authoritative command signature list.
4. **RuneDocs** — [`runedocs.github.io/docs/content/queue`](https://runedocs.github.io/docs/content/queue/).
   Content-author-facing; useful for the "which action uses which strength" table.
5. **The primer in the task prompt** — the only source describing **SOFT** semantics in detail.

> **Confidence.** WEAK / NORMAL / STRONG are well-attested by 1–4 and mutually consistent. SOFT is
> attested only by the primer, plus an unused `SOFT` member annotated `// OSRS` in LostCity's
> `PlayerQueueType`. osrs-docs predates soft queues and documents only three strengths. Treat SOFT
> as the least-verified part of the model.

---

## 3. The authentic model

### 3.1 One queue, not four

The most common misconception, and osrs-docs calls it out explicitly:

> Contrary to popular belief, there is a single queue for players, excluding the area queue.
> All scripts, regardless of the queue type used, will go to the end of the same queue.

Strength is a **property of the entry**, not a separate list. The queue is processed in strict
insertion order; a strong entry queued after a normal one does not jump ahead of it. There is no
known cap on queue length.

LostCity deviates — it keeps `queue` and `weakQueue` as two `LinkList`s and processes
normal-then-weak — and our C port inherited that. See gap **G2/G3** below.

### 3.2 The four strengths

| Strength | Cleared by interruptions? | Modal interface | Blocked by `delayed`? | Clears weak entries? |
| --- | --- | --- | --- | --- |
| **WEAK** | yes — any interruption wipes all weak entries | skipped while a modal is open | yes | — |
| **NORMAL** | no | skipped while a modal is open | yes | no |
| **STRONG** | no | force-closes the modal before executing | yes | yes, while present |
| **SOFT** | no | ignores modals entirely | **no** | no |

**WEAK**
- Removed if *any* strong entry is in the queue at the start of the processing block — even a
  strong entry that is not yet due.
- Removed on any interruption. osrs-docs' non-exhaustive list: interacting with an entity or
  clicking a game square, interacting with an inventory item, unequipping an item, opening an
  interface, closing an interface, dragging inventory items. Their generalisation: *"any action
  which closes an interface also clears all weak scripts from the queue."*
- Skipped (not removed) while a modal is open.

**NORMAL**
- Skipped in the execution block while a modal is open; stays queued and runs once it closes.
- Discarded only on logout, on explicit `clearqueue`, or by running to completion.

**STRONG**
- Its mere presence in the queue, before processing begins, closes the modal and wipes every weak
  entry.
- Additionally force-closes the modal immediately before *it* executes — which is a separate act
  from the pre-pass close, and is what makes the window-mode exception (§3.6) observable.
- Cannot be paused or interrupted.

**SOFT**
- Clears nothing, closes nothing, cannot be cancelled.
- **The distinguishing property: soft entries execute even while the entity is `delayed`.** All
  three strengths above the line are blocked by delay; soft is not. That is the line the primer
  draws.

### 3.3 The processing loop

From osrs-docs, transcribed from their Kotlin:

```
process():
    # --- pre-pass, once per tick ---
    if queue contains any STRONG entry:          # even a not-yet-due one
        closeModalInterfaces()                   # which also wipes all WEAK entries
    # (one exception: the change_window_mode strong script — §3.6)

    # --- main loop ---
    loop:
        n = processQueue()
        if n == 0: break                         # a full pass that processed nothing

processQueue() -> int:
    processed = 0
    for entry in queue:                          # insertion order
        if entry.type == STRONG:
            closeModalInterfaces()
        if canProcess(entry):
            entry.run()                          # may set a delay
            remove entry from queue
            processed += 1
    return processed

canProcess(entry) = not player.delayed()
                and not player.containsModalInterface()
                and not entry.future()
```

Four consequences a naive implementation misses:

1. **The outer loop repeats.** A pass that executes at least one entry runs the whole list again.
   This is how a script that closes an interface unblocks a *later* normal entry within the same
   tick. It terminates when a full pass is a no-op.
2. **`STRONG` closes the modal from inside the loop too**, not only in the pre-pass.
3. **A delay set mid-loop halts everything after it.** If an executing script calls `p_delay`,
   `canProcess` goes false for every subsequent entry. The delayed script resumes at the *start of
   player processing* on the tick the delay ends — and resuming it does **not** resume processing
   the rest of the queue that tick.
4. **A script queued from inside a script cannot execute this tick** — its countdown makes
   `future()` true. But it *is* visible to the loop, so a strong entry queued mid-tick still
   force-closes the modal on the very tick it was queued.

Note the asymmetry: osrs-docs' `canProcess` does not special-case strength. Strong entries clear
the modal check not by exemption but because `closeModalInterfaces()` on the line above already
cleared the condition. Soft entries **do** need a genuine exemption from the `delayed()` term —
the one place the pseudocode must be extended to cover the primer's fourth strength:

```
canProcess(entry) = (entry.type == SOFT or not player.delayed())
                and (entry.type in (STRONG, SOFT) or not player.containsModalInterface())
                and not entry.future()
```

### 3.4 Countdowns

`weakqueue*(smith_generic, 3)` schedules three server ticks from the moment it was queued.
LostCity post-decrements — `const delay = request.delay--` then `if (delay <= 0)` — so delay 0
fires on the *next* tick's pass, matching "the earliest a queued script may execute is the
following server tick". Our C port stores `delay + 1` and pre-decrements, which is the same
arithmetic read the other way round.

The player and NPC paths differ deliberately: the player decrements unconditionally while the NPC
*"purposely only decrements the delay when the npc is not delayed"* (`Npc.processQueue`). Both
sides of our port already reproduce their own convention, with the one-tick difference written out
at [mock230_world.c:3678-3690](../src/net/mock/mock230_world.c#L3678-L3690).

### 3.5 Delays are the gate

From `delays.md`, a `delayed` entity has queues not processed (except soft), entity interactions
not processed, route events not processed (though *already-determined* movement continues), and
timers counting down but holding at zero without executing. Interface clicks still arrive; each
button script decides whether to honour them.

Two flavours: **normal delay** (`p_delay` / `npc_delay`, N ticks, pauses the calling script) and
**arrive delay** (`p_arrivedelay` / `npc_arrivedelay`, exactly one tick, only if the entity moved
this tick or last — it is *not* "wait until arrival"). Arrive delay is the one-tick stall on
reaching a mining rock.

### 3.6 The window-mode exception

Documented, low priority. Changing window mode queues a strong script that does **not** trigger the
pre-pass modal close, only the in-loop one. osrs-docs encodes it as a label check on the strong
entries. Worth a comment and a TODO; not worth wiring until content needs it.

### 3.7 NPC queues

NPCs have a single queue with **one** strength. The entry carries an `ai_queue` trigger id rather
than a script reference, resolved at execution time against the NPC's type and category
(`AI_QUEUE1 + n`). RuneDocs notes ids 1–20 in use, with 1 = retaliate, 2 = damage, 3 = bind.

### 3.8 Area queue

A third kind, player-only, for multiway-zone entry, music unlocks, and farming patch state.
osrs-docs marks it incomplete; out of scope here. Our `MOCK230_QUEUE_ENGINE` (zone triggers) is the
nearest thing we have.

---

## 4. Timers — the sibling system

Queues fire once; timers fire periodically.

- **Ordering: timers run *before* queues for NPCs, and *after* queues for players.**
- **A timer does not execute on the tick it was set.** New timers land in a separate collection,
  folded into the live collection *after* the current tick's timer pass. This is exactly what makes
  prayer-flicking not drain prayer.
- **Normal timers** (players only) need advanced access: they hold at zero while a modal is open or
  the player is delayed, retrying each tick, and reset to their interval only once they fire.
- **Soft timers** (players and NPCs) tick and fire regardless of entity state but have no advanced
  access — they cannot read or write inventories or vars.
- **Limits:** unlimited for players, **exactly one** for NPCs. This is why NPCs with periodic
  overhead chat cannot be poisoned.

Our implementation covers the two types and both access rules
([mock230_scripts.c:1142-1193](../src/net/mock/mock230_scripts.c#L1142-L1193)), and uses an
absolute `clock` rather than a countdown. The "no advanced access for soft timers" restriction is
enforced by running them unprotected. See G12 for the one timer gap.

---

## 5. Command surface

Verbatim from `engine.rs2` (`.`-prefixed variants are the secondary-entity forms, omitted here):

```
[command,queue](queue $queue, int $delay, int $arg)
[command,queue*](queue $queue, int $delay)
[command,weakqueue](queue $queue, int $delay, int $arg)
[command,weakqueue*](queue $queue, int $delay)
[command,strongqueue](queue $queue, int $delay, int $arg)
[command,strongqueue*](queue $queue, int $delay)
[command,longqueue](queue $queue, int $delay, int $arg, int $logout_action)
[command,longqueue*](queue $queue, int $delay, int $logout_action)
[command,npc_queue](int $ai_queue, int $arg, int $delay)
[command,clearqueue](queue $queue)
[command,getqueue](queue $queue)(int)

[command,settimer](timer $timer, int $interval)
[command,softtimer](softtimer $timer, int $interval)
[command,cleartimer](timer $timer)
[command,clearsofttimer](softtimer $timer)
[command,gettimer](timer $timer)(int)

[command,p_delay](int $delay)
[command,npc_delay](int $delay)
```

- The `*` suffix is the **vararg** form: the non-`*` form takes exactly one `int $arg`, the `*` form
  takes a trailing parameter list matching the target script's signature.
- There is **no `softqueue`** in the 2004-era `engine.rs2`. Soft is an OSRS-era addition; we would
  have to mint the command (see G1).
- `longqueue`'s fourth argument is the logout behaviour: **accelerate** (ignore the remaining
  countdown on logout and try every tick) or **discard**.
- `getqueue` returns a **count** of matching entries — content uses it to avoid double-queuing.
- `queue`, `timer` and `softtimer` are distinct ServerScript *types* with their own symbol spaces.
- Strength is distinguished at the *command* level, not by an argument. LostCity's internal
  `ENGINE` kind has no content-facing command.

**Trap, already documented in [serverscript.md:1031-1034](serverscript.md#L1031-L1034):**
`npc_queue`, `npc_settimer` and `npc_walktrigger` take an **int**, not a script name.

---

## 6. What this engine already has

All verified in the working tree.

> **Line numbers drift.** `mock230_world.c`, `mock230_scripts.c` and `mock230.h` were being edited
> concurrently while this audit ran — `mock230_world.c` shifted by ~41 lines mid-session. Treat
> every line reference below as a starting point and re-grep the symbol before editing.

### Opcodes — [src/serverscript/ss_opcode.h](../src/serverscript/ss_opcode.h)

`CLEARQUEUE` 2011, `GETQUEUE` 2021, `LONGQUEUE` 2060 / `LONGQUEUEVARARG` 2061, `QUEUE` 2096 /
`QUEUEVARARG` 2097, `STRONGQUEUE` 2123 / `STRONGQUEUEVARARG` 2124, `WEAKQUEUE` 2135 /
`WEAKQUEUEVARARG` 2136, `NPC_QUEUE` 2531, plus the five timer ops and the four delay ops.
**No `SOFTQUEUE`.**

### Triggers — [src/serverscript/ss_trigger.h:116-139](../src/serverscript/ss_trigger.h#L116-L139)

`QUEUE` 116, `AI_QUEUE1..20` 117–136, `SOFTTIMER` 137, `TIMER` 138, `AI_TIMER` 139.

### Data — [src/net/mock/mock230.h:1691-1723](../src/net/mock/mock230.h#L1691-L1723)

```c
enum Mock230QueueKind
{
    MOCK230_QUEUE_NORMAL = 0,
    MOCK230_QUEUE_LONG,     /* like NORMAL, plus a logout action */
    MOCK230_QUEUE_WEAK,     /* discarded whenever a modal closes */
    MOCK230_QUEUE_STRONG,   /* closes whatever modal is up before the drain */
    MOCK230_QUEUE_ENGINE    /* zone family; engine-produced, delay always 0 */
};

struct Mock230Queued
{
    int active;
    int script_id;
    int delay;                              /* ticks remaining */
    int32_t args[MOCK230_QUEUE_ARG_MAX];    /* 8 */
    int argc;
    int kind;
    int logout_action;                      /* LONG only */
};
```

Caps: `MOCK230_QUEUE_MAX` 32, `MOCK230_QUEUE_ARG_MAX` 8, `MOCK230_ENGINE_QUEUE_MAX` 8,
`MOCK230_NPC_QUEUE_MAX` 8, `MOCK230_TIMER_MAX` 32, `MOCK230_WORLD_QUEUE_MAX` 16.

`ENGINE` lives in a separate array on purpose, and the reason is load-bearing rather than
stylistic: `unlinkQueuedScript`'s default branch never walks `engineQueue`, so `clearqueue` must not
be able to cancel a zone trigger.

### Runtime — [src/net/mock/mock230_scripts.c](../src/net/mock/mock230_scripts.c)

| Function | Line | Role |
| --- | --- | --- |
| `player_can_access` | 710 | the `canAccess()` port — `delayed_until` + both modal groups |
| `drain_queue` | 1052 | one pass over one kind-set |
| `mock230_scripts_clear_weak_queue` | 1103 | |
| `mock230_scripts_process_queues` | 1113 | STRONG pre-scan → `drain_queue(0)` → `drain_queue(1)` |
| `mock230_scripts_process_timers` | 1142 | NORMAL pass then SOFT pass |
| `mock230_scripts_process_engine_queue` | 2427 | the zone family |
| `SS_OP_QUEUE`/`STRONGQUEUE`/`WEAKQUEUE`/`LONGQUEUE` | 7624 | one body; kind is the only difference |
| the four `*VARARG` forms | 7650 | |
| `CLEARQUEUE` / `GETQUEUE` | 7715 / 7731 | |
| `SETTIMER` / `SOFTTIMER` / `CLEARTIMER` / `GETTIMER` | 7747–7859 | |
| `P_DELAY` / `P_ARRIVEDELAY` / `NPC_DELAY` / `NPC_ARRIVEDELAY` / `WORLD_DELAY` | 7404–7578 | |
| `NPC_QUEUE` / `NPC_SETTIMER` | 4537 / 4586 | |

There is a fifth queue beyond the four kinds: `world_queue[16]` on `Mock230Server`, holding scripts
parked by `world_delay` and drained in phase 1. It has no strength model and is not part of the
entity queue system; osrs-docs' "area queue" is the nearest authentic analogue.

Suspension is not a coroutine: `SSVM_Suspend` sets `execution` and returns, leaving pc, stacks and
locals in place, and the engine parks the state and calls `SSVM_Execute` again on a later tick
(`ssvm.h:14-23`). `run_or_park` allows **one parked script per player** — a second is dropped with a
stderr line. That single slot is what stands in for LostCity's `protect` flag.

`close_modal` clears the weak queue *before* its early return
([mock230_world.c:6897](../src/net/mock/mock230_world.c#L6897)), so a close with nothing mounted
still discards weak entries — matching `Player.closeModal`. **This is correct; do not "fix" it.**

### Tick order

Player phase 5 ([mock230_world.c:9771-9791](../src/net/mock/mock230_world.c#L9771-L9791)):
`resume_player` → `process_queues` → `process_timers` → `process_engine_queue`.
NPC phase 4 ([mock230_world.c:3630-3690](../src/net/mock/mock230_world.c#L3630-L3690)): poison →
delay gate → **timers** → freeze decrement → **queues**. Both match the reference's per-entity
ordering, including the players-after / NPCs-before asymmetry §4 calls for.

### Content corpus

240 of 2,330 `.rs2` files under `OSRS-Content/osrs239-content/server/scripts/` call queue ops:
`queue(` ×320, `npc_queue(` ×159, `clearqueue(` ×77, vararg `queue*` ×46, `getqueue(` ×18,
`longqueue(` ×17, `weakqueue(` ×2, `strongqueue(` ×2, `softqueue` ×0. Declarations: 1,237
`[ai_queue<n>]`, 161 `[queue,…]`, 83 `[ai_timer]`, 56 `[timer]`, 45 `[softtimer]`.
`p_delay(` ×1,356.

The purpose-built test corpus is
`OSRS-Content/osrs239-content/server/scripts/selftest_triggers.rs2` (queue-kind, strongqueue-arm,
access-gate, clearqueue, getqueue and weak-only stanzas), driven from
[mock230_world.c:30172-30360](../src/net/mock/mock230_world.c#L30172-L30360).

---

## 7. Gap analysis

Ranked by how observable the divergence is.

| # | Gap | Severity | Source |
| --- | --- | --- | --- |
| **G1** | **SOFT strength does not exist** | high | primer; osrs-docs silent |
| **G2** | **Execution order is slot order, not insertion order** | high | osrs-docs §3.1 |
| **G3** | Two-pass drain (all non-weak, then all weak) instead of one list | high | osrs-docs §3.1 |
| **G4** | No outer repeat-until-no-progress loop | medium | osrs-docs §3.3 |
| **G5** | STRONG does not close the modal from inside the loop | low | osrs-docs §3.3 |
| **G6** | Window-mode exception unimplemented | low | osrs-docs §3.6 |
| **G7** | `[logout]` does not clear queues/timers | medium | LostCity `Player.cleanup()` |
| **G8** | `LONG.logout_action` stored, never read | low | osrs-docs §3.5 |
| **G9** | Fixed caps (32 / 4 / 8 / 8) where the reference is uncapped | medium | — |
| **G10** | Two stale texts claim the vararg queue forms don't exist; they do | trivial | — |
| **G11** | Weak-clear on non-modal interruptions unverified | unknown | osrs-docs §3.2 |
| **G12** | New timers may fire on the tick they were armed | medium | osrs-docs timers.md |

### G1 — SOFT

The engine deliberately omits it. The comment at
[mock230.h:1676](../src/net/mock/mock230.h#L1676) states the reasoning:

> `PlayerQueueType` in the reference, minus SOFT, which the reference declares and never uses — a
> kind nothing can put in the queue is a branch no test can reach.

That was a sound call **against the rev-254 reference**. It stops being sound if we want the
modern model, because SOFT is the only strength that runs while the entity is delayed — the exact
capability the primer identifies as the dividing line. Today, anything that must fire during a
`p_delay` has no queue-shaped way to do it.

Cost: this is the one gap that expands the engine *beyond* the reference, so it needs a decision
before it needs code. There are **zero** `softqueue` call sites in the content corpus, so nothing
regresses either way, and nothing exercises it until content is written.

### G2/G3 — ordering

`SS_OP_QUEUE` allocates the **first free slot** in a fixed array
([mock230_scripts.c:7641](../src/net/mock/mock230_scripts.c#L7641)), and `drain_queue` walks that
array **by index**. Execution order is therefore slot order, which after any slot is freed and
reused is no longer insertion order:

> Slots 0, 1 and 3 are occupied and slot 2 has just been freed. A newly queued entry takes slot 2
> and executes **before** the entry in slot 3, which was queued earlier.

On top of that, `drain_queue(0)` then `drain_queue(1)` runs every non-weak entry before every weak
entry regardless of insertion order.

These are one fix, not two: give the queue a real order. Cheapest form is a monotonically
increasing `seq` on each entry, drained in `seq` order, with the weak/non-weak split removed.

### G12 — timers arming

osrs-docs is explicit that a timer must not execute on the tick it was armed, and that this is
what makes prayer-flicking work. Our timers use an absolute `clock` set to the arming tick and fire
when `srv->tick >= clock + interval`, so `interval > 0` is safe. But
[mock230_scripts.c:1142-1193](../src/net/mock/mock230_scripts.c#L1142-L1193) deliberately allows
`interval == 0` (a real reference case: `settimer(agilityarena_pillar, sub(deadline, map_clock))`
can arm with 0 or negative). An `interval <= 0` timer armed during the timer pass can fire in that
same pass. Worth a same-tick guard, cheaply: skip any timer whose `clock == srv->tick`.

### Not gaps

Verified against the reference and correct as written — do **not** change these:

- `close_modal` clearing the weak queue before its early return.
- The player/NPC one-tick decrement asymmetry.
- Timers-before-queues for NPCs, timers-after-queues for players.
- `ENGINE` living in its own array beyond `clearqueue`'s reach.
- The absence of a lower bound on timer intervals.
- `protect` having no equivalent (the one-parked-script rule stands in — a stated divergence).

---

## 8. Implementation plan

Five phases, ordered so each is independently shippable and independently testable. Phases 1–2 are
the ones worth doing regardless of how the SOFT question is decided.

### Phase 0 — decide SOFT *(blocking for phase 3 only)*

One question for the maintainer, since it is the only item that expands past rev 254:

> Do we want SOFT, given zero content call sites today and the fact that it is the only mechanism
> for "run this while the player is delayed"?

Recommendation: **yes, but last.** Land phases 1–2 first; they fix behaviour content already
depends on. SOFT is new capability, and new capability with no caller is exactly the branch the
existing comment rightly warns about — so it should arrive together with its first real content
user, plus selftest coverage.

### Phase 1 — real queue ordering (G2, G3)

Fixes the two high-severity gaps in one change.

1. Add `int seq` to `struct Mock230Queued`, and `int queue_seq` to `Mock230Player` as the
   monotonic source. Stamp on enqueue in all eight opcode bodies (`QUEUE`/`STRONGQUEUE`/
   `WEAKQUEUE`/`LONGQUEUE` and their varargs) and in `mock230_scripts_queue_hook` /
   `queue_named`.
2. Replace `drain_queue(srv, weak)` with a single `drain_queue(srv)` that selects the lowest-`seq`
   active entry not yet visited this pass, rather than walking by index. With `MOCK230_QUEUE_MAX`
   at 32 an O(n²) selection scan is 1,024 comparisons worst case — irrelevant next to running a
   script, and much easier to reason about than threading a free list.
3. `mock230_scripts_process_queues` becomes: STRONG pre-scan (unchanged) → one `drain_queue(srv)`.
4. Leave `queue_seq` unreset across logout (or reset it in phase 4 with the rest of the cleanup);
   wrap-around at `INT_MAX` is unreachable at 32 slots but assert on it rather than trusting that.

**Test:** extend `selftest_triggers.rs2` with a stanza that queues normal→weak→normal with equal
delays and asserts the fire order via a shared counter var. Today's code fires them
normal, normal, weak; authentic is the insertion order. Per
[verify-blocker-and-failing-test](osrs230_mockserver.md), confirm the new assertion actually fails
against the current implementation before landing the fix.

### Phase 2 — the repeat loop and in-loop strong close (G4, G5)

1. Wrap the drain in `do { n = drain_queue(srv); } while (n > 0);`, with `drain_queue` returning
   the number of entries it executed.
2. Inside the drain, when the selected entry is `MOCK230_QUEUE_STRONG`, call
   `mock230_world_close_modal(srv)` before the access check — this is the per-entry close that is
   distinct from the pre-scan.
3. Guard against a script that re-queues itself with delay 0 spinning the outer loop forever. The
   `+1` store means a self-queue lands at `delay = 1` and cannot be due in the same tick, so the
   loop terminates naturally — but add an iteration cap with a loud `SSVM_Abort` anyway, because
   the engine queue and `queue_hook` paths can store `delay = 0` directly.

**Test:** a stanza where entry A closes a modal and entry B is a normal entry the modal was
blocking. Authentic: both run this tick. Today: B waits a tick.

### Phase 3 — SOFT (G1) *(gated on phase 0)*

1. `MOCK230_QUEUE_SOFT` in `enum Mock230QueueKind`, appended so existing values are stable.
2. Mint `SS_OP_SOFTQUEUE` / `SS_OP_SOFTQUEUEVARARG`. These have no upstream opcode numbers —
   allocate from the local extension range rather than guessing at Jagex's, and say so in
   `ss_opcode.h`. Add arities to `ss_meta.gen.h` via whatever generates it, add
   `softqueue`/`softqueue*` to the script-typed-first-argument table at
   [ssc_compile.c:807-816](../src/serverscript/ssc_compile.c#L807-L816) mapped to the `"queue"`
   namespace, and mirror it in the `mock230_scripts.c` table.
3. Split the access gate in the drain. `player_can_access` stays as-is for timers and every other
   caller; the drain gains a per-entry form:

   ```c
   static int
   queue_entry_can_run(struct Mock230Server* srv, const struct Mock230Queued* entry)
   {
       struct Mock230Player* player = srv->active_player;

       /* SOFT is the one kind that runs through a delay — that is its entire
        * point (SERVER_QUEUES.md §3.2). It ignores modals too. */
       if( entry->kind == MOCK230_QUEUE_SOFT )
           return 1;
       return player_can_access(srv);
   }
   ```
4. SOFT must survive `close_modal` (`clear_weak_queue` already only touches `WEAK`) and must
   survive `clearqueue`? — **no**: `clearqueue` is by script id across all strengths in the
   reference, so SOFT is clearable by id, just not by interruption. Keep the existing behaviour.
5. Run soft entries **unprotected**, mirroring soft timers.

**Test:** new selftest stanza — `p_delay(3)` then a soft entry at delay 1 and a normal entry at
delay 1; assert soft fires during the delay and normal fires after it.

### Phase 4 — lifecycle and limits (G7, G8, G9, G12)

1. **`[logout]` cleanup.** `Player.cleanup()` clears `queue`, `weakQueue`, `engineQueue` and
   `timers`. Ours does not, so a queue entry can survive into the next session in the same player
   slot. Add the clears to the logout path.
2. **`LONG.logout_action`.** Now readable, since `phase_logouts` gains a body in step 1: on
   logout, `^accelerate` (0) forces `delay = 0` on LONG entries and lets them try each tick;
   `^discard` drops them.
3. **Caps.** `SSVM_Abort("the player's queue is full")` at 32 entries is a hard failure where the
   reference cannot fail. Inferno and the QBD session both issue `clearqueue` bursts (14
   consecutive in `rs2012_qbd_session.rs2`), which suggests content already runs near the ceiling.
   Either raise `MOCK230_QUEUE_MAX` with a counter logging the observed high-water mark, or move to
   a growable allocation. Start with the counter — measure before resizing.
4. **Timer same-tick guard (G12).** Skip any timer whose `clock == srv->tick` in the timer pass,
   so a zero-interval timer armed this tick waits for the next one.

### Phase 5 — cleanup (G10, G6, G11)

1. **The vararg queue forms work, and two pieces of text in the tree say they do not.** Both are
   stale and both should go:
   - The comment at
     [mock230_scripts.c:7592-7594](../src/net/mock/mock230_scripts.c#L7592-L7594), which claims the
     vararg forms *"do not exist"* and that the compiler *"refuses [them] outright"*.
   - The `fail()` message at
     [ssc_compile.c:2298-2300](../src/serverscript/ssc_compile.c#L2298-L2300):
     *"the vararg form (queue\*/strongqueue\*/weakqueue\*/longqueue\*) is not supported; it packs a
     type string the compiler does not build"*.

   Ground truth: `parse_command` at
   [ssc_compile.c:704-731](../src/serverscript/ssc_compile.c#L704-L731) lexes the trailing `*` and
   resolves `queue` → `QUEUEVARARG`; `SS_OP_QUEUEVARARG` is implemented at
   [mock230_scripts.c:7650](../src/net/mock/mock230_scripts.c#L7650); and **46 content call sites
   compile and run today** (`queue*(combat_damage_player, 0)(npc_uid, $damage)` in
   `elvarg.rs2:107`, `dragonslayer2.rs2:838`, `gauntlet_hunllef.rs2`, …).

   The `fail()` at 2298 is only the *statement-start* fallback for a stray `*` — a statement
   beginning with `*` — and its message names the four forms that in fact work. It is actively
   misleading: reading it is enough to conclude the feature is missing. Reword it to describe what
   it actually catches.
2. Add a TODO for the window-mode exception (G6) next to the STRONG pre-scan, citing §3.6. Do not
   implement.
3. Audit G11: confirm that entity interaction, game-square click, and inventory drag all reach
   `close_modal` (and therefore clear weak). In LostCity `clearPendingAction()` is
   `clearInteraction()` + `closeModal()`, so they should — but this is an assumption, not a
   verified fact, and it is cheap to check with a selftest stanza that arms a weak entry then
   simulates each interruption.

### Sequencing summary

```
Phase 1 (ordering)      ──┬── independent, highest value, fixes live misbehaviour
Phase 2 (repeat loop)   ──┘   builds directly on phase 1's single drain

Phase 0 (decision) ── Phase 3 (SOFT)   ── new capability, land with its first content user

Phase 4 (lifecycle)     ── independent of 1–3
Phase 5 (cleanup)       ── independent; the stale comment can go immediately
```

### Testing

Everything above is covered by the existing harness — `selftest_triggers.rs2` plus its C driver at
[mock230_world.c:30172-30360](../src/net/mock/mock230_world.c#L30172-L30360). Two standing repo
lessons apply directly:

- **[verify-blocker-and-failing-test]** — mutate the implementation to prove each new assertion can
  fail. A queue-ordering assertion that passes against both orders tests nothing.
- **[headless-runs-are-not-independent]** — use a scratch `MOCK230_SAVES` for these runs; queue and
  timer state is exactly the kind that leaks between runs, and phase 4 is about that leak.

Build with `make -C src`, not CMake, and watch for stale `.o` files.
