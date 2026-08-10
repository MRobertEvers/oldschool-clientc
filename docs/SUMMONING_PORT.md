# Summoning port — plan and working state

**Source:** `2009scape` (RS2 rev 530, Jan 2009) · **Target:** this repo + `OSRS-Content/osrs239-content` (OldSchool rev 239)

**Status:** Phase 0 in progress. Nothing gameplay-facing exists yet.

Summoning is **not an OldSchool skill**. This is a deliberate, feature-flagged port of RS2 content
into an OSRS-shaped tree, kept in a marked lane (`ported/scape2009_summoning/`) so it is never
mistaken for authentic osrs239 content. It reverses a written skip-list policy in five places
(§9).

---

## How this plan was produced

A 17-agent pass (12 recon → 4 design → 1 adversarial red-team), 3.5M tokens, ~48 min. The agents
did real work against both trees: they dumped the 530 cache to CSV, decoded configs, compiled CS2
scripts and measured round-trip failures. Their raw output is preserved:

| document | what it is |
|---|---|
| [`summoning_port/AGENT_RECON.md`](summoning_port/AGENT_RECON.md) | 12 recon reports — 2009scape summoning logic and data, the rev-530 cache, the 530 client side; and on our side the content tree, cachepack, skills, CS2 toolchain, feature flags, mock230, IF3 authoring, port process |
| [`summoning_port/AGENT_DESIGNS.md`](summoning_port/AGENT_DESIGNS.md) | 4 design docs — asset pipeline, skill/CS2, server/content, flag+risk |
| [`summoning_port/AGENT_REDTEAM.md`](summoning_port/AGENT_REDTEAM.md) | adversarial review. **Read this before trusting anything in the designs** — it caught 8 factual errors and 8 unverified load-bearing claims |
| [`summoning_port/pouches_530.json`](summoning_port/pouches_530.json) | the 82-entry `SummoningPouch` table, machine-extracted. The port manifest |

**The designs contradict each other in places and the red-team doc is the tiebreaker.** Where this
plan disagrees with a design, this plan wins; where this plan is silent, check the red-team doc
before the designs.

Not preserved (regenerable once the rev-530 profile exists, ~5 MB): `npc530.csv`, `obj530.csv`
full config dumps.

⚠ **Every recon probe that used `--rev rs643` against the 530 cache is suspect.** `rs643` pins
`FRAME_V2` (a rev-610 format), which corrupts every 530 animation frame it touches. Any frame or
framemap number quoted in the recon doc must be re-measured against the real 530 profile.

---

## 1. Four findings that changed the premise

### F1 — Summoning is stat **24**, not 23. Stat 23 is Sailing, and it is live.

2009scape uses `Skills.SUMMONING = 23`. In osrs239 that id is **Sailing**, wired end to end
**[measured]**:

| record | evidence |
|---|---|
| `configs/all.enum` `[enum_681]` (canonical stat roster) | `val=24,23` — display slot 24 → stat 23 |
| `[enum_108]` slot→string | `valstr=24,Sailing` |
| `[enum_680]` stat→string | `valstr=23,Sailing` |
| `[enum_255]` stat→25px icon | `val=23,228` |
| `[enum_5917]` stat→silhouette | `val=23,7454` |
| `[enum_1505]` stat→13px guide-title icon | `val=23,3230` |
| `[enum_1497]` members-only | `val=23,1` |
| `interfaces/stats.compack` | `24=sailing`, a real 62×30 cell at x=127 y=211 |
| `interfaces/levelup_display.compack` | `57=sailing` |
| `scripts/script_1003/1004.cs2` | XP-drop listener already passes `stat_xp(stat_23)` |
| `scripts/script_8950.cs2` | `case 23 : return(~script8951(1))` — the hide gate |

Taking 23 would make ~26 cache-native clientscripts draw a boat for Summoning.

`RS_PLAYER_STATS_SKILL_COUNT` is already **25** (`src/game/rs_player_stats.h:11`), so index 24 is
the last valid slot and needs **no client C change for storage**. `pack/stat.pack` **[measured]**
stops at `22=construction`, so it needs `23=sailing` (a correctness fix — nothing awards Sailing
xp) **and** `24=summoning`.

### F2 — The feature flag already exists in the cache.

**[measured]** `scripts/script_8950.cs2`:

