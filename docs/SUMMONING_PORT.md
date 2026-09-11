# Summoning port — plan and working state

**Source:** `2009scape` (RS2 rev 530, Jan 2009) · **Target:** this repo + `OSRS-Content/osrs239-content` (OldSchool rev 239)

**Status:** Phases 0–4 and all 77 bounded Phase-5 familiar/pouch cohorts are implemented. The
completion lanes below supersede the former Phase-7 holdbacks for Wolf Whistle, safe audio, and
the separately-lifecycled pets.

### Completion authority — 2026-08-10

The user explicitly authorized completing this port, including whatever bounded content is needed
to remove the outstanding failure states. That authorizes new target-native quest interactions,
the source-backed audio closure, and pet records/lifecycle work. It **does not** authorize a
blanket promotion of the preserved `summoning_roster_530` experiment: every admitted record still
needs its own ledger, source closure, feature-on stage inclusion, and fresh-save real-client proof.
Unsafe synths remain withheld unless their source payload is transcoded and verified against the
target codec.

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
| [`summoning_port/AGENT_RECON.md`](summoning_port/AGENT_RECON.md) | 12 recon reports — 2009scape summoning logic and data, the rev-530 cache, the 530 client side; and on our side the content tree, cachepack, skills, CS2 toolchain, feature flags, ToriRSServer, IF3 authoring, port process |
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

⚠ **A 727 CS2 decompile is not source.** Treat 727 as a separate, unverified opcode dialect:
first preserve a raw instruction/operand and stack-effect disassembly, then decompile with an
explicit 727 dialect, and only then decide whether its logic is relevant. Any accepted logic is
rewritten as fresh osrs239 CS2; 727 output is never pasted or compiled as osrs239 source.

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

Planning initially reversed fields in the osrs239 NPC_INFO v5 add-block. Each add carries a
**16-bit per-client NPC index** (with `0xffff` as the terminator), followed by a **14-bit initial
NPC definition**. The 14-bit field is not a nearby-instance slot. For definition ids
16384..65535, the add's extended/update flag is set and update-mask `0x1` supplies a replacement
definition in the same packet as a transformed unsigned 16-bit `p2Alt3` / `UShortLEAdd` value.
Thus type 20000 is valid; the extended-definition path, not the direct 14-bit initial field,
governs it. Do not scope, tier, or budget this port around the initial-field width.

Consequences for this lane:

- Port all 82 familiars. No tiering for id reasons.
- Wilderness combat twins (`id+1`) remain descoped for behavioural reasons (no wilderness in this
  tree). Pets are now an explicit completion lane with a separate lifecycle; they must never reuse
  familiar ownership/timers.
- `content_register.c` npc `server_base = 20000` is valid. Allocation and collision checks remain
  normal content-lane concerns; they are not wire-width budgeting.

The same distinction is now stated in the queue, red-team review, recon/design reports,
`ITEM_AND_NPCS.md`, `osrs230_mockserver.md`, `MULTI_GENERATIONAL_PARITY.md`, the live-server
handoff and `PORTING_GUIDE.md`; F3 is not the only record of it.

### F4 — The cross-revision asset importer does not exist. This is the project.

**[measured]** all 61,615 models in `osrs239-content/models/` are `FF FD` (V3, 34,625) or `FF FE`
(V2, 26,990). **Zero OB2, zero OB3.** The 530 cache is 39,694 OB3 + 5,778 OB2 — a **disjoint
format set**. Models cannot be copied; they must be decoded and re-encoded, and **there is no
existence proof anywhere in this tree that the client renders either source format.**

Five confirmed pre-existing bugs sit in the path:

1. **Framemap V3→V1 downgrade is a silent no-op.** `tools/common/cache_write.c:564-578` re-encodes
   without clearing `has_transform_actor`/`has_masks`/`tail`, and `RSCache_Dat2FramemapEncode`
   takes **no codec argument**. Left unfixed, every ported familiar animates in bind pose, no error.
