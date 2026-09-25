# Use-on dispatch: the engine fix

Companion to [`TOOL_TRIGGER_ORGANISATION.md`](TOOL_TRIGGER_ORGANISATION.md),
which describes the content-side rules that today's engine forces on every
`[opheldu]` author. This doc is the plan for changing the engine so most of
those rules stop being necessary — so that a lane can bind the side it owns,
say "not mine" when it does not recognise the partner, and never have its
pair swallowed by another lane's binding.

Two changes, both in `src/torirsserver/torirs_server_scripts.c`, one new opcode, and a
dispatch driver so any of it can be proved.

---

## 1. What the engine does today, and what is wrong with it

`ToriRSServer_ScriptsRunOpheldu` (`torirs_server_scripts.c:2519-2582`) is a faithful
port of `OpHeldUHandler.ts:94-113`:

```
rung 1  [opheldu,<obj type>]                     last_item = obj      last_useitem = useObj
rung 2  [opheldu,<useObj type>]      SWAP        last_item = useObj   last_useitem = obj
rung 3  [opheldu,_<obj category>]    (no swap)   last_item = useObj   last_useitem = obj     <- inverted
rung 4  [opheldu,_<useObj category>] SWAP        last_item = obj      last_useitem = useObj  <- inverted
none    "Nothing interesting happens."
```

`run_trigger_script` (`:1791`) runs the first hit and returns
`TORIRSSERVER_TRIGGER_RAN`; the caller (`torirs_server_world.c:5099`) says
`nothing_interesting_message` only on `TORIRSSERVER_TRIGGER_NONE`.

Three defects, all faithful to the reference and all bad:

**D1 — first match is final.** A script that hits its `case default :
~displaymessage(^dm_default)` has *consumed* the click. Rungs below it never
run. So a binding on family A that does not know about family B silently kills
the A × B pair, even when B has a perfectly good binding one rung down. This
is the runite bolts × onyx tips defect (`weapon_poison.rs2:46 [opheldu,_bolts]`
swallows tips-on-bolts), and it is why the content doc has to say "never add
a defensive binding" and "keep the hub set small".

**D2 — the category rungs run inverted.** Rung 2's swap sits outside its null
check, so a category script sees `last_useitem` = *its own subject*. Every
category `[opheldu]` script in our tree except the selftest probe, and every
one in LostCity's, is written for the type-rung orientation
(`switch_obj(last_useitem) { case chisel : … }`). They are wrong as written and
have never been caught because D1 keeps them from running.

**D3 — nothing drives dispatch.** The only OPHELDU packet ever built outside a
real client is the four-item stanza at `torirs_server_world.c:30915`. Every skill
selftest calls its `~proc` directly, which is why D1 and D2 are invisible to
`--selftest`.

The reference has the same three; it does not notice because 2004 content has
one bolt and one gem tool. We extended the families and D1 became load-bearing.

---

## 2. The change

### E1 — decline: a script can hand the click back

A new command, `trigger_decline` (EXTRA band, next free id `11044` in
`gen_opcode_meta.py`'s `EXTRA_OPCODES`; arity `(0,0,0,0)`; no pointer
requirement). Semantics:

> "This script does not handle the current interaction. If the dispatch that
> ran it has further rungs, run the next one; if it does not, behave as if
> no script had bound — say `nothing_interesting_message` and return."

Implementation:

- `struct ToriRSServerPlayer` gains `int trigger_declined;` and
  `int trigger_dispatch_depth;` (or a single `srv->` field — dispatch is on
  the active player). The opcode handler
  (`torirs_server_scripts.c`, alongside `SS_OP_LAST_SUBOP`) sets `declined = 1`
  when `dispatch_depth > 0`, and when `dispatch_depth == 0` (called from a
  queue script, a proc, a debugproc — no chain to fall down) it says
  `nothing_interesting_message` immediately, so it degrades to exactly what
  `~displaymessage(^dm_default)` does today.
- Every chained resolver — `ToriRSServer_ScriptsRunOpheldu` and
  `run_trigger_impl` when `chain` is set (`run_trigger_impl`, `:1937-1956`, the type → category → `_`
  ladder for `[oploc*]`, `[opnpc*]`, `[opobj*]`, `[oplocu]`, `[opnpcu]`,
  `[opobju]`, `[opheld*]`) — becomes a loop over its rungs:

  ```
  depth++
  for each rung in order:
      script = GetByTriggerSpecific(rung)
      if (!script) continue
      set last_item/last_useitem for this rung          (E2)
      declined = 0
      r = run_trigger_script(...)
      if (r != RAN)            -> depth--, return r      (aborted / queue full: unchanged)
      if (!declined)           -> depth--, return RAN
      /* declined: fall through to the next rung */
  depth--
  return DECLINED
  ```

  `run_trigger_impl` stops using `SSVM_ProviderGetByTrigger` for the chained
  case and walks the three rungs itself with `…GetByTriggerSpecific`, so it
  can resume after a decline. `SSVM_ProviderGetByTrigger` stays for the
  unchained callers.
