# Verbs: shops and held items (section 3)

`t.shop.*` and the backpack/worn-item verbs of `t.player`. The shop's purpose (buy only what the
quest's own script makes you buy) is in `gaps-combat.md`.

## `shop` (`ui.lua`)

### `t.shop.open(npc, op=3, shop_inv)`

`t.shop.open(npc, op=3, shop_inv)` -> `ok` `no_row` `not_visible` `timeout` / click_minimenu's own
results. Presses the npc's numbered shop op (3 is "Trade" on this pack's shopkeepers, Razmire's
builders store is 4) and returns only once `shopmain` is mounted AND its stock has landed -- two
server messages, not one. `shop_inv` is the shop's own inv SYMBOL, the one its `.rs2` hands
`~openshop` (`razmirebuildingstore`, `generalshop1`), and it is required: the client keeps a
transmitted container under its inv id with no record of the grid it was pushed to, so "which shop
is this" is a question only the caller can answer. Closes a shop that is already up first, for the
same reason `journal_open` does.

### `t.shop.buy(item, count=1)`

`t.shop.buy(item, count=1)` -> `ok` `refused` `not_found` `no_row`. Buys exactly `count`, composing
the four FIXED rungs on `shopmain:items` -- ops 2/3/4/5 are Buy-1/5/10/50 (`shop.rs2`), never op 1,
which means whatever the quantity bar last said -- so 25 is `Buy-10, Buy-10, Buy-5` and the detail
names the ladder. `ok` is the backpack holding `count` more; anything short is `refused` carrying
the count it did reach, the coins it spent and the server's own sentence ("You don't have enough
coins.", "The shop has run out of stock.", "You don't have enough inventory space."), because a shop
refuses in prose and never in a result word.

#### `no_row` "this shop was not opened through shop.open"

It also REQUIRES that the shop was opened through `t.shop.open` specifically: `shop.buy` answers
`no_row` with "this shop was not opened through shop.open" (`ui.lua`) whenever the driver's own
stock-container bookkeeping is unset -- and a shop mounted by an ordinary dialogue choice mounts the
SAME interface 300 without setting it, so a shop that is plainly open on screen can still refuse
every buy. `shop.attach(shop_inv)` is the answer to that, and it is the one to reach for --
`shop.open` presses the npc and cannot be aimed at a screen something else put up.

#### Read the shop's row in `shop_stock.csv` first

Before picking a shop, read ITS row in `OSRS-Content/osrs239-content/wiki/shop_stock.csv`, not just
the wiki page: a row can carry baseline `stock` 0 for an item the page lists, and a buy then refuses
`stocks 0 <item>` -- the fix is a different shop, not a retry.

### `t.shop.attach(shop_inv)`

`t.shop.attach(shop_inv)` -> `ok` `no_row` `not_visible` `timeout` `refused`. Binds the shop that is
ALREADY on screen -- put there by a dialogue row, `::shop`, a loc's own op -- so `shop.buy` can read
its stock. `open` minus the press: same inv symbol, same stock wait, same detail shape, and it
deliberately does NOT close what it binds. It `refused`s when the grid on screen does not carry that
container's `slot_count + 1` cells, and it has to: every container this client was ever sent stays
resident in its InvManager (the backpack always, and a shop after `[if_close,shopmain]`, which stops
the updates without dropping the container), so "the named inv is resident and stocked" is satisfied
by a container that is not on screen at all -- measured, `attach("inv")` with the Lumbridge store up
would otherwise have bound the player's BACKPACK as the shop and pressed grid cells off backpack
slot numbers three buys later.

### `t.shop.close()`

`t.shop.close()` -> `ok` `timeout`. Interface 300 has no close button, so this is the ESC path
(`close_modal`), which runs `[if_close,shopmain]` server-side. Idempotent. Takes no argument, so
record it with `t.check`/`t.step` -- `t.exec` grades a zero-argument call `bad verb/target`.

