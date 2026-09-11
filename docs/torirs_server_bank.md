# The bank

A working bank against the real rev-230 client: open it at a booth or a banker,
withdraw and deposit by right-click, toggle notes/insert/quantity, and watch the
container come back down the wire.

```
make -C src torirsserver-bank && src/build/bank_torirsserver &
src/torirs --manifest manifests/manifest_osrs230_bank.ini --user testc --pass test
```

Then `::bank` in the chat box, or walk to a bank booth — the Lumbridge castle
ones are two staircases up from the spawn tile.

| file | contents |
| --- | --- |
| `src/torirsserver/torirs_server_bank.c` | the container, the settings, the arithmetic, the wire |
| `OSRS-Content/osrs239-content/server/scripts/interface_bank/` | the ported LostCity content |
| `OSRS-Content/osrs239-content/pack/varbit.pack` | the bank's varbit ids (new namespace) |
| `src/ui/uitree_obj_cell.c` | the one client-side change this needed (§6) |

Tests: `make -C src test-ToriRSServer` covers varbit packing, deposit/withdraw,
notes, the op ladder and the open burst. `make -C src test-torirsserver-bank` is the
same suite against this binary.

---

## 1. What a bank is, at rev 230

Almost nothing, server-side. **The client already knows how to draw a bank** —
`bankmain_init` (clientscript 274) builds 1,220 item cells, a tab strip, a
scrollbar and the settings row out of nothing but a container and a dozen
varbits. So the server's whole job is:

1. mount two interfaces,
2. push the settings,
3. send container 95,
4. and act on the clicks that come back.

That is the shape of the port. LostCity's bank has to describe its own interface
because its client has no CS2; this one describes only state.

| what | id | how it was established |
| --- | --- | --- |
| bank interface | `12` | `tools/dump_interface cache.osrs239 --dat2 --iface 12` |
| bank side panel | `15` | same |
| bank container | `95` | `inv-names.tsv`, and the client's own `INV_MANAGER_CONTAINER_BANK` |
| bank capacity | `1410` | config group 5 (inv), read at startup — **not** written down. It read 1220 until 2026-08-02: `TORIRSSERVER_BANK_SLOTS` was applied as an upper *clamp* under a comment describing a no-cache fallback, so every bank was 190 slots short of the container the client walks. See `torirs_server_containers.md` §5 |
| mount slot, main | `161:16` | toplevel_osrs_stretch `mainmodal` |
| mount slot, side | `161:74` | toplevel_osrs_stretch `sidemodal` |

Every one of those, and every component inside interface 12, is a **name** in
the content tree: `bankmain`, `bankside`, `bankmain_items`, `gameframe_mainmodal`
in `content/pack/`, resolved once at boot into `torirs_server_ids.h`. `torirs_server_bank.h`
contains no ids at all — only the array ceilings and the pending-prompt states.

