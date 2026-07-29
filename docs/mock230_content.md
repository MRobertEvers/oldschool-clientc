# The mock's content tree

Everything the rev-230 mock server knows that is not engine mechanics: which
npcs stand where, how hard they hit, what they drop, which doors open into
which, and what the tutors say. It is a **LostCity content tree** — same pack
files, same config syntax, same `.jm2` map format, same `.rs2` scripts — so a
config can be pasted in either direction and mean the same thing.

The ids are not LostCity's. They come from **OpenRune**, whose gameval table
names every id in a modern OldSchool cache, and every one of them is re-checked
against `cache.osrs230` before it is trusted. See §4.

`build/` is excluded by the repo's `.gitignore`, so **a fresh checkout has no
compiled script pack** until `mock230-scripts` runs. The mock still works
without one — every trigger site falls back to the C behaviour it had before
scripts existed, which is what keeps `test-mock230` green mid-edit — but there
is no dialogue and no drop table until it is built.

```
make -C src mock230              # the server
make -C src mock230-scripts      # compile content/scripts/**.rs2
make -C src mock230-pack         # the validator / cache exporter
make -C src test-mock230         # the game logic, no socket

src/build/mock230_pack           # validate the tree against the cache
src/build/mock230_pack -v        # ... and list every definition
```

---

## 1. Layout

```
src/net/mock/content/
  pack/                      id=name, one file per namespace
    npc.pack obj.pack loc.pack seq.pack        generated (tools/gameval_import.py)
    door.pack                                  generated (tools/door_import.py)
    interface.pack component.pack varbit.pack  generated (tools/gameval_import.py)
    varp.pack                                  generated
    inv.pack stat.pack param.pack              hand-authored
    varp_mock.pack hitsplat.pack               hand-authored
  maps/
    m50_50.jm2 …             ==== NPC ==== / ==== OBJ ==== spawn sections
  scripts/
    areas/lumbridge/configs/lumbridge.npc      combat blocks
    areas/lumbridge/scripts/*.rs2              dialogue
    skill_combat/configs/combat.param          LostCity's param names, verbatim
    skill_combat/configs/npc_combat.param
    skill_combat/combat.rs2
    doors/configs/doors.loc                    generated + cache-validated
    drop tables/scripts/*.rs2                  [ai_queue3] drop tables
    drop tables/configs/lootdrop.constant
    interface_bank/scripts/*.rs2               the bank — docs/mock230_bank.md
    interface_bank/configs/bank.varp           the varps its varbits live in
    interface_bank/configs/bank.constant       quantity modes
    interface_bank/configs/bank.enum           tab index -> tab-size varbit
    skill_prayer/configs/prayers.prayer        the 29 prayers
    skill_prayer/configs/prayers.constant      overhead icon indices
    player/configs/worn.enum                   worn-tab cell -> wear slot
    player/login.rs2
    build/                                     compiled script pack (gitignored)
```

The tree is **read at boot**, not packed. LostCity compiles its configs into the
cache its *client* serves, because its content invents npcs and locs. Nothing
here invents one: every npc, obj and loc already exists in `cache.osrs230`
exactly as OldSchool ships it, so a config block is an **overlay** carrying only
what a cache cannot state. Reading the text costs about a millisecond and takes
a build step out of the edit loop.

`mock230_pack --cache-out` is the packer, for when an overlay should be visible
outside the mock. See §6.

---

## 2. What a config block is allowed to say

`content/scripts/areas/lumbridge/configs/lumbridge.npc` uses LostCity's keys:

```
[goblin]
hitpoints=5
attack=1
strength=1
defence=1
respawnrate=25
wanderrange=8
huntmode=aggressive
param=huntrange,4
param=attackrate,4
param=attack_anim,goblin_attack
param=defend_anim,goblin_block
param=death_anim,goblin_death
param=death_drop,bones
```

**Omitted is the normal case.** Name, models, recolours, walk and ready
animations, menu ops and combat level all come from the cache record. So do the
*equipment bonuses* — see §3 — which is why a config almost never states one.

What has to be authored is what an OldSchool cache has no field for: hitpoints,
the three combat levels, aggression, and the drop. Values follow OpenRune's own
combat def where it has one (its `CowPlugin.kt` is where the cow's 8 hitpoints
and 6-tick attack come from) and the OldSchool monster stats otherwise.

`model=`, `name=`, `op1=` and friends are *accepted and ignored*, so a LostCity
config can be pasted in unedited — but never silently: the loader reports every
key it did not act on.