- A new result `TORIRSSERVER_TRIGGER_DECLINED`. Callers that today branch on
  `== NONE` to say `nothing_interesting_message` (`torirs_server_world.c:1622,
  1660, 1685, 1716, 1726, 4536, 5102, 5458`) treat `DECLINED` the same way
  for the message, but **not** for the C fallback: a declined script is still
  content having spoken, so `torirs_server_world.c`'s engine fallbacks
  (`fallback_stale_blockers` rows — generic doors, ladders) must not run.
  Concretely: message on `NONE || DECLINED`, fallback on `NONE` only.
- A decline is only honoured if the script *finished*. `run_or_park` returns 1
  for parked as well as finished; the resolver must check
  `srv->active_player->active_script != state` (not parked) before reading
  the flag — a script that opened a dialogue and then declined is a content
  bug, reported under `srv->verbose`, and treated as RAN.
- Nested dispatch: the flag is cleared before every run and read immediately
  after, so a script that fires another trigger internally cannot leak a
  decline outward. `depth` is what makes the opcode know whether a chain is
  above it; it is not used for anything else.

Content contract for the op: **call it before doing anything** — it is the
`case default` line and nothing else. The one file that has to change is
`general/scripts/misc/displaymessage.rs2`:

```
[proc,displaymessage](int $index)
if ($index = ^dm_default) {
    trigger_decline;     // engine says the message itself when nothing else binds
    return;
}
mes(enum(int, string, displaymessage_enum, $index));
```

That converts all 799 `[opheldu]` scripts, and every `[oploc*]`/`[opnpc*]`
default arm, to declining without touching them. Scripts that legitimately
want to *stop* with that message after doing something (rare; audit in S2)
call `mes` on the enum directly, or `~displaymessage` moves the decline into
a separate `~useon_pass` and only the `case default` sites migrate — the
former is fewer edits and the audit decides.

### E2 — one orientation for every rung

Stop toggling with `opheldu_swap`; set the pair explicitly per rung:

```
rung 1, rung 3 (bound to obj):     last_item = obj,    last_slot = slot,    last_useitem = useObj, last_useslot = useSlot
rung 2, rung 4 (bound to useObj):  last_item = useObj, last_slot = useSlot, last_useitem = obj,    last_useslot = slot
```

Invariant, stated once in the header comment: **`last_item` is always the
item the script is bound to; `last_useitem` is always the other one.** That
is what every LostCity category script and every one of ours except the probe
already assumes, so it makes them correct rather than breaking them. The
same-orientation rule is what lets E1's fall-through be safe: a rung-3 script
that declines and a rung-4 script that runs both see the pair the way their
author expected.

This is a deliberate divergence from the reference. It is safe because (a) the
inversion is observable only when neither item is type-bound, (b) the only
inversion-aware script in the tree is the selftest probe written to pin
reference behaviour, and (c) every future LostCity port is written for the
non-inverted orientation.

### E3 — a dispatch driver

Two pieces so E1/E2 and every content slice in the companion doc are
falsifiable:

- **`::useon <obj> <useobj>`** debug cheat (`handle_cheat`, `torirs_server_world.c:5826`)
  that finds the two objs in the backpack (adds them if absent, under a
  selftest/dev gate), builds the OPHELDU payload exactly as the stanza at
  `:30925-30934` does, and calls `ToriRSServer_WorldHandle`. Under `srv->verbose`
  it also prints the rung trace (E4).
- **`selftest_useon.rs2` grows a `[debugproc,useonrun]`** that fires the pairs
  each skill's plan names — string×shortbow, knife×logs, chisel×uncut_ruby,
  feather×dart tip, runite bolts×onyx tips, needle×leather, poison×bolts —
  in **both click orders**, and asserts the product landed. Skill selftests
  keep their direct `~proc` calls (they test the recipe); this one tests
  dispatch.

### E4 — rung trace under verbose

