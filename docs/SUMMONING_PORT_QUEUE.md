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
5. **npc ids are not a constraint.** Separate work is in place to remove the npc id cap. Port the
   full 82-familiar roster; do not tier, budget or scope around ids, and do not re-derive a ceiling
   from the current NPC_INFO v5 reader — it is being changed. See
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

---

## Opcode gap table

New Server VM opcodes this lane adds. Extra band, next free **11022** (`ss_opcode.h:453`).

| opcode | id | signature | semantics | slice | status |
|---|---:|---|---|---|---|
| `NPC_SETOWNER` | 11022 | `()` | bind the active npc to the active player (pid + login generation) | 3a | pending |
| `NPC_OWNER` | 11023 | `()(int)` | owner pid, `-1` if unowned | 3a | pending |
| `NPC_FINDOWNED` | 11024 | `()(boolean)` | find the active player's owned npc, set it active | 3a | pending |

---

## Slices

| # | slice | phase | status | notes |
|---|---|---|---|---|
| 0a | Amend the five skip-list lines | 0 | **done** | `PORTING_GUIDE` ×2, `SCAPE2009` ×2, `SKILLS` ×2 |
| 0b | Resolve the `CLAUDE.md` citation gap | 0 | **blocked** | absent since `5cdb9c14`, looks accidental; 4 docs cite it as binding. Restoring changes agent behaviour repo-wide — needs a user decision |
| 0c | `port/summoning_530.map` + `tools/port_summoning_ids.py --check` + `test-port` row | 0 | **done** | 164 manifest roots checked, 0 errors; zero-row mutation fails. Aggregate `test-port` is pre-blocked by its existing 1,775 unsigned LostCity name-diff rows before this row runs |
| 0d | Flag-off byte-identity harness (`stage_summoning_overlay.py`) | 0 | **done** | stages 30 support files with zero base records/assets; cache compare asserts non-zero files and rejects a byte mutation with SHA-256 evidence |
| 0e | `check_summoning_isolation.py` (asserts non-zero check count) | 0 | **done** | 304,368 checks, 0 errors; injected `[summoning_leak]` in a flag-off client root fails |
| 0f | Spike the pack membership add-path on a throwaway obj | 0 | **done** | temp id 40000: membership `1 client`, 0 disagreements; obj-only bake wrote 1 record/19 bytes, 0 failed/unknown/unresolved; temp tree/cache only |
| 0g | Characterise `IF_OPENSUB` on a cache-absent group | 0 | **done** | headless pristine cache, temp group 969: logged missing pack + skipped mount; next script message rendered; BMP written; normal exit, no hang/crash |
| 1a | Stat 24 end to end — `stat.pack`, `MOCK230_STAT_COUNT`, 7 enums, `stats.if` 3×9, `script_8950` case 24, icon | 1 | **in_progress** | zero 530 reads; client edits staged only from the marked lane |
| 2a | `rev_dat2_rs530.c` profile + `revisions.c` rows | 2 | pending | do **not** pin `FRAME` |
| 2b | `SEQUENCE_RS2_530` + `OBJ_RS2_530` codecs | 2 | pending | A/B 634/727 — this touches the whole RS2 branch |
| 2c | `RSCache_Dat2FramemapEncodeCodec` — fixes a silent data bug | 2 | pending | write the failing test first |
| 2d | Sharded RS2 config reader (`cp_common.c:58`) | 2 | pending | |
| 2e | `cachepack import` subcommand | 2 | pending | ~1,600 LOC, the big one |
| 2f | Texture map 680→210 + ledger `signoff` column | 2 | pending | irreducibly human, 1–2 weeks |
| 3a | Owner-bound NPCs — `owner_pid`/`owner_gen`, `npc_run_mode`, `ai_*` dispatch, 3 opcodes | 3 | pending | fixes a documented pre-existing defect |
| 3b | npc `server_base` (`content_register.c:63`) alignment | 3 | blocked | follow the npc-id-cap-removal work; do not pick a number here |
| 3c | Spirit wolf: assets, objs, `.rs2` triggers, timer, dismiss, headless proof | 3 | pending | the vertical slice |

Phases 4–7 (skill surfaces, breadth, Beast of Burden, polish) are scoped in
[`SUMMONING_PORT.md`](SUMMONING_PORT.md) §9 and will be seeded here after the Phase 3 review.

---

## Loop prompt

Read [`SUMMONING_PORT.md`](SUMMONING_PORT.md) + `PORTING_GUIDE` §4/§4.5/§7; take the next pending
unblocked slice; never park sibling lanes; verify (`mock230_pack --check-only`,
`make -C src mock230-scripts`, and the flag-off byte-identity check); update this file and the
budget/opcode tables; re-arm. Stop only when the user stops the loop.
