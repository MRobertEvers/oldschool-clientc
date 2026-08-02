# Implementation plan — two pack files per namespace, entity-level routing

> Written 2026-08-01. **Nothing here is built.** This is a design, measured
> against the packer as it stands today, and it is a deliberate successor to
> [`CONTENT_PACK_PLAN.md`](CONTENT_PACK_PLAN.md) — whose title is *"one pack file
> per namespace, two encoders"*, which is the premise this changes. Read that
> doc's §0 first; its four decisions still hold and this plan does not touch
> them.
>
> Owner's summary of the target, in their words: *ids and names for a data type
> share a namespace; there is a pack file for the server and a separate pack file
> for the client; the packer only packs an entity into the server if it is in the
> server pack and into the client if it is in the client pack; a field goes to a
> side only if declared for that side; at load the server merges the two into one
> runtime structure; and where a server config states a field the client side also
> states, the server's value is packed.*

---

## 1. What already exists, and what does not

Three of the five rules are built. The measurement is from the source, today.

| rule | status | where |
|---|---|---|
| ids and names share one namespace per type | **built** | `content.ini`; one name table per namespace |
| a field goes to a side only if declared for it | **built** | `fields/<type>.ini` |
| server value wins over a duplicative client value | **built** | `cp_merge.c`, rank 1 over rank 0 |
| the server merges both into one runtime structure at load | **built, transitional** | `mock230_boot.c` step 2 + 2b |
| **an entity goes to a side only if it is in that side's pack** | **not built** | this plan |

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
wrote that it does. `npc` is the type with real both-ness — 9 native client
fields, 13 param projections, 20 server-band opcodes on one record.

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

### The load merge, as built

`mock230_boot.c` step 2 parses the text overlays; step 2b loads
`<tree>/server/pack` and **verifies every archive identical to the text parse
before applying it**, reporting `LOADED` / `MISSING` / `STALE` each boot.
`server/pack` is a generated dat2 (`main_file_cache.dat2` + `idx128/129/6/9`),
gitignored, never edited. The remaining step —
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6 item 1 — is deleting the text parse
for band-carried fields now that boot proves the two identical.

### The entity rule, as it is today — and why it is not the target

The only entity-level gate in the packer is `cp_pack.c:972`:

```c
if( rec->origin_rank > 0 && !fields.records_client )
{
    server_only++;
    continue;
}
```

That asks *"was this block authored under `server/scripts`"* — provenance — and
consults one boolean per **type** (`records = client|server`). Two types declare
it: `enum = server`, `param = client`.

Three consequences, all live:

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
answer to all of them.

- **(a) a client-only entity carrying a server field.** The author declared a
  field the entity's own side cannot receive. This is `client = error`'s mirror
  image and should be an **error**: the field register already has the vocabulary
  for "must never appear", and the failure is a statement the tree makes about
  itself, not about the cache.
- **(b) a server-only entity carrying a client field.** Not symmetric, and it
  should be a **counted warning**, not an error. Today's `records = server`
  behaviour is exactly this case and is *already* legitimate at scale: the 29
  authored enums are server tables wearing enum grammar, and every one of their
  fields would be client-shaped. Making it an error would break loading to teach
  nobody anything. Count it, name the type, print it — the way
  `N record(s) the tree adds are server-only` already prints.
- **(c) an entity in neither pack.** A record nothing claims. This is the case
  that cannot exist today, and is the reason the plan is worth doing: it is
  currently *unrepresentable*, so a record that should have been claimed is
  instead silently routed by provenance. **Error**, naming the record and both
  files.

**The `client = error` disposition keeps its current meaning** and is not
subsumed: it is a statement about a *field of a type*, where (a) is a statement
about a *field of an entity*. Both can fire.

---

## 3. What has to be built

### 3.1 The membership files

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
anyone who knows it, and so a comment survives a re-seed.

### 3.2 The gate

`cp_pack.c`'s provenance test is replaced by a membership test, per side, per
entity. `records = client|server` in `fields/<type>.ini` becomes the **default**
for a record not named in either file, rather than the whole answer — which is
what keeps the migration incremental (§4).

### 3.3 The agreement check — the reason to do this at all

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

### 3.4 The load side

`mock230_content_load` gains the membership files so the runtime knows which
entities have a server half, and step 2b's three-way verification extends to
cover membership as well as field values. The merge into one runtime structure
already happens; what changes is that "does this record have a server half" stops
being inferred from whether the text parse found one.

---

## 4. Migration, and why it is not a flag day

The gate change is behaviour-preserving if the membership files are *generated*
from today's provenance answer first:

1. **Emit, don't enforce.** Generate `<ns>.client` / `<ns>.server` from the
   current `origin_rank` + `records =` decision. Land them. The packer still
   routes on provenance; the files are inert.
2. **Check, don't enforce.** Turn on §3.3's two agreement checks against the
   generated files. Every disagreement found here is a real finding about the
   tree *before* any routing depends on it. Expect this step to be the one that
   pays.
3. **Enforce.** Switch `cp_pack.c` to route on membership. Because step 1
   generated the files from the old answer, a correct switch changes **no
   output** — which is the assertion to make: pack before and after, and diff the
   cache byte for byte. `3rd/rscache` already holds byte-exact round-trip as its
   bar; use it.
4. **Author.** Only now can a record be moved between sides by editing a file,
   which is the feature.

Steps 1–3 are independently revertible, and step 3's evidence is a byte-identical
cache. That ordering is the same shape as the `.spawn` migration
(§10.1): *migrate first and byte-identically, so the move is checkable; correct
afterwards, as its own reviewable step.*

---

## 5. Costs and open questions

- **The re-seed merge.** For `names = cache` namespaces, cachepack re-seeds
  `pack/<ns>.pack` from the gameval table, and a save **merges rather than
  truncating** precisely so the re-seed cannot eat authored lines — this tree has
  already lost `pack/param.pack`'s 58-line header to exactly that. Splitting
  membership into new files changes who owns which half of that merge. The
  membership files are `names = authored` by nature and cachepack must never
  write them; state that in `content.ini` rather than leaving it to convention.
- **Which namespaces get files at all.** 20 config types exist; 6 have a
  `fields/<type>.ini` today. Generating 40 membership files for types with no
  server half is noise. Proposal: a namespace opts in, and the absence of a pair
  means "everything is the cache's" — the same shape as the field register's
  own default.
- **dbtable/dbrow.** 16,711 cache dbrows and the reference brings 1,115 of its
  own into a namespace whose ids are the cache's. This is the type where the
  split earns most and where the id-range check is most load-bearing. It is also
  the one type where "in both packs" may need to mean something finer than a
  record-level overlay.
- **`server_base` is real, and `ids` should go.** The base is a field on
  `struct ContentNamespace`, populated by the defaults table in
  `content_register.c` (npc 20000, obj 40000, loc 70000) and overlayable from
  `content.ini`, which states no `base =` key today only because nobody has had
  reason to override one. §3.3's id-range check has an authoritative base.

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

---

## 6. Sequencing

This lands **after** the two lanes in flight (`lane-triggers`,
`lane-droptables`), both of which write content and one of which owns
`make -C src mock230-scripts`. It also wants
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6 item 1 finished first — deleting the
text parse for band-carried fields — so that step 2b's verification has one
source to check membership against rather than two.

Not urgent, and deliberately so: nothing is blocked on it. Its value is that it
makes a class of silent error impossible, and that value is the same whenever it
lands.