`torirsserver: opheldu bolts(x)/onyx tips(y): r1 miss, r2 miss, r3 [opheldu,_bolts]
declined, r4 [opheldu,_bolttips] ran`. One line per dispatch, `srv->verbose`
only. It is the difference between "nothing interesting" and knowing which
lane's binding ate the click, and it is what S0 of the companion doc otherwise
has to reconstruct by hand.

---

## 3. What this buys the content side

| Companion-doc rule | After E1+E2 |
|---|---|
| R1 one binding per pair, on the hub | Still the tidy way to write it, no longer a correctness rule — a redundant reverse bind that declines is harmless. |
| R3 family × family: ONE category binding, written inverted | Bind either or both; write both in the normal orientation. |
| R4 no defensive bindings — they swallow pairs | Gone. A binding that declines swallows nothing. |
| R5 hub × hub is order-dependent, both must route | Only one needs to; the other declines. |
| R9 name binding silently kills category | For `[oploc*]`/`[opnpc*]`/`[opobj*]` too: a name-bound script that declines falls to the category rung. `farming_crop`'s "weeds are deliberately NOT in here" caveat can go. |
| R2, R6, R7 (categories, router/label split, `tools/` home) | Unchanged — those are about coupling and dbrow-only growth, not dispatch. |

The tree's redundant reverse binds and dead category bindings become merely
untidy rather than hazardous; the companion doc's S1/S2/S4 cleanup still pays
in lines and coupling, but nothing waits on it.

---

## 4. Slices — as built

All five landed on 2026-08-16. Deltas from the plan above are marked **CHANGED**
and explained; the plan text in §1–§3 is what was designed, this is what exists.

| # | Slice | Where | Proof it works |
|---|---|---|---|
| **S0** | The driver: `::useon <a> <b>` cheat (`torirs_server_world.c`, `handle_cheat`) plus `selftest_useon()` / `selftest_useon_both()` and a five-pair stanza in the use-on selftest. **CHANGED**: no `[debugproc,useonrun]` — the assertions are C, alongside the existing rung probes, so `--selftest` covers them with nothing to remember to run. | `torirs_server_world.c` | A/B over 3+3 runs: baseline 6 failures, with the probe 8, and the delta is exactly the two real defects. Nothing else shifted. |
| **S1** | E2 orientation: `opheldu_orient()` replaces `opheldu_swap()`; the pair is re-stated per rung from the values `handle_opheldu` latched. | `torirs_server_scripts.c` | Mutation first: with E2 in and the old assertions untouched, `--selftest` went red on exactly the 5 checks that pinned the inversion, each printing the new orientation. Then the assertions and `selftest_useon.rs2`'s `[opheldu,_bones]` were flipped; green. |
| **S2** | E1 decline: `trigger_decline` (opcode 11044), `run_rung()`, rung loops in `ToriRSServer_ScriptsRunOpheldu` and in `run_trigger_impl`'s chained path. **CHANGED**: no `TORIRSSERVER_TRIGGER_DECLINED` enum value — see below. | `gen_opcode_meta.py`, `ss_opcode.h`, `ss_meta.gen.h`, `torirs_server_scripts.c`, `torirs_server.h` | Three new selftest checks: rung 1 declines → rung 2 answers (total 7); a rung that *answers* still ends the dispatch (4, not 7); declined with nothing below it runs the decliner and the engine fallback refuses. Poison-on-bronze-bolts went green with `[opheldu,weapon_poison]` untouched. |
| **S3** | E4 rung trace, under `srv->verbose`, for both resolvers. **ADDED**: `ToriRSServer_WorldSelftest` now reads `TORIRSSERVER_VERBOSE` like the three socket entry points — it was the one caller that could not be attached to with a client, and the trace was unreachable from it. | `torirs_server_scripts.c`, `torirs_server_world.c` | It is what found the third defect below, in one run. |
| **S4** | This document, `TOOL_TRIGGER_ORGANISATION.md` §1/§4, and the content edits. | docs, content | — |

### CHANGED: no `TORIRSSERVER_TRIGGER_DECLINED` enum value

The plan proposed a fourth `ToriRSServerTriggerResult` and said the ~20 sites that
branch on `== TORIRSSERVER_TRIGGER_NONE` would treat it as NONE for the message and
not for the fallback. Reading those sites, the enum turned out to be the more
dangerous of the two designs: every one of them would have had to be edited
correctly, and a missed one fails silently in whichever direction the author of
that line happened to write it.

