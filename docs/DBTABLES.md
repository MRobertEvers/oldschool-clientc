# dbtables — the client database, and the two populations in it

What a dbtable is, where the two populations come from, how each is named, and
how server content reads one. Written from `docs/DBTABLE_NAMING_PLAN.md`'s
execution; every number below says where it was measured.

The headline finding is at the top because it changes what the rest of the
document is about: **the cache names all 246 of its dbtables, and this tree was
reading that name wrong.** Gameval archive 10 is not a flat name list. See §4.2.

---

## 1. What a dbtable is

The client-side database. Three cache structures:

| structure | where | holds |
|---|---|---|
| **dbtable** | config group 39 | one table's *schema* — per column, the tuple types and optional defaults |
| **dbrow** | config group 38 | one row's *values*, plus the id of the table it belongs to |
| **dbtableindex** | cache table 21 | the derived *find* index, one archive per table |

A column holds `tuple_count` tuples of `type_count` typed fields each, laid out
tuple-major. Type ids are ScriptVarType base codes: `0` int, `1` boolean, `36`
string, and a long tail of reference types (`17` stat, `22` coord, `30` loc,
`32` npc, `59` mapelement, `74` dbrow). `RSCache_DbTypeIsString` is the
authority on which decode as strings — validated across the osrs230/239/jan2026
caches, where 36 is the only string type.

In table 21, file 0 of a table's archive is the master index (every row id) and
file N ≥ 1 is the inverted index for column N-1. `DB_FIND` reads a column file,
`DB_FINDALL` the master. **It is a projection of the dbrows, not authored
content** (`cp_decode.c:4352`) — cachepack round-trips it byte-exactly rather
than recomputing it, so a hand edit would survive and silently disagree with the
rows it claims to index. Do not make one.

The client reads all of this through CS2's `DB_*` opcodes, 7500–7510.

---

## 2. Where they are used

Measured, not guessed: every `db_getfield` / `db_getfieldcount` /
`db_find_with_count` / `db_find_filter_with_count` literal in this tree's 9,725
decompiled clientscripts carries a packed column id, and
`db_findall_with_count` a bare table id. Unpacking `(table << 12) | (column << 4)`
over all of them gives:

- **422 clientscripts** touch the database.
- **128 of the 246 tables** are read by at least one of them.
- **Every referenced table id resolves to a table the gameval archive names** —
  zero orphans, which is itself a check on the packing.
- **No script references a column above the count §4.2's decode yields** — which
  is a second, independent check on that decode.
- 74 call sites pass a non-literal column id and are not resolvable statically.

Walking each interface's `onload`/`onop`/`onmouseover`/… hooks and taking the
transitive `~scriptNNNN` closure maps 57 of those 128 tables back to an
interface. The precise ones read exactly as you would expect:

| table | interface |
|---|---|
| `hair_styles`, `facial_hair_styles`, `torso_styles`, `sleeve_styles`, `legging_styles`, `shoe_styles`, `hand_styles` | `makeover` |
| `hiscores_skill_info`, `hiscores_activity_info`, `hiscores_bosses_info` | `hiscores` |
| `spell_override`, `spell_override_list` | `magic_spellbook` |
| `combat_interface_weapon_category` | `combat_interface` |
| `jigsaw` | `jigsaw` |
| `quetzal` | `quetzal_menu`, `quetzalwhistle_menu` |
| `fairyring` | `fairyrings_log` |
| `castle_drakan_room`, `castle_drakan_door` | `castle_drakan_world_map` |
| `sailing_boat*`, `sailing_crew`, `sailing_sidepanel_facility` | `sailing_sidepanel` |

Two honest caveats on that map. The closure **over-reaches** where a generic
helper is shared: the eleven `cluehelper_clue_*` tables come out attributed to
`sailing_menu` because one shared formatter reaches them all. And the 71
unmapped tables are not unused — most UI hooks in this era are armed at
*runtime* by the server (`IF_SETEVENTS`, `RUNCLIENTSCRIPT`; see
`docs/UI_ERA_PORTING_GUIDE.md`), so they are not in the `.if` files to be
walked. Absence from the table is absence of evidence.

The full per-table numbers are in §7.

---

## 3. Two populations, one id space

```
dbtable   0..258      the cache's, 246 tables with gaps   |   2048+    the server's
dbrow     0..16939    the cache's, 16,711 rows with gaps  |   65536+   the server's
```

They must not collide, because `table:column` compiles to
`(table << 12) | (column << 4)` — one flat id space — and a server table landing
on a cache table's number would make `db_getfield` read whichever the loader
inserted last.

**Server ids come from fixed bases far above the cache** (2026-08-06; they used
to sit at the high-water mark, 259+/16940+, which a manually patched future
cache would land on — Jagex added 13 tables between rev 230 and 239 alone).
The bases live in `content_register.c`; the allocator reads them, and the ids
live in `pack/dbtable.alloc` / `pack/dbrow.alloc` — the server's allocation
ledger, not the compack. Width caps verified before choosing: the VM's column
unpack masks the table id to 0xffff, the client host allows 20 bits, and a
dbrow id is only ever a plain int value. The old rule follows this paragraph
for history. `content.ini` declares both namespaces `ids = server`, `names = cache` —
the only two namespaces in the tree where those two axes disagree, and the
disagreement is the point: the cache names its own tables and this tree must not
rename them, while the ids above the mark are ours.

That declaration is new. It used to say `ids = cache`, in
`content_register.c`, with the note *"neither has an encoder, so authored content
cannot create one either way"*. Both halves were stale — the encoders exist
(`RSCache_Dat2ConfigDbTableEncode`, held to byte-identity against every record in
`cache.osrs239`), and a *cache* encoder was never what a server table needed:
`mock230_db.c` parses the server's own text. `tools/ss_allocate.py` had both
namespaces in `DEFAULT_SERVER_NAMESPACES` the whole time, so `coord_pair_table`
(259) and `combat_style_table` (260) were allocated and worked while the register
said they could not exist. `docs/CONTENT_ARCHITECTURE.md` §8.2(c), third
occurrence.

### What actually checks the boundary

Less than the compack header claims, and this was checked by reading the code
rather than the comments.

- `validate_id_bases` (`src/net/mock/mock230_pack.c:553`) checks that each
  namespace's declared `server_base` is **above the cache's largest id**, and
  reports ids that sit between the two. It is not a boot check — it lives in the
  `mock230_pack` validator binary, run by `make -C src test-content`. It reads
  `ContentRegister_Defaults`, **not** the tree's `content.ini`, so it checks the
  C table's base and nothing the ini says.