## Held-item verbs (section 3, `world` / `drive` / `player`)

### `t.player.inv_op(item, op=1)`

`t.player.inv_op(item, op=1)` -> `ok`/error. A numbered held op; op<0 is refused (that is `use_on`'s
arming half). `refused` means THE CLIENT never sent it and the detail names the condition
(`no DISPLAYED node carries that component id`,
`the item cell or an ancestor of it is display-hidden`, ...); `timeout` means it WAS sent and
nothing answered -- that distinction is the whole difference between blaming the driver and blaming
content.

The verb presses up to seven times against a re-pressed backpack tab and says so
(`[pressed on attempt 2; the first answered '...']`); read that as normal, not as a retry hack. An
op whose whole answer is a NON-chat interface (a schematic, a lamp picker, a still) settles `ok` and
names it: `-> 0 left [modal dwarf_rock_schematics]` -- the interface was not up when the settle
began (seam12); dialogue pages still settle through the chat arms.

#### A Wield/Wear through `inv_op` answers `ok ... [WORN <item>: ...]` (seam34)

A Wield or Wear (op 2, `[opheld2,_] ~equip`, `player/scripts/equip.rs2:304`) that lands prints
nothing, mounts nothing and routes nowhere, so before seam34 the settle waited out its ten ticks and
answered `timeout ... -> 0 left [settle_after_click]` for an item that was ON the player (Underground
Pass `leg.4.wield`). `inv_op` now reads the item's worn total before the press and resolves on it
rising: `adamant_scimitar slot 5 op 2 (tab ok nil) -> 0 left [WORN adamant_scimitar: worn 0 -> 1,
wear slot 3]`. A settle `timeout` with the item now worn is promoted to `ok`; a `refused` never is.
An under-level wield ("You need to have an Attack level of 30.") still answers `ok` with no tag
(that sentence is not in the refusal fence), so read the tag. `t.player.equip` is still the verb to
prefer: it asserts the worn container and re-presses a dropped press. Row:
`seam.inv_op_wield_reads_worn`.

#### `ifop1=`..`ifop5=`, and op 1 keeps the item (FIXED seam20)

Rev-239 obj configs spell held ops `ifop1=`..`ifop5=` (`configs/all.obj`), not LostCity's `iop1=`:
grep `ifop` before declaring an item has no op. FIXED (seam20): op 1 keeps the item --
`app_minimenu.c` flashes the backpack cell with `net_out_opheld_component_op(op)`, not the OPHELD
index that ran rev-239's shift-click-drop chain (conformance `seam.held_op1_keeps_the_item`). A
detail carrying `[STRAY DROP: ...]` means a binary built before that fix: never write a
pick-it-back-up row around it.

### `t.player.equip(item)`

`t.player.equip(item)` -> `ok` `refused` (worn count must rise). Pressed up to three times while the
item is still in the backpack and not worn (seam24): the server silently refuses a held-item op
inside another script's `p_delay` (Telekinetic Grab's closing `p_delay(1)`, `telegrab.rs2:51`), as a
person would click again; the detail says `(press N)`.

### `t.player.unequip(item)`

`t.player.unequip(item)` -> `ok` `not_found` `not_visible` `refused` `timeout`. Takes a WORN item
off into the backpack, through the worn tab's own "Remove" (op 1 on `wornitems:slot<N>`, the slot
the content enum names) -- the mirror of `equip`, and the only verb that reaches the worn container
at all. `ok` means the worn count fell AND the backpack count rose, both, and the detail says so
(`worn 1->0, backpack 0->1`); `not_found` is "nothing of it is worn" (your bug is one row earlier);
`not_visible` means the equipment tab never painted that slot. Use it for any mechanic that
alternates worn and carried state -- Mourning's End Part I's paint device must be WORN to fire and
CARRIED to reload.

### `t.player.drop(item)` -- also `t.player.emote`

`t.player.drop(item)` -> `ok` `timeout` (backpack falls AND a stack lands on the ground).

