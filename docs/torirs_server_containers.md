# The container registry

> Written 2026-08-02, lane `lane-blockers`. Everything numeric here was
> measured in this tree; re-measure rather than trusting the prose
> (PORTING_GUIDE §7).

Source: `src/torirsserver/torirs_server_container.h` (the contract, and the reason for
each field), `src/torirsserver/torirs_server_container.c`, `struct ToriRSServerContainer` in
`src/torirsserver/torirs_server.h`.

Permanent check: `ToriRSServer --selftest`, case **"the container registry"**
(`torirs_server_world.c`). It is written against *symbols*, never numbers — the inv
id, the size and the transmit shape all come out of the pack and the cache, so
a test that kept passing after the cache moved them is not possible.

---

## 1. What it replaces, and why three cases could not become four

`container_for` was three `if( inv_id == ToriRSServer_Ids()->… )` branches over
three different storage shapes, all resolved off `srv->active_player`.
`container_dirty` was a second, independent three-way branch. Thirteen call
sites.

Three defects followed from that shape, all measured before the change:

1. **1,023 of the cache's 1,026 invs did not exist.** `inv_size(anything else)`
   returned **0**, and every other container op was a silent no-op on a
   container the *client* is perfectly willing to draw — while
   `ToriRSServer_BankInvSize()`, sitting in the next file, could size all 1,026.
2. **Two callers hand-rolled the dirty half.** `SS_OP_INV_DEL` and
   `SS_OP_INV_DELSLOT` wrote `items[i]` directly and then ran
   `if( backpack ) inv_dirty else worn_dirty` — so `inv_del(bank, …)` marked a
   **worn** slot (a wrong appearance push, and a bank that never
   re-transmitted), and for a bank slot past 31, `1u << i` shifted a 32-bit
   mask by more than its width. `i` reaches 1409. `inv_add` had the same bug
   and had already been fixed once; these were the second and third copies.
3. **`active_player` is not something a `scope=shared` container has.** No
   number of extra cases fixes that: the resolver's *signature* was the
   blocker, not its length.

A fourth defect fell out of the same shape one layer down: `torirs_server_save.c`
had three hardcoded `write_items()` sections and a fixed `enum SaveSection`
mirroring `container_for`'s three cases exactly, so a container the client
could be shown vanished at logout.

## 2. The reference

`Player.getInventory(inv)` —
`LostCity_Server/engine/src/engine/entity/Player.ts:1463-1487` — is the whole
seam:

```ts
if (invType.scope === InvType.SCOPE_SHARED) container = World.getInventory(inv);
else { container = this.invs.get(inv);
       if (!container) { container = Inventory.fromType(inv); this.invs.set(inv, container); } }
```

Two properties are ported rather than invented: the **scope branch**, and
**resolve-or-create**. The second is why content can name any inv without the
engine having heard of it, and therefore why **no inv id enters C**.

The dirty rule is the reference's too: `Inventory.update` is set by every
mutating method on the class and cleared once per tick in
`World.processCleanup()`, never by a caller. That is defect 2's fix stated as
a rule.

## 3. Shape

One row per container, in a fixed table: `ToriRSServerPlayer.containers[16]` and
`ToriRSServer.world_containers[16]`.

| field | why it exists |
|---|---|
| `inv_id`, `slots`, `items` | `slots` is `ToriRSServer_BankInvSize(inv_id)`; an inv the cache does not size is not a container |
| `owner_kind`, `owner` | PLAYER / WORLD. The resolve signature is `(srv, player, inv_id)` and `player` is *unused* for a world row — that is the point |
| `owns_items` | the registry calloc'd it. 0 for an adopted array |
| `per_slot` | **`slots <= 32`, decided at registration and nowhere else** |
| `slot_dirty_ref` / `dirty_ref` | adopted flags, for state read outside the registry |
| `listeners[]`, `listener_count` | LostCity's `invListeners` — `inv_transmit` appends, `inv_stoptransmit` removes by component; one inv may paint several panels |