- It checks the *base*, never an individual authored id. An authored block
  landing on a cache id is not rejected — see §8.
- `dbrow` declares no base at all (`server_base` 0), so `validate_id_bases`
  skips it entirely and allocation runs purely off the high-water mark.
- Nothing at all checks that `configs/all.dbrow`'s `table=` lines agree with
  `configs/all.dbtable.compack`. See §6.

The compack header's line *"`mock230_content.c` checks it on every boot"* is
wrong on both the file and the "every authored id" scope. `mock230_content.c`
checks name **collisions** (`validate_symbols`), not id bounds.

---

## 4. The files and the pipeline

```
cache.osrs239                  cachepack unpack            OSRS-Content/osrs239-content
  config group 39   ────────────────────────────────────►   configs/all.dbtable       (schema text)
  config group 38   ────────────────────────────────────►   configs/all.dbrow         (row text)
  gameval archive 10 ───────────────────────────────────►   configs/all.dbtable.compack  (table names)
  gameval archive 9  ───────────────────────────────────►   configs/all.dbrow.compack    (row names)
  cache table 21    ────────────────────────────────────►   dbindex/ + pack/21_dbtableindex.pack
                    ◄────────────────────────────────────   cachepack pack

server/scripts/**/configs/*.dbtable ──┬─► ssc_symbols.c ──► `table:column` compiles to
                                      │                     (table << 12) | (column << 4)
server/scripts/**/configs/*.dbrow   ──┴─► mock230_db.c  ──► db_find / db_getfield at runtime
```

The two server readers want different halves. `ssc_symbols.c` reads only
`.dbtable`, because a column *index* is all the compiler needs — it counts
`column=` lines in order, which is the third trap in §4.1 stated a second way.
`mock230_db.c` reads both, in two passes, because a `data=` line cannot be
parsed without its column's declared tuple types: `data=coord_pair,A,B` is two
coords or one string depending on the table.

The two compacks are **member indexes**: `<record id>=<name>`, keyed by name in
the data files. `configs/all.dbtable`'s block header and every `table=` line in
`configs/all.dbrow` are that name, so the three files are keyed together and a
rename in one is a rename in all three.

### 4.1 The text format's two traps

Both already paid for once, both documented in `config/cp_db.c`'s header:

- **A comma can be inside a string value.** 1,245 of this cache's 96,439
  `values=` lines hold one (`Graveyard of Heroes, Part 2`). They are escaped
  `\,`, backslash `\\`. Never split a `values=` line on a bare comma.
- **`columns=` is the allocation count, not the number of present columns.**
  8,257 of 16,711 dbrows have `columns=` larger than their highest present
  column. It is written explicitly and must never be re-derived.

And a third, from the schema: **column order is the tuple order**, addressed by
index. Reordering columns silently re-points every lookup in every consumer.

### 4.1a A column declaration, and the second type alphabet

A column is one `columndef=` line — its index, the cache's name for it, and its
tuple types:

```
columndef=13:startcoord,coord
columndef=23:requirement_stats,stat,int
columndef=9:releasedate,int,int,int
defaults=9:0:-1,-1,-1
```

Both halves used to be missing. The line was `defaulttypes=13:22` on a dbtable and
`types=13:22` on a dbrow: no name, and the type as a bare number. Both old
spellings are still parsed, so an un-regenerated tree still packs — dropping them
would have turned a stale tree into a silent empty-record write rather than an
error.

The name is documentation; the packer reads past it, so a tree whose gameval names
have moved on is still a correct tree. It is emitted **empty rather than omitted**
when unnamed (`columndef=7:,int`) — the field's position is what makes the line
parseable.

**`columndef` and not `column`, which it was called first.** The server's own
`.dbtable` grammar is `column=<name>,<type>...`, and `mock230_db_load` walks the
whole content tree for `*.dbtable`, which matches `configs/all.dbtable`. So
`column=13:startcoord,coord` reached the *server's* parser as a column named
`13:startcoord` with one unrecognised type, and the mock refused to boot. Two
grammars cannot share a key in one file namespace.

The type codes are ScriptVarType **base codes**, and they are not the character
codes `cp_param_type_char` uses — a param's coord is `'c'` (99), a dbtable's is 22.
Twenty-five distinct codes occur across this cache. Each name required two signals
to agree: the column names carrying it (from archive 10 — a column literally called
`npc` or `stat` is the cache stating its own type), and the value range against
each namespace's highest id, which *excludes* candidates rather than choosing.

| code | name | code | name | code | name |
|---|---|---|---|---|---|
| 0 | `int` | 22 | `coord` | 39 | `inv` |
| 1 | `boolean` | 23 | `graphic` | 41 | `category` |
| 6 | `seq` | 30 | `loc` | 59 | `mapelement` |
| 9 | `component` | 31 | `model` | 73 | `struct` |
| 10 | `idkit` | 32 | `npc` | 74 | `dbrow` |
| 11 | `track` | 33 | `obj` | 209 | `varp` |
| 13 | `namedobj` | 36 | `string` | | |
| 14 | `synth` | 17 | `stat` | | |

The interesting derivations: **74 = `dbrow`** because its highest value is 16939,
exactly this cache's highest dbrow id and beyond every other namespace, and because
one column carrying it is called `sailing_charting_core` — a table name. **13 =
`namedobj` vs 33 = `obj`**: both sit in the obj range so the range cannot separate
them, but the column *named* `namedobj` carries 13 while `item` and the eleven
`wearpos_*` columns carry 33. **9 = `component`** and **22 = `coord`** fit no flat
namespace at all — 9's values are `(interface << 16) | child`, 22's are packed
coords reaching 855,167,577.

Three codes are left as bare numbers, because one signal is not two: **8** (all 140
of its values are literally `10`), **26** (`enum` and `struct` both fit), and
**118** (one column, six values in 155..160). An unknown code round-trips as its
own number, so a newer cache cannot lose a type this table has never seen.

### 4.2 Gameval archive 10 is keyed, not flat — and this tree read it flat

This is the finding the naming pass turned on.

Archive 9 (dbrow) really is a flat name list: file 0 is the 21 bytes
`quest_animalmagnetism`. Archive 10 (dbtable) is not. A record is a keyed
sequence terminated by a zero byte:

```
u8 key ; cstring text        key 1       the table's name
                             key n >= 2  the name of column (n - 2)
```

Table 0 in hex, abridged:

```
01 71 75 65 73 74 00  02 69 64 00  03 73 6f 72 74 6e 61 6d 65 00 …
   q  u  e  s  t         i  d          s  o  r  t  n  a  m  e
```

