# Two pack files per namespace, entity-level routing — the plan, and what it did

> **This was a plan; it is now a record.** Written 2026-08-01, built 2026-08-01/02.
> Steps 1, 2 and 3 of §4 shipped in that order.
> `pack/<ns>.client` and `pack/<ns>.server` exist for five namespaces, seeded
> from the routing the packer performed then ([§8](#8-what-landed--step-1-emit));
> §3.3's two agreement checks run against them on every
> `make -C src test-content` ([§9](#9-what-landed--step-2-check)); and
> **`cachepack pack` routes on them** ([§10](#10-what-landed--step-3-enforce)) — a
> record gets a server band only if `<ns>.server` names it, and reaches the client
> cache only if `<ns>.client` names it or the base cache already holds its id.
> The packed cache is byte-identical across the switch, over 232 MB, which is
> what step 3's evidence had to be. **Step 4, *author*, is what remains**
> ([§11](#11-what-remains-and-what-was-deliberately-not-done)).
>
> §§1–6 are the plan as written, corrected in place: **where a number here was
> wrong, the measurement stands beside it** and §7 is the ledger. §§8–10 are what
> each step cost and found. The single most valuable thing in the document is
> §9.3 — the disagreements the second source exposed — because the routing itself
> reproduced the old behaviour exactly, by design, and finding those was the
> reason to do any of it.
>
> A deliberate successor to [`CONTENT_PACK_PLAN.md`](CONTENT_PACK_PLAN.md) —
> whose title is *"one pack file per namespace, two encoders"*, which is the
> premise this changes. That doc's §0 still holds in full and nothing here
> touches it; its §7 field register is this change's prerequisite, not its rival.
>
> Owner's summary of the target, in their words: *ids and names for a data type
> share a namespace; there is a pack file for the server and a separate pack file
> for the client; the packer only packs an entity into the server if it is in the
> server pack and into the client if it is in the client pack; a field goes to a
> side only if declared for that side; at load the server merges the two into one
> runtime structure; and where a server config states a field the client side also
> states, the server's value is packed.*
>
> All of that is true of the packer today except the load side (§3.4), which is
> the one clause of the target that was **deliberately not built** — see §11.

---

## 1. What already existed, and what did not

Three of the five rules were built when this was written. The fourth — the one
this document is about — is built now; the table records both states, because
which three were already standing is what made the fifth affordable.

| rule | when written | now | where |
|---|---|---|---|
| ids and names share one namespace per type | **built** | unchanged | `content.ini`; one name table per namespace |
| a field goes to a side only if declared for it | **built** | unchanged | `fields/<type>.ini` |
| server value wins over a duplicative client value | **built** | unchanged | `cp_merge.c`, rank 1 over rank 0 |
| the server merges both into one runtime structure at load | **built, transitional** | unchanged — see §11 | `mock230_boot.c` step 2 + 2b |
| **an entity goes to a side only if it is in that side's pack** | not built | **built** (§10) | `cp_pack.c`'s entity gate, on `pack/<ns>.{client,server}` |

None of the first three were rebuilt, and the fourth did not touch them. The
load-side merge is the same code it was; §3.4 was scoped out and §11 says why.

### The field rule, as built

`fields/<type>.ini` declares per field:

| declaration | meaning |
|---|---|
| `client = <native>` | a real cache opcode the client decodes |
| `client = param:<name>` | projected into the record's param table |
| `client = drop` | no sensible cache representation |
| `client = error` | must never appear; fails the build |
| `server = opcode:<N>` | the server band, opcodes **64..255**, disjoint from client 1..147 |

The default is `client = drop`: a field reaches the client only because someone
wrote that it does. `npc` is the type with real both-ness — ~~9 native client
fields, 13 param projections, 20 server-band opcodes~~ **1 native (`category`),
12 param projections and 19 server-band opcodes, over 21 distinct field names**.
Re-measured from `fields/npc.ini`, which states each field twice, once per side:
21 sections carry a `client =` line (1 `native`, 12 `param:`, 8 `drop`) and 19
carry `server = opcode:`. The original "9" was `grep`'s count of one-word `client
= …` values, which is `drop` plus `native`, not `native` alone.

### The override rule, as built

`cp_merge.c` merges per key. Rank 0 is the machine export from the cache, rank 1
is authored under `server/scripts`, and a rank-1 line *replaces* the rank-0 line
for the same key. That is already "the server-specified value is the one packed".

**One subtlety to preserve, not rediscover.** Arity — whether a key holds one
value or many — is *observed from rank 0*, never declared. Eighteen keys
legitimately repeat inside one block: `dbrow.values` in 14,243 blocks,
`seq.frame` in 12,795, `obj.param` in 7,417. Rank 0 is machine output and
internally consistent, so a key it states twice **is** multi-valued by
construction. A declared arity table was designed and rejected: it would be a
nineteenth thing to keep in step with the cache, and its first missing entry
corrupts a record quietly. The duplicate-key error therefore polices **rank 1
only**, where a repeat really is ambiguous. Any change here must keep that.

**Kept.** Nothing in three steps declared an arity, and the rank-1 duplicate
error is untouched. It cost two types their whole participation: `dbrow` and
`dbtable` state `data=` and `column=` in the authored layer, keys
`configs/all.dbrow` never states, so their arity can never be *observed* and the
merge refuses them before any gate runs (§8.5 item 4). That is the rule working
as designed on a tree where the two layers do not share a key vocabulary — not a
reason to declare arity, and §11 says what to do instead.

### The load merge, as built

`mock230_boot.c` step 2 parses the text overlays; step 2b loads
`<tree>/server/pack` and **verifies every archive identical to the text parse
before applying it**, reporting `LOADED` / `MISSING` / `STALE` each boot.
`server/pack` is a generated dat2 (`main_file_cache.dat2` + `idx128/129/6/9`),
gitignored, never edited. The remaining step —
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6 item 1 — is deleting the text parse
for band-carried fields now that boot proves the two identical.

**That step was not taken, and §6 was wrong to want it first.** Measured while
sequencing this work: of the 2,975 archives step 2b verifies, only **817** are
compared against a real text def (776 loc + 41 npc). For 2,045 more there is no
def, so the "three-way" verification silently substitutes the cache seed and the
comparison becomes a round trip through `cache.osrs239`; 113 are counted and
never compared at all. Deleting the text parse would not give membership one
source instead of two — membership is a *different fact* and this document's
files are its second source. It would delete the second source for the **field
values**, which is the opposite of triage §10.1's rule. See §6 and §11.

### The entity rule as it then was — and why it was not the target

The only entity-level gate in the packer was `cp_pack.c:972`:

```c
if( rec->origin_rank > 0 && !fields.records_client )
{
    server_only++;
    continue;
}
```

That asks *"was this block authored under `server/scripts`"* — provenance — and
consults one boolean per **type** (`records = client|server`). Two types declared
it: `enum = server`, `param = client`.

The function survives, renamed in its documentation to *the default*
(`record_is_client`, §10.1): a record no membership file names still falls
through to it, so the fifteen namespaces with no pair route exactly as they did.
What changed is that it is no longer the answer.

Three consequences, all live at the time:

1. On a type that opts in, **every** authored record goes to the client. There is
   no way to say "this one param is server-only".
2. There is no on-disk statement of which entities are ours. It is inferred at
   pack time and leaves no artifact a second tool could disagree with.
3. Provenance and id range are *correlated but uncoupled*. `ids = server` means
   "ours, from one past the largest id the cache states", and it governs
   **allocation only** — nothing reads it at pack time. A record authored under
   `server/scripts` that somehow landed on a cache-range id is skipped silently
   and plausibly.

(3) is the shape of defect this tree has already paid for twice: `rock_sample1`
is obj 671 in both trees, the name resolves, the id agrees, and it is a different
item — right by one measure, wrong by another, and nothing asked the second
question. §4.1's display-name diff exists because of it. The entity split is the
same medicine one layer down.

**And it caught one of exactly that family on its first enforcing run.**
`configs/all.hitsplat` states `[hitsplat_26]` and `[hitsplat_28]`, the export's
fallback names for cache records 26 and 28, while the compack has since given
ids 26 and 28 to `hitsplat_block` and `hitsplat_damage` — the cache's records
**1 and 2**. Two blocks resolve to nothing and two others pack into ids that are
not theirs. Neither of §3.3's checks can see it and neither did (§10.3); the
routing gate can, because only the gate must answer *"which side is this on"*
for a record nobody mentioned. One layer down, and the same order of magnitude:
a name that resolves against something and it is a different record.

---

## 2. The target, stated as a decision table

Two membership lists per namespace, and the two **overlap rather than
partition** — an npc is in both: a cache record on the client, twenty server
fields in the band. "In the server pack" must mean *"has a server half"*, not
*"is exclusively ours"*.

|  | field declared client | field declared server | field declared neither |
|---|---|---|---|
| **entity in client pack only** | → client cache | **(a)** | dropped |
| **entity in server pack only** | **(b)** | → `server/pack` | dropped |
| **entity in both** | → client cache | → `server/pack` | dropped |
| **entity in neither** | **(c)** | **(c)** | — |

The three lettered cells are the whole design question. Fallthrough is the wrong
answer to all of them. **All three are implemented; §10.2 is the shipped
condition for each and it differs from the wording below in one place (cell (a)),
for a reason §10.6 states.** Measured on this tree: (a) 0, (b) 32, (c) 2.

- **(a) a client-only entity carrying a server field.** The author declared a
  field the entity's own side cannot receive. This is `client = error`'s mirror
  image and should be an **error**: the field register already has the vocabulary
  for "must never appear", and the failure is a statement the tree makes about
  itself, not about the cache.
- **(b) a server-only entity carrying a client field.** Not symmetric, and it
  should be a **counted warning**, not an error. Today's `records = server`
  behaviour is exactly this case and is *already* legitimate at scale: the ~~29~~
  **32** authored enums (in 17 files, ids 5995–6026) are server tables wearing
  enum grammar, and every one of their
  fields would be client-shaped. Making it an error would break loading to teach
  nobody anything. Count it, name the type, print it — the way
  `N record(s) the tree adds are server-only` already prints.

  Shipped and measured, and the count turned out to discriminate: `enum` reports
  **32 of 32** server-only records stating a client field, `varp` **0 of 22**.
  Same counter, two different answers, so "server-only" and "server-only and
  losing something" are separate numbers on the same line.
- **(c) an entity in neither pack.** A record nothing claims. This is the case
  that cannot exist today, and is the reason the plan is worth doing: it is
  currently *unrepresentable*, so a record that should have been claimed is
  instead silently routed by provenance. **Error**, naming the record and both
  files.

  This is the cell that paid. Two records are in it (§10.3) and neither of
  §3.3's checks could see either, because both checks reason about names in the
  files and records the tree authored, and these are rank-0 records in no file.
  Cell (c) as shipped is *"neither file names it **and** the base cache does not
  hold its id"*, and the second conjunct is a fact provenance cannot state.

**The `client = error` disposition keeps its current meaning** and is not
subsumed: it is a statement about a *field of a type*, where (a) is a statement
about a *field of an entity*. Both can fire.

---

## 3. What had to be built

Three of the four subsections shipped. **§3.4, the load side, did not** — see
§11. Each subsection carries its landing site.

### 3.1 The membership files — **built** (§8.1)

One pair per namespace, beside the existing name table:

```
<tree>/pack/<ns>.client
<tree>/pack/<ns>.server
```

They list **entity names, not ids** — the tree's standing rule is that no content
file carries a bare id ([`LOSTCITY_PORT_TRIAGE.md`](LOSTCITY_PORT_TRIAGE.md) §13
bar 5), because an id has nothing to resolve and so cannot fail to resolve. The
id comes from the shared name table, which is the point of the namespace staying
shared.

Format follows the existing `.pack` convention so the files read the same way to
anyone who knows it, and so a comment survives a re-seed. **Correction from
building it:** the *convention* was followed and the *container* was not.
`LC_Pack`'s every rule — merge, sparse, synthetic id — keys off the id column, so
reusing it would have meant inventing an id per line, which is the one thing this
format exists not to have (§8.1). One bare name per line, sorted by name, `//`
comments anchored the way `lc_pack` anchors them.

### 3.2 The gate — **built** (§10.1)

`cp_pack.c`'s provenance test is replaced by a membership test, per side, per
entity. `records = client|server` in `fields/<type>.ini` becomes the **default**
for a record not named in either file, rather than the whole answer — which is
what keeps the migration incremental (§4).

**True of the client, and there is no server half of it** (§10.6). `records =`
is a client-side declaration; the band gate is field presence and has no
per-record default to fall back on. The server side's incremental switch is the
*file's existence*, which is the stronger rule anyway — a per-record default
would make deleting a line from `<ns>.server` a no-op, and deleting a line has to
remove the band or step 4 has no feature. An **absent** file and an **empty** file
are therefore different statements, and both were proven to behave differently
(§10.4).

### 3.3 The agreement check — the reason to do this at all — **built** (§9)

Once membership is stated on disk, it can be held against the two facts that
already exist and currently cannot be cross-examined:

1. **membership vs provenance.** An entity in the server pack that has no rank-1
   block, or a rank-1-only block not in the server pack, is a disagreement.
2. **membership vs id range.** An entity in the server pack whose id is below the
   namespace's server base, or in the client pack whose id is above it, is a
   disagreement.

Neither is currently checkable, because there is only one statement of the fact.
This is the deliverable — not the routing, which mostly reproduces today's
behaviour, but the *second source*. Triage §10.1's rule, twice-proven here:
**whenever a port has a second source for the same fact, spend it.**

**That judgement held: the routing changed no byte and the checks found six
things (§9.3).** Two corrections to the wording, both from building it and both
recorded at §9.6:

- Check 2's **client direction is unsound as worded.** "In the client pack and
  above the base is a disagreement" assumes above-base means server-owned, which
  is exactly the premise §5 retired when it deleted the `ids` axis. `server_base`
  is *where a new name gets its number* for one shared namespace and says nothing
  about sides. The shipped error is the case the base does rule on — **below the
  base and not defined by the cache**, a record on an id no side owns.
- Both checks would have fired on §2's overlap: 2,199 npcs and 776 locs sit far
  below their base with no rank-1 block. They **classify rather than restrict**,
  because restricting to server-*only* membership would have hidden the 2,158,
  which §8.5 calls the largest thing the exercise was for.

### 3.4 The load side — **not built, deliberately** (§11)

`mock230_content_load` gains the membership files so the runtime knows which
entities have a server half, and step 2b's three-way verification extends to
cover membership as well as field values. The merge into one runtime structure
already happens; what changes is that "does this record have a server half" stops
being inferred from whether the text parse found one.

**Scoped out after measuring what it means.** `band_apply_type` iterates *defs*,
not archives, so 2,158 of the 2,199 npc bands are verified and then discarded —
the runtime has no def to apply them to. Making membership the source of "has a
server half" therefore means **creating 2,158 defs**, and each new def carries
its cache combat bonuses where today it inherits `[default]`'s
(`mock230_world.c:1322`, `npc_def_seed_from_cache`). That is a behaviour change,
probably a desirable one, and it is emphatically not byte- or behaviour-identical
— so it belongs in step 4, not inside a migration whose whole evidence is that
nothing moved.

---

## 4. Migration, and why it was not a flag day

The gate change is behaviour-preserving if the membership files are *generated*
from today's provenance answer first:

1. **Emit, don't enforce.** Generate `<ns>.client` / `<ns>.server` from the
   current `origin_rank` + `records =` decision. Land them. The packer still
   routes on provenance; the files are inert. — **landed, §8.** One correction:
   the packer gives *two* answers, not one, and they are not complements
   (§8.5); the seeder takes each from the gate that produces it rather than
   restating either.
2. **Check, don't enforce.** Turn on §3.3's two agreement checks against the
   generated files. Every disagreement found here is a real finding about the
   tree *before* any routing depends on it. Expect this step to be the one that
   pays. — **landed, §9. It paid**: 2,158 npcs whose server half comes out of the
   cache, 1,698 records stating a field routed nowhere, 903 unclaimed overlays,
   44 params, and one entry in a membership file that is not an entity.
3. **Enforce.** Switch `cp_pack.c` to route on membership. Because step 1
   generated the files from the old answer, a correct switch changes **no
   output** — which is the assertion to make: pack before and after, and diff the
   cache byte for byte. `3rd/rscache` already holds byte-exact round-trip as its
   bar; use it. — **landed, §10.** `diff -r --brief` empty over 232 MB and over
   `server/pack`'s 2,975 archives. It also found the two records neither check
   could (§10.3), which the plan did not expect of it.
4. **Author.** Only now can a record be moved between sides by editing a file,
   which is the feature. — **not started, §11.**

Steps 1–3 are independently revertible, and step 3's evidence is a byte-identical
cache. That ordering is the same shape as the `.spawn` migration
(§10.1): *migrate first and byte-identically, so the move is checkable; correct
afterwards, as its own reviewable step.*

**The one thing that made the byte-diff usable, and it is worth writing down:**
`cachepack pack`'s exit status on this tree was **already 1** before any of this
(2 hitsplat misroutes, 8 param encode failures), so it could never have been the
signal for any step. The packer is byte-deterministic — verified by two
consecutive runs with nothing changed between them, over both the 232 MB client
output and `server/pack` — which is what makes `diff -r --brief` an assertion
rather than a hope. `cachepack verify --digests` is **not** a substitute: it
reads records out of the cache and round-trips them through text, and never runs
the walk, the merge or the routing gate, so it cannot see a membership change at
all.

---

## 5. Costs and open questions — and how each resolved

- **The re-seed merge. → Did not arise, and the protection is stated anyway.**
  For `names = cache` namespaces, cachepack re-seeds
  `pack/<ns>.pack` from the gameval table, and a save **merges rather than
  truncating** precisely so the re-seed cannot eat authored lines — this tree has
  already lost `pack/param.pack`'s 58-line header to exactly that. Splitting
  membership into new files changes who owns which half of that merge. The
  membership files are `names = authored` by nature and cachepack must never
  write them; state that in `content.ini` rather than leaving it to convention.

  Measured: `cachepack pack` **never calls `cp_names_save`** — a pack does not
  touch `pack/*.pack` or `*.compack` at all — and `cp_names_load`/`_save` work
  from an explicit fixed path list that no new file joins. So the membership
  files were safe *by omission*, which is what this bullet says not to rely on.
  Two independent guards were built instead (§8.4): `content.ini` states
  `membership = authored` and cachepack reads it, and `cachepack membership`
  creates only files that do not exist and never replaces one. **`names` could
  not carry the fact** — eleven namespaces are named by a gameval archive and
  must claim `names = cache`, npc/loc/varp among them — so it is a second key.
  `cp_membership_save` is a plain write and says so; a second writer would need
  `lc_pack`'s merge first.
- **Which namespaces get files at all. → 10 files, in 5 pairs, and one
  correction to the proposed default.** 20 config types exist; 6 have a
  `fields/<type>.ini` today. Generating 40 membership files for types with no
  server half is noise. Proposal: a namespace opts in, and the absence of a pair
  means "everything is the cache's" — the same shape as the field register's
  own default.

  Adopted, with one exception the proposal gets wrong (§8.3): **`stat` and
  `category` have no config kind, no gameval archive and no client half at all**.
  Reading their absence as "everything is the cache's" would be exactly backwards.
  That is already derivable from `cp_server_group_for(ns) >= 0`, so they get no
  file and any consumer must consult the predicate before reading an absence.
  Twelve config types genuinely have nothing to say and get nothing.
- **dbtable/dbrow. → Excluded, and for a reason the bullet does not name.**
  16,711 cache dbrows and the reference brings 1,115 of its
  own into a namespace whose ids are the cache's. This is the type where the
  split earns most and where the id-range check is most load-bearing. It is also
  the one type where "in both packs" may need to mean something finer than a
  record-level overlay.

  Three corrections, all measured:

  - Neither type has a pair, because **the packer has no routing answer to
    record**: their authored layer states `data=`/`column=`, keys
    `configs/all.dbrow` never states, so the arity rule (§1) cannot observe them
    and the merge refuses them as duplicates *before any gate runs*. 65 dbrows
    and 10 dbtables are routed by nothing at all today.
  - **The id-range check cannot run on `dbrow` at all.** Its `server_base` is 0
    deliberately — `content_register.c`: allocation comes off the high-water mark
    alone — and `validate_id_bases` skips a zero base. The check turned out
    load-bearing for `param` instead (§9.3 item 6). The *provenance* check is the
    whole value here.
  - Record-level overlay **is** sufficient for the data that exists and for the
    data the reference would bring: all 65 authored rows attach to authored
    tables, and 0 of the reference's 1,115 rows attach to a table outside its own
    27. What is waiting instead is a **name collision in the one shared
    namespace**: the cache's `music` is dbtable 44, 15 columns, 876 rows;
    LostCity's `music` is 4 columns, 195 rows. Same name, different table, and a
    port that resolves the name through the shared namespace gets id 44. That is
    `rock_sample1` one layer down and an order of magnitude larger, and it argues
    for a name-collision check rather than a finer split (§11).
- **`server_base` is real, and `ids` should go. → Half retired; the other half is
  one file and is safe to finish today.** The base is a field on
  `struct ContentNamespace`, populated by the defaults table in
  `content_register.c` (npc 20000, obj 40000, loc 70000) and ~~overlayable from~~
  `content.ini`, which states no `base =` key today only because nobody has had
  reason to override one. §3.3's id-range check has an authoritative base.

  Correction: `content.ini` overlays **nothing** — `grep '^base'` returns
  nothing and there is no `base` key in the loader's ladder, so every base in
  force is `content_register.c`'s. The id check ran against those and found
  **no violation in either direction on any of the 20 types** (§9.3 item 6 is
  information, not an error). Every server-allocated block sits in a perfectly
  consecutive run immediately above its base: enum 5995–6026, param 2634–2677,
  varp 5705–5726, dbtable 259–268, dbrow 16940–17004. **The id check is a guard,
  not a bug-finder, on this tree** — worth having as the second source, not worth
  expecting a payout from.

  The `ids = cache|server|protocol` axis beside it does **not** survive contact
  with what it does. Ids are chosen by the pack file — `configs/all.<type>.compack`
  maps name to id and that mapping *is* the answer — and `ids` never participates
  in resolving one. Measured across all three consumers it: unions into
  `ss_allocate.py`'s allocatable set (which also has a hardcoded default tuple, so
  two tables must agree by hand); flips one `mock230_pack.c` diagnostic between
  error and information; and is validated as incompatible with a nonzero base when
  it is `protocol`. No routing, no resolution.

  It is also already false where it matters most. `npc` is `ids = cache` with a
  base of 20000, and the register's own message says *"`ids = cache` says the base
  is only where new records would start"* — so allocation happens under both
  values and the label does not gate the behaviour it is named for. Worse, the
  `ids = server` branch errors when allocated ids sit below the base and advises
  lowering it to `cache_max + 1`, while the base field's own documentation says
  bases are deliberately round numbers *above* the maximum so an id reads as ours
  at a glance. Two rules in one register that cannot both be satisfied.

  The three values collapse: `protocol` requires `server_base == 0`, and 0 already
  means "never allocate"; `cache` and `server` both allocate from the base. One
  fact per namespace survives — **where a new name gets its number, 0 meaning
  never** — and it is already spelled `server_base`.

  The drift is documented rather than theoretical. `ss_allocate.py` records that
  `param` sat in its tuple while the register said `ids = cache`, so it allocated
  params the compiler then refused to resolve; and that `declared_base`'s docstring
  claimed to read a base from `content.ini` while the function returned 0 for every
  namespace. Both are one authority stated twice.

  **Retiring `ids` belongs in this change**, not after it: the entity split makes
  the client/server question a membership fact, which is the last thing `ids` was
  standing in for. Sequence it as its own step with the same emit → check → enforce
  discipline, and expect the check step to find at least the npc/`ids = cache`
  disagreement above.

  **Done on two of the three consumers, and stalled on the third.** The C
  register accepts-and-ignores the key (`content_register.c:471`), and
  `ss_allocate.py`'s `server_namespaces()` no longer unions it in — that is the
  union this bullet calls the drift, and its removal is what let `varp` be swept
  at all. But **`id_authority()` still parses `ids =` out of `content.ini`** and
  still refuses to allocate into any namespace whose value is not `server`, and
  the tree still carries **10 `ids =` lines**. Two facts make finishing it a
  small, checkable change rather than an open question:

  - All eight namespaces the tool sweeps resolve to `server` today — four by an
    explicit line (`varp`, `param`, `dbtable`, `dbrow`) and four by the function's
    default (`enum`, `struct`, `mesanim`, `inv`). So deleting the lines and the
    gate changes no allocation. Measured, not assumed.
  - `server_namespaces()` already has **unreachable code after its `return`** —
    the old `ids` parser, left in place. A half-retired axis reads as a live one
    to the next person.

  Carried to §11 as the one piece of §5 this change owed and did not pay.

---

## 6. Sequencing — as planned, and as it happened

This lands **after** the two lanes in flight (`lane-triggers`,
`lane-droptables`), both of which write content and one of which owns
`make -C src mock230-scripts`. It also wants
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6 item 1 finished first — deleting the
text parse for band-carried fields — so that step 2b's verification has one
source to check membership against rather than two.

Not urgent, and deliberately so: nothing is blocked on it. Its value is that it
makes a class of silent error impossible, and that value is the same whenever it
lands.

**The first sentence held. The second was wrong and the prerequisite was
dropped**, on measurement rather than convenience:

- **The stated justification is backwards.** The text parse is the second source
  for *field values*. Membership is a different fact, and `pack/<ns>.{client,
  server}` is its second source. Deleting the text parse does not give membership
  one source instead of two — it destroys the value check. Once the text stops
  loading `hitpoints`, `band_compare`'s `text_value` has nothing behind it and
  the 817 archives that today have a real two-source comparison degenerate into
  the band-vs-cache-seed round trip the other 2,045 already are.
- **It is genuinely separable**, which is why dropping it cost nothing: it is
  load-side only, touches no packer output, and so can neither perturb step 3's
  byte-diff nor be perturbed by it. The two changes share no file.
- **It has an unpaid prerequisite of its own.** `authored_combat` is set by the
  *presence of the `hitpoints` key*, not by its value
  (`mock230_content.c:1110`), and is read twice in `mock230_pack.c`. Deleting the
  branch zeroes it silently and disables two checks. It needs an opcode or a
  derivation first, as its own reviewable step.
- Two of the 19 npc fields cannot move at all until the band grows a symbolic
  wire (`huntmode`, 10 values), and one is a key-name mismatch content has to
  migrate (`moverestrict` → `nomove`, 11 records). `fields/npc.ini` says so.

**Recommended re-wording of §3.6 item 1**, which this lane does not own and did
not edit: *choose whether the band or the text is the source, knowing that
whichever loses stops being a check.*

---

## 7. The ledger

What each step cost, what it asserted, and what it found. Detail in §§8–10.

| | step 1 — emit (§8) | step 2 — check (§9) | step 3 — enforce (§10) |
|---|---|---|---|
| **built** | `cachepack membership`, `cp_membership.{h,c}`, 10 files, `membership = authored` | `membership --check-only`; `validate_membership_ids` in `mock230_pack` | the entity gate in `cp_pack.c`, both sides |
| **routing** | unchanged (provenance) | unchanged (provenance) | **membership** |
| **evidence** | `diff -r --brief` empty, 232 MB + `server/pack` | same, from step-1 code | same, from step-2 code |
| **proof it can fail** | 2 of 47 suite checks mutated to fail | 9 tree mutations, each reverted | 7 tree mutations, each reverted |
| **found** | the packer answers the question twice, and the two answers differ by 2,801 records | 2,158 · 1,698 · 903 · 44 · `[default]` | the 2 hitsplat records neither check can see |

Standing numbers on this tree, all re-measured for this write-up:

| fact | measured |
|---|---:|
| membership files | 10, in 5 pairs |
| names stated across all of them | 3,073 |
| `npc.server` / `loc.server` / `enum.server` / `varp.server` / `param.client` | 2,199 / 776 / 32 / 22 / 44 |
| client records routed by the substrate clause | 173,000 |
| …by `pack/param.client` | 44 |
| …by the type default (both hitsplat, both cell (c) errors) | 2 |
| server bands, all routed by name | 2,975 |
| §2 cell (a) / (b) / (c) | 0 / 32 / 2 |
| `mock230_pack --check-only` | 0 errors, 15 warnings |
| membership id check | 5 namespaces, 3,073 names: 2,975 below base and in the cache, 54 above base in `.server`, 44 above base in `.client` |
| `make -C 3rd/rscache test` membership suite | 54 checks |

---

## 8. What landed — step 1 (emit)

Built on the branch this document was written for, in the order §4 requires.
Nothing routes on the files; the packed cache is byte-identical before and
after, which is the whole claim this stage makes.

### 8.1 The files

Ten, in five pairs:

| file | names | what it says |
|---|---:|---|
| `pack/npc.server` | ~~2,200~~ 2,199 | records the packer gives a server band. It also held `[default]`, which is not an entity — step 2's id check caught it; see §9.3 item 2 |
| `pack/npc.client` | 0 | the tree adds no npc the client is told about |
| `pack/loc.server` | 776 | the door-stage overlays |
| `pack/loc.client` | 0 | — |
| `pack/enum.server` | 32 | today's type-wide `records = server`, per entity |
| `pack/enum.client` | 0 | — |
| `pack/varp.server` | 22 | the `%com_*` block and the mock's zone counters |
| `pack/varp.client` | 0 | — |
| `pack/param.client` | 44 | today's type-wide `records = client`, per entity |
| `pack/param.server` | 0 | see §8.5 — this zero is a finding, not a fact |

Format: one bare entity name per line, sorted by name, `//` comments carried
through a rewrite exactly as `lc_pack` carries them. **Names and not ids** —
`configs/all.<ns>.compack` remains the one id authority both sides share, which
is what "one namespace per data type" means. A new module,
`3rd/rscache/tools/cachepack/cp_membership.{h,c}`, with its own suite at
`3rd/rscache/test/test_membership.c` (47 checks, in `make -C 3rd/rscache test`).

`LC_Pack` was not reused: every one of its rules — merge, sparse, synthetic id —
keys off the id column, and sharing the container would have meant inventing an
id per line, which is exactly the thing this format is trying not to have.

### 8.2 A subset, not a roster — and why

`<ns>.client` lists only records the *tree adds*. Complete client rosters would
be **143,435 lines restating `configs/all.<type>.compack` verbatim**
(`all.loc.compack` alone is 62,194), and two files that must agree by hand is the
failure `src/content/content_register.h` catalogues three times. A record the
machine export states is a cache record by construction.

The enforcement rule this implies, for step 3:

```
in the client pack  iff  named in pack/<ns>.client
                     or  the base cache already holds (config kind, id)
```

The second clause is CONTENT_PACK_PLAN §0's substrate rule stated as a lookup,
and it is a genuine second source rather than provenance wearing a new name — the
cache is open during `cp_pack_run` anyway. Step 3 must verify it: every rank-0
record must be present in the base cache, and no rank-1-only record may be.

### 8.3 Which namespaces, and what an absence means

A pair is written where either gate has something to say. Twelve of the twenty
config types have no `server/scripts` file and no server band at all — forty
files would make the absence of a file mean nothing. Both halves of a pair are
written even when one is empty, so "npc adds nothing to the client" is a
statement rather than a missing file.

**`stat` and `category` get nothing, deliberately, and their absence must not be
read as "the cache's".** They have no config kind, no gameval archive and no
client half; they are 100% server. That is already derivable from
`cp_server_group_for(ns) >= 0`, so a file would be a second copy of a fact the
codec tables state. Any consumer that reads "no pair" as "all client" has to
consult that predicate first.

### 8.4 `content.ini` — a new key, and why not `names`

```ini
[namespace:npc]
membership = authored
```

`names = authored` **cannot** express this. Eleven namespaces are named by a
gameval archive, so `cp_register_check` refuses to let them claim anything but
`names = cache`; npc, loc and varp are among them and all three have membership
files. Two files, two facts, two keys.

`authored` is the default and the only supported value; `generated` is spellable
only so `cp_register_check` can refuse it, because nothing generates these files.
Verified both ways against a minimal tree.

Protection is two independent guards, so losing either does not lose it:

1. `content.ini` states the fact, and cachepack reads it
   (`cp_register_membership_is_authored`).
2. `cachepack membership` **creates only files that do not exist** and never
   replaces one. Re-running it is a no-op — verified.

`pack`, `unpack` and `verify` open no membership file at all. This is why the
re-seed-merge hazard §5 flags does not arise here: there is one writer and it
writes once. `cp_membership_save` is therefore a plain write and says so; if a
second writer ever lands, it needs `lc_pack`'s merge *first*.

**`src/content/content_register.c` does not carry the key.** Nothing on that side
opens a membership file — the runtime and the compiler load symbols, and
membership is not a symbol — so a field there would be written and never read,
which is the `ids` mistake again. cachepack has its own reader of the same
`content.ini` by design (`cp_register.h`) and validates the key there, so a typo
is still caught, by the only tool with a reason to care. The field lands in
`struct ContentNamespace` when §3.4 gives the runtime a reader for it.

### 8.5 What the seeding measured

**The packer gives two answers, not one, and they are not complements.** The
seeder records both, from the gates themselves rather than from a restatement of
them — `record_is_client()` and `server_band_build()` are now named functions
that `pack_type`/`pack_server_type` and the seeder all call.

- client: provenance plus one boolean per type.
- server: **field presence** (`band.stated != 0`), which is why npc writes 2,199
  bands off 42 authored blocks.

`<ns>.server` is the union of "the client gate refused it" and "the band gate
accepted it" — both are the packer saying the record has a server half.

Five things the seed makes visible that were not on disk before:

1. **npc.server is ~~2,200~~ 2,199 names against ~~42~~ 41 authored blocks** —
   step 2 removed `[default]` from both counts (item 2 there). 2,158 are cache npcs
   whose `param=attackrate` line `fields/npc.ini` declares a server field, so the
   packer writes them a band on the strength of a value that came *out of the
   cache*. Emitted faithfully rather than corrected: step 2's
   membership-vs-provenance check should report ~2,158 disagreements, and that is
   the single largest thing this exercise was for. Correcting it is step 4.
   **Predicted ~2,158; measured 2,158**, attributed to `attackrate` and to no
   other field.
2. **`param.server` is empty and should not be.** All 44 entries in
   `param.client` are *server* allocations (ids 2634–2677, one past the cache's
   maximum), and the packer cannot say so because `param` declares no server
   band. "Has a server half" and "the packer writes a band for it" are different
   facts and only the second has ever been on disk. This is §2's overlap rule
   failing to be representable in the direction nobody had looked at.
3. **`obj` gets no pair at all, and has a real server half.** 857 authored blocks,
   every one an overlay on a cache record, and `levelrequire` is `client = drop`
   with no `server = opcode:` row — so the packer has no answer, while
   `mock230_content.c` reads 1,496 requirement rows out of the text at boot. Its
   server half is real and invisible to every artifact.

   Step 2 measured this and it is **not confined to `obj`**: 1,698 records across
   four namespaces state a field their register routes to neither side. See §9.3
   item 3, which also corrects the spelling — `obj` states the requirement as
   `param=levelrequire,…`, not as a bare `levelrequire=` key.
4. **`dbrow` and `dbtable` get no pair, because the packer has no routing answer
   to record.** Their authored layer states `data=` and `column=`, keys
   `configs/all.dbrow` never states, so the merge cannot observe their arity and
   refuses them as duplicates *before any gate runs* (`docs/DBTABLES.md`). 65
   dbrows and 10 dbtables are therefore routed by nothing at all today, which is
   precisely the population §5 expects the split to earn most on. They join when
   the arity hole is closed. The seeder reports this per type rather than
   skipping silently.
5. **A declared `[namespace:X]` section used to change the answer to a question it
   said nothing about.** `cp_register_load` initialised `machine_owned = true` on
   entering a section, so merely *appearing* in `content.ini` — to state
   `membership`, say — would have made an unnamed namespace claim `names = cache`
   and fail `cp_register_check`. It now starts from the same derived fact an
   undeclared namespace gets, which is what `cp_register_may_write_pack` already
   documented. Provably inert for every existing section: `cp_register_check` is
   exactly the assertion that each one's `names` equals that derivation.

### 8.6 Evidence

- `cachepack pack --base cache.osrs239 --out DIR --types <18>` before and after
  the whole change: `diff -r --brief` empty over the 232 MB output, and over
  `server/pack`. The packer is byte-deterministic (confirmed by two consecutive
  runs with nothing changed in between), so the diff is a real assertion.
- The two-type merge blocker means the byte-diff covers 18 of 20 types. `dbrow`
  and `dbtable` cannot be packed at all today, which is a pre-existing failure
  and the reason step 3's evidence will have the same shape.
- `cachepack pack`'s exit status is **already 1** on this tree (2 hitsplat
  misroutes, 8 param encode failures, all pre-existing and ~~unrelated~~ — the
  hitsplat two turned out to be exactly what step 3's gate names, §10.3), so the
  diff — not the exit status — is the usable signal for every step of this
  migration.
- Emitted sets re-derived independently from the raw text by a script that shares
  no code with the packer: npc ~~2,200~~ 2,199 (see item 1 above) / loc 776 /
  enum 32 / varp 22 / param 44, exact match in both directions.
- Gates: `make -C 3rd/rscache test`, `make -C src test-content`,
  `test-content-register`, `test-mock230`. `mock230_pack --check-only` unchanged
  at 0 errors, 15 warnings.
- The membership suite's assertions were mutated to prove they can fail (drop the
  preamble on save → 2 of 47 checks fail, and exactly the two that should).

### 8.7 Corrections to this document

Re-measured, because [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §7 says to:

- §1 says npc has "9 native client fields, 13 param projections, 20 server-band
  opcodes". `fields/npc.ini` says **1 native (`category`), 12 param projections,
  19 server opcodes**, over 21 declared fields.
- §2 says "the 29 authored enums". There are **32**, in 17 files, at ids
  5995–6026. ~~`fields/enum.ini` and `cp_pack.c:918` both still say "seven".~~
  Three prose counts for one population — 7, 29, 32 — and none of the first two
  was ever measured. `fields/enum.ini`, `cp_fields.h` and both `cp_pack.c`
  comments now say 32 and name the id range, so the next reader can check it.
- §3.3's two checks are unusable as worded against §2's "has a server half"
  reading: 2,199 npcs and 776 locs sit far below their namespace's server base
  with no rank-1 block, so both checks would report 2,975 false disagreements at
  once. **They must be restricted to server-*only* membership**, which is what
  §2's decision table implies and §3.3's text does not say. — *Superseded by
  §9.2: restricting would have hidden the 2,158, so the checks **classify**
  instead. The diagnosis here was right; the remedy was not.*
- §5 says `content.ini` overlays `base`. It states no `base` key on any
  namespace, so every base in force is `content_register.c`'s.
- §5's expectation that the id-range check is "most load-bearing" for dbrow is
  wrong in a specific way: `dbrow` has `server_base = 0` deliberately, and
  `validate_id_bases` skips a zero base — so that check cannot run on dbrow at
  all. The membership-vs-provenance check is the whole value there.

---

## 9. What landed — step 2 (check)

§4 step 2, *check, do not enforce*. Both of §3.3's agreement checks run on every
`make -C src test-content`; neither routes anything, and the packed cache is
byte-identical to what step 1 produced (§9.5).

### 9.1 Two checks, two binaries, and why they are not one

| check | where | authority it needs |
|---|---|---|
| membership vs **provenance** | `cachepack membership --check-only` | the two-rank walk + merge, and the two gates themselves |
| membership vs the **id range** | `mock230_pack`, `validate_membership_ids` | `server_base`, and the base cache |

Splitting them is the point rather than a compromise. `server_base` is a field of
`struct ContentNamespace`, defaulted in `src/content/content_register.c` and
overlaid from `content.ini`; cachepack deliberately links nothing from `src/`
(`cp_register.h`), so a base over there would be a **second copy of the number** —
the failure the register exists to prevent, and the one §5 records `ss_allocate.py`
already paying for. Symmetrically, the provenance check needs `cp_merge`'s
`origin_rank`/`overlaid`, which only the packer has.

The *format* is not duplicated: `3rd/rscache/tools/cachepack/cp_membership.c`
compiles into `mock230_pack` (it depends on nothing but stdio). One parser, two
callers. The borrowing goes src ← cachepack and never the other way, so
cachepack stays usable apart from the client.

### 9.2 What the provenance check asks

Per namespace with a pair, and per record:

| population | verdict | today |
|---|---|---:|
| a name in either file that no config layer states | **error** | 0 |
| a name in `<ns>.server` with no authored block *and* no server band | **error** | 0 |
| a record stated only under `server/scripts` that neither file names — §2's cell (c) | **error** | 0 |
| a record only under `server/scripts` in a namespace with no pair at all | **error** | 0 |
| a name in `<ns>.client` the live client gate refuses | **error** | 0 |
| a duplicate or unreadable line in a membership file | **error** | 0 |
| a name in `<ns>.server` with no authored block whose **band gate** accepts it | counted | 2,158 |
| a name in `<ns>.client` that `configs/all.<ns>` already states | counted | 0 |
| an authored overlay named by neither file | counted | 903 |

**Every error case is zero on this tree, and that is what makes them usable as
errors.** Each was proven to fire by mutation (§9.4).

**§8.7 asked for the two checks to be *restricted* to server-only membership;
they classify instead.** Restricting would have hidden the 2,158, and finding 1
of §8.5 says reporting them is the single largest thing this exercise was for.
Both are right about the same fact and only classification satisfies both: the
population is named, counted, attributed to the field that produced it, and does
not fail the build.

### 9.3 What it found

1. **The 2,158 are confirmed, and attributed to exactly one field.** `npc.server`
   lists 2,199 names; 41 have an authored block and **2,158 have none at all** —
   their server half exists because `configs/all.npc` (the machine export *from
   the cache*) states `param=attackrate` and `fields/npc.ini` declares
   `attackrate` a band field. The check prints the responsible field per
   namespace, and it is `attackrate=2158` and nothing else. Predicted at ~2,158
   in §8.5; measured at 2,158.

2. **`pack/npc.server` named `default`, which is not an entity.** The id check
   reported it on its first run: `[default]` in `general/configs/npc_default.npc`
   states what an npc is before any block describes it, has no id in
   `configs/all.npc.compack` and never will, and `pack_server_type` skips it two
   lines before its own id lookup. §8.1's table recorded it as "plus `[default]`",
   which was an unexamined artifact of the band gate rather than a decision.
   **Fixed**, and stated as a fix: the line is gone from `pack/npc.server` (with a
   comment saying why, since comments are the format's point), and the seeder
   skips it through a named predicate `record_is_defaults_block` shared with the
   writer. A re-seed of a tree without the file now reproduces the hand-edited
   name list exactly — verified by moving the file aside and re-seeding.

3. **1,698 records state a field their register routes to *neither* side.** This
   is the finding, and it is bigger than §8.5's finding 3 said:

   | namespace | records | the field |
   |---|---:|---|
   | `obj` | 857 | `levelrequire` (`param=levelrequire,attack,60`) |
   | `loc` | 776 | `category` |
   | `varp` | 54 | `scope`, `protect`, `transmit`, `wholewrite` |
   | `npc` | 11 | `moverestrict` |

   Each is declared `client = drop` with no `server = opcode:` row, so the packer
   reads the value and drops it, and the only reader left is
   `mock230_content.c`'s parse of the same text. **No membership statement can
   route one of these**, which is why the entity split alone does not finish the
   job: `pack/obj.server` would be a true statement about 857 records whose server
   half still reaches no packed file. Cell (a)/(b) work, and it belongs to step 4.

   §8.5 finding 3 had the shape right and two details wrong: `obj`'s requirement
   is stated as a **param row**, not a bare `levelrequire=` key (a check reading
   only bare keys reports 0 — it did, until it read both spellings the way
   `merged_value` does), and the same defect is in three other namespaces.

4. **`varp`'s 32 authored overlays are named by neither file, and that is not
   benign.** `bankcert`, `prayer23`, `slayer_rewards_unlocks` and 29 more are
   cache varps the tree overlays with `protect=`/`scope=`/`transmit=` — all four
   of `fields/varp.ini`'s declarations route nowhere, so `varp` has no band, so
   the seeder had nothing to write. Same class as `obj`, in a namespace that
   *does* have a pair.

5. **`param`'s 14 authored overlays are benign, and the check now says so.**
   `stabattack` and friends state `type=`/`default=`/`autodisable=`, which are
   native param opcodes and reach the client cache. `<ns>.client` lists only
   records the tree *adds*, so there is nothing to state about a cache record
   whose overlay is fully routed. The check separates this population from (3)
   and (4) rather than counting them together.

6. **44 params are stated client-side at ids the server allocator handed out**
   (2634–2677, base 2634). This is §8.5 finding 2 re-derived from a completely
   independent fact — the id, rather than the absence of a band — which is what
   §3.3 was for. Reported as information, not an error, for the reason in §9.6.

### 9.4 Proof that each check can fail

Nine mutations, each applied to the tree, run, and reverted; every membership
file verified byte-identical afterwards by `shasum`.

| mutation | expected | got |
|---|---|---|
| a made-up name in `npc.server` | provenance error | exit 1, named |
| `molanisk` (a cache npc, no band, no overlay) in `npc.server` | provenance error | exit 1, named |
| a made-up name in `param.client` | provenance **and** id error | both exit 1 |
| `bank_tabs` (a server enum) in `enum.client` | client-gate error | exit 1 |
| `molanisk` in `npc.client` | counted, **not** a failure | exit 0, counted |
| `bank_tabs` removed from `enum.server` | cell (c) error | exit 1 |
| a name repeated in `enum.server` | duplicate error | exit 1 |
| `pack/obj.server` naming `obj_33835` | id error (below base, not in cache) | exit 1 |
| `pack/obj.client` naming `obj_33835` | the same, other side | exit 1 |

### 9.5 The cache did not move

- `cachepack pack --src … --base cache.osrs239 --out DIR --types <18>` run from
  the step-1 code and from the step-2 code: **`diff -r --brief` empty over the
  232 MB output**. The only packer-path change in this step is the `[default]`
  test becoming a named predicate with an identical body, and this is the
  assertion that it is inert.
- `pack --server-only` likewise byte-identical (2,975 archives).
- `mock230_pack` unchanged at **0 errors, 15 warnings**; its server-band
  verification still reports 2,975 archives identical to the text parse.
- Gates: `make -C src test-content` (which now runs both checks),
  `test-content-register`, `test-mock230`, `make -C 3rd/rscache test` — all green,
  membership suite 47/47.

### 9.6 Corrections to §3.3, from building it

- **The id check's client direction, as §3.3 words it, is unsound.** "An entity in
  the client pack whose id is above the base is a disagreement" assumes
  above-base means server-owned — which is exactly the premise §5 retired when it
  deleted the `ids` axis. `server_base` is *where a new name gets its number*, for
  one shared namespace, and says nothing about sides. So the implemented error is
  the case the base does rule on: **below the base and not defined by the cache** —
  a record sitting on an id no side owns. Above-base-and-client-stated is reported
  as information, naming each entity `<ns>.server` does not also list, which is
  how the 44 params surface.
- **`<ns>.server` below the base is normal**, not a disagreement, whenever the
  cache defines the id: that is §2's overlap. 2,975 of the 3,073 stated names are
  in exactly that position, which is why §8.7 was right that the check as worded
  would have reported them all.
- §5 predicted the id check would be "most load-bearing" for `dbrow`. It cannot
  run there at all (`server_base = 0`, and dbrow/dbtable still do not merge), and
  it turned out to be load-bearing for `param` instead.

---

## 10. What landed — step 3 (enforce)

§4 step 3, *enforce*. `cp_pack.c` routes on `pack/<ns>.client` and
`pack/<ns>.server`; provenance survives only as the documented default for a
record no file names. The packed cache did not move by a byte (§10.5), which is
the assertion this stage exists to make.

### 10.1 The rule, as built

Per record, per side, in this order:

| side | rule |
|---|---|
| **client** | named in `pack/<ns>.client`; **or** the base cache already holds (config kind, id); **or** — if neither file names it — the type default `records = client\|server` |
| **server** | named in `pack/<ns>.server`; **or** — if `pack/<ns>.server` does not exist — the old field-presence gate |

The second client clause is §8.2's substrate rule as a lookup, and it is what
lets `<ns>.client` stay a subset: 173,000 of the 173,046 records routed to the
client this run were routed by it, 44 by `pack/param.client` and 2 by the
default. It is a genuine second source and not provenance renamed — the answer
comes from the cache being built on, which `cp_pack_run` has open anyway, rather
than from which directory a `[block]` was found in. The two are held against each
other and the disagreements are counted (§10.3).

**Being in `<ns>.server` does not take a record off the client.** The files
overlap, so the client question is answered entirely by the client clauses; what
takes a record off the client is that nothing puts it on. That is why naming
2,199 npcs in `npc.server` left all 16,292 of them in the client cache.

### 10.2 The three cells, and what each one does

| cell | condition | verdict |
|---|---|---|
| **(a)** | the record states a field `fields/<type>.ini` routes to the band, and `<ns>.server` exists and does not name it | **error**, no band written |
| **(b)** | `<ns>.server` names it, `<ns>.client` does not, the cache does not hold it, and it states a field declared for the client | **counted**, never fatal |
| **(c)** | neither file names it and the base cache does not hold its id | **error**, naming the record and both files; the default still routes it |

Cell (b) is measured rather than assumed: `enum` reports 32 of 32 server-only
records stating a client field, `varp` reports 0 of 22. The same counter
separates the two, so "server-only" and "server-only and losing something" are
different numbers on the same line.

Cell (c) errors but **does not change where the record goes** — the default
routes it, so a tree mid-correction still builds the cache it built yesterday and
the exit status is what says the tree is wrong. That is not fallthrough: the
record is named, both files are named, and `cachepack pack` returns non-zero.

**Cell (c) is scoped by the substrate clause, and it has to be.** "In neither
file" describes every record in the fifteen namespaces with no membership pair;
§8.3 says that absence means the opposite — everything there is the cache's — so
the error fires only where the cache cannot claim the record either.

`[default]` is skipped before any of this. It is not an entity, no membership file
names it and none should, so it would otherwise be a permanent cell (c). That
makes the routing gate the **fourth** caller of `record_is_defaults_block`, and
the comment there now says four.

### 10.3 What enforcement found that neither check found

**The two hitsplat records are inside the entity split's reach after all.** §8.6
and §9's "still open" both record them as outside it. They are not:

```
cachepack: hitsplat [hitsplat_26] is named by neither pack/hitsplat.client nor
  pack/hitsplat.server and configs/all.hitsplat.compack gives it no id —
  nothing can claim it
```

`configs/all.hitsplat` carries `[hitsplat_26]` and `[hitsplat_28]`, the export's
fallback names for cache records 26 and 28. The compack has since given 26 and 28
to `hitsplat_block` and `hitsplat_damage` — which are the cache's records **1 and
2** — so two blocks now resolve to nothing and two others pack into ids that are
not theirs. Neither §3.3 check sees it: the provenance check walks the names in
the files and the records the tree *authored*, and these are rank-0 and not
overlaid; the id check walks the names in the files, and these are in no file.
Only the gate asks the question that catches them, because only the gate has to
answer *"which side is this record on"* for a record nobody mentioned.

This is the substrate clause paying for itself on its first run: cell (c) is
"neither file claims it **and** the cache does not hold it", and the second half
of that conjunction is a fact provenance cannot state.

**§8.2's demanded verification, measured.** It asks that every rank-0 record be
present in the base cache and no rank-1-only record be:

| population | expected | measured |
|---|---|---:|
| rank-0 records on an id the base cache does not hold | 0 | **0** |
| rank-1-only records the base cache *does* hold | 0 | **0** |
| rank-0 records with no id in `configs/all.<ns>.compack` | — | **2** (both hitsplat) |

The third row is a case §8.2's sentence does not anticipate, and the packer
distinguishes it in both the message and the count: a name with no id cannot be a
record the cache holds, so the substrate clause answers *no* rather than
*unknown*, and that answer is what makes it cell (c).

**The client half of membership is not load-bearing on this tree, and that is a
finding rather than a defect.** Only `pack/param.client` names anything (44), and
its 44 would have reached the client by `records = client` anyway. The client
file becomes load-bearing the moment it disagrees with the type default — proven
by mutation (§10.4, M5b), where naming a server enum in `enum.client` routed it
to the client encoder for the first time and the encoder refused it, exactly as
§2 predicts for a server table wearing enum grammar.

### 10.4 Proof that each rule can fail

Seven mutations, each applied, run and reverted; the ten membership files
`shasum`-verified identical afterwards and `server/pack` re-diffed against the
baseline.

| mutation | expected | got |
|---|---|---|
| a name removed from `pack/loc.server` | cell (a); one band fewer | exit 1, 775 records, the loc named |
| `pack/loc.server` moved aside entirely | the old field gate, no error | exit 0, 776 records, "by the field gate (no file)" |
| `pack/loc.server` emptied to a comment | cell (a) for all of them | exit 1, 776 errors — an empty file is a statement |
| `bank_tabs` removed from `enum.server` | cell (c), naming both files and the id | exit 1, "the base cache does not hold enum 5995" |
| …and the enum group it produced | unchanged: the default still routes it | byte-identical |
| `molanisk` (a cache npc) added to `npc.client` | read, counted, harmless | "1 by pack/npc.client, 16291 by the cache", byte-identical |
| `bank_tabs` added to `enum.client` | the file beats `records = server` | the client encoder saw it and refused it; cell (b) fell 32 → 31 |

The third and second rows together are why `struct CP_Membership` gained
`present`: an **absent** file and an **empty** file are different statements and
the gate acts on the difference. `test_membership.c` holds that separately (54
checks, up from 47).

### 10.5 The cache did not move

- `cachepack pack --src … --base cache.osrs239 --out DIR --types <18>` from the
  step-2 code and from the step-3 code: **`diff -r --brief` empty** over the
  232 MB output (227,820,840-byte `main_file_cache.dat2`).
- `server/pack` after the same two runs: byte-identical, 2,975 archives.
- `cachepack pack --server-only` (what `make mock230-servpack` runs): **exit 0**,
  byte-identical to the full pack's bands.
- `cachepack pack`'s exit status is 1, as it was before: 10 failed records
  (8 param encode failures, 2 hitsplat) — and now also the 2 cell (c) errors,
  which are the *same two records*. The byte-diff remains the signal.
- Gates: `make -C src test-content` (0 errors, 15 warnings; membership id check
  unchanged at 5 namespaces / 3,073 names), `test-content-register`,
  `test-mock230`, `make -C 3rd/rscache test` — all green.

### 10.6 Corrections to this document, from building it

- **§3.2 is right for the client and has no server half.** `records =` is a
  client-side declaration; the band gate is field presence and has no per-record
  default to fall back to. The server side's incremental switch is therefore the
  *file's existence*, not a per-record default — which §5 already implies ("a
  namespace opts in, and the absence of a pair means everything is the cache's")
  and §3.2 does not say. It is also the stronger rule: it is what makes deleting a
  line from `<ns>.server` actually remove the band, which is step 4's whole
  feature. A per-record default there would have made a deleted line a no-op.
- **§2's cell (a) cannot be asked of the client file alone.** "A client-only
  entity carrying a server field" needs to know the entity is the client's, and
  for a cache record that fact is the substrate clause — which `pack --server-only`
  cannot evaluate, because it opens no cache. The implemented condition is the one
  both modes can answer identically: *states a band field, and `<ns>.server` does
  not name it*. It covers cell (a) exactly and additionally catches the unclaimed
  case, which the client pass reports as cell (c) with the record named.
- §8.6 says the two hitsplat misroutes are "pre-existing and unrelated", and both
  earlier steps recorded them as outside the entity split's reach. **They are
  inside it** — see §10.3. What is outside its reach is *fixing* them: the split
  names them, and the compack is where the repair goes.
- §9.3 item 3's 1,698 unrouted-field records remain step 4's work, unchanged by
  this step: no membership statement can route a field the register sends
  nowhere.

---

## 11. What remains, and what was deliberately not done

### 11.1 Step 4 — author

The feature the first three steps exist to make safe: **a record moves sides by
editing a file.** Nothing on this tree has used it yet, and the four candidates
are already named and counted.

| work | size | where it is described |
|---|---:|---|
| the 2,158 npcs whose server half comes out of the cache | 2,158 | §9.3 item 1 |
| the 1,698 records stating a field routed to neither side | 1,698 | §9.3 item 3 |
| the 2 hitsplat records nothing can claim | 2 | §10.3 |
| `param.server` empty when 44 params are server allocations | 44 | §8.5 item 2 |

The first two are not the same job. The **2,158** are a membership question and
step 4 can answer them by editing `pack/npc.server` — the check already prints
the responsible field. The **1,698** are not: a field the register routes
nowhere reaches no packed file on either side, so `pack/obj.server` naming all
857 objs would be a *true statement about records whose server half still goes
nowhere*. That wants a `server = opcode:` row, or a decision that the text is the
transport; it is field-register work wearing a membership label, and conflating
the two is the mistake this table exists to prevent.

### 11.2 Deliberately not done

- **§3.4, the load side.** Scoped out on measurement, not on effort: making
  membership the runtime's source of "has a server half" means creating 2,158
  npc defs that do not exist today, and each one changes that npc's combat
  bonuses from `[default]`'s to the cache's. A real behaviour change inside a
  migration whose entire evidence is that nothing moved. §3.4 has the detail.
- **PORTING_GUIDE §3.6 item 1** — deleting the text parse for band-carried
  fields. §6 wanted it first; measuring it inverted the argument. It would delete
  the second source for field values to simplify a membership check that has its
  own second source now, and it has an unpaid prerequisite (`authored_combat`).
  §6 carries the recommended re-wording; **this lane does not own that file and
  did not edit it.**
- **A declared arity table.** Rejected before this work and re-rejected during
  it, at the cost of `dbrow`/`dbtable`'s participation (§1). Its first missing
  entry corrupts a record quietly, which is worse than two types not merging
  loudly.
- **Complete client rosters.** 143,435 lines restating the compacks verbatim, and
  two files that must agree by hand is the failure `content_register.h`
  catalogues three times. `<ns>.client` is a subset and the substrate clause is
  what makes that sound (§8.2, §10.1).
- **`stat` and `category` membership files.** They are 100 % server, already
  derivable from `cp_server_group_for(ns) >= 0`, and a file would be a second
  copy of what the codec tables state (§8.3).
- **Anything under `src/` beyond `mock230_pack`'s new check.** `cp_membership.c`
  compiles into `mock230_pack` so the *format* has one parser; the borrowing goes
  src ← cachepack and never the other way, so cachepack stays usable apart from
  the client (§9.1).

### 11.3 Carried forward, with the measurement

1. **~~`dbrow`/`dbtable` do not merge~~ — paid, 2026-08-06.** `cp_merge`'s
   `key_seen_rank0` rule made the arity observable (a rank-1 key rank 0 never
   states is multi-valued by construction), and the population that then hit
   cell (c) — 1,004 dbrows, 48 dbtables — is routed by the **allocation
   ledger**: `pack/<ns>.alloc` claims a record for the server with no
   membership roster, because a roster restating the allocator was measured to
   drift within days (560 varps/enums/params/structs allocated after the
   2026-08-02 seeding, every one a cell-(c) error). The alloc clause sits
   between the membership files and cell (c) in `routing_client_member`.
2. **A dbtable name collision is already waiting in the reference**: cache
   `music` is dbtable 44, 15 columns, 876 rows; LostCity's `music` is 4 columns,
   195 rows. A port resolving the name through the shared namespace gets 44.
   Recommend a name-collision check against the cache's own names as this
   namespace's second agreement check — the id check cannot run there at all.
3. **`ids` is half retired.** `ss_allocate.py`'s `id_authority()` still parses it
   and still gates allocation; `server_namespaces()` has unreachable code after
   its `return`; the tree carries 10 `ids =` lines. All eight swept namespaces
   already resolve to `server`, so finishing it changes no allocation (§5).
4. **`13_fonts` declares `server_base = 100` while `pack/13_fonts.pack` lists
   cache font ids 494..6315.** `validate_id_bases`' own rule makes that an error
   ("raise the base"); it does not fire only because the check covers config
   groups. Extending §3.3's id check to asset namespaces finds this on run one.
5. **`--types` makes the server pack partial.** A `--types npc,…` run rewrites
   only the idx it was asked for and leaves the previous run's bands in place.
   Any before/after diff of `server/pack` must use full runs. Verified.