2. **The sequence codec is wrong for the entire RS2 branch.** `dat2_config_sequence.c:1139-1155`
   calls `RSCache_RevisionAtLeastOsrs(..., default_when_unknown=true)`; an rs2 profile can never
   satisfy an OSRS threshold, so every RS2 cache falls through to V3 — 649 bad seqs at 530, 2,977
   at `void634`, 3,139 at `rs727_preeoc`, 0 at osrs239.
3. **`cachepack` cannot read RS2 sharded config layouts** (`cp_common.c:58` refuses outright).
4. **No `rs530` profile exists.**
5. **Material flattening must preserve OB3 render types.** OB2/V2 use the face-info texture bit,
   but OB3/V3 store texture assignment separately and use render types 2/3 for hidden helper
   geometry. Clearing the render type while dropping a rev-530 procedural material exposes black
   triangles; a textured type 3 must become untextured type 2 because untextured type 3 means flat
   black. The importer branches on model provenance and post-import verification compares the
   hidden-face count. This affected Spirit terrorbird model 31096 and model 31211.

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
   The visual order is Construction / Hunter / Summoning across row 8, then Sailing at the left
   of row 9 with Total level spanning the other two cells. This is the rev-530 arrangement with
   Sailing retained, not a centred lone Summoning cell. A dedicated clientscript owns the new
   Summoning cell; its icon nudge is the measured rev-530 `x=5` (`int3=2`) placement.
2. **Full roster — all 82 familiars.** No id budgeting: NPC_INFO directly sends a 14-bit initial
   definition, but its same-packet extended/update path replaces high definitions with the
   16-bit mask-`0x1` value (F3).
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

⚠ **The three flag layers are independent knobs with no mechanism binding them.** `TORIRSSERVER_SCRIPTS`
(env) and `[cache:boot] dir=` (manifest) are unrelated, and `TORIRSSERVER_CACHE_DIR_DEFAULT` is
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

⚠ A directory name is invisible to `sscompile`, `cachepack` and `ToriRSServer` — all three walk
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
- [x] **No `CLAUDE.md` prerequisite.** The user explicitly rejected restoring an agent-specific
      file. The four stale citations were deleted; binding process rules remain in
      `PORTING_GUIDE.md`, the queue documents and `.cursor/rules/no-park-sibling-content.mdc`.
- [x] `docs/SUMMONING_PORT_QUEUE.md` — the per-slice loop doc, in the house queue format
- [x] `port/summoning_530.map` ledger + `tools/port_summoning_ids.py --check`, wired into
      `make -C src test-port`
- [x] **Byte-identity harness** — prove the flag-off bake is unchanged. `stage_summoning_overlay.py`
      does not exist; this is new work and the whole isolation claim rests on it
- [x] `check_summoning_isolation.py` — **must print and assert a non-zero check count.** A skip
      that reads as a pass is the #1 risk (§8)
- [x] **Spike the membership add-path** on a throwaway obj before designing on top. All five
      `pack/*.client` files have **zero data lines**; `PACK_ENTITY_SPLIT_PLAN.md` §11.1 says step 4
      "author" is unexercised. This port is that mechanism's first consumer. Budget a full day
- [x] Characterise `IF_OPENSUB` on a cache-absent group — graceful logged skip; script continues

---

## 6. Phase 1 — "Summoning is a skill" · ~5–8 d · zero assets, zero 530 cache reads

The slice none of the designs proposed and the right first move: it settles the stat decision,
exercises the overlay and membership mechanisms on trivial records, proves the CS2 compile gate,
proves byte-identity, and produces a screenshot — all before a single model is transcoded.

- [x] `pack/stat.pack` += `23=sailing`, `24=summoning`
- [x] `TORIRSSERVER_STAT_COUNT` 23 → 25 (`src/torirsserver/torirs_server.h:558`; 40 call sites, all bounds or
      array sizes; `stat_dirty` is `uint32_t`, 25 bits fits; `torirs_server_save.c` is id-keyed ini so
      saves stay forward/backward compatible)