`cp_names.c`'s seeder read that with `sanitise_name`, which collapses every byte
outside `[A-Za-z0-9_.+-]` to `_`, into a `char name[256]`. The result was

```
0=_quest__id__sortname__displayname__release_type__type__members__difficulty__…
```

— truncated at 255 characters. **That was not a derived name and not a bad
name. It was `quest` plus its 49 column names with the framing bytes turned into
underscores**, and it was the spelling every dbtable in this tree answered to,
including the `[block]` headers of `all.dbtable` and all 16,711 `table=` lines.

It is the same shape as archive 14, which carries an interface *and* all of its
components in one record and which `content.ini` already documents: *"Read flat
it looks like one 63-character name per interface, which is why it went
unclaimed."* Archive 10 is the second one, and nothing knew.

**Four independent checks on the decode**, all over cache.osrs239's 246 records:

1. **Structure.** All 246 parse to exactly that shape — keys 2..N ascending and
   contiguous, one terminator, zero trailing bytes. None rejected.
2. **Byte identity with what was there.** Re-flattening the decode reproduces
   all 246 previous compack lines *character for character*. The correspondence
   between the mangled line and the record is proved, not assumed.
3. **Schema agreement.** For every table,
   `highest present column + 1 ≤ columns= ≤ decoded column count`. No config
   record declares a column the gameval does not name; 118 tables have
   `columns=` exactly equal to the decoded count and 128 have it lower, which is
   the alloc-count semantics of §4.1 and never the other way round.
4. **Consumer agreement.** No column index in the 422 clientscripts of §2 sits
   above the decoded count, for any of the 128 tables they read.

Plus the corroborating signal the plan called B1: 242 of 246 tables have rows,
the rows carry archive 9's own names, and those names agree with the decoded
table name everywhere and contradict it nowhere. Where the crude token test
misses — `hair_styles` rows are `dreadlocks`/`tonsure`/`mohawk`, `furniture`
rows are `poh_armchair_1`, the twelve `drakan_*` tables use `dr_*` — the
agreement is obvious on inspection.

Both defects that follow from the flat read are now fixed in
`3rd/rscache/tools/cachepack/cp_names.c`:

- `keyed_gameval_name()` decodes archive 10 in the seeder, so a re-seed writes
  `quest`. Cross-checked against a second, independently written decoder:
  **246/246 identical**.
- `cp_names_emit_gamevals` now **skips archive 10**, as it already skipped 14.
  It did not, and `dbtable` is a `CP_Type` with a gameval archive, so
  `cachepack pack --gamevals` wrote the pack's one name back as a flat string —
  replacing the keyed record and destroying roughly 3,000 column names that
  exist nowhere else in the cache.

---

## 5. How server content uses one — `combat_style_table` end to end

The schema, `server/scripts/skill_combat/configs/combat.dbtable`:

```
[combat_style_table]
column=damagestyle,int,LIST
column=damagetype,int,LIST
```

`LIST`, `INDEXED` and `REQUIRED` are *flags*, not types; `mock230_db.c` tells
them apart by being upper-case, which is the only discriminator the format
offers.

Fifteen rows, one per weapon category, each appending one tuple per `data=`
line — so **the row's length is the weapon's style count**:

```
[weapon_spear_table]
table=combat_style_table
data=damagestyle,^style_melee_controlled
data=damagestyle,^style_melee_controlled
data=damagestyle,^style_melee_controlled
…
```

Read by `~combat_get_weapon_style_data`, with `%com_mode` — the slot the combat
tab's button wrote — clamped to the row length:

```
%com_mode = min(%com_mode, sub(db_getfieldcount($styles, combat_style_table:damagestyle), 1));
return(db_getfield($data, combat_style_table:damagetype, $mode));
```

The clamp is not defensive tidiness: a player who last held a four-style weapon
and picks up a three-style one reads past the end of the row without it.

**The spear row is why this is a table and not a heuristic.** All four of a
spear's styles are *controlled*, and the damage type walks stab / slash / crush
across the first three. No rule over "style index" expresses that, and any
heuristic that gets a scimitar right gets a spear wrong in every slot. The
reference authors the table; so does this tree. `docs/CONTENT_ARCHITECTURE.md`
§8.2(b) is the general form — port the proc, not the field it reads.

Note what the server does **not** get: the cache's 16,711 rows *with their values*.
It does, however, read the files — see the phantom-load finding in §8. An earlier
draft of this document said `mock230_db.c` "loads only the server population",
which is false; it loads whatever `*.dbtable`/`*.dbrow` it finds anywhere under
the content directory, and `configs/all.dbtable` matches.

---

## 6. Editing workflows

| change | what to run |
|---|---|
| edit a `data=` in a server `.dbrow` | restart the server. No rebuild — the text is read at load. |
| add a server table or row *name* | `make -C src mock230-scripts` (runs `ss_allocate.py`, then `sscompile`) |
| add or reorder a **column** | see the trap in §4.1 — reordering re-points every consumer |
| rename a cache table | below |

### Renaming a cache table

Four steps, verified end to end on table 0 (`quest`):

1. **The name line** in `configs/all.dbtable.compack` — the compack is now
   purely the cache's (server allocations live in `pack/dbtable.alloc`), so
   there is no marker discipline left to respect; every line is a cache id.
2. **Re-key the data files with the tool**, never by hand:

   ```
   cd 3rd/rscache && ./tools/cachepack/cachepack unpack \
       --cache ../../cache.osrs239 --rev osrs239 \
       --src ../../OSRS-Content/osrs239-content --types dbtable,dbrow
   ```

   Measured on table 0: **exactly 215 lines changed** — one `[block]` header in
   `all.dbtable` and 213 `table=` lines in `all.dbrow` — and both compacks came
   back byte-identical, prose header and allocator marker intact. The re-unpack
   is idempotent for everything it was not asked to change, which is *not* the
   general warning `CONTENT_ARCHITECTURE.md` §6.4 gives for other types.
3. **Round-trip.** `cachepack verify … --types dbtable,dbrow` → 246/246 and
   16,711/16,711 exact, `lost-here` 0.
4. **Gates.** `make -C src test-content`, `make -C src test-mock230`, and a
   booted embed binary.

### What none of that proves — and the defect it found

`cachepack verify` reads records **from the cache**, unpacks them to text in
memory and packs them back. It never opens `configs/all.dbtable` or
`configs/all.dbrow`. So a compack renamed with the data files left stale
verifies **fully green**. Verify is not a check on step 2.

