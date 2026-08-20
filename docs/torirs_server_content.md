# The mock's content tree

Everything the rev-230 mock server knows that is not engine mechanics: which
npcs stand where, how hard they hit, what they drop, which doors open into
which, and what the tutors say. It is a **LostCity content tree** — same pack
files, same config syntax, same `.jm2` map format, same `.rs2` scripts — so a
config can be pasted in either direction and mean the same thing.

The ids are not LostCity's. They come from **OpenRune**, whose gameval table
names every id in a modern OldSchool cache, and every one of them is re-checked
against `cache.osrs239` before it is trusted. See §6.

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

The content tree is shared with the unpacked cache — see `OSRS-Content/README.md`.
`pack/` and `maps/` belong to both halves; `server/` is the part that never enters
a cache.

```
OSRS-Content/osrs239-content/
  pack/                      id=name, one file per namespace — written by cachepack
    npc.pack obj.pack loc.pack seq.pack        the cache's own gameval names
    interface.pack component.pack varbit.pack
    varp.pack inv.pack param.pack hitsplat.pack
  maps/
    m50_50.jm2 …             ==== MAP ==== / ==== LOC ==== from the cache,
                             ==== NPC ==== / ==== OBJ ==== spawns from the server
  server/
    pack/
      stat.pack              skills; no cache table holds them
      varp_mock.pack         aliases for varps this world repurposed
      category.pack          obj categories the cache states but does not name
    scripts/
      interface_chat/scripts/chat.rs2            ~chatnpc / ~chatplayer / ~mesbox
      interface_chat/configs/chat.constant       chathead expressions
      areas/lumbridge/configs/lumbridge.npc      combat blocks
      areas/lumbridge/scripts/*.rs2              dialogue: hans, bob,
                                                 father_aereck, tutors, citizens
      skill_combat/configs/combat.param          LostCity's param names, verbatim
      skill_combat/configs/npc_combat.param
      skill_combat/combat.rs2
      skill_combat/configs/equipment.obj          equip requirements, GENERATED
      skill_combat/configs/equipment_disputed.obj the 18 the sources fight over
      skill_thieving/scripts/pickpocket.rs2      [opnpc3] on every human
      skill_thieving/configs/thieving.constant   levels, experience, stun
      skill_prayer/scripts/bury_bone.rs2         [opheld1,_bones]
      skill_prayer/scripts/altar.rs2             [oploc1,altar]
      skill_prayer/configs/bones.constant        prayer experience per bone
      skill_prayer/configs/prayers.dbtable       the prayer table (schema)
      skill_prayer/configs/prayers.dbrow         the 29 prayers (rows)
      skill_prayer/scripts/cheat_prayer.rs2      [debugproc,pray]
      skill_prayer/configs/prayers.constant      overhead icon indices
      general/scripts/food.rs2                   eating, bound by obj category
      general/configs/food.constant              hitpoints healed per food
      doors/configs/doors.loc                    generated + cache-validated
      drop_tables/scripts/*.rs2                  [ai_queue3] drop tables
      drop_tables/configs/lootdrop.constant
      interface_bank/scripts/*.rs2               the bank — docs/mock230_bank.md
      interface_bank/configs/bank.varp           the varps its varbits live in
      interface_bank/configs/bank.constant       quantity modes
      interface_bank/configs/bank.enum           tab index -> tab-size varbit
      player/configs/worn.enum                   worn-tab cell -> wear slot
      player/login.rs2
      build/                                     compiled script pack (gitignored)
```

`server/` is **read at boot**, not packed. LostCity compiles its configs into the
cache its *client* serves, because its content invents npcs and locs. Nothing
here invents one: every npc, obj and loc already exists in `cache.osrs239`
exactly as OldSchool ships it, so a config block is an **overlay** carrying only
what a cache cannot state. Reading the text costs about a millisecond and takes
a build step out of the edit loop.

When you *do* need a portable cache that carries those overlays (npc combat
params, door `next_loc_stage`), `cachepack pack` is the baker: it emits the
client projection (every `client = param:<name>` field) and the server band
from one merged record, driven by `fields/<type>.ini` — see
`CONTENT_PACK_PLAN.md` §5.4. (`mock230_pack --cache-out`, which used to bake
the params on its own, is deleted; `mock230_pack` validates only. See §8.)

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
*equipment bonuses* — see §4 — which is why a config almost never states one.

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

