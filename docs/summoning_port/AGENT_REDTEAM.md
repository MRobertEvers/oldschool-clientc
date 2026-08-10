# Red-team review — Summoning port design

Everything marked **[measured]** I ran against the two trees during this pass.

## Post-review binding corrections

- An NPC_INFO v5 add carries a 16-bit per-client NPC index (`0xffff` terminator), then a 14-bit
  initial definition. Definitions 16384..65535 set the add's extended/update flag and use
  update-mask `0x1` to replace that definition in the same packet with a transformed unsigned
  16-bit `p2Alt3` / `UShortLEAdd` value. Type 20000 is regression-tested. No roster tier, id
  ceiling, or allocation budget follows from the direct initial-definition width.
- `LOC_ADD_CHANGE_V2` is a different wire contract: its loc config id is an exact 16-bit
  `p2Alt3`. The generic loc base 70000 is unusable there and would truncate to 4464. The port maps
  source obelisk 28716 to free target loc 62201; do not transfer the NPC slot/type conclusion to
  loc configs.
- Treat rev-727 clientscripts as a distinct, unverified dialect. Capture raw instructions,
  operands, and stack effects before decompilation; use an explicit 727 dialect; then translate
  relevant logic into newly authored osrs239 CS2. A readable decompile alone is not evidence of
  dialect correctness.

---

## 1. FACTUAL ERRORS

### E1. `content.ini`'s `ids` axis is dead. Two designs build a prerequisite on an inert edit.

design-server-content Phase D1 ("`content.ini` `[namespace:varbit] ids = cache` → `server`") and design-skill-and-cs2 §5.2 (same edit, called a "prerequisite") are writing to a key nothing reads.

`src/content/content_register.c:494-497`:
> `/* `ids` was here. It is accepted and ignored: the key still appears in content.ini and an unknown key is silently skipped by the loop below, so those lines are inert rather than an error until they are deleted. */`

The parse loop that follows handles `names`, `vardomain`, `gameval`, `base`, `index` — **not `ids`**. And `tools/ss_allocate.py:96-112`:

```python
def server_namespaces(tree):
    ...
    del tree
    return tuple(sorted(SERVER_NAMESPACES))
    section = None                      # ← unreachable. The content.ini reader is dead code.
```

with a docstring that says outright *"The axis is gone because it decided nothing… `npc` declared `ids = cache` while allocating from a base of 20000."* The only two levers are the `SERVER_NAMESPACES` tuple (`ss_allocate.py:84-92`) and `server_base` in `content_register.c`. Every plan step phrased as "flip `ids` to server" is a no-op that will read as done and change nothing.

### E2. design-flag-and-risk §0 row 4 names the wrong gate, and gets its polarity backwards.

> *"`record_is_client()` (`cp_pack.c:524-528`) returns false for any rank-1 record whose type lacks `records = client`… An authored pouch/familiar is **server-only by construction**."*

`record_is_client` is reached in exactly two places: for a **defaults block** (`cp_pack.c:698-699`), and as the terminal fallback *after* the cell-(c) error has already been raised. The live gate is `routing_client_member`, ordered `cp_pack.c:712-796`:

```
.client by name → 1
in base cache   → 1     ("substrate")
origin_rank==0  → 1     ← every base-cache record, regardless of `records =`
.server by name → 0
alloc claims it → 0
in_cache == 0   → cp_warn(...); (*errors)++;   ← cell (c)
```

So a new rank-1 obj does **not** quietly route server-side — it emits `"obj [x] is named by neither pack/obj.client nor pack/obj.server…"` and increments the error count, i.e. `cachepack pack` exits non-zero. The flag-on tree does not "just work with the records inert"; it **fails the bake** until `pack/obj.client` exists. That inverts §1.3's "invisible with the flag off, functional with it on" story into "the flag-on path is broken until an unexercised mechanism is seeded."

design-asset-pipeline §4.3 gets this right (`routing_client_member` … "→ `server_by_alloc`, return 0"). design-flag-and-risk does not.