`cachepack pack` is the only thing that reads them, and it used to be silent
too. `cp_pack_dbrow` resolved `table=` with a bare `cp_name_find` — the one
reference in the whole tool that did not go through `cp_resolve_ref`. On a miss
that returns -1, and -1 is exactly how the decoder spells *"opcode 4 never
appeared"*, so the encoder simply omits the opcode: the row packs with **no
table binding at all** and `DB_GETROWTABLE` answers -1.

Measured as the negative control: staling one `table=` line took the dbrow group
from 1,774,143 to 1,774,141 bytes, and the run reported `0 failed, 0 unknown
keys, 0 unresolved names`. It is now `cp_resolve_ref`, which warns and accepts a
bare number — the contract every other reference already had. The same control
now prints

```
cachepack: unknown dbtable reference '_quest__id__sortname__displayname'
Done. 16957 records written, 0 failed, 0 unknown keys, 1 unresolved names.
```

**Nothing else in the tree notices a stale `table=`.** `mock230_db.c` reads only
the server population; `sscompile` reads the compack, not the data files; the
client reads the cache. `make -C src test-mock230` stays green through it. That
is a finding, not a failure: renames are validated by `cachepack pack` and by
nothing else.

---

## 7. The naming register

Every table's name below is the cache's own, decoded per §4.2, and there is no
tiering to report: this was never a set of inferences that needed ranking. The
name is what gameval archive 10's key 1 says, the decode has the four
independent checks of §4.2, and two independently written decoders agree on all
246. The four rowless tables (17, 169, 183, 240) are named on the same footing
as the rest — the cache names them whether or not this revision ships rows.

**All 246 are shipped.** Table 0 went first, by hand and through the tool, as the
one-before-many check; the other 245 followed as a single layer-0 regeneration —
drop the machine-written half of `configs/all.dbtable.compack` (every line
matching `^\d+=_` above the allocator marker; the prose header, the marker and
259/260 do not match and stay) and re-unpack:

```
cd 3rd/rscache && ./tools/cachepack/cachepack unpack \
    --cache ../../cache.osrs239 --rev osrs239 \
    --src ../../OSRS-Content/osrs239-content --types dbtable,dbrow
```

245 lines dropped, 246 names written, 0 still flat-read, 259 and 260 in place
below an untouched marker. All 246 `[block]` headers in `all.dbtable` and all
16,711 `table=` lines in `all.dbrow` re-keyed — 0 mangled, 242 distinct. Round
trip 246/246 and 16,711/16,711 exact, `lost-here` 0. The independent decoder of
§4.2 agrees with the seeder on 246/246.

Columns are the decoded count (which is ≥ `columns=`); rows are from
`all.dbrow`; CS2 is the §2 script count; the interface column is the closure,
with §2's caveats.