#### A rake fills the backpack with weeds; dropping a second weeds copy FAILs (sonnet-b42)

Every farming patch you rake gives weeds, several per patch. A long farming quest (Garden of
Tranquillity rakes nine patches and three allotments) fills the backpack, and a later pick or
harvest fails with the inventory full. Drop the weeds after each rake. `t.player.drop` is `ok`
only when a ground stack lands, and a second weeds stack nearby does not raise the ground count
(seam-facts: Seam pass 26 (n)), so wrapped in `t.exec` the second drop is a FAIL row. Call it
directly in a loop that reads `t.inv.count("weeds")` (`gardenoftranquility.lua:100-105`), and let
the next row assert what the drop was for.

#### `t.player.emote(name)`

`t.player.emote(name)` -> `ok` `refused` `timeout` `no_row` `not_visible` `unsupported`: opens the
emote tab, presses the emote's own cell (`emote:contents`, sub = emote.constant's index; names fold
case and spaces, 'Blow Kiss'; a number is that cell; Sit/Crab Dance are `unsupported`, op 1 is
Loop), puts the sidebar back on the backpack and settles on the first new chat line -- content's
reaction is `ok` naming it, "You haven't unlocked that emote yet." is `refused`, and NO line in 5
ticks is `timeout`, because the driver cannot read an animation: play an emote for its content
reaction and then assert the content's own varp (Throne of Miscellania's `misc_affection`).

### `t.player.use_on(item, target, opts)`

`t.player.use_on(item, target, opts)` -> `ok` `unsupported` `refused` `covered`. `opts` is
`{ stand_on_square = true }` or nothing (trap 32); an NPC target that answers `I can't reach that!`
(it wandered while you walked) is re-pressed up to twice once the player stops, and the row says
`[npc reach retry: ...]`. Arms the item, then `click_minimenu(target, "select")`, walking to other
sides of a loc that answers `covered` and RE-ARMING before every retry press (an arming that
survived costs nothing and sends nothing).

The ROW that got pressed is checked: the `select` wildcard matches any row for the target once the
arming is gone, so a press that only examined it answers
`refused -- pressed 'Examine <thing>', an ordinary op row and not the held-item row`. Since the
re-arm landed that string means the arming was spent BETWEEN the arm and the press -- a driver bug
worth reporting, not a quest's fault. A `refused` whose detail is a SERVER sentence
(`Nothing interesting happens.`) is the opposite: the press landed the held-item row and the WORLD
refused it, so read the content.

`I can't reach that!` is handled for you -- the whole press, arming included, is re-taken from the
loc's other approach tiles before that word reaches your row. That multi-side walk is LOCS ONLY: an
npc target is re-pressed from where the player already stands, so a `walk_near` that reads
`ok`/within-N can still leave you on the wrong side of an interior fence -- pick the approach tile
yourself (Sheep Herder's pen feed loop).

Each retry presses from wherever that walk actually landed, not the approach tile it was aimed at,
and a second candidate whose walk ends on a tile already tried is not pressed again.

### `t.player.use_item_on_item(item_a, item_b)`

`t.player.use_item_on_item(item_a, item_b)` -> `ok` `not_found` `refused` `no_row` `timeout`
`unsupported`. Item-on-item (OPHELDU) through real clicks: arms item_a's backpack cell (the "Use"
row) and then clicks item_b's cell, so the client encodes the use itself. Settles on a dialogue page
that differs from the one before the click, a new chat line, or either item's backpack total
changing (10 ticks). The detail names which of the three answered, the server's own sentence when
there is one, and what the backpack gained and lost -- `refused` carries content's refusal ("Nothing
interesting happens."), `not_found` names the item that is not carried, `no_row` means both names
resolved to one cell.

A worn item is NOT a cell either half can name: this engine's `handle_opheldu` validates both slots
against the backpack and drops the packet in silence when either misses, so `not_found` on an item
you are wearing says so in its detail and names `player.unequip` -- take it off first.