**`per_slot` is a correctness branch, not an optimisation.**
`ToriRSServer_SendInvPartial` addresses its slots out of a 32-bit mask
(`dirty & (1u << i)`). **304 of the cache's 1,026 invs are larger than 32
slots.** The backpack (28) and worn set (14) land on the per-slot side by
measurement, not by being named.

**No self-referential pointer is stored.** A row that owns its dirty state is
asked for the address of its own field at the point of use (`slot_mask_of` /
`dirty_of`), so a `memset` — and `ToriRSServer_WorldPlayerInit` does one — cannot
leave a row pointing at a stale twin.

**Adopted rows.** Three containers predate the registry and their flags are
read from outside it: `player->inv_dirty` and `player->worn_dirty` feed the
appearance path and two selftests, `bank.dirty` feeds `ToriRSServer_BankFlush`.
Those are passed in (`ToriRSServer_ContainerAdopt`) so there is **one** flag per
container rather than two that can disagree. `appearance` is a row flag, so
the worn container raises `TORIRSSERVER_PMASK_APPEARANCE` from inside the mutator —
the two places that used to remember it by hand no longer can forget.

## 4. What resolves where — and the hole

`ToriRSServer_ContainerScope(inv_id)` returns `TORIRSSERVER_CONTAINER_PLAYER` for
everything, and that is a **missing input, not a decision**:

- LostCity reads `scope` from its **server-side** `data/pack/server/inv.dat`
  (opcode 1).
- The **client cache carries no such field**:
  `3rd/rscache/src/datatypes/dat2_config_inv.c` decodes only opcode 2 (size)
  and 249 (params).
- This tree has **no `fields/inv.ini`** (`fields/` holds enum, loc, npc, obj,
  param, varp) and **no `[namespace:inv]` in `content.ini`**, so
  `scope=` / `restock=` / `stockN=` / `allstock=` have nowhere to live.

So the world table is **empty by construction**. The branch is real and under
test; the classifier is the hole, and `fields/` belongs to another lane. This
is the concrete reason `shop` is still blocked rather than half-built.

## 5. Sizes come from the cache, not from C

`torirs_server_bank.c`'s `load_inv_sizes()` decodes config group 5 for every file id
at boot (`torirs_server_boot.c`, before any player exists) and
`ToriRSServer_BankInvSize(int)` answers any id. Measured: **1,026 invs, every one
carrying an explicit `size=`**; `bank` 1410, `collection_transmit` 500,
`tradeoffer` 28, `inv` 28, `worn` 14.

**`TORIRSSERVER_BANK_SLOTS = 1220` was a clamp wearing a fallback's comment.**
`ToriRSServer_BankInitPlayer` read `if( size <= 0 || size > TORIRSSERVER_BANK_SLOTS )
size = TORIRSSERVER_BANK_SLOTS;` — the second half is not a no-cache fallback, and
the cache says **1410**, so every bank this server ever allocated was **190
slots short of the container the client walks**. The allocation is a `calloc`;
there was never a ceiling for it to enforce. The `>` half is gone; the `<= 0`
half stays, because a run with no cache still needs a usable bank.

## 6. Transmit

`inv_transmit` appends a `{component, first_seen}` listener on the row and
sends a full update to **that** component immediately — the reference's
`Player.invListenOnCom`. Re-binding the same `(inv, com)` is a no-op; binding a
`com` that already listens elsewhere moves it. `inv_stoptransmit` removes only
the named component, so closing the equipment-stats screen
(`inv_stoptransmit(equipment:universe)`) leaves the worn-tab listener intact.

One inv may have several listeners at once. That is load-bearing for worn: the
login bind paints `wornitems`, and `~equipment_refresh` also
`inv_transmit(worn, equipment:universe)`. A single `component` field used to
overwrite the login binding; after stats closed, unequips still said "You
remove X" but sent no `UPDATE_INV_*`, so the worn tab kept stale icons.