```
[proc,script8950](int $int0)(int)
switch_int ($int0) {
    case 23 : return(~script8951(1))     // → varbit content_restrict_sailing_serverside
    case default : return(0)
}
```

`configs/all.varbit.compack:16780` → `18166=content_restrict_sailing_serverside`. Non-zero = hide
the skill. Consumed by `script_393` (lock overlay) and `script_1007`/`1008`/`1320` (total level,
total xp, F2P total).

**This is a shipped, cache-native, per-skill, server-driven feature flag.** Adding `case 24`
against a new `content_restrict_summoning_serverside` varbit gives us the client half in the
cache's own vocabulary, and total level includes/excludes Summoning with **zero script edits**.

We do **not** touch `src/features/features.h`. That is a client-*era* table keyed on cache lineage
(`features.c:190` discards revision); Summoning is not an era divergence. Using it would be a
category error.

### F3 — NPC id space is **not** a constraint on this port.

**The full 82-familiar roster is in scope with no id budgeting.**

Planning initially treated the 14-bit npc type field in the osrs239 NPC_INFO v5 add-block
(`src/net/rev/osrs239/osrs239_entity_info.c:1923`) as a hard ceiling of 16383, which — against a
cache high-water of 16293 — would have left only ~90 free ids. **That is not a live constraint:
separate work is in place to remove the npc id cap.** Do not scope, tier or budget this port
around it, and do not re-derive the ceiling from the current v5 reader — it is being changed.

Consequences for this lane:

- Port all 82 familiars. No tiering for id reasons.
- Wilderness combat twins (`id+1`) and pets stay descoped for *behavioural* reasons (a separate
  lifecycle, no wilderness in this tree), not id reasons.
- `ss_allocate.py` / `content_register.c` npc `server_base` should follow whatever the cap-removal
  work settles on. Coordinate rather than picking a number here.

> **TODO:** link the cap-removal branch/doc here once it lands, so this section stops being the
> only record of the decision.

### F4 — The cross-revision asset importer does not exist. This is the project.

**[measured]** all 61,615 models in `osrs239-content/models/` are `FF FD` (V3, 34,625) or `FF FE`
(V2, 26,990). **Zero OB2, zero OB3.** The 530 cache is 39,694 OB3 + 5,778 OB2 — a **disjoint
format set**. Models cannot be copied; they must be decoded and re-encoded, and **there is no
existence proof anywhere in this tree that the client renders either source format.**

Four confirmed pre-existing bugs sit in the path:

1. **Framemap V3→V1 downgrade is a silent no-op.** `tools/common/cache_write.c:564-578` re-encodes
   without clearing `has_transform_actor`/`has_masks`/`tail`, and `RSCache_Dat2FramemapEncode`
   takes **no codec argument**. Left unfixed, every ported familiar animates in bind pose, no error.
2. **The sequence codec is wrong for the entire RS2 branch.** `dat2_config_sequence.c:1139-1155`
   calls `RSCache_RevisionAtLeastOsrs(..., default_when_unknown=true)`; an rs2 profile can never
   satisfy an OSRS threshold, so every RS2 cache falls through to V3 — 649 bad seqs at 530, 2,977
   at `void634`, 3,139 at `rs727_preeoc`, 0 at osrs239.
3. **`cachepack` cannot read RS2 sharded config layouts** (`cp_common.c:58` refuses outright).
4. **No `rs530` profile exists.**

And the one that decides the schedule: **texture ids are cache-local and un-transcodable.** 530 has
~680 procedural materials; osrs239 has ~210 sprite-backed ones. No converter exists
(`3rd/rscache/EXCEPTIONS.md` A5, no `ProctextureEncode`). Every familiar model sampled (30443,
31211, 30435, 31168, 30469) carries 3–21 live texture triangles.

**This is the one error class no automated check can catch.** A mis-mapped material renders a wolf
with a stone hide: exact-consumption passes (the id decoded), semantic round-trip passes (the id
was carried faithfully), render-compare can't help (different texture systems, never
pixel-identical), and eyeball tiers read a plausible wrong texture as done. The mitigation is
procedural, not technical — import untextured, hand-build the map as a reviewed pass, human-verify
every model before its ledger row flips to `ok`. That converts an invisible defect into a visible
backlog of a few hundred renders. It does not shrink it.

New code required: ~900 LOC library + ~1,600 LOC `cachepack import`.

---

## 2. Decisions — settled