### E3. "Summoning is stat 24" and "Summoning takes the existing cell 24" are mutually exclusive. design-flag-and-risk asserts both.

**[measured]** `interfaces/stats.if`:
```
[sailing]  x=127 y=211 width=62 height=32  onload=i:393,i:-2147483645,i:20971553,i:24,i:1
```
The `i:24` is an **`enum_681` slot**, and **[measured]** `configs/all.enum` `[enum_681] val=24,23` → stat **23**. Cell 24 *is* stat 23.

design-flag-and-risk §0 argues stat 23 must not be touched (26 CS2 scripts read it), then §0 row 3 claims "no geometry change — Summoning takes the existing cell 24", then Phase 1 writes `enum_681 val=24,24` and "`script_8950.cs2` case 23 removed" — which **deletes Sailing from the client roster entirely**. That is a defensible design (Sailing is unimplemented server-side: `pack/stat.pack` **[measured]** stops at `22=construction`), but it is not "no change" and it contradicts its own §0.

Settled implementation: slot **25** keeps Sailing and uses the pitch-26 grid. Construction /
Hunter / Summoning are contiguous across row 8; Sailing moves to the left of row 9 and Total
spans its other two cells. Component 34 uses dedicated script 1198 with the rev-530-measured
wolf-head nudge, while the compressed legacy cells use the gated script-393 overlay. The former
centred lone Summoning cell was wrong.

### E4. The varp-ceiling risk is stale and one design still carries it.

**[measured]** `src/net/mock/mock230.h:325` — `MOCK230_VARP_SERVER_HEADROOM = 1024`, so `MOCK230_VARP_COUNT = 6729`, against a `varp.alloc` high-water of 6225. design-flag-and-risk corrected this; design-skill-and-cs2 residual risk #8 still says *"`MOCK230_VARP_COUNT = 6217` is already exceeded… fix it before slice 10."* Delete it — it will send someone on a fix for a non-bug.

### E5. CORRECTED — the review reversed NPC_INFO's index and definition fields.

The earlier finding here was wrong. NPC_INFO v5 sends a **16-bit per-client NPC index**
(`0xffff` terminator), then a **14-bit initial definition**. For ids 16384..65535, the add's
extended/update flag and update-mask `0x1` replace that definition with the transformed unsigned
16-bit `p2Alt3` / `UShortLEAdd` value in the same packet; a regression covers index 321 with type
20000. `content_register.c:63`'s `server_base = 20000` is therefore valid and must not be moved
to 16294. The measured free run below 16384 is irrelevant to Summoning scope.

### E6. CORRECTED — the loc wire risk was real and is now measured/fixed.

`content_register.c:65` gives loc `server_base = 70000`, while the native cache high-water is
62200. Rev239 `LocAddChangeV2Encoder` writes the loc config id as `p2Alt3`: obfuscation changes
byte order/value transforms, not width, so the field is exactly 16 bits and 70000 truncates to
4464. The Summoning port does not use that base: source obelisk 28716 maps to target 62201.
Permanent client acceptance proves the runtime packet decodes/picks 62201 and never 4464.

### E7. design-asset-pipeline oversells "already written".

> *"Point `ctx->profile` at 530 and the same function emits a 530 record as tree text. That is the entire config import, already written."*

It isn't. `cachepack` refuses RS2 sharded config layouts outright — the design itself lists "sharded RS2 config reader (lift the `cp_common.c:58` refusal), ~80 LOC" as new work in §9 item 5. §1's framing will be read by a planner as "config import is free."

### E8. Small ones

- design-skill-and-cs2 §3.2 first writes *"`interfaces/stats.compack` gains `34=summoning`, `35=tooltip` — **no.**"* then corrects itself. **[measured]** the file is 0..33 with `33=tooltip`; `34=summoning` is correct. Clean this before anyone copies the first version.
- design-flag-and-risk Phase 1 leaves BasType as an open "either add a cachepack type or set explicit anim slots." design-server-content §13 already resolved it (`tool_neutral_npc_from_dat2` flattens BAS; `fields/npc.ini` carries `walkanim`/`readyanim` as `scope = client`). Take the resolved answer.