### Drops are scripts, not config

LostCity puts drop tables in `[ai_queue3,<npc>]` handlers, and so does this:

```rs2
[ai_queue3,goblin] @goblin_drop_table;

[label,goblin_drop_table]
obj_add(npc_coord, npc_param(death_drop), 1, ^lootdrop_duration);
def_int $dropint = random(128);
if ($dropint < 1) {
    obj_add(npc_coord, coins, 1, ^lootdrop_duration);
} else if ($dropint < 7) {
    obj_add(npc_coord, coins, 20, ^lootdrop_duration);
}
...
```

The engine fires the trigger when an npc reaches zero hitpoints. With no script
bound, the config's `death_drop` still drops — so an npc with no table leaves
bones rather than nothing.

### No ids in headers

Everything the engine addresses by id resolves through a pack at the point of
use. What used to be a `#define` or a literal table in C:

| was | now |
|---|---|
| a 24-entry `{ slot, group }` table in `mock230_world.c` | the `gameframe` **.enum** — keys are components, values are interfaces |
| `MOCK230_HIT_DAMAGE 0` / `MOCK230_HIT_BLOCK 1` | `hitsplat.pack`, and they were **backwards** (see below) |
| `MOCK230_VARP_ATTACK_STYLE 43`, `MOCK230_VARP_RUN 173`, … | `varp.pack`, resolved by `mock230_world_varp("com_mode")` |
| `MOCK230_CHAT_CONTAINER_UID (162 << 16) | 559` | `component.pack` |
| combat-tab varbits | `varbit.pack`; the *bit ranges* stay in the cache |
| `MOCK230_BANK_IFACE 12`, `MOCK230_BANK_COM_*`, the eleven bank varbits | `interface.pack` / `component.pack` / `varbit.pack`, through **`mock230_ids.h`** |
| `MOCK230_EQUIPSTATS_IFACE 84` and interface 84's eighteen text rows | `component.pack` — `equipment_stats_stabatt` and friends |
| the 29-row `k_prayers[]` table in `mock230_prayer.c` | a **`.prayer`** config, which is LostCity's prayers `.dbrow` flattened |
| `MOCK230_HEADICON_PROTECT_MELEE 0` … | `prayers.constant`, LostCity's `headicon.constant` shape |
| `MOCK230_BANK_QTY_1 0` … | `bank.constant`, the same shape as `^attack_style_accurate` |
| `k_worn_slot_by_child[]` in `mock230_world.c` | the `worn_slots` **.enum** — keys are components, values are wear slots |
| `MOCK230_ROOT_IFACE 161`, `MOCK230_INV_BACKPACK 93`, `MOCK230_WORN_IFACE 387` | `mock230_ids.h`, resolved from the packs at boot |

The rule the table encodes: **an id lives in a pack, a tunable lives in a
config, and arithmetic stays in C.** `+8` in the effective-level formula is not
a magic number — it is the formula. `MOCK230_INV_SLOTS 28` is not either; it is
the size of a backpack, and LostCity states it in a `.inv` config for the same
reason it states `size=28` rather than deriving it.

### The engine's own symbol table

C cannot write `bankmain:items` and have a compiler resolve it, which is what
LostCity's engine gets for free. `src/net/mock/mock230_ids.h` is the substitute:
one struct naming every id the engine addresses by hand, filled once at boot by
`mock230_ids_resolve()` out of the packs and the `.constant` files, and
**nothing downstream holds a literal**. Adding an interface is a line in
`tools/gameval_import.names` and a field in that struct.

Three kinds of number stay in C on purpose, and the distinction is worth
keeping:

- **Storage ceilings** — `MOCK230_BANK_TABS`, `MOCK230_PRAYER_MAX`,
  `MOCK230_INV_SLOTS`. They size arrays, so they have to be compile-time
  constants. Content decides how much of the array is used and the loader
  checks it fits.
- **Protocol encodings** — event-mask bits, op numbers, the IF_OPENSUB mount
  type. Those say what the *packet* means, not what the cache calls something;
  no config file could be checked against a cache.
- **Arithmetic** — the `+8` in the effective-level formula is the formula.

`mock230_pack` resolves the table too, so a renamed or dropped symbol is a
validator failure rather than a dead interface discovered by clicking on it.
The trade is that the mock **needs its content tree**: with none, every id is
-1 and `--selftest` says so on the first line instead of running a server that
addresses component 0. `test-mock230` also pins each resolved id to the number
it had as a `#define`, so a regenerated pack that renumbered something fails
there.

