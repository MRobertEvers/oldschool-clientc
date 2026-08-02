# Bank PIN (`bankpin_keypad` 213, `bankpin_settings` 14): what the server owes

> **NOT BUILT — triaged 2026-08-02: READY, content only, no blocker.** The
> keypad self-bootstraps (`bankpin_keypad.if:14` carries `onload=i:333,…` with
> all 18 component uids baked in) and `P_COUNTDIALOG` is implemented and
> covered, so the server owes only "open 213 → `p_countdialog` → compare".
> **§3's "confirmed zero PIN entries in `pack/varbit.pack`/`pack/varp.pack`" is
> wrong twice**: neither file exists (the name tables are
> `configs/all.*.compack`), and varp **563 `bankpin_2`** and varbit **1011
> `bankpin_anticracker`** are both real — and 563 is already occupied by
> `ge_newoffer_quantity` (varbit 4396, bits 0-30) and `ge_newoffer_type`
> (4397, bit 31), so author a fresh varp rather than reusing it. Two smaller
> ones: §1's shuffle is 20 rounds of `random(9)` swapping against index 9, not
> Fisher-Yates, and `bankpin_settings.if` does carry 7 `onload=` rows (chrome
> only — §2's "zero `onop=`" substance stands).

> Layered on the already-landed bank (`docs/mock230_bank.md`) — this doc
> covers only what the PIN itself adds. Short, since it's a small feature
> on infrastructure that already exists.

## 0. Status at a glance

Fully greenfield, confirmed — and this is already noted in
`docs/mock230_bank.md:311`'s own "not implemented" table ("Equipment-bonus
panel, potion store, deposit box, bank pin — Separate interfaces with their
own containers"). No PIN varp/varbit, no lockout state, no persistence hook
exists anywhere. Also confirmed: **no LostCity precedent at all** — zero
hits for "bankpin"/"bank pin" anywhere in the reference tree.

## 1. The keypad — comparison is server-side, confirmed

`bankpin_keypad_init` (`script_333.cs2`) → `bankpin_keypad_set`
(`script_653.cs2`): tracks digit-index (0-3) and an accumulator, masking
entered digits as `*`. On submit (index < 0) it deletes all ten digit
buttons — inert once a full guess is sent — and **Fisher-Yates shuffles the
10 button slots using the plain client-VM `random()` op** (opcode 4004,
confirmed, not a host call) before rebuilding them.

**The crux, `bankpin_button_op` (`script_685.cs2`)**: accumulates the
4-digit guess purely arithmetically client-side (`1000×`/`100×`/`10×`/`1×`
per digit position), and **only on the 4th digit** does
`resume_countdialog(tostring(accumulator))` fire (confirmed at
`script_685.cs2:9`) — sending the single fully-assembled number once. This
is the exact same generic mechanism the bank's own quantity prompt already
uses: `P_COUNTDIALOG`/`last_int` (confirmed, `mock230.h:1263`,
`mock230_scripts_resume_countdialog` at `mock230.h:2429`).

**Confirmed server-side, not a client bug**: the client CS2 never reads a
stored/correct PIN from anywhere — no varp/varc read, no comparison op
exists in `script_653`/`script_679`/`script_685`. It only tracks which
shuffled slot was clicked and assembles what the player typed; the real PIN
is compared wherever the content script blocks on `p_countdialog` reading
`last_int`. The digit-shuffle is a per-render, client-local RNG re-shuffle
(re-run after every digit, not server-seeded) — it defeats shoulder-surfing
of click *positions* specifically because the number that reaches the
server is the digit value the player meant, never the position, and the
server is the only party that ever sees the true PIN.

A "Cancel"/"I don't know it" pair send distinguishable sentinels
(`12345`/`54321`) through the same channel, outside the `0000..9999` range.

## 2. Settings screens — corpus gap

`bankpin_settings.if` is a state machine over {no PIN, has PIN, PIN change
pending} — three mutually-exclusive, hidden-by-default button groups
("Set a PIN"/"Change your PIN"/"Delete your PIN"/recovery-delay/logout
behaviour) plus a generic yes/no warning sub-screen. **No script in this
corpus wires any of it** — zero `onop=` fields in the `.if`, and
`grep -rln "bankpin_settings" scripts/` returns nothing. The two-entry
"set new PIN, re-enter to confirm" flow presumably reuses the keypad twice
in sequence, but no script demonstrates the chaining — flagged, not
guessed.

## 3. Server obligations

| need | status |
|---|---|
| Real PIN storage (likely 4×varbit or a packed varp/dbrow field, `scope=perm`) | **greenfield** — confirmed zero PIN entries in `pack/varbit.pack`/`pack/varp.pack` |
| Transport for a guess | **already reusable** — `P_COUNTDIALOG`/`last_int`, zero new engine work |
| Attempt-lockout/cooldown | **greenfield, and undesigned even in the CS2** — no lockout/attempt/tries logic anywhere in the traced scripts |
| "Forgot PIN"/recovery-delay flow | **greenfield** — UI and the `54321` sentinel exist, no driving script found |
| Digit-order shuffle | **already works**, pure client, nothing to build |
| Settings state machine + two-entry confirm | **greenfield** — UI groups exist, no wiring script found |

## 4. LostCity precedent — none, confirmed

Zero hits anywhere in the reference tree, content or engine. There is no
`.rs2` proc to port for storage/compare/lockout — that logic has to be
authored fresh as content, same shape as clan chat
(`docs/PORTING_GUIDE.md` §5.2, "greenfield on both sides"), except here the
client CS2 surface is already fully present and only the server-facing
content script and its state are missing.

## 5. What this doc does not cover

- `bankpin_settings`'s entry-point clientscript and its op-wiring — missing
  from this corpus entirely, not merely untraced.
- Whether the PIN-setup two-entry confirm genuinely chains two keypad opens
  or does something else — inferred, not demonstrated anywhere in the
  corpus.