- [x] Cache overlay: `enum_681` `val=25,24` (**keys must stay contiguous** — a hole silently
      truncates the roster loop and drops every later skill from Total level), plus `enum_680`,
      `enum_108`, `enum_255`, `enum_5917`, `enum_1497`, `enum_1505`
- [x] `interfaces/stats.compack` `34=summoning_stats_cell` (**[measured]** the file is 0..33 with `33=tooltip`)
- [x] `stats.if` — Summoning at `x=127,y=183`, Sailing at `x=1,y=209`, and Total level at
      `x=64,y=209,width=126`; dedicated script 1198 builds the Summoning cell
- [x] `script_8950.cs2` case 24 + a new `content_restrict_summoning_serverside` varbit
- [x] Exact rev-530 wolf-head skill icon: source sprite pack **222**, canvas
      `25×25, crop 22×23, offset 0,2`, SHA-256
      `89726834d13ce73b8fff38eb34567ed2e52c7757b2d8405577e801979e4178cd`; emitted through the
      independently allocated target name `summoning_staticon` at target pack id 229
- [x] Regression assertion that combat level does **not** move

**No ServerScript compiler change is needed** — `ssc_compile.c:755-771` gives `STAT*` opcodes
`base_hint = SSC_SYM_STAT`, so `stat_base(summoning)` / `stat_advance(summoning, …)` resolve the
moment `stat.pack` lands.

**Combat level cannot move** — all three implementations name stats explicitly, none loops
(`rs_player_stats.c:76-89`, `torirs_server_combat.c:913-925`,
`server/scripts/player/configs/combat_level.rs2:5-11`). **Total level moves automatically** —
`script_1007` walks `enum_681` until null, gated on `~script8950`.

⚠ **Never name a ported record exactly `summoning`.** `ssc_compile.c:2286` resolves trigger
subjects with `SSC_SYM_UNKNOWN` — any namespace, first match wins, and mis-resolution is silent.
Prefix everything `summoning_*` and add a `ToriRSServer_Pack --check-only` rule that every name in
`pack/stat.pack` is unique across all namespaces.

**Acceptance:** log in → Summoning in the skills tab at level 1 → `::setlevel summoning 20` moves
it → survives logout → total level includes it → flag off hides it → flag-off bake proven
byte-identical. Four BMPs.

**Verified:** `make -C src test-summoning-phase1 PLATFORM_OBJ_BASE=build_summoning` performs 36
non-skipped checks and leaves the four BMPs plus logs in `build/summoning-phase1/`. The flag-on
totals are 34 at level 1 and 53 at level 20; the second login restores `24 = 20 44700`; the
pristine flag-off cache has no stats component 34 and remains total 33. The checks pin the exact
wolf pixels and metadata, its rendered `x=639,y=388` client position, and Total level beside
Sailing on the final row. The full mock selftest is
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
- [x] `cachepack import` — manifest, closure walk, id remap, tree writer, ledger (~1,600 LOC)
- [x] `anim_compare --b-cache/--b-rev/--b-seq/--b-model` (~200 LOC)
- [x] `pack/{obj,seq,spotanim}.client` + `content.ini` membership blocks

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

- `npc_run_mode` (`torirs_server_world.c:2752`) resolves the followed player as `srv->active_player`,
  which `phase_npcs` never sets — the code admits it at `:2773`. With two summoners, both familiars
  follow the same arbitrary player.
- `run_trigger_script` (`torirs_server_scripts.c:1666`) sets `SSVM_ENT_PLAYER` from the same stale
  pointer for **every** trigger including `ai_*`.
- `npc_uid` has no generation counter (`torirs_server_scripts.c:4585` says so) — a stashed uid resolves to
  whoever took the slot.

- [x] `struct ToriRSServerNpc` gains `int owner_pid; uint32_t owner_gen;` (zeroed by `npc_spawn`, so an
      unowned npc is unchanged)
- [x] `npc_run_mode` resolves from `owner_pid` when set, else today's behaviour — **narrowing, not
      branching**