---

## 2. UNVERIFIED LOAD-BEARING CLAIMS, by blast radius

**1. Can the rev-239 client render an OB3 or OB2 model at all?**
**[measured]** all 61,615 models in `osrs239-content/models/` are `fffd` (34,625) or `fffe` (26,990). **Zero OB2, zero OB3.** design-asset-pipeline's census is exactly right, and its consequence is understated: there is no existence proof anywhere in this tree that either format loads. Its transcode plan (OB3→V3, OB2→V2 via `RSCache_ModelEncodeFormat`) is inferred from a header comment (`model.h:174-179`) and has never been round-tripped on a single 530 model. And its own T1 tier cannot catch a loss here — T1 compares the *neutral struct* on both sides, so a field the OB3 decoder never populates compares equal to the V3 encoder never writing it. If this is wrong, every familiar, every pouch icon and the obelisk are wrong, silently.

**2. The chained-overlay bake.** The two merge mechanisms check out — **[measured]** `cache_edit.c:513` calls `dat2_edit_load_existing_files` before applying puts, and `cp_reference_sync` (`cp_binary.c:396-440`) extends `archives`/`ids` by archive id rather than rebuilding. But three things nobody looked at: (a) `cp_names_emit_gamevals` (`cp_names.c:1342+`) emits **whole archives** from the pack files, so a partial `configs/all.<ns>.compack` truncates that type's gameval table; (b) `cp_reference_write` does `rt->version += 1` on every dirty table, which a JS5 client caches against; (c) a partial `configs/all.<ns>` with a full `.compack` puts every base record at an origin the merge never sees — `resolve_id`/`record_in_base_cache` behaviour untested. The entire "flag-off bake is byte-identical" claim rests on `stage_summoning_overlay.py`, which does not exist.

**3. `IF_OPENSUB` on a group absent from the cache.** Three designs flag it; none resolves it. design-flag-and-risk's mitigation is *"the flag is paired, so the mismatch is unreachable"* — but `MOCK230_SCRIPTS` (env) and `[cache:boot] dir=` (manifest) are independent knobs, `MOCK230_CACHE_DIR_DEFAULT` is **[measured]** `"cache.osrs239"` (pristine), and nothing binds them. That is a convention, not a mechanism.

**4. CORRECTED / VERIFIED.** The rev-239 sidebar renders the modified interface 320. Fresh-save
headless runs show component 34, exact source-222 wolf pixels through target sprite 229, Sailing
at the lower left, and Total beside it. The permanent test pins non-zero checks and the rendered
coordinates; script 1198 is the dedicated Summoning-cell clientscript.