### Content gets first refusal, and that cuts both ways

Every interaction trigger dispatches content first and the engine's own
behaviour second — `[opnpc<n>]`, `[oploc<n>]`, `[opheld<n>]`, `[inv_button<n>]`.
That is what lets the mock run with no script pack at all, and it is also a trap
worth stating once: a script bound to an op the *cache gives a verb to* replaces
the engine's handling of it rather than running alongside it.

A goblin's "Attack" is op 2, so `[opnpc2,goblin]` is the Attack click. The
version of `skill_combat/combat.rs2` that only said a line made goblins say it
and stand there. The fix is one statement, and it is the same call the engine's
own branch makes:

```rs2
[opnpc2,goblin]
npc_say("Ye be dead soon!");
p_opnpc(2);      // without this, "Attack" no longer attacks
```

### Categories: the grouping a cache already states

An OldSchool obj record carries a `category` (config opcode 94), and a trigger
can be bound to one. `[opheld1,_bones]` is every bone in the game — 38 of them
in `cache.osrs239`, and a 39th needs no edit:

```rs2
[opheld1,_bones] @bury_bones(last_slot, last_item);
[opheld1,_cooked_meat] @eat_food(last_slot, last_item);
[opheld1,_cooked_fish] @eat_food(last_slot, last_item);
```

The cache has no table naming categories, so `server/pack/category.pack` does —
six of them, each checked by grouping the obj table and confirming the group is
what the name claims. **Category 0 is not a name and must not be one**: it is
the decoder's default for "no category stated", so a trigger bound to it would
match every uncategorised obj in the game. The engine passes `-1` rather than
`0` for that reason.

Two things a category cannot do here. `[opnpc<n>,_citizen]` — LostCity's way of
binding one script to every townsperson — has no equivalent, because an
OldSchool npc record carries no such grouping; those bindings are one line per
npc. And a loc category is not read at all, so `[oploc1,_prayer_altar]` becomes
`[oploc1,altar]`.

### No ids in headers

Everything the engine addresses by id resolves through a pack at the point of
use. What used to be a `#define` or a literal table in C:

| was | now |
|---|---|
| a 24-entry `{ slot, group }` table in `mock230_world.c` | the `gameframe` **.enum** — keys are components, values are interfaces |
| `MOCK230_HIT_DAMAGE 0` / `MOCK230_HIT_BLOCK 1` | `hitsplat.pack`, and they were **backwards** (see below) |
| `MOCK230_VARP_ATTACK_STYLE 43`, `MOCK230_VARP_RUN 173`, … | `varp.pack`, resolved by `mock230_world_varp("com_mode")` |
| `MOCK230_CHAT_SLOT_UID (162 << 16) | 561` | `component.pack` — `chatbox:chatmodal`, and it was **the wrong component** (see `osrs230_mockserver.md` §3.11) |
| combat-tab varbits | `varbit.pack`; the *bit ranges* stay in the cache |
| `MOCK230_BANK_IFACE 12`, `MOCK230_BANK_COM_*`, the eleven bank varbits | `interface.pack` / `component.pack` / `varbit.pack`, through **`mock230_ids.h`** |
| `MOCK230_EQUIPSTATS_IFACE 84` and interface 84's eighteen text rows | `component.pack` — `equipment_stats_stabatt` and friends |
| the 29-row `k_prayers[]` table in `mock230_prayer.c` | a **`.dbtable`/`.dbrow`** pair, the reference's own schema — the whole C module went with it |
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
`the cache's own gameval table` and a field in that struct.

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
`configs/all.varp.compack` at the one place the engine needs it (the attack style, which
the combat formulas read back) — so the engine and the scripts name the same
thing, and the id lives in one file.

---

## 3. Dialogue

`interface_chat/scripts/chat.rs2` is LostCity's dialogue toolkit, and every
conversation in the tree goes through it. Four procs:

