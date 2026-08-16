# Bank PIN (`bankpin_keypad` 213, `bankpin_settings` 14)

> ## ⚠ Read this before anything else
>
> **This document used to be a spec, and its §3 was wrong in a way that would
> have corrupted live player state.** It claimed, as a measured fact, "confirmed
> zero PIN entries in `pack/varbit.pack` / `pack/varp.pack`", and concluded the
> PIN could be stored greenfield.
>
> **Both of those files do not exist.** There is no `pack/varp.pack` and no
> `pack/varbit.pack` in this tree — `pack/` holds numbered cache archives, and
> the name tables are `configs/all.varp.compack` / `configs/all.varbit.compack`.
> A check that reads nothing cannot confirm anything, and this one reported
> "zero entries" because it found zero *files*.
>
> **The real tables say the opposite.** `all.varp.compack:606` is
> `563=bankpin_2`, and **varp 563 is already fully occupied by the Grand
> Exchange** — `ge_newoffer_quantity` owns bits 0-30 and `ge_newoffer_type`
> owns bit 31. Anyone who had followed this doc would have written a bank PIN
> straight over a player's live GE offer, in the one place where the damage is
> silent: a varbit write and a whole-varp write to the same id both "work".
>
> The name is a vestigial 2007 label over storage that was reassigned; so is
> `bankpin_anticracker` (varbit 1011, whose base is `tai_bwo_cleanup`). **At
> rev 239, neither "bankpin" name is a bankpin.** §1.1 has the measurement and
> the three fresh varps that landed instead. This is also now guarded in the
> compiler — see §1.1 — so the mistake cannot be made silently a second time.
>
> The general lesson, worth more than the specific ids: **a grep that returns
> nothing is evidence about the grep until you have proved the file it read
> exists.** `PORTING_GUIDE.md` §7 says re-measure rather than trust prose; this
> is the case where the prose had itself never measured.

> **LANDED 2026-08-02.** The keypad works end to end: entering a PIN sets it,
> the bank asks for it on the next login, a wrong guess is refused, three wrong
> guesses end the visit, and the PIN survives a logout. Verified in the real
> client headlessly (§6) and by `mock230 --selftest` (§7).
>
> The triage that preceded this was **wrong on both of its headline claims**,
> and one of them is the storage claim above. §1 is the full correction; the
> original §3 table is kept at the bottom (§9) with the wrong rows struck, so
> the record of what was believed is still readable.
>
> **This is no longer a spec.** Everything below describes what is in the tree,
> what was measured, and what was deliberately not built (§8) — not what
> somebody should do.

Layered on the already-landed bank (`docs/mock230_bank.md`) — this doc covers
only what the PIN itself adds.

---

## 1. Two corrections, both re-measured

### 1.1 "Confirmed zero PIN entries in `pack/varbit.pack`/`pack/varp.pack`"

Wrong three times over.

* **Those files do not exist.** `pack/` holds numbered cache archives
  (`3_interfaces.pack`, `12_clientscripts.pack`, …). The name tables are
  `configs/all.varp.compack` and `configs/all.varbit.compack`.
* **There are PIN entries.** `configs/all.varp.compack:606` is
  `563=bankpin_2`; `configs/all.varbit.compack:1012` is
  `1011=bankpin_anticracker`.
* **varp 563 is fully occupied**, and by something live. `configs/all.varbit`:

  ```
  [ge_newoffer_quantity]  basevar=bankpin_2  startbit=0   endbit=30
  [ge_newoffer_type]      basevar=bankpin_2  startbit=31  endbit=31
  ```

  All 32 bits. A PIN written there destroys a Grand Exchange offer.
  `bankpin_anticracker` is no better: its own base is `tai_bwo_cleanup`, bits
  30-31, so it is not a bankpin varp either. **Both "bankpin" names at rev 239
  are vestigial 2007 labels over storage that has since been reassigned.**

**What landed instead:** three fresh varps. `[namespace:varp]` is
`ids = server`, so `tools/ss_allocate.py` gives each an id one past the cache's
highest and nothing is copied from anywhere.

| varp | scope | why |
|---|---|---|
| `bankpin_code` | perm | the PIN, 0000-9999 |
| `bankpin_set` | perm | whether there is one — a separate flag because **0000 is a legal PIN**, and the alternative (store code+1, 0 means none) is an encoding that reads correctly until it does not |
| `bankpin_verified` | **temp** | answered this session. Not saved, deliberately: a PIN asked once ever is not a PIN |

All three are `transmit=no`. The client is never told the correct PIN — it only
ever collects a guess — and that is the entire security property of the screen.