1. **Sailing is kept.** Summoning becomes display slot **25** (`enum_681` gains `val=25,24`) and
   the stats panel goes to a **3×9 grid**. `stats.if [universe]` is 190×261 **[measured]**.
   **Prefer a dedicated clientscript for the Summoning cell alone** over rewriting the positioning
   of all 25 cells — strictly less blast radius on a panel whose builder (`script_1904.cs2`) is one
   of the 95 committed scripts that already fail to recompile.
2. **Full roster — all 82 familiars.** No id budgeting; the npc id cap is being removed
   separately (F3).
3. **First pass runs through Phase 3** — governance, the skill in the tab, the asset pipeline, and
   a summonable Spirit wolf. Review before breadth.

---

## 3. Feature flag architecture — three layers

| layer | mechanism | gates |
|---|---|---|
| **Asset presence** | a separate baked cache (`cache.osrs239.summoning`) from a chained overlay | whether summoning records exist at all |
| **Client display** | `script_8950` case 24 → `content_restrict_summoning_serverside` varbit (F2) | tab cell, total level, guide, lock overlay |
| **Server behaviour** | one `^summoning_enabled` ServerScript constant, checked at the top of every ported proc | all gameplay |

**Hard requirement: flag off ⇒ the baked cache is byte-identical to today.**

⚠ Unproven. The two merge mechanisms check out (`cache_edit.c:513` loads existing files before
applying puts; `cp_reference_sync` extends by archive id rather than rebuilding), but three corners
are untested: `cp_names_emit_gamevals` emits **whole archives** from pack files, so a partial
`configs/all.<ns>.compack` truncates that type's gameval table; `cp_reference_write` bumps
`rt->version` on every dirty table, which a JS5 client caches against; and a partial `all.<ns>`
with a full `.compack` puts base records at an origin the merge never sees. Phase 0 builds and
proves this or the design changes.

⚠ **The three flag layers are independent knobs with no mechanism binding them.** `MOCK230_SCRIPTS`
(env) and `[cache:boot] dir=` (manifest) are unrelated, and `MOCK230_CACHE_DIR_DEFAULT` is
`"cache.osrs239"` (pristine). "The flag is paired so a mismatch is unreachable" is a convention,
not a guarantee. **[measured, Phase 0]** `IF_OPENSUB` of absent group 969 against the pristine cache
logs `pack 969 missing from cache; skipping mount`, returns to the next script instruction, renders
the follow-up message, and exits normally. The mismatch is a missing panel, not a soft hang/crash.

---

## 4. Content tree layout

One prefix everywhere, so provenance is visible in the pack file, on disk, and in every script
header:

```
OSRS-Content/osrs239-content/
  ported/scape2009_summoning/            PROVENANCE.md + configs (.npc .obj .loc .seq .spotanim)
  models/ported/scape2009_summoning/     *.model
  animsets/ported/scape2009_summoning/
  framemaps/ported/scape2009_summoning/
  sprites/ported/scape2009_summoning/
  server/scripts/ported_scape2009_summoning/   *.rs2
  port/summoning_530.map                 530 id ⟶ 239 id ⟶ disposition ⟶ signoff
```

Asset paths need **zero tool changes**: the pack name *is* the path (`import_one` at
`cp_assets.c:1407` does `snprintf(base,"%s/%s",root,name)`), and **[measured]** 53,421 of 61,615
model names already contain a `/`. Every `.rs2` carries the house-convention
`// Policy: 2009scape <Class>.java` header — 295 files already do.

⚠ A directory name is invisible to `sscompile`, `cachepack` and `mock230` — all three walk
recursively and skip only a leading `.`. Provenance is therefore marked three ways: the folder
name, a `PROVENANCE.md`, and the mandatory per-file header.

---

## 5. Phase 0 — Governance & guardrails · ~3–4 d

The port is currently **illegal under the repo's own written policy**; the agent loops will keep
deleting the lane until that is fixed.

- [x] `docs/PORTING_GUIDE.md:35` — drop Summoning from the skip clause, point at the new queue
- [x] `docs/PORTING_GUIDE.md:683` — drop Summoning from the §4.5 skip-list sentence
- [x] `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65,68` — remove the two skip rows, add a "moved off
      this skip list" note so nobody re-adds them
- [x] `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` — Summoning is no longer skipped
- [x] `docs/SKILLS_CONTENT_PORT_QUEUE.md:349` — the "Audit roster complete (23/23)" claim now says
      Summoning is a 24th row deliberately outside that count