```rs2
~chatnpc("Hello. What are you doing here?");          // npc head, left  (231)
~chatnpc_anim(^chat_angry, "Get yer own!");
~chatplayer("I'm looking for whoever is in charge.");  // player head, right (217)
~chatplayer_anim(^chat_happy, "Thanks!");
~mesbox("You need level 40 Thieving to pick the guard's pocket.");  // no head (229)
```

Each sets the head, the name, the body and the continue row, mounts the
interface into the chatbox slot, arms the continue row as a resume button and
blocks on `p_pausebutton`. A five-line conversation is five statements.

**The speaker is read off the active npc**, `npc_type` for the head and
`npc_name` for the label, which is why a `~chatnpc` line copies across from a
LostCity script unedited — and why no dialogue file writes an npc's name down.
A hand-written "Cooking tutor" beside npc 3219 is a second copy of what the
cache says, and a second copy can disagree.

Three differences from the reference, all with the same root:

- **No paging.** LostCity measures the string with `split_init` and picks one of
  `npcchat1..npcchat4` by line count, because each has a fixed number of text
  components. rev 230 has one interface with one multi-line body (231:6, 67px,
  lineheight 16) that wraps by itself, so there is nothing to split. A ported
  line replaces LostCity's `|` hard breaks with spaces; anything past about four
  lines is clipped rather than paged, which is the one thing this does worse.
- **`<p,expression>` becomes an argument.** The tag is stripped by `split_init`
  in the reference, so the expression is the first argument of the `_anim` form
  instead. `configs/chat.constant` maps the seven one-for-one.
- **No `~p_choice2..5`, and this is the real gap.** rev 230's option dialogue
  (interface 219, `chatmulti`) has *two* components and builds its rows with
  `cc_create` from a clientscript, so `if_settext` cannot address them at all —
  the server drives it with RUNCLIENTSCRIPT, which this server does not send.
  Every ported conversation with a choice in it is linearised, and each one
  names the branches it dropped at the point it drops them. Closing this is the
  highest-value piece of dialogue work left; nothing else about those scripts
  would change.

---

## 4. The cache already knows the combat bonuses

This is the single most useful thing in the whole system and it is easy to miss.

An OldSchool obj or npc record carries its equipment bonuses in its own **param
table**: ids 0–11 are the twelve bonuses in the order `Mock230CombatParam`
names them, and 14 is the attack rate in ticks. OpenRune's
`cache/src/main/kotlin/org/alter/ParamMapper.kt` documents the mapping;
`cache.osrs239` was checked against it before anything was built on it:

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

## 5. Items: what the cache states, and the one thing it does not

The bonuses in §4 are not the half of it. An OldSchool obj record also carries
the name, the description, the model and its recolours, the 2D transforms, the
`cost` (so alchemy values are `× 6/10` and `× 4/10`, never authored), the weight,
stackability, the four stack variants, all three wear positions, the category,
the inventory ops, and the note/placeholder links. `configs/all.param.compack` names four
more that nothing was reading: ranged strength (12 on ammunition, **189** on worn
gear — two ids for one equipment-stats row, and naming only the one OpenRune
documents would silently zero every arrow in the game), attack range (13), and
magic damage in tenths of a percent (299).

So porting items is mostly *not* transcribing tables. It is naming what is
already there, and authoring the one thing that genuinely is not.

### The level you need to wear it

This is the gap, and it is a sharp one: before this existed the mock let a
level-1 character wield a dragon scimitar.

The cache half-states it. Params 434/436 and 435/437 are a `(skill, level)`
requirement pair, and reading them is worth doing — rune scimitar Attack 40,
green d'hide body Ranged 40 + Defence 40, all OldSchool values. But it is
half-stated in two distinct ways, and each cost a wrong first attempt:

- **It covers 698 of 6,153 wearable objs.** Rune and dragon are there; mithril
  and adamant are not, and they need 20 and 30. So the cache is a *cross-check*,
  not the authority — the same relationship this tree already has with npc
  hitpoints.
- **The pair is all the room there is.** Void knight gear needs seven
  requirements, so no cache record can express it. The config's own key is
  therefore repeatable, deliberately unlike the cache's fixed pair.