That guard has already earned itself once. OpenRune's table calls 160:55
`orbs:worldmap`; at rev 230 the world-map orb is 160:53, and 53 is `wiki_icon`
in the newer table — two components were inserted between the revisions.
Importing the name would have armed the wiki button. That one id stays a
literal in `mock230_worldmap.c` with the clientscript evidence beside it, which
is what a symbol cannot give when the symbol is wrong.

Two of these were only *discoverable* once they were data:

- **The hitsplat ids were the wrong way round.** Type 0 is the blue zero splat
  (sprite 2270) and type 1 is red damage (3521); the mock had damage on 0. Every
  hit drew a block and every miss drew damage, and nothing failed. Each id in
  `hitsplat.pack` now carries the sprite it resolves to and that sprite's
  measured dominant colour, which is what makes the pairing checkable rather
  than assertable.
- **The gameframe list was already OpenRune's**, entry for entry, in the same
  order — its `GameframeLoader` mounts exactly these 24. Naming both sides made
  that visible; as two columns of numbers it was not.

### Varps are declared, not hardcoded

LostCity keeps player variables in two places and so does this: a `.varp` config
declares each one, and content writes it as an ordinary `%name = value`.

```
content/scripts/player/configs/player_controls.varp
  [option_nodef]
  protect=no
  transmit=yes
  scope=perm

content/scripts/player/login.rs2
  %com_mode = ^attack_style_accurate;
  %option_nodef = ^player_auto_retaliate_on;
  %sa_attack = 0;
  %sa_energy = ^sa_max_energy;
```

The engine reads exactly one key, and it is the one that matters. **`transmit=`
decides whether the varp reaches the client**, and an *undeclared* varp is
server-only — the safe default, and what keeps the mock's own counters
(`mock_greeting_count`, `lumbridge_visited`) off the wire while the combat tab's
four go out. `protect` and `scope` are parsed and carried so a config shared
with a LostCity tree keeps its meaning, but this server has neither protected
scripts nor persistence to apply them to.

Two things follow from copying the reference's semantics rather than inventing
some:

- **Assigning a varp always transmits, even when the value is unchanged.**
  LostCity's content contains `%option_nodef = %option_nodef; // resync varp`,
  which only means anything under that rule. It is also what makes an opening
  state work at all: `[login]` setting `%com_mode = 0` on a varp that is already
  0 still has to *tell* the client 0, because the client has never been told
  anything.
- **The encoder is picked by magnitude, not by content.** `VARP_SMALL` carries a
  signed byte; special-attack energy is in tenths of a percent, so a full bar is
  1000 and would land as −24. Content writes `%sa_energy = ^sa_max_energy` and
  never learns there are two packets.

There is no varp id in any header. `com_mode` is resolved through
`pack/varp.pack` at the one place the engine needs it (the attack style, which
the combat formulas read back) — so the engine and the scripts name the same
thing, and the id lives in one file.

---

## 3. The cache already knows the combat bonuses

This is the single most useful thing in the whole system and it is easy to miss.

An OldSchool obj or npc record carries its equipment bonuses in its own **param
table**: ids 0–11 are the twelve bonuses in the order `Mock230CombatParam`
names them, and 14 is the attack rate in ticks. OpenRune's
`cache/src/main/kotlin/org/alter/ParamMapper.kt` documents the mapping;
`cache.osrs230` was checked against it before anything was built on it:

```
obj 1321 Bronze scimitar   [1]=7 (slashattack)  [10]=6 (strengthbonus)  [14]=4
npc 3254 Guard             [5]=18 [6]=25 [7]=19 (stab/slash/crush defence) [14]=4
npc 3028 Goblin            [10]=-15 [5..8]=-15
```

So the mock computes a real OldSchool max hit and a real accuracy roll with no
hand-written bonus table for any item in the game. `mock230_objinfo.c` and
`mock230_npcinfo.c` read them during the one decode pass they already do, and
`test-mock230`'s "combat arithmetic" section pins the values above — because if
a future cache moves those param ids, every fight goes quietly wrong rather than
failing.

A `param=slashattack,7` line in a config overrides what the cache said. It
should be rare enough to deserve a comment.

---

## 4. Where the ids come from, and why they are checked