- [x] `run_trigger_script` prefers `npc->owner_pid` for `ai_*` triggers on owned npcs
- [x] Opcodes `NPC_SETOWNER` / `NPC_OWNER` / `NPC_FINDOWNED` in the extra band — **next free is
      11022** (`ss_opcode.h:453`). `NPC_FINDOWNED` removes the need for a uid varp and so dodges
      the uid-generation hazard rather than working around it. Log them in the queue's opcode-gap
      table and implement in the same slice (`PORTING_GUIDE` §2.4/§4.5)
- [x] npc `server_base` (`content_register.c:63`, 20000) is retained; an NPC_INFO add uses a
      16-bit per-client index and 14-bit initial definition, then type 20000 takes the
      extended/update + mask-`0x1` transformed-16-bit replacement path (F3)

### Content

- [x] Port npc 6829 assets (model, framemap, seqs) through the Phase 2 pipeline
- [x] Objs: `summoning_pouch_spirit_wolf`, `summoning_shard`, `summoning_charm_gold` — with
      `pack/obj.client` created
- [x] `[opheld1,…]` → level check → `npc_add` → `npc_setowner` → `npc_setmode(playerfollow)`
- [x] **One** `[timer,summoning_tick]` at interval 1 carrying decay, point drain, special regen and
      both warnings. `TORIRSSERVER_TIMER_MAX = 8` per player and existing content already competes
- [x] Dismiss, logout, death, call-familiar. Verified with a visible type-20000 Spirit wolf,
      call, persisted relog reconstruction, explicit dismiss and real timer-expiry paths

⚠ Ported npcs must set **explicit `walkanim=`/`readyanim=`**, not `bas_type_id`. `bas` is not a
cachepack type, so every rev-530 familiar (which uses `bas_type_id` with `standing_anim = -1`)
would T-pose. `fields/npc.ini` declares both as `scope = client`, so this costs one line per npc
and removes a whole tool dependency.

**Points model:** summoning points are `stat(24)` dynamic vs `stat_base(24)` max — exactly
2009scape's model, riding `UPDATE_STAT` for free with zero new wire. ToriRSServer has no boosted-level
restore tick, so points correctly do not regenerate, for free. Leave a comment so a future restore
tick excludes stat 24 alongside prayer.

**Demo:** log in, `::setlevel summoning 20`, click the pouch, a wolf appears and follows you to
Varrock, log out and back in, it is still there, the timer expires, it leaves.

---

## 9. Current and later phases