| id | name | cols | rows | CS2 | interface (closure) |
|---:|---|---:|---:|---:|---|
| 0 | `quest` | 49 | 213 | 41 | chartering_menu_side, cr_ui … |
| 1 | `events` | 9 | 17 | — | — |
| 2 | `cr_module` | 4 | 4 | 2 | cr_ui |
| 3 | `cluehelper_cluetype` | 8 | 3 | 2 | — |
| 4 | `cluehelper_clue_anagram` | 9 | 104 | 5 | chartering_menu_side, sailing_menu |
| 5 | `cluehelper_clue_map` | 7 | 41 | 5 | chartering_menu_side, sailing_menu |
| 6 | `cluehelper_clue_cipher` | 9 | 18 | 5 | chartering_menu_side, sailing_menu |
| 7 | `cluehelper_clue_coordinate` | 9 | 169 | 5 | chartering_menu_side, sailing_menu |
| 8 | `cluehelper_clue_cryptic` | 15 | 289 | 7 | chartering_menu_side, sailing_menu |
| 9 | `cluehelper_clue_emote` | 14 | 126 | 9 | chartering_menu_side, sailing_menu |
| 10 | `cluehelper_clue_fairyring` | 10 | 11 | 5 | chartering_menu_side, sailing_menu |
| 11 | `cluehelper_clue_falobard` | 8 | 20 | 5 | chartering_menu_side, sailing_menu |
| 12 | `cluehelper_clue_hotcold` | 8 | 142 | 5 | chartering_menu_side, sailing_menu |
| 13 | `cluehelper_clue_music` | 9 | 25 | 6 | chartering_menu_side, sailing_menu |
| 14 | `cluehelper_clue_skillchallenge` | 8 | 60 | 5 | chartering_menu_side, sailing_menu |
| 15 | `cluehelper_target_npc` | 8 | 222 | 4 | — |
| 16 | `cluehelper_target_loc` | 8 | 105 | 3 | — |
| 17 | `cluehelper_target_mapzone` | 7 | — | 2 | — |
| 18 | `cluehelper_target_coord` | 7 | 535 | 3 | — |
| 19 | `cluehelper_target_key` | 12 | 12 | 5 | — |
| 20 | `cluehelper_target_kill` | 7 | 14 | 3 | — |
| 21 | `cluehelper_requirement_obj` | 4 | 23 | 1 | — |
| 22 | `cluehelper_requirement_obj_param_trail_item` | 2 | 7 | 1 | — |
| 23 | `cluehelper_requirement_quest` | 3 | 1 | 1 | — |
| 24 | `cluehelper_requirement_stat` | 2 | 62 | 1 | — |
| 25 | `cluehelper_challenge_question` | 1 | 90 | 1 | — |
| 26 | `cluehelper_challenge_box` | 1 | 3 | 1 | — |
| 27 | `cluehelper_combat_encounter` | 2 | 9 | 2 | — |
| 28 | `cluehelper_outfit` | 24 | 134 | 4 | — |
| 29 | `dbg_dummy_table` | 1 | 2 | — | — |
| 30 | `fsw_info_fresh_table` | 1 | 1 | 3 | — |
| 31 | `fsw_info_normal_table` | 1 | 1 | 1 | — |
| 32 | `fsw_points_info_table` | 1 | 2 | 2 | — |
| 33 | `fsw_points_boss_info_table` | 1 | 2 | 2 | — |
| 34 | `item_transmog` | 7 | 205 | 2 | — |
| 35 | `combination_lock_dataset` | 5 | 7 | 1 | — |
| 36 | `combination_lock_values` | 2 | 15 | 4 | — |
| 37 | `hair_styles` | 6 | 57 | 1 | makeover |
| 38 | `facial_hair_styles` | 4 | 15 | 1 | makeover |
| 39 | `omnishop_shop_data` | 19 | 19 | 16 | — |
| 40 | `omnishop_stock_data` | 26 | 291 | 8 | deadmanskull_interface |
| 41 | `omnishop_currency_data` | 4 | 44 | 5 | — |
| 42 | `omnishop_purse_data` | 2 | 5 | — | — |
| 43 | `whisperer_seed_spawns` | 5 | 4 | — | — |
| 44 | `music` | 15 | 876 | 16 | chartering_menu_side, sailing_menu |
| 45 | `woodcutting_resource` | 11 | 30 | — | — |
| 46 | `woodcutting_basic_resource_data` | 10 | 26 | — | — |
| 47 | `gathering_event_sapling_loc` | 3 | 4 | — | — |
| 48 | `group_gathering_resource` | 7 | 18 | — | — |
| 49 | `gathering_event_chance_data` | 6 | 12 | — | — |
| 50 | `gathering_event_events_list` | 1 | 1 | — | — |
| 51 | `misc_woodcutting_resource_data` | 8 | 3 | — | — |
| 52 | `dt2_lassar_barrier` | 3 | 9 | — | — |
| 53 | `dt2_lassar_remnant` | 4 | 10 | — | — |
| 54 | `dt2_lassar_door` | 8 | 2 | — | — |
| 55 | `dt2_lassar_chest` | 11 | 3 | — | — |
| 56 | `dt2_lassar_ghosts` | 1 | 1 | — | — |
| 57 | `dt2_lassar_npcs` | 1 | 1 | — | — |
| 58 | `dt2_lassar_items` | 1 | 1 | — | — |
| 59 | `dt2_lassar_braziers` | 2 | 1 | — | — |
| 60 | `dt2_scar_maze` | 4 | 3 | — | — |
| 61 | `speedrun` | 8 | 16 | 9 | speedrunning_panel |
| 62 | `clan_setting_options_list` | 5 | 4 | 1 | — |
| 63 | `varlamore_thieving_house` | 12 | 3 | — | — |
| 64 | `quetzal` | 9 | 14 | 6 | quetzal_menu, quetzalwhistle_menu |
| 65 | `torso_styles` | 7 | 20 | 1 | makeover |
| 66 | `sleeve_styles` | 5 | 17 | 1 | makeover |
| 67 | `legging_styles` | 5 | 22 | 1 | makeover |
| 68 | `shoe_styles` | 5 | 2 | 1 | makeover |
| 69 | `hand_styles` | 5 | 2 | 1 | makeover |
| 70 | `vmq3_tower_trial_3` | 5 | 4 | — | — |
| 71 | `vmq3_tower_trial_4_cone` | 2 | 4 | — | — |
| 72 | `pendant_of_ates_teleports` | 4 | 6 | 2 | pendant_of_ates |
| 73 | `eaa_shame_game` | 2 | 19 | — | — |
| 74 | `varlamore_wyrm_agility_route` | 8 | 2 | — | — |
| 75 | `huey_special_attack` | 3 | 4 | — | — |
| 76 | `dynamic_builders_demo_sets` | 1 | 1 | 1 | — |
| 77 | `prepot_device_loadout_ui` | 5 | 4 | 5 | — |
| 78 | `combat_interface_weapon_category` | 2 | 36 | 2 | combat_interface |
| 79 | `hiscores_skill_info` | 3 | 24 | 10 | hiscores |
| 80 | `hiscores_activity_info` | 3 | 10 | 2 | hiscores |
| 81 | `hiscores_bosses_info` | 3 | 69 | 2 | hiscores |
| 82 | `region_data` | 14 | 23 | 12 | league_summary |
| 83 | `toggle_list_interface` | 3 | 8 | 2 | — |
| 85 | `leagues_echo_bosses` | 11 | 10 | 5 | — |
| 86 | `magic_enchant` | 10 | 91 | — | — |
| 87 | `charges` | 5 | 44 | — | — |
| 88 | `synth` | 4 | 668 | 2 | — |
| 89 | `fairyring` | 12 | 64 | 2 | fairyrings_log |
| 90 | `didyouknow` | 6 | 48 | — | — |
| 91 | `multirunes` | 2 | 2 | 1 | — |
| 92 | `comborune_recipe` | 11 | 14 | — | — |
| 93 | `vmq4_metzli_boss_special_teleport` | 5 | 12 | — | — |
| 94 | `vmq4_teleporters` | 4 | 9 | — | — |
| 95 | `vmq4_zema_taht_translations` | 3 | 32 | — | — |
| 96 | `vmq4_sun_puzzle_altars` | 2 | 8 | — | — |
| 97 | `vmq4_moon_puzzle_roots` | 2 | 7 | — | — |
| 98 | `vmq4_moon_puzzle_braziers` | 3 | 8 | — | — |
| 99 | `vmq4_crypt_waves` | 5 | 5 | — | — |
| 100 | `poh_heraldic_decor_variant` | 4 | 96 | — | — |
| 101 | `jigsaw` | 9 | 1 | 9 | jigsaw |
| 102 | `fletch_greenman_data` | 6 | 2 | — | — |
| 103 | `greenman_mask` | 4 | 7 | 1 | — |
| 104 | `ent_totems_base_data` | 10 | 6 | — | — |
| 105 | `ent_totems_animal_data` | 7 | 5 | — | — |
| 106 | `ent_totems_site_data` | 7 | 8 | — | — |
| 107 | `ent_totems_decoration_data` | 4 | 32 | — | — |
| 108 | `dom_droptable` | 4 | 6 | — | — |
| 109 | `dom_delve_level` | 36 | 9 | — | — |
| 110 | `furniture` | 10 | 525 | 10 | poh_trophy_menu |
| 111 | `poh_room` | 12 | 30 | 7 | — |
| 112 | `poh_hotspot` | 1 | 123 | 2 | — |
| 113 | `slayer_task` | 20 | 163 | 6 | chartering_menu_side, sailing_menu |
| 114 | `slayer_master_task` | 7 | 332 | 2 | — |
| 115 | `slayer_area` | 9 | 55 | 4 | chartering_menu_side, sailing_menu |
| 116 | `slayer_task_sublist` | 5 | 33 | 1 | — |
| 117 | `slayer_unlock` | 8 | 67 | 11 | — |
| 118 | `action` | 37 | 2174 | 6 | — |
| 119 | `bingo_events` | 8 | 1 | 9 | event_rewards |
| 120 | `bingo_grids` | 4 | 1 | 2 | — |
| 121 | `reward_selection` | 3 | 64 | 3 | — |
| 122 | `reward` | 20 | 60 | 1 | — |
| 123 | `teleport_generic` | 9 | 566 | — | — |
| 124 | `poll_filters` | 5 | 7 | 1 | — |
| 125 | `ui_highlighting_fx_pulse` | 2 | 2 | — | — |
| 126 | `ui_highlighting_style_border` | 4 | 2 | — | — |
| 127 | `restrict_content_obj` | 3 | 1 | — | — |
| 128 | `music_area_group` | 1 | 1 | 1 | — |
| 129 | `amenity` | 12 | 10 | — | — |
| 142 | `fletching_blowpipe_crafting` | 6 | 4 | — | — |
| 143 | `sailing_bt_trial_core` | 26 | 3 | 3 | sailing_bt_selection |
| 144 | `sailing_bt_gwenith_glide_crystal_data` | 4 | 8 | — | — |
| 145 | `sailing_bt_gwenith_glide_portals` | 7 | 26 | — | — |
| 146 | `sailing_bt_jubbly_jive_pillars` | 4 | 8 | — | — |
| 147 | `boat_location_sprite_data` | 3 | 4 | 3 | — |
| 148 | `boat_facilities_default_sprite_data` | 2 | 17 | 2 | — |
| 149 | `boat_selection_type` | 16 | 12 | 15 | — |
| 150 | `sailing_chance_encounters` | 3 | 7 | — | — |
| 151 | `sailing_chance_encounter_rescue_npcs` | 10 | 7 | — | — |
| 152 | `sailing_chance_encounters_lost_goods` | 3 | 12 | — | — |
| 153 | `sailing_chance_encounters_lost_goods_resource` | 2 | 8 | — | — |
| 154 | `sailing_charting_core` | 8 | 358 | 8 | — |
| 155 | `sailing_charting_generic` | 3 | 94 | — | — |
| 156 | `sailing_charting_spyglass` | 3 | 57 | — | — |
| 157 | `sailing_charting_current_duck` | 4 | 54 | — | — |
| 158 | `sailing_charting_drink_crate` | 6 | 65 | — | — |
| 159 | `sailing_charting_weather_troll` | 5 | 31 | — | — |
| 160 | `sailing_charting_mermaid_guide` | 7 | 57 | — | — |
| 161 | `sailing_charting_tool_recovery` | 4 | 5 | 2 | sailing_boat_cargohold |
| 162 | `sailing_combat_facility` | 25 | 7 | 2 | combat_interface |
| 163 | `sailing_combat_support_facility` | 13 | 6 | — | — |
| 164 | `sailing_boat_facility_stats` | 37 | 118 | 10 | — |
| 165 | `sailing_combat_facility_ammunition` | 10 | 22 | 3 | — |
| 166 | `sailing_boat` | 46 | 4 | 34 | sailing_sidepanel |
| 167 | `sailing_sidepanel_facility` | 6 | 12 | 4 | sailing_sidepanel |
| 168 | `sailing_sidepanel_widget_button` | 11 | 10 | 2 | — |
| 169 | `sailing_sidepanel_widget_button_target_npc` | 4 | — | 1 | — |
| 170 | `sailing_sidepanel_widget_graphic` | 1 | 8 | 1 | — |
| 171 | `sailing_sidepanel_widget_object` | 2 | 1 | 1 | — |
| 172 | `sailing_sidepanel_widget_text` | 1 | 3 | 1 | — |
| 173 | `sailing_crew` | 25 | 10 | 6 | sailing_crew, sailing_sidepanel |
| 174 | `sailing_shipyard` | 11 | 1 | — | — |
| 175 | `sailing_boat_hotspot` | 3 | 21 | 13 | sailing_sidepanel |
| 176 | `sailing_boat_facility` | 27 | 99 | 36 | sailing_sidepanel |
| 177 | `sailing_boat_keel` | 14 | 15 | 17 | — |
| 178 | `sailing_boat_hull` | 18 | 23 | 21 | sailing_sidepanel |
| 179 | `sailing_boat_sail` | 17 | 22 | 19 | sailing_sidepanel |
| 180 | `sailing_boat_sail_fx` | 26 | 6 | — | — |
| 181 | `sailing_boat_steering` | 19 | 22 | 23 | sailing_sidepanel |
| 182 | `sailing_boat_steering_fx` | 14 | 3 | — | — |
| 183 | `sailing_boat_hull_ornament` | 15 | — | 1 | — |
| 184 | `sailing_boat_flag` | 14 | 9 | 16 | — |
| 185 | `sailing_boat_brazier` | 16 | 3 | 19 | — |
| 186 | `sailing_boat_trim` | 15 | 37 | 18 | — |
| 187 | `sailing_boat_name_options` | 2 | 3 | 1 | sailing_sidepanel |
| 188 | `sailing_customisation_loc_angles` | 2 | 23 | 1 | — |
| 189 | `sailing_customisation_tab` | 8 | 16 | 3 | — |
| 190 | `sailing_shoal` | 17 | 6 | — | — |
| 191 | `sailing_shoal_specific` | 9 | 16 | — | — |
| 192 | `sailing_shoal_droptable` | 5 | 9 | — | — |
| 193 | `sailing_trawling_animations` | 12 | 3 | — | — |
| 194 | `sailing_dock` | 17 | 59 | 7 | — |
| 195 | `sailing_npc_boat` | 30 | 17 | — | — |
| 196 | `sailing_boat_cargohold_whitelist_obj` | 2 | 1 | — | — |
| 197 | `port_task` | 28 | 606 | 5 | — |
| 198 | `task_board_layout` | 4 | 2 | 1 | — |
| 199 | `sailing_anchor_anims` | 6 | 2 | — | — |
| 200 | `sailing_sea_hazard` | 7 | 11 | — | — |
| 201 | `sailing_sea` | 10 | 81 | 15 | sailing_log |
| 202 | `sailing_shipwreck_cluster` | 5 | 29 | — | — |
| 203 | `sailing_shipwreck` | 14 | 8 | — | — |
| 204 | `sailing_salvaging_hook_animations` | 7 | 3 | — | — |
| 205 | `thieving_chest` | 11 | 3 | — | — |
| 206 | `chartering_destinations` | 9 | 24 | 8 | chartering_menu_side, sailing_menu |
| 207 | `chartering_costs` | 1 | 1 | — | — |
| 208 | `patchy_data` | 3 | 13 | 2 | patchy |
| 209 | `skill_guide_v2_inline_icon` | 5 | 109 | — | — |
| 210 | `deadmanskull_interface_tab` | 5 | 3 | 2 | deadmanskull_interface |
| 211 | `cowboss_scenerynpcs` | 1 | 1 | — | — |
| 212 | `skill_guide_subsections` | 4 | 196 | 2 | — |
| 213 | `skill_features` | 10 | 3447 | 7 | — |
| 214 | `minigame_teleport` | 4 | 21 | 1 | — |
| 215 | `sailing_gun_ports` | 9 | 8 | — | — |
| 216 | `talent_tree` | 8 | 132 | 5 | — |
| 217 | `talent_debug` | 5 | 61 | — | — |
| 218 | `league_relic_teleport_item` | 4 | 41 | — | — |
| 219 | `league_relic_clue_direct_teleport_item` | 3 | 2 | — | — |
| 220 | `league_relic_effect_toggle_list` | 1 | 6 | 1 | — |
| 221 | `league_relic_effect_toggle` | 7 | 8 | 4 | combat_interface |
| 222 | `league_guardian_data` | 3 | 4 | — | — |
| 223 | `league_guardian_body_data` | 9 | 4 | — | — |
| 224 | `league_guardian_anim_data` | 14 | 14 | — | — |
| 225 | `transmutation` | 2 | 21 | — | — |
| 226 | `butlers_bell_actions` | 7 | 5 | — | — |
| 227 | `spell_override_list` | 1 | 1 | 3 | magic_spellbook |
| 228 | `spell_override` | 3 | 6 | 3 | magic_spellbook |
| 229 | `sailing_sidepanel_widget_objbutton` | 8 | 1 | 1 | — |
| 230 | `sailing_boat_sail_pattern` | 16 | 31 | 17 | — |
| 231 | `sailing_npc_boat_base_stats` | 24 | 2 | — | — |
| 232 | `sailing_npc_boat_weapon` | 18 | 2 | — | — |
| 233 | `sailing_npc_boat_steering` | 2 | 2 | — | — |
| 234 | `ambient_sfx` | 5 | 8 | — | — |
| 235 | `drakan_attack_list` | 1 | 2 | — | — |
| 236 | `drakan_attack_sequence_list` | 2 | 66 | — | — |
| 237 | `drakan_attack_sequence` | 5 | 130 | — | — |
| 238 | `drakan_tile_attack` | 12 | 11 | — | — |
| 239 | `drakan_tracking_attack` | 6 | 2 | — | — |
| 240 | `drakan_unique_attack` | 1 | — | — | — |
| 241 | `drakan_visuals` | 7 | 24 | — | — |
| 242 | `drakan_anims` | 2 | 13 | — | — |
| 243 | `drakan_spotanims` | 3 | 14 | — | — |
| 244 | `drakan_spotanim_projanim_pairs` | 1 | 1 | — | — |
| 245 | `drakan_fight_ally` | 5 | 5 | — | — |
| 246 | `castle_drakan_room` | 12 | 51 | 5 | castle_drakan_world_map |
| 247 | `castle_drakan_door` | 14 | 59 | 5 | castle_drakan_world_map |
| 248 | `castle_drakan_stairs` | 5 | 16 | — | — |
| 249 | `sangvesti_drakan_patrol` | 3 | 12 | — | — |
| 250 | `sangvesti_spawn` | 4 | 29 | — | — |
| 251 | `sotfa_forest_variant` | 2 | 1 | — | — |
| 252 | `sotfa_forest_encounter` | 6 | 9 | — | — |
| 253 | `sotfa_forest_maxilla_beast_patrol` | 1 | 2 | — | — |
| 254 | `coordinate_set_list` | 1 | 1 | — | — |
| 255 | `coordinate_set` | 2 | 18 | — | — |
| 256 | `preparation_recipe` | 9 | 5 | — | — |
| 257 | `river_fishing` | 16 | 4 | — | — |
| 258 | `arrow_fletching` | 11 | 10 | — | — |