What is built instead: an all-declined dispatch returns `TORIRSSERVER_TRIGGER_NONE`
— which is *true* from the player's side, where "content looked and said no" and
"content binds nothing" are the same event and get the same message — and sets
`srv->dispatch_declined`. `ToriRSServer_ScriptsFallback()` consumes that flag and
answers 0. That function's entire job is already "may C stand in for content
here?", it is the single gate every fallback passes through, and it already
distinguishes two other cases for the same reason. One edit, no call-site audit,
and the door still does not open because content declined to open it.

### CHANGED: `trigger_decline` ends the script

Not in the plan, and it is what makes the command safe to give to content. The
plan's contract was "call it before doing anything", enforced by review. But the
shape it replaces is *`if (...) { ...work... } ~displaymessage(^dm_default);`
with no `return`* — `bows.rs2` strings a bow and then falls through to the
message, and so do the other eleven unstrung bows, `[opheldu,weapon_poison]`,
and much of Herblore. Had `~displaymessage(^dm_default)` simply been redefined to
decline, as the plan's first option suggested, a successful stringing would have
declined and the next rung would have strung a second bow.

So `trigger_decline` sets `state->execution = SSVM_FINISHED`: a script that
declines does nothing further, by construction rather than by promise. And
`displaymessage.rs2` was left alone — declining is a per-site decision the
author makes, not a change of meaning for a proc 799 scripts already call.
`run_rung()` additionally refuses to honour a decline from a script that
*parked*, which `trigger_decline` now makes impossible but which is a cheap
check for a claim worth keeping true.

### The third defect, found by S3's trace

Fixing dispatch was not enough to make gem-tipped bolts work, and the trace said
why in one line: after `[opheldu,_bolt_ammo]` correctly declined, the rung below
it declined too, because its own guard `oc_category(last_useitem) = bolts` was
false.

**`bolts` was both a category (63) and an obj** — a dragon drop, used as
`obj_add(npc_coord, bolts, ...)` in three drop tables. `parse_case_value` and
expression symbols resolve with `SSC_SYM_UNKNOWN`, so a bare name takes whichever
namespace is found first; this one took the obj, and the guard compared category
63 against an obj id for ever. `switch_category` does not help — the
`switch_<type>` prefix is not used for symbol resolution.

`pack/category.pack`'s own header documents this trap and calls the previous one
"its fifth instance". This was the sixth, and it got past the check that note
prescribes (`grep -rn "=<name>$" configs/*.compack pack/*`) because an obj name
is never written `=bolts` — it is a `[bolts]` section header in `configs/all.obj`.
The header now says to grep the section headers too.

Category 63 is renamed `bolt_ammo`, which fixed a second live consumer nobody was
looking for: `combat_stats.rs2:214`'s quiver check
(`oc_category($obj) = bolts & (... weapon_crossbow ...)`) was equally false, so
**no crossbow was granting its ammo's rangebonus**. That one has nothing to do
with use-on dispatch and would not have been found by this work except through
the rename.

### Content edited

Deliberately small — the point of the engine change is that content stops having
to be arranged around the resolver:

- `selftest_useon.rs2` — `[opheldu,_bones]` flipped to the one orientation, and
  two new probe scripts (`bucket_empty` declines, `vial_empty` answers).
- `weapon_poison.rs2` — the five category bindings' `case default` arms now
  `trigger_decline`. Their header records that E2 also fixed them: they pass
  `last_slot` as the weapon slot, which under the old inverted category rungs was
  the *poison's* slot.
- `bolts.rs2` — the two `else` arms decline; the guard names `bolt_ammo`.
- `combat_stats.rs2`, `pack/category.pack` — the rename.

## 5. Considered and rejected

- **Pair-keyed triggers** (`[opheldu,knife,logs]`): needs a compiler change,
  a provider index by pair, and nothing ported from LostCity would use it.
  Decline gets the same decoupling with one opcode.
- **Reordering rungs** (obj type → obj category → useObj type → useObj
  category): makes target-side category bindings reachable, but every hub ×
  category-family pair then resolves to the *family's* script instead of the
  hub's, which is the opposite of the router pattern the content doc wants.
- **Inferring decline from "the script printed the default message"**: the
  engine cannot tell `mes("Nothing interesting happens.")` from any other
  `mes`; the explicit opcode is one line and cannot be misread.
- **Returning `NONE` on all-declined** so C fallbacks run: turns "content
  looked and said no" into "content never looked", which is the exact
  confusion `fallback_stale_blockers` exists to prevent. Hence `DECLINED`.
- **Leaving the inversion (E2) as reference parity**: parity with a bug that
  parity's own content does not observe is not parity. Every LostCity
  category script is written for the non-inverted orientation.