`phase_client_out` runs **one loop** (`ToriRSServer_ContainerFlush`) over the
rows, replacing two hardcoded `ToriRSServer_SendInvPartial` calls that named the
backpack and the worn set. For each dirty (or firstSeen) row it emits
partial/full to **every** listener, then cleans once. Partial when `per_slot`,
full otherwise, trimmed to the used prefix (UPDATE_INV_FULL clears everything
past the capacity it carries). Rows with no listeners still get cleaned, so a
container written while its interface was closed does not re-transmit the
moment something binds — the bind's own full update already covered it.
Unbound rows that carry an external `dirty_ref` are left dirty: that flush
owns the bit (`ToriRSServer_BankFlush` for the bank).

**The bank is deliberately not bound through the registry.** Its transmit is
gated on `bank.open` and carries tab bookkeeping the generic binding does not
model; binding it here would put two senders on one container, which is the
failure the registry exists to remove. Its *row* is in the registry all the
same, and that is what fixes defect 2. Folding the transmit in means moving
`bank.open` into the binding table — a real simplification, and not this
stage's.

## 7. Persistence

`torirs_server_save.c` writes `[inv]`, `[worn]` and `[bank]` by name (a person
hand-editing a save is looking for those, and an old file still loads) and
every other registry row as `[container.<inv_id>]`. The test that separates
them is **`owns_items`**, not an id — no inv id in the loop. Load resolves the
section through `ToriRSServer_ContainerResolve`, which creates the row: on a real
login it does not exist until the save recreates it.

## 8. The compiler defect this turned up

`[debugproc,container](inv $inv, …)` passed **container 0**, whatever name was
typed.

`parse_header_lists` records each declared type in `script->param_types` via
`SSC_SymbolsFind(symbols, type, SSC_SYM_TYPE)`, falling back to `int`. The
seed loop for type names guarded with `SSC_SYM_UNKNOWN`, which matches **any**
kind — and the backpack container is *named* `inv` (`pack/inv.pack`), so
`SSC_SYM_INV` claimed the name and the `inv` **type** was never registered at
all. `(inv $x)` therefore recorded its param type as `int`.

Nothing in a compiled script could notice: an inv rides the int stack either
way. The only consumer that reads `param_types` back is
`ToriRSServer_ScriptsRunDebugproc`, which resolves each word through the pack its
type names — so with `inv` recorded as `int` it ran
`strtol("collection_transmit")` and passed 0. `inv_size(0)` is 13, and the
symptom was a plausible sentence about a 13-slot container.

Fixed by seeding type names **per kind**
(`ssc_symbols.c`); lookups already walk every entry with a name and take the
first of the requested kind, so `inv` as a *value* still resolves to the
container. Pinned by `test_param_type_shadowing` in `ssc_test.c` — which also
had to start calling `SSC_SymbolsSeedBuiltins`, **after** the pack adds, the
way `sscompile` does. The fixture never called it, which is why every header
type in every existing test silently fell back to `int`.

## 9. What is proven, and what is not

Proven, in the headless client over the embed transport
(`manifest_osrs230_embed.ini`, `TORIRS_NET_CHEAT="container …"`):

- `collection_transmit` — a container **no C file names** — resolves, sizes
  itself to **500** from the cache, accepts a write at **slot 480**, refuses
  slot 600, and survives a real logout/login through `[container.620]` in the
  save.
- `bank` reports **1410**, not the clamped 1220.
- `inv` slot 5 accepts a write and the item appears in the sidebar — the
  per-slot flush path, unchanged.

Not proven, and not attempted: the **collection-log feature**. Nothing here
draws its panel or earns an entry; the point-of-earning trigger is a genuine
corpus gap (no script in the tree fires it) and its ~20 varps are content this
lane has no reference for.

## 10. Bounds

`TORIRSSERVER_CONTAINER_MAX = 16` per player, same for the world table. A storage
ceiling, not a container count — the reference's `Player.invs` is a Map.
Overflowing it is reported on stderr and resolves to NULL; every ServerScript
container op **aborts** on NULL rather than returning a plausible zero, which
is the same rule the VM applies to an unimplemented opcode. Before this stage
the read ops (`inv_total`, `inv_freespace`, `inv_getobj`, `inv_itemspace`,
`inv_clear`, `inv_movetoslot`) returned zero silently while the write ops
aborted; they all abort now.