**5. `dbindex` derivability.** **[measured]** no regenerator exists anywhere in `tools/` (grep hits only perf logs and `src/makefile`'s `task_dat2_dbindex_load.c`). design-skill-and-cs2's `tools/gen_dbindex.py` with a byte-identical-regeneration acceptance test is the right call and the only scheduled one. But the acceptance bar is itself unproven: nobody knows whether the committed `.dbi` ordering is derivable at all. If it isn't, the guide is hand-edited forever and `db_find` misses are silent.

**6. 530 decode exactness beyond npc.** Only npc was measured (8590/8590). obj is 9178/14654 — **37% short**. loc, seq and spotanim at 530 are entirely unmeasured, and `find_named --type loc` returns nothing. Three designs schedule loc and spotanim work on top of that.

**7. `load_inv_sizes` reads the *boot* cache.** **[measured]** `mock230_bank.c:127-152` opens config group INV from disk, so design-server-content's "no `.inv` walker needed" correction is right — **provided the server boots the baked cache**. Nothing in any design states that, and the default is the pristine one. BoB silently returns NULL from `mock230_container_resolve` and every op aborts.

**8. `script_1010.cs2` in the XP-drop edit set.** design-skill-and-cs2 asserts "all 70 xpdrops-touching scripts compile, 0 failures" and separately lists `script_1010` as an edit target. Nobody checked whether 1010 is in that 70.

For the record, things I **confirmed** so they stop being re-litigated: `SS_OPCODE_MAX 11022` (next free extra opcode is 11022); `SS_TRIGGER_IF_OPEN 178`; `MOCK230_TIMER_MAX 8`, `MOCK230_PLAYER_MAX 8`, `MOCK230_CONTAINER_MAX 16`; all five `pack/*.client` files have **0 data lines**; `RSCache_Dat2FramemapEncode` takes no codec argument and `cache_write.c:564-578` re-encodes without clearing `has_transform_actor`/`has_masks`/`tail` (the V3→V1 silent no-op is real; `transcode.c:118` is the only guard); the sequence codec falls to V3 for any rs2 profile (`dat2_config_sequence.c:1139-1155`); no `rs530` in `revisions.c`; `script_1904.cs2` is the sole failure among the six skill-tab scripts I compiled — `FAIL script_1904.cs2: line 62: unknown command '_1703'` / `compiled 5, failed 1`; `staticons2_14..17` (sprite ids 229-232) are 25×25 blanks with `pack.meta` and no BMP; `SummoningCreationPlugin.java:88` really does say `28278`.

---

## 3. MISSING WORK

Against the user's own list:

| Asked for | Coverage |
|---|---|
| **models** | design-asset-pipeline only. Hole: §2.1 above. |
| **animations** | Covered (frame COPY + framemap codec fix + `SEQUENCE_RS2_530`). **Missing: the `port/` ledger has no `frame_archive` row** — 530 has 2,724 frame archives vs osrs239's 10,902, every one needs remapping *and* every referring seq's `(archive<<16)\|file` rewritten, and none of the three ledger schemas has a column for it. |
| **sounds** | design-asset-pipeline: byte COPY, correct. **Missing: 2009scape has no per-familiar summon-sound table at all** (`Familiar.java:713` is a TODO). "One shared summon sound in tier 1" is a content *invention* and nobody records it as such in a ledger. |
| **scrolls** | Mechanic covered (1 pouch → 10). **The 67 distinct scroll obj ids appear in no asset manifest** — every ported-obj example is pouches. |
| **"any items that appear in the summoning content"** | **Worst-covered item on the list.** Nobody enumerates the ~60 tertiary ingredients (237, 249, 311, 383, 440, 571, 590, 1115, 1119, talismans 1438/1440/1442/1444, bars, furs, eggs, 10818…). Most already exist in osrs239 under different ids, so this is a **530→239 obj name-resolution pass** — exactly what a `port/` ledger is for, and no design schedules it. Also entirely absent: summoning potions (12140/12142/12144/12146 + super restore mix + the SC variants), Bogrog's pouch-swap, the summoning cape/hood (12169), Wolf Whistle items (12527/12528/12530). Only design-server-content mentions potions, in one line, with no obj list. |
| **interfaces** | Three `.if` files named. **Missing: the familiar tab needs `side14`/`stone14`/`icon14` in `toplevel_osrs_stretch` — [measured] side0..13 is full — plus the same edit in `toplevel` (548) and `toplevel_pre_eoc` (164) or the tab disappears on a layout switch, plus row 14 in cache enums `enum_1137/1138/1139` which `script_912` walks.** Only design-flag-and-risk mentions "three toplevels", in Phase 5, deferred. |
| **skill guide + icons** | design-skill-and-cs2 only, and it is the strongest section in the pack. **`script_9176.cs2b` is bytecode-only, so the Overview tab is unmodifiable** — flagged, not planned around. `enum_1505` (the 13px guide-title icon, **[measured]** `val=23,3230`) appears in *only* that one design; the other two would ship a guide with a blank title icon and never know. |
| **locs** | Obelisk covered via runtime `loc_add`, which correctly sidesteps `maps/`. **Missing: the 14 "inert obelisk" partner locs** (28717/28718 …) that are the off-state transforms. Plus E6. |
| **distinct ported folder** | **Three incompatible answers.** `models/ported/scape2009_summoning/` + `server/scripts/ported_scape2009_summoning/` (asset-pipeline) vs `ported_2009scape/summoning/` + `server/scripts/ported_2009scape_summoning/` (server-content) vs a **top-level `ported_scape2009_summoning/` deliberately outside every walker** (flag-and-risk). These are not naming variants — the third one has completely different flag semantics from the other two. Unreconciled. |
| **feature flag** | Three answers again, and they don't compose cleanly. Only the separate-cache layer gates *asset presence*; only the `^constant` gates *server behaviour*; only the `script_8950` varbit gates the *tab and totals*. No design writes the composed contract or says which is authoritative when they disagree. |

Also absent across all three: **pets** (descoped in one, silently missing in two — `FamiliarManager.parse` NPEs on `petDetails`, and nobody designs the save shape so it can be added later); **charm drops** (1,222 rev-530 npc ids, deferred to "tier 3" with no id-translation design); a single agreed **`port/` ledger name and `--check` script** (three proposed names); a single agreed **topic-doc name**; and the **`[advancestat,summoning]` name-collision lint** (proposed by one design only — `ssc_compile.c:2286` resolves trigger subjects with `SSC_SYM_UNKNOWN`, so one ported record named bare `summoning` silently mis-binds).

---

## 4. SEQUENCING PROBLEMS

1. **design-server-content C5 (skill-guide dbrows) precedes E1 (`cachepack membership`).** dbrow ids come from `pack/dbrow.alloc`, and `routing_client_member:758` sends **any alloc-claimed name server-side**. Guide rows authored in Phase C hit cell (c) and fail the pack. `pack/dbrow.client` must exist first.
2. **design-flag-and-risk's Phase 0 does not contain the membership spike its own Risk 5 says must come first.** The spike lives only in the mitigation prose. Phase 1 then depends on it.
3. **`cachepack import` appears in no phase list except design-asset-pipeline's.** design-server-content Phases G2/G3 import objs and npcs; design-flag-and-risk Phase 1 imports npc 6829. Both assume a ~1,600 LOC tool that the third design costs at 8–12 days and schedules independently. Nobody wired the two plans together.
4. **design-asset-pipeline's own ordering fights the content plans.** Its Phase 5 (npc/obj/loc config records) is last; design-server-content's G2 needs pouch objs early, and design-skill-and-cs2's guide rows need obj ids for the `icon` column in slice 7.
5. **The stat decision gates everything and is unresolved** (E3). design-skill-and-cs2 slice 4 and design-flag-and-risk Phase 1 write `enum_681` with different values.
6. **design-server-content C5 hand-edits `dbindex_21{2,3}.dbi`** while design-skill-and-cs2 slice 2 builds the generator that makes hand-editing unnecessary — and slice 2 is earlier. Take the generator; delete the hand-edit step.
7. **design-server-content F (owner opcodes) is after G3** in its list but G4 (Spirit wolf) needs it. Reading the phases in order, G3 lands Pikkupstix (no owner needed) so it survives, but the dependency is not stated and a reader working the list will reorder it wrong.

---

## 5. THE HARDEST UNSOLVED PROBLEM

**The cross-revision asset import does not exist, and its correctness is undetectable by the verification the designs propose.**

Assemble the pieces, all confirmed:

- **[measured]** osrs239 has zero OB2/OB3 models. 530 has 39,694 OB3 + 5,778 OB2. The transcode target is inferred, never tested.
- Texture ids are cache-local: 530 has 680 procedural materials, osrs239 has one archive of ~210 sprite-backed ones. No transcode exists (`EXCEPTIONS.md` A5; no `ProctextureEncode`). Every familiar model sampled carries live texture triangles.
- The framemap V3→V1 downgrade is a **confirmed silent no-op today** — familiars animate in bind pose with no error.
- The sequence codec is wrong for the entire RS2 branch (649 bad seqs at 530).
- `cachepack` cannot read RS2 sharded configs at all.
- No `rs530` profile exists, and `--rev rs643` — which every recon probe used — pins `FRAME_V2`, a rev-610 format, corrupting every animation frame it touched.
- The importer itself is ~1,600 LOC that nobody has started.

design-asset-pipeline is the only design that engages, and its T0–T4 tier structure is genuinely good work. But it **admits its own top risk is invisible to all five tiers**: T0 passes (the id decoded), T1 passes (the id was carried faithfully), T2 can't compare (different texture systems; `--by-label` explicitly ignores materials), T3/T4 read a wrong texture as "done." Its mitigation is *procedural* — import untextured, hand-build the map, human-eyeball every model via `ev_server` before flipping a `signoff` column. For ~60 familiars × 2 forms + ~80 spotanim models + pouch/scroll icons, that is a hand-review backlog in the hundreds, priced at "1–2 weeks, irreducibly human."

That is honest. It is also not solved — it is converted from an invisible defect into a visible one, which is the right move but does not shrink the work. And **the other two designs simply assume the pipeline exists and works.** Every content estimate in them is downstream of an unbuilt, unvalidated tool.

The former runner-up about “92 free npc ids” was invalid because it treated the direct 14-bit
initial-definition field as a cache-definition ceiling. Type 20000 is now an explicit
extended-path protocol regression case, not a truncation hypothesis.

---

## 6. SCOPE HONESTY

### Is design-flag-and-risk's Phase 1 a demonstrable slice?

No. Read what it bundles: rev-530 profile + three codec fixes + a nonexistent import tool + the first-ever `pack/obj.client` + the first-ever new npc id + the first-ever authored asset + `MOCK230_STAT_COUNT` widening + six enum rows + a `stats.if` edit + a `script_8950` edit + three new VM opcodes + owner-bound-NPC engine plumbing + a wolf that follows you through a region boundary and survives logout. At **12–18 days**.

That is the whole project minus the UI, priced at three weeks. Its own sibling design costs the asset pipeline alone at 3–5 weeks, and Phase 1 sits entirely downstream of it. The estimate is not credible.

### The Phase 1 that nobody proposed and should

**Summoning appears in the skills tab at level 1, `::setlevel summoning 20` moves it, it persists across logout, total level includes it, and the flag-off bake is proven byte-identical.**

Zero assets. Zero 530 cache reads. Zero new npc ids. Zero interfaces. Zero new opcodes. Touches: `pack/stat.pack`, `MOCK230_STAT_COUNT` 23→25, six `configs/all.enum` rows, one `stats.if` block, one `script_8950.cs2` case, one 25×25 sprite. **~5–8 days.**

That slice settles the stat decision (E3), proves the rank-1-overlay-into-a-cache-enum mechanism, proves the CS2 compile gate (the near-silent one: a failed compile ships base bytes and only a counter says so), proves the byte-identity gate, and produces a screenshot — all before a single model is transcoded. Every design has these pieces; none of them isolates them.

### Are the totals credible?

design-flag-and-risk's **63–97 days** is the only whole-project number and the most honest artifact in the pack. It is still low, for three reasons:

1. It excludes the ~1,600 LOC `cachepack import`, which its sibling separately prices at 8–12 days on top of ~900 LOC of library work (3–5 days).
2. It prices Phase 3 breadth at "0.5–1 day per familiar once the pipeline is warm" while the same pipeline's own author calls the texture map "1–2 weeks, irreducibly human" and mandates a per-model human render review.
3. It assumes the membership add path works. **[measured]** all five `.client` files have zero data lines, and `PACK_ENTITY_SPLIT_PLAN.md` §11.1 says step 4 "author" is unexercised. This port is that mechanism's first consumer.

Fold the tool in, price both hand-review passes, and add the doc/queue/ledger work three designs each propose separately: **4–7 months of one engineer**, with pets, charm drop tables, wilderness combat forms and npc↔npc combat all *already* descoped. Anyone committing to "a working Summoning skill this quarter" is committing to the 5–8 day slice above plus a research project.