- **The same pair states the requirement to *make* the record.** A fire
  battlestaff reads Crafting 62, which is what it takes to build one. "Is it
  wearable" does not separate the two — 19 records in this cache are wearable
  *and* carry a creation requirement — so the engine's test is whether the skill
  is a **combat** skill, because no combat skill is ever a creation requirement.
  A non-combat requirement that really does gate wearing (a skill cape needs 99,
  a larupia hat 28 Hunter) comes in through the overlay, stated rather than
  inferred.

a Kronos dump (importer since removed) fills the rest in from Kronos'
`data/items/item_info.json` — 11,512 hand-curated entries whose ladders match
OldSchool exactly — and prints an audit rather than asserting the merge:

```
$ a Kronos dump (importer since removed) --report
kronos entries with a requirement : 1172
  cache already states it exactly : 251  (no line emitted)
  emitted to the overlay          : 790
  no name match in this cache     : 108
  name matches, id drifted 184->239: 65
  withheld, sources disagree      : 18  -> equipment_disputed.obj
```

Items are matched by **display name**, never by id — Kronos is rev 184 and this
cache is 239 — and the 108 that match on neither are printed, not dropped. Most
are abbreviations Kronos writes and the cache does not (`Blue d'hide vamb`).

### Why there is no tie-break rule

The 18 conflicts are the interesting part. Where both sources state a level for
the same skill and the levels differ, **neither side is reliably right**:

```
twisted bow      cache 85 Ranged   kronos 75 Ranged   -> 75
guardian boots   cache 75 Defence  kronos 60 Defence  -> 75
```

Both directions, in the same list. Preferring the cache because it is 55
revisions newer puts the twisted bow ten levels too high; preferring Kronos
because it is hand-curated drops guardian boots fifteen too low. So the importer
**withholds** them and they are settled by hand in
`skill_combat/configs/equipment_disputed.obj`, each carrying both candidates in a
comment and two marked `UNSURE`. The generator does not own that file and will
not overwrite it — the same rule the LostCity exporter has.

### Where it ends up

A sparse table, not a field on every record: **1,496** of 33,747 objs have a
requirement, so eight `(stat, level)` pairs on all of them would cost 2 MB to say
"none" thirty-two thousand times.

```
mock230: content loaded (… 1496 equip reqs (675 from the cache) …)
```

**The requirement is a MERGE, and the parenthesis is the half everyone misread.**
1,496 objs carry one, and only **857** of them come from `param=levelrequire` in
`skill_combat/configs/*.obj`. The other 639 come from the cache's own
`skillrequire` / `levelrequire` params (434/436 and 435/437), read by
`read_requirements` in `mock230_objinfo.c` for every wearable and filtered by
`gates_wearing()`. The `.obj` overlay then **replaces** an obj's set outright
rather than adding to it, which is what lets `equipment_disputed.obj` *correct*
the cache for the eighteen items where the two disagree.

That matters more than it looks. Every prose account of this data — including
this file until 2026-08-02 — quoted the overlay's 857 objs / 1,254 pairs as if it
were the whole table. It is 59% of it. Anything built against the overlay alone
passes a structural check against the `.obj` files and silently stops gating 639
items, a rune scimitar among them, whose Attack 40 is the cache's.

Two things about the check are decisions rather than plumbing. It reads the
**base** level, so a potion does not let you wield what you could not wield
sober — which is what LostCity's `levelrequire_*` labels do with `stat_base`. And
an obj with **no** requirement is wearable, so a cache the importer has not been
run against stays playable rather than refusing everything it does not recognise.

**The check itself is content now, and this paragraph used to argue the
opposite.** It was `mock230_equipment_may_wear` in C, on the argument that the
rule "applies to every wearable obj in the cache, and LostCity's per-item form —
528 lines of `[opheld2,rune_scimitar] @levelrequire_attack(40, last_slot);` —
would be a second copy of a table the content tree already states". Half of that
survives and half does not. **The values are still data** and were never
relocated: the `.obj` params remain the authored home, `mock230_pack` still
validates them, and `mock230_obj_require` still holds them. What did not survive
is the inference that a rule reading data must therefore be C. A
refusal-with-a-message is a rule, and rules are content's:
`~levelrequire_check` (`skill_combat/scripts/levelrequire.rs2`) decides and
speaks, from one `[opheld2,_]` binding rather than 857, and
`mock230_equipment_may_wear` is deleted (`osrs230_mockserver.md` §3.18, triage
§10.1).