- [ ] **`CLAUDE.md` is absent** — deleted in `5cdb9c14` ("ranged combat and crystal bow"), 19
      lines, and it looks accidental: it was the only doc removed in a combat commit. Four files
      still cite it as binding (`CONTENT_PORT_QUEUE.md:21`, `SCAPE2009…:478`, `map_instances.md:15`,
      `.cursor/rules/no-park-sibling-content.mdc:38`). Restore with
      `git show 4a3f2645:CLAUDE.md > CLAUDE.md`, or delete the four citations. **Decide with the
      user — restoring it changes agent behaviour repo-wide, beyond this port.**
- [ ] `docs/SUMMONING_PORT_QUEUE.md` — the per-slice loop doc, in the house queue format
- [ ] `port/summoning_530.map` ledger + `tools/port_summoning_ids.py --check`, wired into
      `make -C src test-port`
- [ ] **Byte-identity harness** — prove the flag-off bake is unchanged. `stage_summoning_overlay.py`
      does not exist; this is new work and the whole isolation claim rests on it
- [ ] `check_summoning_isolation.py` — **must print and assert a non-zero check count.** A skip
      that reads as a pass is the #1 risk (§8)
- [ ] **Spike the membership add-path** on a throwaway obj before designing on top. All five
      `pack/*.client` files have **zero data lines**; `PACK_ENTITY_SPLIT_PLAN.md` §11.1 says step 4
      "author" is unexercised. This port is that mechanism's first consumer. Budget a full day
- [x] Characterise `IF_OPENSUB` on a cache-absent group — graceful logged skip; script continues

---

## 6. Phase 1 — "Summoning is a skill" · ~5–8 d · zero assets, zero 530 cache reads

The slice none of the designs proposed and the right first move: it settles the stat decision,
exercises the overlay and membership mechanisms on trivial records, proves the CS2 compile gate,
proves byte-identity, and produces a screenshot — all before a single model is transcoded.

- [x] `pack/stat.pack` += `23=sailing`, `24=summoning`
- [x] `MOCK230_STAT_COUNT` 23 → 25 (`src/net/mock/mock230.h:558`; 40 call sites, all bounds or
      array sizes; `stat_dirty` is `uint32_t`, 25 bits fits; `mock230_save.c` is id-keyed ini so
      saves stay forward/backward compatible)
- [x] Cache overlay: `enum_681` `val=25,24` (**keys must stay contiguous** — a hole silently
      truncates the roster loop and drops every later skill from Total level), plus `enum_680`,
      `enum_108`, `enum_255`, `enum_5917`, `enum_1497`, `enum_1505`
- [x] `interfaces/stats.compack` `34=summoning_stats_cell` (**[measured]** the file is 0..33 with `33=tooltip`)
- [x] `stats.if` — a 25th cell and the 3×9 grid (per Decision 1, via a dedicated clientscript)
- [x] `script_8950.cs2` case 24 + a new `content_restrict_summoning_serverside` varbit
- [x] One 25×25 skill icon. **[measured]** `sprites/staticons2_14..17` (ids 229–232) are
      already-reserved 25×25 blanks with `pack.meta` and no BMP
- [x] Regression assertion that combat level does **not** move

**No ServerScript compiler change is needed** — `ssc_compile.c:755-771` gives `STAT*` opcodes
`base_hint = SSC_SYM_STAT`, so `stat_base(summoning)` / `stat_advance(summoning, …)` resolve the
moment `stat.pack` lands.

**Combat level cannot move** — all three implementations name stats explicitly, none loops
(`rs_player_stats.c:76-89`, `mock230_combat.c:913-925`,
`server/scripts/player/configs/combat_level.rs2:5-11`). **Total level moves automatically** —
`script_1007` walks `enum_681` until null, gated on `~script8950`.

⚠ **Never name a ported record exactly `summoning`.** `ssc_compile.c:2286` resolves trigger
subjects with `SSC_SYM_UNKNOWN` — any namespace, first match wins, and mis-resolution is silent.
Prefix everything `summoning_*` and add a `mock230_pack --check-only` rule that every name in
`pack/stat.pack` is unique across all namespaces.

**Acceptance:** log in → Summoning in the skills tab at level 1 → `::setlevel summoning 20` moves
it → survives logout → total level includes it → flag off hides it → flag-off bake proven
byte-identical. Four BMPs.

