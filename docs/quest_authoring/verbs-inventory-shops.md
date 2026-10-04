# Verbs: shops and held items (section 3)

`t.shop.*`, `t.bank.*` and the backpack/worn-item verbs of `t.player`. The shop's purpose (buy only what the
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

#### `shop.open` times out on a shopkeeper whose Trade speaks first (Bob's axes; matthew-mbp-m4-b50-seam1)

Bob's Trade (`bob.rs2 [opnpc3,bob]`) speaks two pages before the shop opens, so `shop.open("bob", 3,
"axeshop")` times out. Use `t.player.talk_to("bob", 3)`, `t.chat.play{"player:Have you anything to
sell?", "npc:Yes! I buy and sell axes!"}`, then `t.shop.attach("axeshop")`. A shop whose stock is 1
(Wydin's redberries) restocks in about 100 ticks: buy, close, `t.ticks(110)`, open again.

### `t.shop.buy(item, count=1)`

`t.shop.buy(item, count=1)` -> `ok` `refused` `not_found` `no_row`. Buys exactly `count`, composing
the four FIXED rungs on `shopmain:items` -- ops 2/3/4/5 are Buy-1/5/10/50 (`shop.rs2`), never op 1,
which means whatever the quantity bar last said -- so 25 is `Buy-10, Buy-10, Buy-5` and the detail
names the ladder. `ok` is the backpack holding `count` more; anything short is `refused` carrying
the count it did reach, the coins it spent and the server's own sentence ("You don't have enough
coins.", "The shop has run out of stock.", "You don't have enough inventory space."), because a shop
refuses in prose and never in a result word.

#### `cell N of shopmain:items is not mounted` after `holds K stocked slot(s) of K` (FIXED seam35)

The stock was there and the grid had no cells: a shop whose inv is declared in CONTENT
(`pack/inv.alloc` + `size=`; the Ardougne silver stall, Zaff, the Pie Shop) has no cache InvType,
the client's `INV_SIZE` answered 0, and `shop_main_init` built 0 cells. Since seam35 `INV_SIZE`
falls back to the capacity the server transmitted, and these shops open with their stock
(seam-facts: Seam pass 35 (a)). If it comes back, it is the client, not your buy.

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

## `bank` (`ui.lua`, matthew-mbp-m4-b56-seam2)

The bank driven the way a player drives it. When to bank and the Dream Mentor recipe:
`gaps-combat.md`, "Bank the fight food". The verbs reach v3 with batch matthew-mbp-m4-b56's PR.

### `t.bank.open(booth_or_banker, op=2, opts)`

`-> ok no_row not_visible timeout` / click_minimenu's own results. Presses the loc's or npc's
numbered Bank op through `click_minimenu` (op 2 on 60 of the 78 booth records, op 1 on chests,
`bank_booths.rs2`) and returns once `bankmain` AND the bank container have landed (two server
messages). `opts.at = {x, z[, level]}` names the booth copy, as `click_loc` does; `opts.kind =
"loc"|"npc"` when a symbol is both. A bank already on screen is closed first. The `ok` detail names
the bank's used slots and the backpack's free slots.

### `t.bank.withdraw(item, n|"all")` and `t.bank.deposit(item, n|"all")`

`-> ok closed not_found refused no_row`. Withdraw presses `bankmain:items` cell <bank slot> with the
fixed rungs (op 4 Withdraw-10, op 3 Withdraw-5, op 2 Withdraw-1, op 7 Withdraw-All; `bank.rs2`);
deposit presses `bankside:items` cell <backpack slot> (op 5 Deposit-10, op 4 Deposit-5, op 3
Deposit-1, op 8 Deposit-All; `bank_deposit.rs2`). `ok` only when the backpack AND the bank both moved
by `n`, read back from the client's containers: `bank.withdraw: shark backpack 0 -> 12 (+12), bank 30
-> 18 (-12), 12 asked; backpack free slots 28 -> 16 [Withdraw-10@8, ...]` (conformance row
`bank.withdraw`). `closed` = no bank on
screen; `not_found` = the source holds none and nothing was pressed; `refused` = the server moved
less than asked, led by `backpack full --` and ending in the server's own sentence (`You don't have
enough inventory space.`). The bank verbs assume every item is in the main tab; bank tabs are
unproven.

### `t.bank.count(item)` and `t.bank.close()`

`count` -> `(ok, n)` from the OPEN bank, `closed` otherwise (the client keeps a closed bank's last
copy, which is not a reading). `close` -> `ok timeout`, the ESC path, idempotent; it takes no
argument, so record it with `t.check(name, t.bank.close())`.

### `::bankgive <obj> <n>` -- SETUP only

Stocks the bank (`general/scripts/misc/cheat_bank.rs2`, stored uncerted; replies `Banked N x <name>;
the bank holds M.`). `t.cheat("::bankgive ...")` after `t.quest.bind` answers `refused ... SETUP
cheat` and sends nothing (core.lua `QD.cheat`). `fresh_lumbridge.ini`'s bank already holds ten
slots.

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

#### `inv_op` answers `timeout` on an op that swaps the item for another obj (sack Fill; matthew-mbp-m4-b50-seam1)

Fill on `sack_empty` turns the sack into `sack_potato_10`, and the verb's settle watches the old obj,
so it answers `timeout` though the op ran. Read the new obj back instead:
`t.inv.await("sack_potato_10", 1, 10)` (conformance `seam.vegetable_sack_fill`).

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

`t.player.drop(item)` -> `ok` `timeout`. It is graded on the BACKPACK falling, with at least one of
the item on the player's own tile; a ground count rising is not required. The detail is
`drop <item>: backpack B -> A, ground on the player's tile G0 -> G1 (N row(s))`. A second identical
non-stackable copy dropped on its twin's tile is `ok`, and since seam pass matthew-mbp-m4-b53-seam1
its ground half rises too: `ground on the player's tile 1 -> 2 (2 row(s))`. The client keeps a LIST
of ground rows per tile, one per OBJ_ADD, so two identical drops are two rows with a Take each, and
picking one up leaves the other drawn and takeable (seam-facts: Seam pass matthew-mbp-m4-b53-seam1
(a); conformance `seam.two_copies_one_tile_both_takeable`). Before b53-seam1 the client merged them
into one row and the second copy vanished after the first pick (b52-seam1 (a), FIXED).

#### A rake fills the backpack with weeds; dropping a second weeds copy FAILs (FIXED b52-seam1)

Every farming patch you rake gives weeds, several per patch. A long farming quest (Garden of
Tranquillity rakes nine patches and three allotments) fills the backpack, and a later pick or
harvest fails with the inventory full. Drop the weeds after each rake. Until seam pass
matthew-mbp-m4-b52-seam1, `t.player.drop` was `ok` only when the ground count rose, so a second
weeds drop wrapped in `t.exec` was a FAIL row and tests called it directly in a loop that reads
`t.inv.count("weeds")` (`gardenoftranquility.lua:100-105`; also legends.lua makeBowl and
enlightenedjourney.lua). Those loops still work; new drops can be `t.exec` rows.

#### `t.player.emote(name)`

`t.player.emote(name)` -> `ok` `refused` `timeout` `no_row` `not_visible` `unsupported`: opens the
emote tab, presses the emote's own cell (`emote:contents`, sub = emote.constant's index; names fold
case and spaces, 'Blow Kiss'; a number is that cell; Sit/Crab Dance are `unsupported`, op 1 is
Loop), puts the sidebar back on the backpack and settles on the first new chat line -- content's
reaction is `ok` naming it, "You haven't unlocked that emote yet." is `refused`, and NO line in 5
ticks is `timeout`, because the driver cannot read an animation: play an emote for its content
reaction and then assert the content's own varp (Throne of Miscellania's `misc_affection`).