**Phase 4 — skill surfaces (~12–18 d).** Skill guide (dbtable 212/213). The former `dbindex`
gap is closed: `tools/gen_dbindex.py` deterministically regenerates all 147 table indexes, and its
mutation test proves an omitted or misordered key fails before byte-exact repair. The Summoning
rows live in the marked lane and the real client acceptance right-clicks the Summoning stats cell,
sends op2, opens `skill_guide_v2`, renders the live Spirit wolf row through `db_find`, and sends its
pouch through the object/model renderer. Note `script_9176.cs2b` is **bytecode-only**, so the
guide's Overview tab remains unmodified. The Summoning-points orb is now live in interface 160:
it draws the cache's OWN orb art rather than a rev-530 import — target 20001/20027 are sprites
1071/1072 (`orb_frame,0` and its hovered twin) mirrored left-to-right, 20002 is the special-attack
gauge 1607 hue-rotated -24 degrees into Summoning's teal-green, and 20003 is `orb_filler,0`
unchanged (`tools/make_summoning_orb_sprites.py` derives all four). Only target 20000, the wolf
head, is still the rev-530 import, reboxed from 20x20 into the 26x26 box every osrs239 orb icon is
centred in. The chrome is mirrored because this orb hangs off the minimap's bottom-RIGHT arc while
the other four curve down the bottom-left: unmirrored, its gauge would face away from the map and
its number panel would run off the edge. Authored clientscript 12004 is the default and reshapes
those pieces into the modern 57x34 orb layout, mirrored through the plate — the 26px gauge, unlit
disc and icon share one box at 4,4 where a native orb's share one at 27,4, and the number panel
sits at 30,16 where a native orb's sits at 4,16. It is visible only
while a familiar is active and displays the server-owned 0..60 special-move points. The active and
special varps transmit directly and drive the orb's `onvartransmit` hook. The source-era
clientscript 12000 remains packed as the legacy stat-points alternative. The originally
proposed `(54,158)` position is behind the fixed client's tab strip; real-client measurement moved
it to visible `(89,128)`, immediately right of the special-attack orb. Its real op1 packet calls
the active familiar. Summoning access now stays inside Worn Equipment (group 387): clientscript
12001 configures the cache-native top-right Call-follower button with the exact rev-530 sprite-222
wolf head (target graphic 229), and its real op1 mounts compact group 969 into
`wornitems:universe`. Group 969 is 190x205, reuses its native 140x28 Call/Dismiss chrome, has a
top-right Back-to-equipment button, and composes the active familiar's packed chathead rather than
a raw body or fallback model. No component is added to top-level groups 161/548/164. Real-client
acceptance asserts the final model draw id (`0x50004e20`), live points text, compact bounds, and
real Call/Dismiss packets. The obelisk is now installed by feature-gated, idempotent runtime `loc_add`, which
sidesteps `maps/` entirely. The rev239 `LOC_ADD_CHANGE_V2` measurement is settled: its loc config
id is `p2Alt3`, exactly 16 bits, so the generic loc `server_base = 70000` cannot be used on this
wire (70000 would truncate to 4464). The imported source loc 28716 is therefore mapped to the
first free target id, 62201. This is deliberately different from NPC_INFO: it has a 16-bit
per-client NPC index, a 14-bit initial definition, and a same-packet extended/update + mask-`0x1`
transformed-16-bit replacement for high definitions such as type 20000. Real-client acceptance
picks loc 62201, selects its actual second menu option,
restores points 0/1→1/1, decodes player sequence 20003 and spotanim 20000, combines the imported
effect model, and verifies the visible green effect in the framebuffer. The infusion UI is a fresh IF3 group
970 in target vocabulary, mounted through the `mainmodal` role alias; it does not transcode rev-530
interface 669 or its foreign clientscript. Its Spirit wolf row exposes ordinary target operations for
1/5/10/X/All. Obelisk op1 records the actual interacted `loc_coord`; the button handler re-finds that loc
before it plays source sequence 9068 as the player animation, imported source sequence 8509 as the charge
animation, and the existing 8510 idle reset. It consumes a gold charm, imported blank pouch (source 12155),
wolf bones, and seven shards into a Spirit wolf pouch plus 4.8 XP (48 target tenths). The unsafe source craft
synth 4164 is intentionally deferred. `test-summoning-phase4f` runs two fresh-save real-client cases: one
retains the rendered panel/framebuffer, and one clicks the live loc then real `IF_BUTTON1` row, asserts the
player and active→idle loc animations, and parses the saved inventory/stat result.

**Phase 5 — breadth.** 82 familiars · 67 scroll objs · special moves · the ~60 tertiary ingredients
(mostly a 530→239 obj **name-resolution** pass — most already exist under different ids; this is
what the `port/` ledger is for) · summoning potions (12140/12142/12144/12146 + mixes) · charm drops
(1,222 rev-530 npc ids to translate; expect heavy attrition). ⚠ 2009scape has **no per-familiar
summon-sound table** (`Familiar.java:713` is a TODO) — one shared summon sound is a content
*invention* and must be recorded as such. Only sound 188 is safe to byte-copy; 4161/4164/4214/4265/
4372 are above the 3826 divergence point.

**Phase 5a — roster boundary and provenance audit (done).**
[`pouches_530.json`](summoning_port/pouches_530.json) records 82 source pouches: 78 active
familiar/pouch pairs and four Sacred Clay pairs that remain explicitly deferred. The separate
[`roster_boundary_530.json`](summoning_port/roster_boundary_530.json) originally allowed only the
existing Spirit-wolf proof roots; its one later, separately owned Phase-5b admission is Dreadfowl.
The boundary now admits the separately-owned clean familiar/pouch cohorts listed in the queue. It
still permits only the documented safe source synth 188 as policy; it does not turn an unreviewed
import into accepted content.