`tools/gameval_import.py` reads OpenRune's `data/cfg/gamevals-binary/gamevals.dat`
— a Java `writeUTF` table of every id in its cache with the symbolic name its
content refers to it by — and writes the `.pack` files.

```
tools/gameval_import.py --search npcs goblin
tools/gameval_import.py --names tools/gameval_import.names --out src/net/mock/content/pack
```

`tools/gameval_import.names` is the request list; a symbol not in it is not
imported, which keeps the packs a readable subset rather than 3 MB of text.

**OpenRune's cache is revision 235.10 and the mock runs against 230.** An id
that moved between them does not fail loudly — it resolves to a *different* npc,
which spawns and fights and looks entirely plausible. The worked example is
`npcs.goblin`: id 3028 at both revisions, while the mock's original roster used
655, which `cache.osrs230` also calls "Goblin" and OpenRune calls
`goblin_red_soldier_2`. Two monsters, one display name.

So `mock230_pack` checks every one, and prints the cache's combat level beside
the authored hitpoints. That line is not an assertion — neither number derives
from the other — but a level-2 goblin with 75 hitpoints is visibly the huge
spider's row.

`tools/spawn_import.py` does the same job for OpenRune's Lumbridge
`SpawnPlugin.kt`, converting `spawnNpc(npc = "npcs.man", x = 3206, z = 3219)`
into the `==== NPC ====` section of the map square it stands on. `walkRadius` is
dropped on purpose: LostCity carries wander range on the npc *type*, and so does
this tree.

---

## 5. Doors: derive broadly, then let the cache decide

Two loc ids look identical to a cache reader — one closed, one open — and
nothing in the cache says which pairs with which. LostCity records the pairing
as a config overlay, and so does `content/scripts/doors/configs/doors.loc`:

```
[poordoor]
category=door_closed
param=next_loc_stage,poordooropen

[poordooropen]
category=door_opened
param=next_loc_stage,poordoor
```

The engine's door handler is then one generic rule and every door in the game is
data.

OpenRune curates 13 pairs. That is not enough for Lumbridge, so
`tools/door_import.py` proposes the rest from OpenRune's gameval names by five
transforms (`X`→`Xopen`, `X`→`open X`, `Xclosed`→`Xopen`, …). About one in
seven of those is scenery that merely reads like a door —
`wooden_fur_door_always_closed`, `lassar_door_closed_noop`, a dozen Colosseum
gates.

The step that makes the guess safe is the cache:

```
src/build/mock230_pack --prune-doors
```

A closed door offers an "Open" action and its partner does not. A pair failing
that test is deleted from the config, both halves together. 454 derived pairs
went in; 374 survived.

**Stairs and ladders have no config at all.** The direction is already in the
cache as the loc's own menu text — "Climb-up", "Climb-down" — so the engine
reads it there. A config that restates what the cache says is a config that can
disagree with it.

---

## 6. `mock230_pack`

```
src/build/mock230_pack [--content DIR] [--cache DIR] [--cache-out DIR]
                       [--prune-doors] [-v]
```

Validates, and exits non-zero on an error. It checks that every spawned id is in
the cache, that every symbol resolves, that a config with a combat block names
an npc the cache makes attackable, and that every door pair holds up (§5).

`--cache-out DIR` writes a **derived cache**: the source cache copied, with each
authored npc's combat block folded into that record's param table using the ids
in `content/pack/param.pack`. The mock does not need it — it reads the text —
but a cache is the portable form, readable by `tools/dump_npc`, by the client,
and by any other server pointed at it:

```
$ tools/dump_npc/dump_npc --rev osrs230 cache.mock --id 3028
npc 3028 Goblin  params: [10]=-15 [5]=-15 … [2100]=5 [2101]=1 [2104]=25 [2000]=526
                                             hitpoints  attack   respawn  death_drop
```

Two things worth knowing about the export:

- **It copies the whole cache** (~180 MB). An edit appends its new archive to
  `main_file_cache.dat2` and repoints the index at it, so the `.dat2` is part of
  the output whether or not most of it changed.
- **Untouched records keep their original bytes.** Only the 38 npcs with a
  config block are re-encoded, because the npc encoder is a semantic round trip
  rather than a byte-exact one — it cannot distinguish "field absent" from
  "field present and zero", so a re-encoded record is usually a few bytes
  shorter. See `3rd/rscache/EXCEPTIONS.md`.

It refuses to export from a tree the validator rejected. A baked cache is wrong
in exactly the way the errors said and then outlives the message.
