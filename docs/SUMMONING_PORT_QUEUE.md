# Summoning port queue

Agent-loop state for the **2009scape → OSRS-Content** port of **Summoning**, a rev-530 (Jan 2009)
RS2 skill that OldSchool never had.

**Plan and findings: [`SUMMONING_PORT.md`](SUMMONING_PORT.md).** Read it before taking a slice —
especially §1 (the four premise-changing findings) and §11 (facts already confirmed; do not
re-measure them). The raw agent research is in [`summoning_port/`](summoning_port/), and
[`summoning_port/AGENT_REDTEAM.md`](summoning_port/AGENT_REDTEAM.md) is the tiebreaker whenever the
design docs disagree.

Parallel to [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) (which no longer
skip-lists Summoning) and [`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md) (whose
23/23 audit deliberately excludes it).

Each tick ports **one** pending unblocked slice per [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §4 and
§4.5. Status: `pending` | `in_progress` | `done` | `blocked`.

---

## This lane is different from every other port queue

1. **Summoning is not an OldSchool skill.** It is not claimed to be authentic osrs239 content. It
   lives in a marked lane (`ported/scape2009_summoning/`, `server/scripts/ported_scape2009_summoning/`)
   and is gated behind a feature flag. Do not "fix" it toward OSRS authenticity.
2. **It reverses written policy.** Five skip-list lines were amended to legalise it
   (`PORTING_GUIDE` §35 and §683, `SCAPE2009` skip list ×2, `SKILLS` skip list). **Do not re-add
   them.** If you find a doc that still skip-lists Summoning, amend the doc.
3. **The target cache contains none of it.** Unlike every other lane, there are no cache records to
   port *against* — models, animations, sounds, sprites and configs must be **transcoded from a
   foreign revision** (rev 530 → 239, disjoint model formats). That tooling does not exist yet; see
   `SUMMONING_PORT.md` §7.
4. **Summoning is stat 24, not 23.** 2009scape's `Skills.SUMMONING = 23` collides with Sailing,
   which is live in osrs239. Never copy the 530 stat id.
5. **npc ids are not a constraint.** NPC_INFO's 14-bit value is the per-player client-local slot
   of a nearby NPC instance, not its cache/config id. Rev239 carries the separate type id in 16
   bits. Port the full roster; do not tier or budget around client-local slots. See
   [`SUMMONING_PORT.md`](SUMMONING_PORT.md) §1 F3.

## Non-negotiables

- **No game-facing strings / ids / config constants in C.** 2009scape Java/Kotlin is a *reference*.
  Express as `.rs2` + configs. New Server VM opcodes only when content cannot say it
  (`PORTING_GUIDE` §2.4/§2.5) — plan **and** implement in the same slice, logged in the opcode
  table below.
- **Resolve names through the pack.** Never copy rev-530 ids. Every translation gets a row in
  `port/summoning_530.map`.
- **Never name a ported record exactly `summoning`** — `ssc_compile.c:2286` resolves trigger
  subjects with `SSC_SYM_UNKNOWN`, first match wins, and mis-resolution is silent. Prefix
  everything `summoning_*`.
- **Never park sibling content** to green a compile — no `*.skip`, no moving live trees aside.
  Fix your own slice (`PORTING_GUIDE` §7).
- **A skip is not a pass.** Every summoning target must assert a non-zero check count.
- **727 CS2 is foreign bytecode, not osrs239 source.** Preserve a raw instruction/operand plus
  stack-effect disassembly first, decompile with an explicit 727 dialect second, and only then
  decide whether to translate the logic into freshly authored osrs239 CS2.
- **The real client is the acceptance authority.** Every client-visible slice must boot with a
  fresh `MOCK230_SAVES=$(mktemp -d)`, exercise the actual interaction path, and retain a rendered
  framebuffer plus logs. Required end-to-end proofs include familiar models/animations,
  skill-guide opening and live rows, orb/sidebar/infusion interactions, and every scroll special.
  Pack/compiler/structural checks are prerequisites, never substitutes.

---

## Opcode gap table

New Server VM opcodes this lane adds. Extra band, next free **11022** (`ss_opcode.h:453`).

| opcode | id | signature | semantics | slice | status |
|---|---:|---|---|---|---|
| `NPC_SETOWNER` | 11022 | `()` | bind the active npc to the active player (pid + login generation) | 3a | done |
| `NPC_OWNER` | 11023 | `()(int)` | owner pid, `-1` if unowned | 3a | done |
| `NPC_FINDOWNED` | 11024 | `()(boolean)` | find the active player's owned npc, set it active | 3a | done |

---

## Slices

| # | slice | phase | status | notes |
|---|---|---|---|---|
| 0a | Amend the five skip-list lines | 0 | **done** | `PORTING_GUIDE` ×2, `SCAPE2009` ×2, `SKILLS` ×2 |
| 0b | Remove obsolete `CLAUDE.md` citations | 0 | **done** | user explicitly rejected restoring an agent-specific file; four stale citations removed; binding rules remain in `PORTING_GUIDE`, queue docs and the no-park cursor rule |
| 0c | `port/summoning_530.map` + `tools/port_summoning_ids.py --check` + `test-port` row | 0 | **done** | 164 manifest roots checked, 0 errors; zero-row mutation fails. Aggregate `test-port` is pre-blocked by its existing 1,775 unsigned LostCity name-diff rows before this row runs |
| 0d | Flag-off byte-identity harness (`stage_summoning_overlay.py`) | 0 | **done** | stages 30 support files with zero base records/assets; cache compare asserts non-zero files and rejects a byte mutation with SHA-256 evidence |
| 0e | `check_summoning_isolation.py` (asserts non-zero check count) | 0 | **done** | 304,368 checks, 0 errors; injected `[summoning_leak]` in a flag-off client root fails |
| 0f | Spike the pack membership add-path on a throwaway obj | 0 | **done** | temp id 40000: membership `1 client`, 0 disagreements; obj-only bake wrote 1 record/19 bytes, 0 failed/unknown/unresolved; temp tree/cache only |
| 0g | Characterise `IF_OPENSUB` on a cache-absent group | 0 | **done** | headless pristine cache, temp group 969: logged missing pack + skipped mount; next script message rendered; BMP written; normal exit, no hang/crash |
| 1a | Stat 24 end to end — `stat.pack`, `MOCK230_STAT_COUNT`, 7 enums, corrected `stats.if` 3×9, `script_8950` case 24, exact 530 wolf icon | 1 | **done** | 36-check permanent headless target; exact rev-530 sprite 222 pixels render under target allocation 229; Construction/Hunter/Summoning are contiguous, Sailing + Total share the last row; four BMPs; totals 34→53; stat 24 persists at 20; flag-off has no cell 34 and 25 cache files are byte-identical; CS2 3/0; isolation 642,936/0; pristine `mock230_pack` 0 errors. Full mock suite is pre-blocked only by unrelated concurrent content work |
| 2a | `rev_dat2_rs530.c` profile + `revisions.c` rows | 2 | **done** | aliases `530`/`rs530`; explicit FRAMEMAP_V3, deliberately derived FRAME_V1; profile suite 134 checks; real cache npc 6829 resolves seed seqs 8297/8291 and framemap 1491 (codec noise is the next slice) |
| 2b | `SEQUENCE_RS2_530` + `OBJ_RS2_530` codecs | 2 | **done** | exact 530 sweeps: obj 14,654/14,654, seq 11,155/11,155; synthetic changed-opcode suite 17 checks; profile suite 140 checks; clean full rscache suite; true `HEAD` A/B identical for seq 1/100/5000 on both 634 and 727 |
| 2c | `RSCache_Dat2FramemapEncodeCodec` — fixes a silent data bug | 2 | **done** | regression first failed to compile against missing API; now V3→V1/V2 and V3 preservation pass 8 checks; cache writer selects destination codec; roundtrip suite 246 checks |
| 2d | Sharded RS2 config reader (`cp_common.c:58`) | 2 | **done** | real rs530 `cachepack unpack`: obj 14,654 + seq 11,155, first/last ids present, 0 short decodes/unresolved names; OSRS cachepack fidelity unchanged with lost-here=0 |
| 2e | `cachepack import` subcommand | 2 | **done** | dry-run/apply/idempotence; Spirit wolf closure 1 npc, 3 objs (pouch/shards/gold charm), 5 models, 2 seqs, 1 animset, 1 framemap; 52-check permanent test; feature bake config 14/0 and CS2 3/0; exact config verify; 18-frame source/destination visual sheet; flag-off 25-file A/B identical; `mock230_pack --check-only` 0 errors; Phase 1 headless rerun 28/0 |
| 2f | Texture map 680→210 + ledger `signoff` column | 2 | **blocked** | importer implements the settled drop-textures policy and preserves human ledger columns; the 680→210 mapping/signoff is deliberately irreducibly human and is not a dependency of the untextured Spirit wolf slice |
| 3a | Owner-bound NPCs — `owner_pid`/`owner_gen`, `npc_run_mode`, `ai_*` dispatch, 3 opcodes | 3 | **done** | pid + nonzero login generation fails closed on slot reuse; two-player mode and real-VM ai_timer context checks; all three opcodes executed through VM; generated metadata/coverage current; rev230 world suite + 68-check metadata suite green. Rev239 full world suite remains pre-blocked by the concurrent 23,139-NPC/capture work (198 unrelated failures); ownership assertions themselves pass there |
| 3b | npc `server_base` (`content_register.c:63`) alignment | 3 | **done** | retain base 20000. Fixed the reversed NPC_INFO model: nearby instance slot remains 14-bit/client-local; separate rev239 cache type is 16-bit. Writer/reader regression round-trips slot 321 with type 20000 |
| 3c | Spirit wolf: assets, objs, `.rs2` triggers, timer, dismiss, headless proof | 3 | **done** | paired off/on ServerScript packs; every entry point gated; visible type-20000 wolf in the real headless client; exactly one spawn on summon and persisted relog; call, dismiss and real timer-expiry paths; 40/0 slice checks, rev239 slot-321/type-20000 codec regression, CS2 3/0, server pack and `mock230_pack --check-only` 0 errors, flag-off no spawn/message |
| 4a | Deterministic dbindex generator + byte-identical regeneration tests | 4 | **done** | `gen_dbindex.py --check` verifies all 147 indexes; mutation regression is 156/0 and proves omitted/misordered keys fail before byte-exact repair; wired into `test-port`; isolation 642639/0, flag-off 25/0, pack 0 errors, fresh-save headless 28/0 |
| 4b | Summoning skill-guide rows, events and live client `db_find` proof | 4 | **done** | corrected wolf-cell context menu/op2 opens `skill_guide_v2`; dbtables 212/213 render all six tabs plus the full Spirit wolf recipe, and obj 40000 enters the object/model renderer; fresh-save client acceptance 19/0 with retained BMP/log; feature bake is 16,978 records, 0 failed/unresolved, CS2 3/0, 147 indexes; all-content ServerScript compile emits 12,774 scripts; `mock230_pack --check-only` 0 errors; Overview remains bytecode-only and unmodified |
| 4c | Summoning points orb | 4 | **done** | exact rev-530 interface-747 art (sprites 1200/1206/1244/1245) remapped to target 20000..20003; visible target position `x=89,y=128` avoids the fixed-client tab strip that hid the proposed `54,158`; authored script 12000 updates live on stat-24 transmit; real click sends interface-160 component-64 op1 and calls the active wolf; 19/0 fresh-save client checks, CS2 4/0, ServerScript 12,781 compiled, isolation 644,151/0, pack 0 errors |
| 4d | Summoning sidebar access | 4 | **done** | exact rev-530 sprite-222 wolf icon at target graphic 229; tab 14 mounted in 161/548/164 without displacing Sailing; authored osrs239 script 12001 reflows Classic/Fixed and shifts Modern's movable row; group 969 uses script 12002 `if_setnpchead(npc_20000)` with no fallback model; final real-client command is `kind=5 model=1342197280`; Call/Dismiss use real IF_BUTTON1; 44/0 fresh-save client checks; CS2 6/0, feature bake 16,986 records and 171 asset archives with 0 failures/unresolved names. Shared `mock230-servpack` is currently blocked only by concurrent QBD/TD membership and animation-name errors |
| 4e | Loc-id wire proof + runtime obelisk and Renew-points | 4 | **pending** | no maps; prove loc 70000 before `loc_add` |
| 4f | Authored infusion interface and pouch production | 4 | **pending** | click/render/make a pouch in the real client; use osrs239 vocabulary; do not transcode interface 669 |

Phases 5–7 (breadth, Beast of Burden, polish) are scoped in
[`SUMMONING_PORT.md`](SUMMONING_PORT.md) §9 and will be seeded as Phase 4 completes.

---

## Loop prompt

Read [`SUMMONING_PORT.md`](SUMMONING_PORT.md) + `PORTING_GUIDE` §4/§4.5/§7; take the next pending
unblocked slice; never park sibling lanes; verify (`mock230_pack --check-only`,
`make -C src mock230-scripts`, and the flag-off byte-identity check); update this file and the
budget/opcode tables; re-arm. Stop only when the user stops the loop.