The earlier generated `summoning_roster_530` import is preserved intact as review-only evidence:
630 source files, 2,175 pack references, and 1,365 ledger rows. Its last broad CSV and INI manifests
are retained byte-for-byte in [`summoning_port/review_only/`](summoning_port/review_only/), rather
than being overwritten by the bounded candidate. It is neither deleted nor silently accepted.
Feature-on staging withholds its cohort-named assets and its line-oriented mixed pack rows, then
asserts that no review-only marker reaches the staged tree. The same audit fails closed for any
other generated cohort, pet record, unsafe synth, or `npc_sounds=yes` closure. The permanent
`test-summoning-phase5a` target performs 94 checks over that boundary, the ledger, archive hashes,
preservation counts, and staging exclusion. Current evidence: `port_summoning_ids.py --check`
reports 1,541 required rows and 1,418 total ledger rows, 0 errors; staging admission is 4,545/0,
with 3,785 review-only references held and 2,805 withheld, yielding 417 staged actual files and a
417/0 review-exclusion check; the feature cache is 16,998 records/0 errors, 187 asset archives/23
tables, CS2 6/0, ServerScript 12,963 scripts, and `ToriRSServer_Pack --check-only` 8,340/0; the staged
flag-off comparison remains 25/0. Phase 5a did not accept the broad roster; the separately owned
Phase-5b closure below does not change its review-only status.

**Phase 5 scroll-object closure (done).** All 78 active familiar mappings now resolve to their
67 distinct rev-530 scroll objects. Call to Arms, Petrifying Gaze, and Titan's Constitution retain
their source sharing, and the commented Phoenix object 14622 is explicitly included so Phoenix is
not the lone familiar without a packed scroll. `scroll_assets_530.ini` imports the 67 object
records and their 62 distinct inventory models into target ids 47400..47466 and 124000..124061;
`summoning_scrolls_530.map` records every translation. Both `obj.alloc` and `obj.client` admit all
67 records and `7_models.pack` admits every model. `test-summoning-scroll-assets` checks the 78→67
mapping, the three documented source pouch-key corrections, configs, ledger, files, and pack
membership before every feature-cache bake.
The guide's additional post-rev-530 Fetch Casket row is sourced separately from rev 727 by
`scroll_fetch_casket_727.ini` (object 19621/model 58228 → target 47467/124062), bringing the
visible Summoning Scrolls subsection to 68/68 packed icons without misattributing that object to
the familiar roster.

**Phase 5b — bounded Dreadfowl familiar/pouch cohort (done).** The first admitted breadth closure
is source NPC 6825 / pouch 12043 to target NPC 26000 / pouch 46000. Its exact closure is body,
head, and pouch models 120000/120001/120002 from source 30429/31147/30664; ready/walk sequences
23000/23001 from source 5386/7808; animation archive 1399 at target 23000; and framemap 1255 at
target 10000. The separate nine-row Dreadfowl ledger is deliberately `minted`/`unreviewed`: it is
not a human material or signoff approval. No combat, pet, scroll/special-move, or audio closure is
accepted with this cohort.

`test-summoning-phase5b` passes 202/0 while preserving the broad review-only footprint and the
normal embedded-client acceptance, `test-summoning-phase5b-runtime`, passes 110/0 using the actual
right-click Dreadfowl-pouch menu: authored `ifop4=Summon` appears as native action 2231/op 5,
serializes as dynamic `IF_BUTTONX op=6`, dispatches canonically to `OPHELD4`, consumes the pouch,
and summons Dreadfowl rather than dropping it. Fresh-save and no-cheat relog sessions render the
live type-26000 body/head and ready sequence, include normal-server `IF_SETNPCHEAD` and its
`npc_head ... applied=1` sidebar marker and title, prove the 400-tick persisted lifetime plus the
one-point summon and 100-tick drain boundary, and exercise
real sidebar Call and Dismiss. The retained logs and framebuffers are under
`build/summoning-phase5b-runtime/`.

