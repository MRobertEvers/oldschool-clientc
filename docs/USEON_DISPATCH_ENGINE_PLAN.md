# Use-on dispatch: the engine fix

Companion to [`TOOL_TRIGGER_ORGANISATION.md`](TOOL_TRIGGER_ORGANISATION.md),
which describes the content-side rules that today's engine forces on every
`[opheldu]` author. This doc is the plan for changing the engine so most of
those rules stop being necessary — so that a lane can bind the side it owns,
say "not mine" when it does not recognise the partner, and never have its
pair swallowed by another lane's binding.

Two changes, both in `src/net/mock/mock230_scripts.c`, one new opcode, and a
dispatch driver so any of it can be proved.

---

## 1. What the engine does today, and what is wrong with it

`mock230_scripts_run_opheldu` (`mock230_scripts.c:2519-2582`) is a faithful
port of `OpHeldUHandler.ts:94-113`:

```
rung 1  [opheldu,<obj type>]                     last_item = obj      last_useitem = useObj
rung 2  [opheldu,<useObj type>]      SWAP        last_item = useObj   last_useitem = obj
rung 3  [opheldu,_<obj category>]    (no swap)   last_item = useObj   last_useitem = obj     <- inverted
rung 4  [opheldu,_<useObj category>] SWAP        last_item = obj      last_useitem = useObj  <- inverted
none    "Nothing interesting happens."
```

`run_trigger_script` (`:1791`) runs the first hit and returns
`MOCK230_TRIGGER_RAN`; the caller (`mock230_world.c:5099`) says
`nothing_interesting_message` only on `MOCK230_TRIGGER_NONE`.

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
real client is the four-item stanza at `mock230_world.c:30915`. Every skill
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

- `struct Mock230Player` gains `int trigger_declined;` and
  `int trigger_dispatch_depth;` (or a single `srv->` field — dispatch is on
  the active player). The opcode handler
  (`mock230_scripts.c`, alongside `SS_OP_LAST_SUBOP`) sets `declined = 1`
  when `dispatch_depth > 0`, and when `dispatch_depth == 0` (called from a
  queue script, a proc, a debugproc — no chain to fall down) it says
  `nothing_interesting_message` immediately, so it degrades to exactly what
  `~displaymessage(^dm_default)` does today.
- Every chained resolver — `mock230_scripts_run_opheldu` and
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
- A new result `MOCK230_TRIGGER_DECLINED`. Callers that today branch on
  `== NONE` to say `nothing_interesting_message` (`mock230_world.c:1622,
  1660, 1685, 1716, 1726, 4536, 5102, 5458`) treat `DECLINED` the same way
  for the message, but **not** for the C fallback: a declined script is still
  content having spoken, so `mock230_world.c`'s engine fallbacks
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

- **`::useon <obj> <useobj>`** debug cheat (`handle_cheat`, `mock230_world.c:5826`)
  that finds the two objs in the backpack (adds them if absent, under a
  selftest/dev gate), builds the OPHELDU payload exactly as the stanza at
  `:30925-30934` does, and calls `mock230_world_handle`. Under `srv->verbose`
  it also prints the rung trace (E4).
- **`selftest_useon.rs2` grows a `[debugproc,useonrun]`** that fires the pairs
  each skill's plan names — string×shortbow, knife×logs, chisel×uncut_ruby,
  feather×dart tip, runite bolts×onyx tips, needle×leather, poison×bolts —
  in **both click orders**, and asserts the product landed. Skill selftests
  keep their direct `~proc` calls (they test the recipe); this one tests
  dispatch.

### E4 — rung trace under verbose

`mock230: opheldu bolts(x)/onyx tips(y): r1 miss, r2 miss, r3 [opheldu,_bolts]
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

## 4. Slices

| # | Slice | Files | Proof |
|---|---|---|---|
| **S0** | E3: `::useon` cheat + `[debugproc,useonrun]` with the seven pairs, both orders. Land it against today's engine so the defects are on record. | `mock230_world.c` (cheat table), `selftest_useon.rs2` | `useonrun` red on runite bolts × onyx tips both orders; green on string × shortbow both orders. |
| **S1** | E2 orientation. Flip the C stanza (`mock230_world.c:30962-31000`, "inverted, as the reference leaves it") and `selftest_useon.rs2 [opheldu,_bones]` to the new invariant; update the rung comments in `mock230_scripts_run_opheldu` (`:2531-2563`) and `selftest_useon.rs2:44-60` — they currently document the inversion as deliberate. | `mock230_scripts.c`, `mock230_world.c`, `selftest_useon.rs2` | Mutate first (`verify-blocker-and-failing-test`): with E2 in and the stanza *not* flipped, `--selftest` must go red on exactly the four "inverted" checks. Then flip; green. `useonrun`: bolts-on-tips still red (D1), tips-on-bolts still red. |
| **S2** | E1 decline: opcode in `gen_opcode_meta.py` (+ regenerate `ss_opcode.h`/`ss_meta.gen.h`), handler, `trigger_declined` field, loop in `run_opheldu`, loop in `run_trigger_impl` chained path, `MOCK230_TRIGGER_DECLINED`, the eight `nothing_interesting_message` call sites. Audit content for `~displaymessage(^dm_default)` reached *after* a state change (grep for it not at a `case default` / not the last statement; expect a handful). Then the one-file `displaymessage.rs2` change. | `src/serverscript/gen_opcode_meta.py`, `ss_opcode.h`, `ss_meta.gen.h`, `mock230_scripts.c`, `mock230_world.c`, `displaymessage.rs2` | `useonrun` fully green — runite bolts × onyx tips both orders, with `weapon_poison.rs2` **untouched**. `--selftest` unchanged. Mutation: comment out the fall-through `continue` → bolts probe red again. |
| **S3** | E4 rung trace. | `mock230_scripts.c` | Verbose run of the bolts pair prints `r3 [opheldu,_bolts] declined, r4 [opheldu,_bolttips] ran`. |
| **S4** | Docs + memory: rewrite `TOOL_TRIGGER_ORGANISATION.md` §1 and §4 per §3 above; retire `name-binding-silently-kills-category` and the R3 "write inverted" rule; note the divergence from reference in `PORTING_GUIDE.md`'s engine-vs-content section. | docs | — |

S1 before S2 on purpose: with orientation fixed first, the moment decline
lands, every category script in the tree is *correct and reachable*
simultaneously, and `useonrun` measures exactly that.

---

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