**Verified:** `make -C src test-summoning-phase1 PLATFORM_OBJ_BASE=build_summoning` performs 28
non-skipped checks and leaves the four BMPs plus logs in `build/summoning-phase1/`. The flag-on
totals are 34 at level 1 and 53 at level 20; the second login restores `24 = 20 44700`; the
pristine flag-off cache has no stats component 34 and remains total 33. The full mock selftest is
currently pre-blocked by two failures in the separate dirty NPC-area subscription work
(`slot=993 type=2862`), after the Summoning combat assertions themselves pass.

---

## 7. Phase 2 — Asset import pipeline · ~15–25 d

A new `cachepack import` subcommand in C plus a rev-530 profile. Not Python, not standalone —
cachepack already owns both halves of the destination format (`cp_unpack_npc(ctx, …)` decodes with
`ctx->profile` and emits tree text; point the profile at 530 and the same function emits a 530
record).

- [x] `3rd/rscache/src/revisions/rev_dat2_rs530.c` + 2 rows in `revisions.c` (~70 LOC)
- [x] `SEQUENCE_RS2_530` codec — op 13 u16 count, op 14 bare flag (~120 LOC)
- [x] `OBJ_RS2_530` codec — 96/121/122/125-130; 23/25 with no trailing byte (~60 LOC)
- [x] `RSCache_Dat2FramemapEncodeCodec` — **fixes the confirmed silent data bug** (~40 LOC). Write
      a test that fails on today's code first
- [x] Sharded RS2 config reader — lift the `cp_common.c:58` refusal (~80 LOC)
- [ ] `cachepack import` — manifest, closure walk, id remap, tree writer, ledger (~1,600 LOC)
- [ ] `anim_compare --b-cache/--b-rev/--b-seq/--b-model` (~200 LOC)
- [ ] `pack/{obj,seq,spotanim}.client` + `content.ini` membership blocks

⚠ Pin `FRAMEMAP_V3` (the threshold is exactly ≥530 — pin it so the boundary is a declaration rather
than an off-by-one) but **do not pin FRAME**; it must auto-derive V1. Copying rs643's `FRAME_V2`
pin is exactly what corrupts 530 frames.

⚠ Fixing the RS2 sequence codec touches the whole RS2 branch. **A/B 634 and 727 before and after**
— per the pristine-baseline rule, do not attribute a 634 change to Summoning.

**Texture policy:** import with textures **dropped**. The 680→210 material map is a separate
reviewed pass with its own ledger column and a `signoff` starting at `unreviewed`; every ported
model gets a human-eyeballed `ev_server` render before it flips to `ok`. Budget 1–2 weeks,
irreducibly human.

⚠ **`content.ini`'s `ids` axis is dead** — `content_register.c:494-497` accepts and ignores it, and
`ss_allocate.py:96-112` returns a frozen tuple before its content.ini reader is ever reached. Any
plan step phrased as "flip `ids` to server" is a no-op that reads as done. The only levers are the
`SERVER_NAMESPACES` tuple and `server_base` in `content_register.c`.

---

## 8. Phase 3 — Spirit wolf vertical slice · ~8–12 d

npc 6829 / pouch 12047. Proves the whole path.

### The one genuinely new engine relation: owner-bound NPCs

Verified pre-existing defects it must fix:

- `npc_run_mode` (`mock230_world.c:2752`) resolves the followed player as `srv->active_player`,
  which `phase_npcs` never sets — the code admits it at `:2773`. With two summoners, both familiars
  follow the same arbitrary player.
- `run_trigger_script` (`mock230_scripts.c:1666`) sets `SSVM_ENT_PLAYER` from the same stale
  pointer for **every** trigger including `ai_*`.
- `npc_uid` has no generation counter (`mock230_scripts.c:4585` says so) — a stashed uid resolves to
  whoever took the slot.

- [ ] `struct Mock230Npc` gains `int owner_pid; uint32_t owner_gen;` (zeroed by `npc_spawn`, so an
      unowned npc is unchanged)
- [ ] `npc_run_mode` resolves from `owner_pid` when set, else today's behaviour — **narrowing, not
      branching**