**Phase-5 breadth status:** 77 familiar/pouch cohorts are admitted with dedicated ledgers and
disjoint target ranges. Phoenix is a nine-row non-audio closure; source synths 5776 and 5753 are
withheld because they are outside the safe audio policy, and its missing `Familiar` class keeps
gameplay out of scope.

**Client acceptance for breadth:** every familiar must be summoned in the real client with its
model and animations rendered, and every scroll must be activated through its actual client
interaction with visible/logged special-move effects. Definition presence or a successful bake
alone is not acceptance.

**Phase 6 — Beast of Burden (~8–12 d), prerequisite complete.** The shared inventory foundation
now declares `fields/inv.ini` and `[namespace:inv]`; the feature-on `summoning_bob` record bakes
as a 30-slot cache inventory and resolves through the ordinary container path. This also clears the
documented shop prerequisite. The next slice binds a single admitted familiar to that inventory,
then proves store/withdraw, dismiss spill, and logout/relog persistence in the real client.

**Phase 7 — polish.** Execute these slices in order; each client-visible slice retains its
fresh-save framebuffer and logs.

1. **7a — per-account unlock (done).** `summoning_unlocked` persists and synchronizes the
   existing `content_restrict_summoning_serverside` varbit on login; every runtime Summoning
   entry point requires that state. The familiar view now mounts only inside
   `wornitems:universe`; it does not add or resize a gameframe tab in any layout. Fresh
   locked/unlocked/relog acceptance is 42/0, and the real Spirit-terrorbird Store → relog →
   Withdraw → Dismiss-spill regression passes.
2. **7b — Wolf Whistle unlock writer (done).** The upstream completion contract is a
   persisted unlock, 276 Summoning XP, and 275 gold charms. The generic gold charm was already
   present (`12158 → 40002`); the missing closure was the quest-only reward copy
   (`12527 → 40256`), whose verified charm model is deliberately a separate target item so it
   cannot be infused. The idempotent completion writer now grants the persistent gate, 2760
   ServerScript XP units, and 275 quest-copy charms; it is idempotent and covered through relog
   in the real-client harness.
3. **7c — safe audio closure (done).** Source synth 188 maps to the already cache-native
   `summon_npc` record after a byte-identical payload check, so no new sound archive was admitted.
   The real sidebar Call action now emits SYNTH_SOUND 188 (one loop, zero delay), retained in the
   permanent client log. 4161/4164/4214/4265/4372 remain withheld until their 530 payloads are
   transcoded and decoded by the target codec.
4. **7d — first pet lifecycle.** Admit the smallest complete pet closure in a dedicated ledger.
   Persist only pet-specific type/state, spawn it independently of familiar state, and prove
   release, pickup/dismiss/death, and logout/relog in the real client.
5. **7e — Wolf Whistle interaction (done).** A target-native `Begin Wolf Whistle` third
   operation on the feature-on obelisk calls the existing idempotent writer and remains available
   while the account is locked. The permanent harness sends the real client's `OPLOC3` packet,
   retains the completion frame/log, and proves the persistent unlock through relog.
6. **7f — completion audit.** For every remaining withheld audio or pet candidate, record either
   an admitted closure with interaction proof or an explicit codec/asset reason it cannot ship.
   The port is complete only when no queue row is left pending or in progress.

---

## 10. Risk register