##### `t.exec(..., t.player.emote, ...)` FAILs `timeout` when the emote's reaction prints no line (Below Ice Mountain's Flex)

*Origin: matthew-mbp-m4-b52 belowicemountain reviewer.*

The verb settles on a new game-message line, and some content answers an emote with a dialogue
and a var write but no game message. Below Ice Mountain's Flex for Checkal opens his dialogue and
sets `varb12065_bim_checkal` to 40, so `t.player.emote` answers `timeout`. Wrapped in `t.exec`,
that `timeout` is a FAIL row although the step worked. Call the verb directly, read the dialogue
next (`t.chat.drain` or `t.chat.play`), then grade the var in one `t.check` that carries the verb's
answer in its detail (`belowicemountain.lua:74-79`:
`t.check("flexCheckal", v == 40, "emote=" .. tostring(er) .. " " .. tostring(ed) .. "; checkal=" .. tostring(v))`).

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

#### `use_on` walks off the tile the content checks: Betty's doorway (matthew-mbp-m4-b55)

*Origin: author batch matthew-mbp-m4-b55 (handinthesand, accepted).*

The Hand in the Sand's lens is used on Betty's counter from her doorway, 3016,3259. The counter is
three tiles away. `[aplocu,handsand_counter_multiloc]` accepts the press from up to four tiles
(`p_aprange(4)`, `handsand_betty.rs2:243`), and `handsand_counter_focus` then requires
`coord = ^handsand_doorway_coord`. Otherwise it answers the mesbox "You need to stand in Betty's
open doorway to focus the light on the vial." `t.player.use_on` walks into `walk_near` range 2 of
the loc before it presses, so it leaves the doorway and gets that mesbox. `handinthesand.lua`
(`useLensOnCounter`) does it like this. It runs `goto_tile 3016,3259`, then
`t.player._arm_held(item, cell)`, then `t.drive.click_minimenu(loc, "select")`. The aplocu's own
range holds the tile, and a `doorway.tile` row records where the player stood. The two helpers
are private. Asking `use_on` to press without walking is a driver seam.

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