It reads both halves in their own natural forms — the overlay through a
generated `levelrequire.dbtable` (125 rows, transposed: a row is one
*requirement* and `obj` is a `LIST`, because the requirements repeat hard), the
cache's two pairs through `oc_param`, which is already the opcode for reading a
cache param.

`mock230_pack` pins the ladder, so a source swapped for a worse one shows up as
a number out of order rather than as silence:

```
equipment requirements
        1496 requirement rows checked, 11 ladder values pinned
```

The check that actually matters is not that one and not the generator's
`--check`: `mock230 --selftest` walks **every** one of the 1,496 at both sides of
its own boundary — every skill set to `peak-1`, then to `peak`, with the boosted
level pinned at 99 throughout so a gate reading `stat` instead of `stat_base`
fails — and compares content's answer to the C table. A regeneration makes a
textual diff go away without anyone learning the gate had been wrong in between.

---

## 6. Where the ids come from, and why they are checked

the cache's own gameval table reads OpenRune's `data/cfg/gamevals-binary/gamevals.dat`
— a Java `writeUTF` table of every id in its cache with the symbolic name its
content refers to it by — and writes the `.pack` files.

```
the cache's own gameval table --search npcs goblin
the cache's own gameval table --names the cache's own gameval table \
    --out OSRS-Content/osrs239-content/names
```

`the cache's own gameval table` is the request list; a symbol not in it is not
imported, which keeps the packs a readable subset rather than 3 MB of text.

**`--out` is `names/`, not `pack/`.** This command used to say `pack/`, and the
script opened every output with `"w"` — so running it as documented truncated
`configs/all.npc.compack` from 16,292 lines to 39 and `configs/all.varp.compack` from 5,705 to 11,
after which a `cachepack unpack` refilled them from the cache and reverted every
alias the import had just made. An imported name is an *authored* name from a
foreign revision, so it belongs in layer 1; `pack/` is regenerated wholesale from
the cache's own gameval table. The script now refuses a `pack/` output and merges
into whatever is already in the target rather than replacing it. See
[`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md) §4.1 and §6.2.

**OpenRune's cache is revision 235.10 and the mock runs against 230.** An id
that moved between them does not fail loudly — it resolves to a *different* npc,
which spawns and fights and looks entirely plausible. The worked example is
`npcs.goblin`: id 3028 at both revisions, while the mock's original roster used
655, which `cache.osrs239` also calls "Goblin" and OpenRune calls
`goblin_red_soldier_2`. Two monsters, one display name.

So `mock230_pack` checks every one, and prints the cache's combat level beside
the authored hitpoints. That line is not an assertion — neither number derives
from the other — but a level-2 goblin with 75 hitpoints is visibly the huge
spider's row.

an OpenRune spawn dump (importer since removed) does the same job for OpenRune's Lumbridge
`SpawnPlugin.kt`, converting `spawnNpc(npc = "npcs.man", x = 3206, z = 3219)`
into the `==== NPC ====` section of the map square it stands on. `walkRadius` is
dropped on purpose: LostCity carries wander range on the npc *type*, and so does
this tree.

---

## 7. Doors: derive broadly, then let the cache decide

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
a naming-convention pass (importer since removed) proposes the rest from OpenRune's gameval names by five
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

## 8. `mock230_pack`

```
src/build/mock230_pack [--check-only] [--content DIR] [--cache DIR]
                       [--prune-doors] [-v]
```

Validates, and exits non-zero on an error. It checks that every spawned id is in
the cache, that every symbol resolves, that a config with a combat block names
an npc the cache makes attackable, that every door pair holds up (§6), that
every namespace's allocation base clears the cache's high-water mark, and that
`server/pack` (when present) agrees byte-for-byte with the text parse.
`--check-only` is accepted explicitly — validation is all the tool does, so the
flag changes nothing, but the documented `mock230_pack --check-only` invocation
runs as written.

It used to also write: `--cache-out` copied the source cache and folded the
server overlays into each record's param table. That was the second baker
`CONTENT_PACK_PLAN.md` §5.4 retires — `cachepack pack` now emits the cache
projection and the server band from one merged record, so the export here is
deleted rather than kept as a diverging twin.