- [ ] `run_trigger_script` prefers `npc->owner_pid` for `ai_*` triggers on owned npcs
- [ ] Opcodes `NPC_SETOWNER` / `NPC_OWNER` / `NPC_FINDOWNED` in the extra band — **next free is
      11022** (`ss_opcode.h:453`). `NPC_FINDOWNED` removes the need for a uid varp and so dodges
      the uid-generation hazard rather than working around it. Log them in the queue's opcode-gap
      table and implement in the same slice (`PORTING_GUIDE` §2.4/§4.5)
- [ ] npc `server_base` (`content_register.c:63`, currently 20000): align with whatever the
      npc-id-cap-removal work settles on rather than picking a number here (F3)

### Content

- [ ] Port npc 6829 assets (model, framemap, seqs) through the Phase 2 pipeline
- [ ] Objs: `summoning_pouch_spirit_wolf`, `summoning_shard`, `summoning_charm_gold` — with
      `pack/obj.client` created
- [ ] `[opheld1,…]` → level check → `npc_add` → `npc_setowner` → `npc_setmode(playerfollow)`
- [ ] **One** `[timer,summoning_tick]` at interval 1 carrying decay, point drain, special regen and
      both warnings. `MOCK230_TIMER_MAX = 8` per player and existing content already competes
- [ ] Dismiss, logout, death, call-familiar

⚠ Ported npcs must set **explicit `walkanim=`/`readyanim=`**, not `bas_type_id`. `bas` is not a
cachepack type, so every rev-530 familiar (which uses `bas_type_id` with `standing_anim = -1`)
would T-pose. `fields/npc.ini` declares both as `scope = client`, so this costs one line per npc
and removes a whole tool dependency.

**Points model:** summoning points are `stat(24)` dynamic vs `stat_base(24)` max — exactly
2009scape's model, riding `UPDATE_STAT` for free with zero new wire. mock230 has no boosted-level
restore tick, so points correctly do not regenerate, for free. Leave a comment so a future restore
tick excludes stat 24 alongside prayer.

**Demo:** log in, `::setlevel summoning 20`, click the pouch, a wolf appears and follows you to
Varrock, log out and back in, it is still there, the timer expires, it leaves.

---

## 9. Later phases (not in this pass)

**Phase 4 — skill surfaces (~12–18 d).** Skill guide (dbtable 212/213). ⚠ **The most fragile step
in the port:** no `dbindex` regenerator exists anywhere in `tools/`, and a wrong `.dbi` ordering
makes `db_find` miss *silently* — build `tools/gen_dbindex.py` with a byte-identical-regeneration
acceptance test first. Note `script_9176.cs2b` is **bytecode-only**, so the guide's Overview tab is
unmodifiable. Summoning orb (lowest-risk authored UI — the minimap chrome is a *cache record*,
interface 160 `orbs`, 57 components; copy the `orb_specenergy` block to (54,158) and hide the two
inert orbs). Sidebar tab (expensive — `side0..side13` is **full**, needs new components in *three*
toplevels 161/548/164 plus row 14 in `enum_1137/1138/1139`). Obelisk via runtime `loc_add`, which
sidesteps `maps/` entirely. ⚠ `content_register.c:65` gives loc `server_base = 70000` and **no
`loc_type_bits` field exists anywhere** in `src/net/rev/` — verify the wire width before
allocating, or 70000 truncates the way npc 20000 does. Infusion UI authored fresh in the 239
vocabulary; do not transcode 530's interface 669.

**Phase 5 — breadth.** 82 familiars · 67 scroll objs · special moves · the ~60 tertiary ingredients
(mostly a 530→239 obj **name-resolution** pass — most already exist under different ids; this is
what the `port/` ledger is for) · summoning potions (12140/12142/12144/12146 + mixes) · charm drops
(1,222 rev-530 npc ids to translate; expect heavy attrition). ⚠ 2009scape has **no per-familiar
summon-sound table** (`Familiar.java:713` is a TODO) — one shared summon sound is a content
*invention* and must be recorded as such. Only sound 188 is safe to byte-copy; 4161/4164/4214/4265/
4372 are above the 3826 divergence point.

**Phase 6 — Beast of Burden (~8–12 d), blocked on an unrelated prerequisite.** `fields/inv.ini` and
`[namespace:inv]` do not exist — the same gap that keeps `shop` unportable. `mock230_container_resolve`
returns NULL for an inv the cache does not size and every container op aborts. Its own slice, its
own acceptance, credited against `shop` too. Must not block Phases 1–5.