They were read out of `dump_interface` against this cache and **not** borrowed
from another server's table. The bank's child numbering moved between OldSchool
revisions: a constant taken from a newer cache — xrsps's `BankMainChild.ITEMS =
12`, for instance — names a `line` here, not the item container. `test-ToriRSServer`
pins each resolved id to the number it was verified at, so a pack regenerated
from a newer gameval table fails there rather than in the panel.

---

## 2. The settings are varbits, and that is the whole difficulty

LostCity's bank writes `%bankcert` and `%bankinsert`, player variables it
invented. At rev 230 every one of those is a **varbit**: a named bit range
inside a varplayer the client already owns.

```
bank_withdrawnotes   3958   varp  115  bit  0
bank_currenttab      4150   varp  115  bits 4..7
bank_insertmode      3959   varp  304  bit  0
bank_requestedquantity 3960 varp  304  bits 1..31
bank_quantity_type   6590   varp 1666  bits 2..4
bank_tab_1..9        4171+  varps 867, 1052, 1053, 1793, 3750
```

The varbit **ids** are names too — `varbit.pack`, imported from OpenRune's
`varbits` group — and the nine tab counters are a keyed table rather than
`bank_tab_1 + i`: `interface_bank/configs/bank.enum` maps tab index to varbit,
because their being consecutive is a fact about one cache. The quantity modes
the interface encodes into `bank_quantity_type` are `^bank_qty_*` in
`bank.constant`, beside it.

Two consequences, and both are load-bearing:

- **Writing one as a whole varp destroys the others in it.** Withdraw-as-note
  and the current tab share varp 115. There is no symptom until the panel is
  looked at, so `ToriRSServer_BankSetVarbit` does a read-modify-write of the bit
  range and nothing writes those varps directly.
- **The ranges come from the cache**, config group 14, decoded at startup by
  `ToriRSServer_BankLoad`. They are not in `varbit.pack` and not in the header. A
  second copy of a cache fact is the failure mode this content tree exists to
  avoid, and the ranges are the client's, not the server's, so they cannot be
  allowed to drift.

`TORIRSSERVER_VARP_COUNT` went from 256 to 5000 for this. None of those ids were
chosen here; the tab counters alone are spread over five varps up to 3750, and
the side panel's slot locks are varp 4611.

`.varp` configs matter too. The mock only transmits a varp a config declares
with `transmit=yes` (undeclared means server-only, which is right for its own
counters and wrong for these), so
`content/scripts/interface_bank/configs/bank.varp` declares all nine.

### The one that is not a setting

`bank_side_slot_ignoreinvlocks` (5450) is pushed to 1 on open. The side panel
draws a padlock over every inventory slot unless told to ignore the lock varbit,
which the mock never sets — so without this the bank opens with a locked-looking
inventory.

---

## 3. Notes come out of the obj record, both directions

A bank holds one stack of an item, never two, so a deposit un-notes and a
withdraw re-notes on request. Both directions read straight out of the objtype:

```
1511 Logs      noted 1512 / template  -1     <- an item, pointing forward
1512 "null"    noted 1511 / template 799     <- its note, pointing back
```

Opcode 97 is "the record on the other side of the link" and opcode 98 says which
side this record is on. So `oc_cert` and `oc_uncert` are one field lookup each
and neither needs an index — worth stating, because the pair reads like one
field and a flag until you print a few.

Two things about note records bit during this work, both silent:

- **Every note is named `null`.** A note takes its display name from the item it
  stands for. `ToriRSServer_ObjInfo` used to gate its "do I know this obj" test on
  the name, so all ~8,000 notes reported as unknown; the gate is now a `known`
  flag and the name is borrowed from the linked item at load.
- **Every note records `stacking_behaviour = 0`.** The reference client tests
  `stackable == 1 || notedTemplate != -1`. Without the second half, a stack of
  20 noted swordfish takes 20 backpack slots — which is precisely the thing
  notes exist to avoid.

---

## 4. Content owns the sparse op ladder

LostCity binds five handlers, `[inv_button1..5,bank_main:inv]`, one per withdraw
amount. Its interface always offers the same five rows in the same order.

rev 230's bank builds its rows in CS2 (`script_5272` / Kronos `bankmain_drawitem`)
with **fixed sparse indices**. Omitting the row that would duplicate the current
default leaves a hole in the menu — it does not renumber later ops:

```
op 1                    Withdraw-<default>          always
op 2  if quantity != 1  Withdraw-1
op 3  if quantity != 5  Withdraw-5
op 4  if quantity != 10 Withdraw-10
op 5  if quantity != X  Withdraw-<X>                and only when X is set
op 6                    Withdraw-X                  always (the prompt)
op 7  if quantity != All Withdraw-All
op 8                    Withdraw-All-but-1          always
op 10                   Examine
```

Content binds `[if_button1..8,bankmain:items]` and the side-panel deposit map in
[`bank.rs2`](../OSRS-Content/osrs239-content/server/scripts/interface_bank/scripts/bank.rs2) /
[`bank_deposit.rs2`](../OSRS-Content/osrs239-content/server/scripts/interface_bank/scripts/bank_deposit.rs2).
Amounts come from `%bank_quantity_type` / `%bank_requestedquantity` and
`^bank_qty_*`. Withdraw-X uses `p_countdialog` and writes last-X into
`%bank_requestedquantity`. The engine keeps open/close, `IF_SETEVENTS`,
`UPDATE_INV`, and `inv_moveitem_cert`/`_uncert` → `ToriRSServer_BankWithdraw`/`deposit`.

`UITREE_MENU_OPTION_SLOTS` is **10**. Armed component item ops emit
`IF_BUTTON1..10` (component + sub=slot). Settings bind the **armed** comps
(`swap_insert`, `note`, `quantity*`, `depositinv`, `depositworn`), not the
graphic/text children beside them.

---

## 5. Two packets that did not exist

- **`P_COUNTDIALOG`** — assigned opcode 128, zero-length. Modern OldSchool drives
  the "Enter amount" prompt through a clientscript rather than a packet, so
  there was no real opcode to transcribe. The client already had the handler
  (`RS_Chat.dialog_input`); nothing was reaching it. Same assignment convention
  as the `IF_SET*` family — see `docs/osrs230_mockserver.md` §3.5.
- **`VARP_LARGE` per value, not per call site.** A varp holding packed varbits is
  routinely wider than `VARP_SMALL`'s signed byte (the tab counters occupy bits
  0..25 of theirs), so the encoder is now chosen from the magnitude in the phase
  10 flush. Truncating would corrupt every bit above the eighth.

`UPDATE_INV_FULL`'s arena was sized at 8 KB, which was fine for a 28-slot
backpack and a 14-slot worn set. A full bank is 1,220 slots at up to 7 bytes
each; the buffer is now sized from the count. Only the used prefix goes out —
`UPDATE_INV_FULL` clears everything past the capacity it carries, so a bank
holding twelve objs is a twelve-slot packet.

---

## 6. The client change: whose ops does a cell offer?

Everything above was server work. One thing was not, and it is a single line.

`UITree_ObjCellForNode` resolved a CS2-created item cell's verbs from the cell's
**container**:

```c
out->ops_node_index = node->parent >= 0 ? node->parent : node_index;
```

which is right for the backpack and the worn tab — they set their ops once on
the static parent and every cell inherits them. The bank does not.
`bankmain_drawitem` calls `cc_setop` on the *child* it just drew, because the
rows differ per item and per setting. Reading only the parent gave a bank item
the ObjType's Drop/Use rows and none of its own:

```
row[1] "Examine @lre@ Coins"        <- the client's synthesised row
row[2] "Drop @lre@ Coins"           <- the ObjType's
row[3] "Use @lre@ Coins"
```

The child now wins when it has any ops, which is also what the reference does —
it reads the ops off whatever component the cursor is over. Same click
afterwards:

```
row[2] "Withdraw-All @lre@ Coins"
row[3] "Withdraw-X @lre@ Coins"
row[4] "Withdraw-10 @lre@ Coins"
row[5] "Withdraw-5 @lre@ Coins"
row[6] "Withdraw-1 @lre@ Coins"
```

`obj_ops` falls out of the same test, so the ObjType rows disappear on their own
rather than needing a second rule.

This closes follow-up 1 of `docs/osrs230_mockserver.md` §6 — inventory item ops
through the rev-230 UI — which had been described there as unreachable.

### The settings buttons needed arming, not code

The Item/Note, Swap/Insert and quantity buttons each have a CS2 hook of their
own (`bankmain_itemnote_op` and friends) that flips the varbit **client-side and
sends nothing**. Pressing Note visibly worked and then a withdraw came out as an
item, because the server's copy had not moved.

The client already handles this: a numbered op on an IF3 widget sends
`IF_BUTTON<n>` **and** runs the hook, gated on the server's events mask. The
mock simply was not arming those components. `bank_set_events` now does.

### Rev-239 object rows are still component buttons

The modern `IF_BUTTONX` payload includes an object id for both inventory-held
actions and ordinary component rows. That is not a routing discriminator: bank
Withdraw and Deposit rows are object-backed too. `handle_if_buttonx_packet`
therefore normalizes only the named backpack component to `OPHELD`; all other
rows retain their `IF_BUTTON<n>` trigger and reach the bank content/fallback.
Treating every object-backed op 1–5 as held silently discarded bank clicks.

Watch the bit convention — the client's test is `events & (1 << op_num)` with
`op_num` **one-based**, so ops 1..10 are `0x7fe`, not `0x3ff`. The same word is
read 0-based elsewhere in the codebase.

---

## 7. What the content tree owns

`content/scripts/interface_bank/` is the LostCity port. The *rules* carried over
verbatim, because they are OldSchool's:

- a deposit un-notes;
- a withdraw refuses rather than half-completes, with the reference's three
  different "no space" messages (a stack that will not fit and a pile that will
  not all fit are different sentences);
- insert mode shuffles rather than swaps (`insert_bank`, one slot at a time);
- the bank is compacted on open and on close, because a drag can leave gaps.

What content owns: the ways in (`[oploc1,bankbooth]`, `[opnpc1,banker]`), the
settings buttons, and the two deposit-everything buttons. What the engine owns:
the item rows (§4), the space arithmetic, and the varbit push on open.

Both routes exist, and that is the tree's standing contract: **with no script
pack loaded, the bank still works**, because `ToriRSServer_BankHandleButton` is the
fallback for every trigger content might have bound. That is what keeps
`test-ToriRSServer` green while content is mid-edit.

Opening the bank is verb-driven, not id-driven. The engine opens it for any loc
whose cache menu op is `Bank`, the same way "Attack" is decided — OldSchool has
dozens of booths, chests and counters and every one of them says `Bank` in the
cache. An id list would be a hand-kept second copy of that, wrong for whichever
booth nobody added. `loc.pack` names `bankbooth` so content *can* bind a
specific booth, not so the engine can find one.

New script host commands, all with LostCity's signatures so the content ports as
text: `inv_size`, `inv_getobj`, `inv_getnum`, `inv_itemspace`, `inv_itemspace2`,
`inv_movetoslot`, `inv_moveitem` / `_cert` / `_uncert`, `inv_clear`,
`inv_transmit`, `inv_stoptransmit`, `oc_cert`, `oc_uncert`, `~varbit` read and
write, `if_openmain`, `if_openmain_side`, `p_countdialog`, `last_int`,
`last_slot`, `last_targetslot`.

---

## 8. Not implemented

Stated rather than left to be discovered. Each is an interface feature with no
server state behind it, and the varbit that hides it is pushed on open.

| what | why |
| --- | --- |
| **The incinerator** | `bank_showincinerator` pushed to 0. |
| **Potion store, deposit box, bank pin** | Separate features; the PIN has since landed (`interface_bankpin`). |
| **Per-item "Placeholder" (op 10)** | `~script669` offers it while the *global* setting is off. It is a per-slot flag — "remember this one when it goes" — and this bank has no per-slot state to keep it in. Deliberately unbound rather than guessed at. |
| **Search** | Client-side filtering the server never sees. |
| **Membership / reduced capacity** | LostCity's `^bank_free_slots` gate and `BANK_EXTRA_BLOCKS_PURCHASED`. Every slot is free; `bankmain:capacity` is `inv_size(bank)` (1410). |
| **Tab collapse / strip reorder** | Assigning a stack to a tab works (`INV_BUTTOND` onto `bankmain:tabs` → `ToriRSServer_BankMoveToTab`); Collapse-tab and dragging tabs past each other are still client chrome only. |

### The equipment view (now wired)

`interface_bank/scripts/bank_worn.rs2`. The panel behind `bankmain:wornitems_button`
drew from the start — the client owns all of it — but it did nothing, because the
server owes it three facts and was sending none of them:

- **`%if2` (varp 262), the equipable-slot mask.** Bit N set = backpack slot N
  holds something with a wearpos. `bankside_extraop` (2576) and
  `bankside_worn_drawitem` (3327) both read it with
  `testbit(%var262, $index)`, and it is the whole of the Wear/Wield row on the
  side panel — plus, in the worn view, which cells draw solid and which at
  trans 120. Zero meant *every* side-panel item greyed out with no equip option.
  It is one of the cache's six general-purpose interface varps (261..266), so
  `@closebank` gives it back.
- **`if_setevents` on the eleven `bankmain:wornslot*`.** `wear_updateslot` (546)
  writes op 1 Remove, op 2 Bank and op 10 Examine onto them; without the arming
  the client showed those rows, ran its own hook and sent nothing.
- **`%bank_wornview`.** The side panel's equip verb is op 9 in the normal view
  and op 1 in the worn view, and op 9 in the worn view is "Deposit" — the same
  component and op index meaning two things. The client keeps the view in
  `%varcint386`, which a server cannot read, so the two view buttons are armed
  and the server mirrors the state machine in `bankmain_viewbuttons` (3276).

### Placeholders (now wired)

`interface_bank/scripts/bank_placeholder.rs2`. A placeholder is the third slot
state — obj, empty, *remembered* — and the cache carries it exactly the way it
carries bank notes: obj opcodes 148/149 against the note's 97/98, an item naming
its placeholder and a placeholder naming the item back. New server commands
`oc_placeholder` / `oc_unplaceholder` (11020/11021) read that link, named after
the CS2 commands the client has had all along so the server's "is this a
placeholder" test is the client's, character for character.

- The padlock button is armed; `bankmain_toggleplaceholders_op` (1269) writes
  varbit 3755 client-side and sends nothing, so this is the same dual-write the
  Note and Swap/Insert buttons use. The varbit *is* the storage (varp 1053,
  perm + transmit), so `bank_push_settings` no longer forces it to 0 — which it
  did, silently throwing the setting away on every open.
- A withdraw that empties a slot leaves `oc_placeholder($obj)` in it with a
  count of **0**. That count is why `ToriRSServer_SendInvFull` had to stop treating
  `count > 0` as the occupancy test: it sent placeholders as empty slots.
- A deposit reclaims its own placeholder slot before any other placement rule
  (`ToriRSServer_ContainerPlaceholderSlot`), because a placeholder *is* where that
  item belongs — landing beside it is the one outcome the feature exists to
  prevent.
- Op 8 is "Withdraw-All-but-1" on an item and "Release" on a placeholder; the
  cell's contents pick. Op 7 on the tab strip releases the lot.

The client needed one change to draw them: a placeholder record has no model and
no name of its own, so `bridge_obj_icon` resolves it to the linked item (the
reference's item-sprite builder takes the same `placeholderId` branch) and
`CacheProvider_ObjtypeGet` borrows the name the way it already borrows a note's.
The faded look is the interface's — `bankmain_drawitem` sets `cc_settrans(120)`.

### Capacity and tabs (now wired)

- **Capacity text** — `bankmain:capacity` ships empty; CS2 only writes `occupiedslots`. Content's `[proc,openbank]` (and the C open fallback) `if_settext` the inv size.
- **Tabs** — `bank_tab_1..9` track the contiguous prefix CS2 lays out; deposit into the viewed tab, withdraw, and drag-to-tab keep them coherent. The strip is armed so View-tab clicks sync `%bank_currenttab`.

The bank is also **not persisted** — the mock has no storage at all, so a fresh
login gets the same seeded stock (`ToriRSServer_WorldInit`).