**This mistake is now guarded twice.** `sscompile` refuses a whole-varp write to
a carrier and names the collateral; pointing the PIN at `bankpin_2` produces

```
`%bankpin_2` is varp 563, which 2 varbit(s) are packed into — writing it whole
destroys them (ge_newoffer_quantity (0..30), ge_newoffer_type (31..31)).
```

and `mock230 --selftest` asserts at runtime that no PIN varp carries a varbit,
for the case where somebody reaches for `wholewrite=allow`.

### 1.2 "READY, content only, no blocker"

Wrong. Three engine gaps stood between the shipped tree and a submittable PIN,
and none of them is visible from the content side. §2, §3 and §4.

Two smaller triage corrections, both confirmed: §1 of the old doc called the
digit shuffle Fisher-Yates — it is 20 rounds of `random(9)` swapping against
index 9, which is not the same thing; and `bankpin_settings.if` does carry seven
`onload=` rows (chrome only, so §2's "zero `onop=`" substance stands).

---

## 2. Client gap 1: CS2 3104 `RESUME_COUNTDIALOG` had no handler

The keypad's fourth digit runs `resume_countdialog(tostring($int2))`
(`scripts/script_685.cs2:9`). Opcode 3104 was declared `CS2_HANDLER_HOST` in
`cs2_opcode_meta.c` with **no dispatch anywhere**, and its generated stack row
was `{ 0, 0, 0, 0, 0 }` — `known = 0`. That is not a silent no-op: it reached
`CS2VM2_Op_StackMetaStub`'s assert, so submitting a PIN aborted the client.

Landed: a real stack shape (**pops one string, pushes nothing**), a
`CS2VM_HOST_REQUEST_RESUME_COUNTDIALOG` kind, and a handler that queues the send
through the same outbound queue `docheat` already uses — the CS2 host has no
socket, so `app.c` is where it becomes `RESUME_P_COUNTDIALOG`.

The argument is a **string** because the callers push one; the wire packet
carries an int and the conversion is the App's, exactly as it already is for the
chatbox's own "Enter amount" path (`app.c`, `(int)atol(dialog_copy)`).

Check: `make -C src test-cs2-resume-countdialog`.

---

## 3. Client gap 2: `random(max)` popped nothing and returned nothing bounded

Found by reading the keypad on screen, not by inspection.

`CS2VM2_Op_Random` pushed a raw `rand()` and **popped no argument**. The
generated table agreed with it — `[4004] = { 0, 0, 1, 0, 1 }` — because 4004 was
the one opcode of the `RANDOM`/`RANDOMINC` pair with no stack doc comment in
`cs2_opcode.h`, so the generator's name heuristic supplied "no arguments".

The reference is explicit: `[command,random](int $num)(int)`, "within the range
of 0 to $num - 1" (LostCity `content/scripts/engine.rs2:958`).

Both halves failed quietly. A leftover int on the stack is tolerated at script
end, and an unbounded result is almost always used as an array index, where
`CS2VM2_Op_PushArrayInt` answers 0 for anything out of range. The keypad is
where it surfaced: `bankpin_keypad_set` shuffles its ten digit buttons with
`random(9)` twenty times, every swap read past the end of the array, and the
keypad drew

```
      before                    after
    0  1  2  3               4  6  2  0
    4  5  6                  1  3  9
    7  9  _                  8  5  7
```

— plain ascending order with the tenth button blank. The screen's whole
anti-shoulder-surfing property was inert, and nothing on screen said so.

Landed: the missing doc comment (so the generator derives the row rather than
guessing it), and a handler that pops `max` and returns `rand() % max`, 0 for
`max <= 0`.

Check: `make -C src test-cs2-math` — the `RANDOM pops its argument` case puts a
sentinel *under* the argument, because a test that only reads the top of the
stack cannot tell a popping op from a non-popping one.

---

## 4. Server gap: `p_countdialog` opens a prompt this screen must not have

`p_countdialog` (2071) is two things at once, in the reference and here: it
writes `PCountDialog` and it sets `ScriptState.COUNTDIALOG`
(`PlayerOps.ts:371-373`). At 2004 those are inseparable, because the chatbox
prompt is the only thing that can produce a number.

At rev 230 they are not. `resume_countdialog` is an ordinary CS2 opcode, so any
interface can answer a parked script — and the cache ships one that does.
Waiting for the keypad with `p_countdialog` would pop the "Enter amount" prompt
underneath it, and that prompt **echoes each digit as it is typed**. Over a
keypad whose entire purpose is that the digits are never typed, that is not a
cosmetic difference: it hands the PIN to a shoulder-surfer through the exact
channel the screen exists to close.

Landed: **`p_countdialog_noprompt`, opcode 11004** — the wait, without the
packet. Declared in `gen_opcode_meta.py`'s `EXTRA_OPCODES` alongside
11000-11003, in the same reserved band past the reference's highest opcode, and
carrying P_COUNTDIALOG's own `ProtectedActivePlayer` require mask through a new
`EXTRA_POINTERS` dict (`parse_pointers` reads the reference, and the reference
has no such opcode). `p_countdialog` is unchanged and keeps its two callers, the
bank's quantity prompt and the farming store's.

The reference offers no design here, and this doc says so rather than implying
one: there is no bank PIN anywhere in LostCity, engine or content — zero hits
(§10). This is `PORTING_GUIDE.md` §5.1: the client already implements the
feature and the server's job is to drive it.

---

## 5. What content owns

`server/scripts/interface_bankpin/`:

* **`scripts/bankpin.rs2`** — `~bankpin_ask` (open 213, fill the two strings the
  interface ships empty, park, return the number), `~bankpin_gate` (the
  three-attempt loop), `~bankpin_choose` (enter twice, compare, store),
  `~bankpin_remove`, and a `[debugproc,bankpin]`.
* **`configs/bankpin.varp`** — the three varps of §1.1.
* **`configs/bankpin.constant`** — every player-facing string, the attempt
  limit, and the two sentinels.

`interface_bank/scripts/bank.rs2` gained one line at the top of
`[proc,openbank]`: `if (~bankpin_gate = ^false) return;`. It is deliberately
*before* the interface opens — gating after the mount would show the contents of
the bank behind the keypad, which is the failure the feature is about.

**The two sentinels are the client's, and are read out of its own script.**
`bankpin_keypad_set` arms the two non-digit rows literally:

```
if_setonop("bankpin_otherbutton(12345, …)", $component2)   // "Exit"
if_setonop("bankpin_otherbutton(54321, …)", $component3)   // "I don't know it."
```

and `bankpin_otherbutton` (script_686) passes the number straight to
`resume_countdialog`. Both are outside 0000-9999 so they cannot collide with a
PIN. `^bankpin_cancelled` / `^bankpin_forgotten` write that down; testing
`> 9999` instead would make the two rows indistinguishable.

**The attempt counter is a script local, not a varp.** The gate script stays
parked across every question, so the count lives exactly as long as the visit
does. A varp would need resetting by somebody, and "somebody forgot to reset the
counter" is how a player is locked out of their own bank.

**Three is this world's number.** Nothing in the traced CS2 has any notion of
attempts — the keypad will accept guesses forever — so the limit is server
policy, and policy is content's. There is no reference to match.

### 5.1 Why the entry point is a `[debugproc]`

`bankpin_settings` (interface 14) is the screen that would normally set and
clear a PIN. This cache ships it with **no `onop=` on any component**, and
`grep -rln bankpin_settings scripts/` returns nothing. It has no entry point on
either side — a corpus gap, not something to paper over by inventing button
bindings the cache does not have.

So `::bankpin set | remove | ask | echo | forget` stands in, and it drives the
exact procs the settings screen would. Cheats are content in the reference too
(its whole `content/scripts/_test/scripts/cheats/` tree). The `echo` mode says
back the number the keypad just sent; it reads nothing and reveals nothing the
person at the keyboard did not just type, and it is what §6 uses.

---

## 6. Verified in the client, headlessly

`SDL_VIDEODRIVER=dummy … src/torirs --manifest manifest_osrs230_embed.ini`, with
`TORIRS_NET_CHEAT` for the entry point, `TORIRS_SIM_CLICK_AT` for the digits and
`TORIRS_BMP_SERIES` to read the shuffled layout before each click.

| what was driven | what the screen said |
|---|---|
| `::bankpin echo`, four clicks whose digits the frame dumps show as **6, 8, 0, 4** | "The keypad sent **6804**." |
| `::bankpin set`, two entries reading **4622** and **7605** | "Those two PINs did not match. Nothing has changed." |
| `::bankpin set`, both entries **4622** | "Your bank PIN has been set." — and `saves/testbl.ini` gained `5727 = 4622` / `5728 = 1`, with 5729 (`bankpin_verified`, temp) correctly absent |
| relogin, `::bankpin ask`, the right PIN | "PIN accepted." |
| relogin, `::bankpin ask`, three wrong guesses | "That is not the right PIN." ×2, then "Too many wrong guesses. Try again later." |

The `6804` run is the one that proves the chain rather than merely exercising
it: the four digits were read off the rendered buttons *after* the shuffle, so
the number could only arrive intact if the shuffle, the accumulator, the packet,
the parked script and the compare all agreed.

`rand()` is never seeded in this client, so a headless run is reproducible — the
same click sequence gives the same layouts. That is what made "enter the same
PIN twice" drivable at all, and it is worth knowing before writing another
keypad test.

---

## 7. The permanent checks, and the mutations that prove them

| check | what it pins | mutation → red |
|---|---|---|
| `mock230 --selftest`, **"the bank PIN"** | the whole gate through content's own procs: no PIN → straight in; PIN → keypad and a parked script; wrong → refused and re-asked; right → verified and the bank opens; Exit → neither; three wrong → over. Plus: the varps carry no varbits, `bankpin_code`/`_set` are perm, `bankpin_verified` is not, and none is transmitted | send the prompt from `P_COUNTDIALOG_NOPROMPT` ⇒ *"the keypad must not also open the chatbox amount prompt"* ✅ · declare the PIN on `bankpin_2` ⇒ **sscompile refuses outright**, naming both GE varbits ✅ (and with `wholewrite=allow` to get past that, six selftest assertions fail) |
| `make -C src test-cs2-resume-countdialog` (**new**) | 3104 pops exactly one **string** (sentinel underneath), the text reaches the host verbatim, nothing lands on the int stack, and both sentinels pass through unfiltered | pop an int instead, as the wire packet's shape suggests ⇒ **6** assertions fail ✅ |
| `make -C src test-cs2-math` (extended) | `random` pops its argument and is bounded, exclusive of `max`; `randominc` is inclusive; neither divides by zero | restore `return CS2VM2_PushInt(vm, rand());` ⇒ **5** assertions fail ✅ |

Every id in the selftest is resolved by **name** —
`mock230_world_varp("bankpin_code")`,
`mock230_content_symbol(MOCK230_PACK_INTERFACE, "bankpin_keypad")` — because two
of the three varps are allocated by `ss_allocate.py` and will move the moment
the tree grows another one.

---

## 8. Still not built

* **`bankpin_settings` (interface 14).** No entry point in this corpus, on
  either side (§5.1). Whether its "enter the new PIN twice" flow chains two
  keypad opens is still inferred, not demonstrated — `~bankpin_choose` does
  chain two, which is the obvious reading, but no script in the cache shows it.
* **The recovery delay behind "I don't know it."** The sentinel arrives and the
  gate answers with a message; there is no timer, no pending-removal state and
  no reference for either.
* **Lockout beyond the visit.** Three wrong guesses end the *visit*; walking
  back to the booth starts over. A persistent lockout needs state and a policy
  neither the CS2 nor the reference supplies.
* **Anything reading `bankpin_anticracker`.** It is a varbit of
  `tai_bwo_cleanup` and has nothing to do with this (§1.1).

---

## 9. The original §3 table, corrected in place

| need | triage said | actually |
|---|---|---|
| Real PIN storage | greenfield — ~~"confirmed zero PIN entries in `pack/varbit.pack`/`pack/varp.pack`"~~ | **the files do not exist and the claim is false**; varp 563 is `bankpin_2` and the GE owns all 32 of its bits. Landed on three fresh server-allocated varps (§1.1) |
| Transport for a guess | ~~"already reusable — `P_COUNTDIALOG`/`last_int`, zero new engine work"~~ | **three engine gaps**: CS2 3104 had no handler (§2), `random` was broken (§3), and `p_countdialog` opens a prompt this screen must not have (§4) |
| Attempt lockout | greenfield, and undesigned even in the CS2 | confirmed. Three per visit, a script local, content's policy (§5) |
| "Forgot PIN" / recovery delay | greenfield | confirmed, and still not built (§8) |
| Digit-order shuffle | ~~"already works, pure client, nothing to build"~~ | **it did not work.** `random()` returned an unbounded value and popped nothing, so the shuffle was the identity and the tenth button was blank (§3) |
| Settings state machine | greenfield, UI groups exist, no wiring script found | confirmed (§5.1) |

---

## 10. LostCity precedent — none, confirmed

Zero hits for "bankpin"/"bank pin" anywhere in the reference, content or engine.
Rev 254 predates the feature. There is no `.rs2` proc to port for storage,
compare or lockout — the same shape as clan chat
(`docs/PORTING_GUIDE.md` §5.2, "greenfield on both sides"), except that here the
client CS2 surface is already complete and only the server-facing content script
and its state were missing.