**Phase 7 — polish.** Sidebar tab if deferred · per-account unlock · Wolf Whistle · remaining
sounds · pets (a separate lifecycle — stub the save shape now so it can be added later without a
migration).

---

## 10. Risk register

| # | risk | mitigation |
|---|---|---|
| 1 | **Texture map errors are undetectable by automation** — a wrong material renders plausibly and every verification tier passes | import untextured; per-model human render signoff in the ledger |
| 2 | **A skipped suite reads as a pass.** A pristine worktree skips whole suites silently | every summoning target asserts a non-zero check count; grep runs for `SKIP` and fail |
| 3 | **Silent CS2 decline** — `cp_decode.c:2447-2452` ships base-cache bytes and only a counter says so; **95 of 9,368** committed `.cs2` already fail to recompile, **including `script_1904.cs2`**, the skill-guide builder | standalone `cs2 compile` gate requiring `failed 0` before every bake. Refresh stale sources **selectively** — a blanket `--assets=scripts` unpack overwrites hand-authored comments in `script_73.cs2`/`script_7304.cs2` and trips `check_crystal_set_contract.py`, a hard prerequisite of `mock230-cache`. `RUNESTAR_CS2_NAMES` is an undeclared hard dependency |
| 4 | ~~npc id exhaustion~~ — **not a risk.** Separate work removes the cap (F3) | none; do not scope this port around npc ids |
| 5 | **The membership add-path has never been run** — all five `pack/*.client` files have zero data lines | spike on a throwaway obj in Phase 0 before designing on top |
| 6 | **Chained-overlay byte-identity is unproven** and the staging script does not exist | Phase 0 builds it; fallback is the third walk root (`CP_WALK_MAX_ROOTS = 4`) |
| 7 | **Framemap/sequence codec fixes touch the whole RS2 branch** | A/B 634 and 727 before and after |

**Standing hazards.** Never `git stash` here (a no-op push turns `pop` into restoring an old
stash) · no ASAN on this Mac · `MOCK230_SAVES=$(mktemp -d)` on **every** headless run, baked into
the make recipe rather than left to the operator · never bare `pkill -f build/mock230` (it eats
`mock230_dev`) · `embed_test` decode is broken pre-existing, nothing decodes past login — A/B
against `HEAD~` before blaming any change · distrust prose counts in docs, re-measure from
generated sources.

---

## 11. Facts confirmed during the red-team pass — stop re-litigating these

`SS_OPCODE_MAX 11022` (next free extra opcode) · `SS_TRIGGER_IF_OPEN 178` · `MOCK230_TIMER_MAX 8`,
`MOCK230_PLAYER_MAX 8`, `MOCK230_CONTAINER_MAX 16` · all five `pack/*.client` files have 0 data
lines · `RSCache_Dat2FramemapEncode` takes no codec argument and `cache_write.c:564-578` re-encodes
without clearing the V3 fields (the silent no-op is real; `transcode.c:118` is the only guard) ·
the sequence codec falls to V3 for any rs2 profile · no `rs530` in `revisions.c` ·
`script_1904.cs2` is the sole failure among the six skill-tab scripts (`unknown command '_1703'`) ·
`staticons2_14..17` (sprite ids 229–232) are 25×25 blanks · `MOCK230_VARP_SERVER_HEADROOM = 1024`
→ ceiling 6729 against a high-water of 6225, so **the varp-ceiling risk quoted in some agent docs
is stale** · `mock230_bank.c:127-152 load_inv_sizes` reads inv sizes from the **cache on disk**, so
no `.inv` walker is needed *provided the server boots the baked cache* — which nothing currently
guarantees.

---

## 12. Verification

```sh
# build (never CMake; set PLATFORM_OBJ_BASE if another agent shares the repo)
make -C src

# content, flag OFF — must be green before and after every summoning commit
make -C src test-content-register test-servercodec test-ss-symbols \
            mock230-scripts mock230-servpack test-membership \
            mock230-pack test-server-clean test-port

# cache fidelity (read 3rd/rscache/EXCEPTIONS.md FIRST)
make -C src test-cachepack-fidelity

# the flag-on bake — MUST print "compiled N, failed 0"; a CS2 failure is near-silent
# headless proof, per slice, left permanent
MOCK230_SAVES=$(mktemp -d) ...
```

**Effort:** Phases 0–3 ≈ **31–49 days**; whole port ≈ **72–129 days**, one engineer, with pets,
wilderness forms and npc↔npc combat already descoped.