---

## 8. Known gaps

**~~`cachepack pack` cannot run against this tree~~ — closed twice over
(2026-08-06).** The merge blocker fell first: `cp_merge` learned that a rank-1
key rank 0 never states (`data=`, `column=`) is multi-valued by observation
(`key_seen_rank0`), so `.dbrow` trees merge. That immediately exposed the next
layer — 1,004 authored dbrows and 48 dbtables hit the routing gate with no
membership statement and failed the pack — and the answer is the allocation
ledger: an id in `pack/dbrow.alloc` routes its record server-side with no
roster (`routing_client_member`'s alloc clause). `cachepack pack` now exits 0
against the full tree, with the authored db population attributed to the
ledger in its report.

**Column names now live in `columndef=`** — this gap is closed. See §4.1a. The
seeder keeps archive 10's column names in `CP_Names.dbtable_columns`, and both
unpackers emit them beside the types they name, in `all.dbtable` and in every
`all.dbrow` that fills the column. 99.4% of the ~93,000 tuple positions in the
tree now read as words rather than numbers; the remainder are the three
undetermined codes.

**Gameval archive 11 remains unclaimed, and now it is measured rather than
assumed.** It is the only one of the cache's fourteen gameval archives that
nothing reads. `content_register.c` says it "names songs *and* jingles in one id
space, so neither table alone can verify it", and that is right:

- 1,196 records, ids 0..1195 dense. The music tables hold 881 songs and 315
  jingles — **exactly 1,196**.
- The boundary is visible at 881: id 880 is `rellekka`, id 881 is
  `advance_herblaw2`. Songs below, jingles above.
- But the numbering is neither table's. The `music` dbtable pairs a `displayname`
  with a `midi` track id, and over 875 rows, `gv11[midi]` matches the display name
  **0 times** — `gv11[50]` is `beneath_the_stronghold` where track 50 is
  `Al Kharid`. The ordinal-position hypothesis also scores **0/875**.

So archive 11 carries 1,196 good names behind an index that maps onto neither
music table by id or by position, and attributing them needs evidence the cache
does not carry. Leaving it unclaimed is correct; what is new is that the cost is
known — 1,196 names, and a decisive test to re-run against any future proposal.

**The server ingests the cache's own db config files.** `mock230_db_load` walks
the entire content directory for `*.dbtable` and `*.dbrow`, and
`configs/all.dbtable` / `configs/all.dbrow` match. Proved rather than reasoned: a
deliberately malformed line in `configs/all.dbrow` is reported as
``configs/all.dbrow:4: `data=` before `table=` in dbrow `quest_animalmagnetism` ``.

Harmless so far, and only by accident — the cache files use `columndef=`/`values=`
where the server grammar wants `column=`/`data=`, so the server registers all 246
cache tables with **zero columns** and all 16,711 cache rows with **no data**, and
nothing ever asks. It is worth fixing (the walk should skip `configs/`, which is
cachepack's output rather than authored server content) and it was left alone
here: it is a change to the server's load surface, not to naming. It is also the
reason the key had to be `columndef` — see §4.1a.

**Adopting a cache table from RuneScript already works, and nothing checks it.**
This was the open question, and it was answered by probe rather than by reading
alone. A server `.dbtable` block whose header names a cache table:

```
[quest]
column=id,int,INDEXED
column=sortname,string
column=displayname,string
```

resolves through `SSC_SymbolsFind(…, SSC_SYM_DBTABLE)`, which searches the
*whole* namespace including the cache half. `quest:displayname` compiled, the db
column count went 3 → 6, `mock230_pack` reported 0 errors, `ss_allocate --check`
exited 0 (no allocation — the name already has a line), and
`make -C src test-mock230` passed. Probe reverted.

So no mechanism needs inventing. What is missing is a *check*, and the
recommendation is a narrow one: **`validate_id_bases` should also report
authored blocks that land below the cache's high-water mark in a namespace whose
`names` is `cache`.** Today it only compares the declared base. Adoption is a
legitimate thing to want — declaring column names for a cache table is exactly
what §8's first gap needs — but it should be visible in the validator's output
rather than indistinguishable from a typo that happened to resolve.

Two related notes for whoever writes that check:

- It must read `ContentRegister_Load`, not `ContentRegister_Defaults`. Today
  `validate_id_bases` reads the defaults, so the tree's `content.ini` has no
  influence on it at all.
- The packed id is the same number in both worlds — `quest:displayname` is
  `(0 << 12) | (2 << 4)` for the server's VM and for CS2 alike. The two stores
  are separate, so there is no runtime collision, but a reader comparing a
  server trace with a CS2 trace will see the same integer mean two things.

**`dbcolumn` packing is unverified.** Carried over from the CS2 DB session; the
`(table << 12) | (column << 4)` layout is confirmed by §2's zero-orphan result
over 422 scripts, but the low nibble (a tuple index the corpus does not use) has
no test behind it.

**Stale prose in `config/cp_db.c`.** The block above `refuse()` still says
*"Both packers refuse rather than approximate"* and gives the comma-escaping
reason. Neither holds: `append_escaped`/`split_escaped` implement the escaping,
both packers encode, and `refuse` is now only a parse-failure fallback.

**~~`make -C src test-db` is broken, and was before this work.~~ Fixed
2026-08-02** — see §9. Two independent breakages, both invisible because the
first one aborted before the first line of output: the test never called
`CacheProvider_SetProfile`, so the profile assert in `CacheProvider_Profile`
fired inside the very first load task; and its `DB_FIND_WITH_COUNT` call site
still pushed two arguments after the find family grew its third (the type tag).
It still hardcodes `cache.osrs230`.

---

## 9. What a read resolves to — row, then table, then type

> Added 2026-08-02, from the rev-230 skill guide's missing item icons. The
> failure was invisible as a database bug: the panel opened, laid out and filled
> in, and only the icon column was blank.

**A dbrow lists only the columns it sets.** Nothing else in the row says a
column exists — there is no "present but empty" encoding. So a read of a column
the row omits has to be answered from somewhere else, and there is exactly one
somewhere else: the **dbtable** (config group 39), which states every column's
field types and, where it declares one, a block of default values.

The resolution chain `DB_GETFIELD` walks, in order:

1. the row's value block for that column,
2. the **table's default block** for that column,
3. the **per-type default**: `-1`, or `""` for a string.

`DB_GETFIELDCOUNT` walks the same first two rungs and reports that block's
`tuple_count`; with neither, 0.

### Why rung 3 is -1 and not 0

Measured over the 9,368 decompiled clientscripts, not chosen. For every
`db_getfield` of a column that carries **no** table default, the guard the
script puts on the result:

| type | `!= -1` / `== -1` | `> 0` / `!= 0` |
|---|---|---|
| `dbrow` | 47 | 1 |
| `obj` | 20 | — |
| `stat` | 7 | — |
| `loc` | 7 | — |
| `graphic` | 11 | — |
| `int` | 10 | 9 |
| `namedobj`, `inv`, `component`, `struct`, `npc`, `coord` | 15 | — |

Zero appears only in `> 0` / `!= 0` shapes, which a real value satisfies too and
which therefore discriminate nothing. Every unambiguous guard in the corpus is
against -1. Strings keep `""`: the tables that declare a string default declare
it empty (`defaults=2:0:` on `skill_features.text`).

### The arity is the half that bites

A whole-tuple read (low nibble 0) pushes **one value per field**. Answering a
5-field column with a single integer does not produce one wrong value — it
shifts every local the script's multi-assignment writes, and the wrongness
surfaces somewhere else entirely.

That is precisely what hid this. `skill_features.sprite` is
`graphic,int,int,int,int` and **no row in the table sets it**, so
`[proc,script9347]`'s

```
$int13, $int14, $int15, $int16, $int17 = db_getfield($int0, 872464, 0);
if ($int13 ! -1) { cc_setgraphic($int13); ... } else { cc_setobject($int12, -1); }
```

read four values that were never pushed. `$int13` came back 0 rather than -1, the
guard took the sprite branch, and every row of every obj-icon skill guide called
`cc_setgraphic(0)` instead of `cc_setobject(<obj>, -1)`. No `CC_SETOBJECT` ever
reached the host, which is why the obj-icon path looked broken when it was never
entered. See `docs/skill_guide.md` §8.

### What it cost

| | |
|---|---|
| `src/engine/dat2/task_dat2_dbtable_load.c` | new; config group 39, same shape as the DBROW task |
| `CacheProvider_DbTable{Add,Get,Has}` / `DbTablesCleanup` | new hmap beside `dbrow_cache`, 256 entries (the cache has 247 tables) |
| `CS2VM_DB_LOAD_TABLE` + `TASK_CS2_YIELD_DBTABLE` | the third DB load kind |
| `db_table_or_yield`, `db_column_of`, `db_push_missing` | the chain above, in `rs_cs2_host.c` |

One ordering trap is written into the code and repeated here because it is not
obvious: `DB_GETFIELD` can now yield **twice** — once for the row, once for the
table — and `RS_CS2Host` has a *single* `awaited` slot. Yielding for the table
overwrites the row's record. That is safe only because the table lookup is gated
on `row != NULL`: a resident row cannot yield again on the retry. A row that is
genuinely absent would re-arm its own yield every time the table yield cleared
it, and the two would ping-pong until the VM's yield-halt guard fired.

### The check

`make -C src test-db` (`src/game/test/db_cache_test.c`), which this pass also
un-broke (§8). It discovers the columns from the cache rather than naming them,
so it does not depend on which revision's table 0 is on disk: it finds a
multi-field column row 0 omits *with* a table default and one *without*, and
asserts the push count on both stacks, the defaulted value, and the tuple count.
Proved to fail by mutation — reverting the type default to 0 and dropping the
table rung from `db_column_of` turns 5 of the 20 checks red.