| # | risk | mitigation |
|---|---|---|
| 1 | **Texture map errors are undetectable by automation** — a wrong material renders plausibly and every verification tier passes | import untextured; per-model human render signoff in the ledger |
| 2 | **A skipped suite reads as a pass.** A pristine worktree skips whole suites silently | every summoning target asserts a non-zero check count; grep runs for `SKIP` and fail |
| 3 | **Silent CS2 decline** — `cp_decode.c:2447-2452` ships base-cache bytes and only a counter says so; **95 of 9,368** committed `.cs2` already fail to recompile, **including `script_1904.cs2`**, the skill-guide builder | standalone `cs2 compile` gate requiring `failed 0` before every bake. Refresh stale sources **selectively** — a blanket `--assets=scripts` unpack overwrites hand-authored comments in `script_73.cs2`/`script_7304.cs2` and trips `check_crystal_set_contract.py`, a hard prerequisite of `torirsserver-cache`. `RUNESTAR_CS2_NAMES` is an undeclared hard dependency |
| 4 | ~~14-bit direct NPC-definition ceiling~~ — **not a risk.** High definitions use the add's extended/update flag plus mask `0x1` 16-bit replacement in the same packet (F3) | keep the direct/extended-definition regression; do not scope this port around the initial-field width |
| 5 | **The membership add-path has never been run** — all five `pack/*.client` files have zero data lines | spike on a throwaway obj in Phase 0 before designing on top |
| 6 | **Chained-overlay byte-identity is unproven** and the staging script does not exist | Phase 0 builds it; fallback is the third walk root (`CP_WALK_MAX_ROOTS = 4`) |
| 7 | **Framemap/sequence codec fixes touch the whole RS2 branch** | A/B 634 and 727 before and after |
| 8 | **727 CS2 may use a different opcode dialect** | preserve raw bytecode/instruction+stack disassembly first; decompile only with an explicit 727 dialect; translate accepted logic into newly authored osrs239 CS2 |

**Standing hazards.** Never `git stash` here (a no-op push turns `pop` into restoring an old
stash) · no ASAN on this Mac · `TORIRSSERVER_SAVES=$(mktemp -d)` on **every** headless run, baked into
the make recipe rather than left to the operator · never bare `pkill -f build/torirsserver` (it eats
`ToriRSServer_Dev`) · `embed_test` decode is broken pre-existing, nothing decodes past login — A/B
against `HEAD~` before blaming any change · distrust prose counts in docs, re-measure from
generated sources.

---

## 11. Facts confirmed during the red-team pass — stop re-litigating these

`SS_OPCODE_MAX 11022` (next free extra opcode) · `SS_TRIGGER_IF_OPEN 178` · `TORIRSSERVER_TIMER_MAX 8`,
`TORIRSSERVER_PLAYER_MAX 8`, `TORIRSSERVER_CONTAINER_MAX 16` · all five `pack/*.client` files have 0 data
lines · `RSCache_Dat2FramemapEncode` takes no codec argument and `cache_write.c:564-578` re-encodes
without clearing the V3 fields (the silent no-op is real; `transcode.c:118` is the only guard) ·
the sequence codec falls to V3 for any rs2 profile · no `rs530` in `revisions.c` ·
`script_1904.cs2` is the sole failure among the six skill-tab scripts (`unknown command '_1703'`) ·
`staticons2_14..17` (sprite ids 229–232) are 25×25 blanks · `TORIRSSERVER_VARP_SERVER_HEADROOM = 1024`
→ ceiling 6729 against a high-water of 6225, so **the varp-ceiling risk quoted in some agent docs
is stale** · `torirs_server_bank.c:127-152 load_inv_sizes` reads inv sizes from the **cache on disk**, so
no `.inv` walker is needed *provided the server boots the baked cache* — which nothing currently
guarantees.

---

## 12. Verification

```sh
# build (never CMake; set PLATFORM_OBJ_BASE if another agent shares the repo)
make -C src

# content, flag OFF — must be green before and after every summoning commit
make -C src test-content-register test-servercodec test-ss-symbols \
            torirsserver-scripts torirsserver-servpack test-membership \
            torirsserver-pack test-server-clean test-port

# cache fidelity (read 3rd/rscache/EXCEPTIONS.md FIRST)
make -C src test-cachepack-fidelity

# the flag-on bake — MUST print "compiled N, failed 0"; a CS2 failure is near-silent
# headless proof, per slice, left permanent
TORIRSSERVER_SAVES=$(mktemp -d) ...
```

**Effort:** Phases 0–3 ≈ **31–49 days**; whole port ≈ **72–129 days**, one engineer, with pets,
wilderness forms and npc↔npc combat already descoped.
