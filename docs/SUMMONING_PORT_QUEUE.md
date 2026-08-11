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
5. **npc ids are not a constraint.** An NPC_INFO v5 add carries a 16-bit per-client NPC index
   (`0xffff` terminator), then a 14-bit initial definition. Definitions 16384..65535 use the
   add's extended/update flag and update-mask `0x1` to replace that definition in the same packet
   with a transformed unsigned 16-bit `p2Alt3` / `UShortLEAdd` value. Port the full roster; do not
   tier or budget around the direct initial-definition width. See
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
- **Completion authority does not mean bulk promotion.** The user's completion authorization
  permits required quest/audio/pet work, but every former review-only record still needs a
  dedicated ledger, source closure, staged feature-on record, and real-client acceptance.
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
| 0c | `port/summoning_530.map` + `tools/port_summoning_ids.py --check` + `test-port` row | 0 | **done** | 164 manifest roots checked, 0 errors; zero-row mutation fails. Aggregate `test-port` reaches the Phase-5a 94/0 and Phase-5b 202/0 structural gates, then is blocked only by 11 unrelated rs2012 QBD/Tormented Demons unresolved sequence names |
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
| 2f | Texture map 680→210 + ledger `signoff` column | 2 | **done** | Global approval is recorded in `summoning_port/texture_map_530_to_239.ini`: all 680 rev-530 material ids resolve to osrs239 material ids, with the earlier Dreadfowl visual assignments retained as overrides. Every dedicated model row (235 total) is `ok`; importer validation loads the table automatically for each Summoning manifest, rejects incomplete maps, and supports texture-only reapplication without rewriting gameplay configs. |
| 3a | Owner-bound NPCs — `owner_pid`/`owner_gen`, `npc_run_mode`, `ai_*` dispatch, 3 opcodes | 3 | **done** | pid + nonzero login generation fails closed on slot reuse; two-player mode and real-VM ai_timer context checks; all three opcodes executed through VM; generated metadata/coverage current; rev230 world suite + 68-check metadata suite green. Rev239 full world suite remains pre-blocked by the concurrent 23,139-NPC/capture work (198 unrelated failures); ownership assertions themselves pass there |
| 3b | npc `server_base` (`content_register.c:63`) alignment | 3 | **done** | retain base 20000. Correct NPC_INFO model: 16-bit per-client index (`0xffff` terminator), then 14-bit initial definition; type 20000 uses extended/update + mask `0x1` transformed-16-bit replacement in the same packet. Writer/reader regression covers index 321 with high definition 20000 |
| 3c | Spirit wolf: assets, objs, `.rs2` triggers, timer, dismiss, headless proof | 3 | **done** | paired off/on ServerScript packs; every entry point gated; visible type-20000 wolf in the real headless client; exactly one spawn on summon and persisted relog; call, dismiss and real timer-expiry paths; 40/0 slice checks, rev239 index-321/type-20000 extended-path codec regression, CS2 3/0, server pack and `mock230_pack --check-only` 0 errors, flag-off no spawn/message |
| 4a | Deterministic dbindex generator + byte-identical regeneration tests | 4 | **done** | `gen_dbindex.py --check` verifies all 147 indexes; mutation regression is 156/0 and proves omitted/misordered keys fail before byte-exact repair; wired into `test-port`; isolation 642639/0, flag-off 25/0, pack 0 errors, fresh-save headless 28/0 |
| 4b | Summoning skill-guide rows, events and live client `db_find` proof | 4 | **done** | corrected wolf-cell context menu/op2 opens `skill_guide_v2`; dbtables 212/213 render all six tabs plus the full Spirit wolf recipe, and obj 40000 enters the object/model renderer; fresh-save client acceptance 19/0 with retained BMP/log; feature bake is 16,978 records, 0 failed/unresolved, CS2 3/0, 147 indexes; all-content ServerScript compile emits 12,774 scripts; `mock230_pack --check-only` 0 errors; Overview remains bytecode-only and unmodified |
| 4c | Summoning points orb | 4 | **done** | exact rev-530 interface-747 art (sprites 1200/1206/1244/1245) remapped to target 20000..20003; visible target position `x=89,y=128` avoids the fixed-client tab strip that hid the proposed `54,158`; authored script 12000 updates live on stat-24 transmit; real click sends interface-160 component-64 op1 and calls the active wolf; 19/0 fresh-save client checks, CS2 4/0, ServerScript 12,781 compiled, isolation 644,151/0, pack 0 errors |
| 4d | Summoning Equipment access | 4 | **done** | authored osrs239 script 12001 positions the cache-native Call-follower button at the top-right of Worn Equipment and assigns exact rev-530 sprite-222/target graphic 229; its real op1 mounts compact 190x205 group 969 only into `wornitems:universe`; group 969 keeps native 140x28 script-97 Call/Dismiss chrome and adds Back-to-equipment; no top-level 161/548/164 overlay remains; group 969 uses script 12002 `if_setnpchead(npc_20000)` with no fallback model; Call/Dismiss use real IF_BUTTON1; CS2 compiles 6/0 and the feature cache repacks with 0 failed/unresolved/missing assets |
| 4e | Loc-id wire proof + runtime obelisk and Renew-points | 4 | **done** | dedicated rev530 loc codec exact-sweeps all 42,004 locs and preserves 643/727; source loc 28716→target 62201 because rev239 loc config is exact 16-bit `p2Alt3` (70000→4464). Feature-gated/idempotent runtime `loc_add`; real right-click/op2 restores 0/1→1/1 and renders seq 20003 + spotanim 20000/model 100006. Import 117/0; client 27/0; sidebar regression 56/0; CS2 6/0; feature bake 16,991 records + 179 assets, 0 failures/unresolved/missing; base `mock230_pack` 0 errors; flag-off 25/25 identical. Full rscache target reaches the new 530 checks but remains pre-blocked by the existing cachepack-fidelity missing synth/song/font files; the independent mock239-playerinfo target currently aborts in its unrelated NPC entering-view fixture |
| 4f | Authored infusion interface and pouch production | 4 | **done** | fresh target IF3 group 970, mounted through `mainmodal`, with real 1/5/10/X/All row operations; imports blank pouch 12155 plus craft/charge seqs 9068/8509, but deliberately defers unsafe synth 4164. `oploc1` preserves the interacted loc and button production re-finds it before player/loc animation, recipe consumption, pouch add, and 4.8-XP award. `test-summoning-phase4f` 69/0 runs fresh-save render and make cases through real loc + `IF_BUTTON1`, verifies saved inputs/output/stat plus active→idle obelisk animation; full Make target passes |
| 5a | Roster import boundary and provenance audit | 5 | **done** | Boundary records 82 pouch sources: 78 active familiar/pouch pairs and four explicitly deferred Sacred Clay pairs. Phase 5a itself admitted no breadth cohort; its later separately-owned Dreadfowl admission is listed below. The `summoning_roster_530` experiment remains review-only evidence (630 source files, 2,175 pack references, 1,365 ledger rows), neither deleted nor accepted; its last broad manifests remain archived under `summoning_port/review_only/`. Staging holds it out, including mixed pack rows, and rejects other generated cohorts, pets, unsafe synths, and `npc_sounds=yes`. Evidence: Phase-5a audit 94/0; ledger 1,541 required/1,418 rows/0 errors; staging 4,545/0, 3,785 review references held and 2,805 withheld, with 417 staged actual files and review exclusion 417/0; feature cache 16,998 records/0 errors, 187 assets/23 tables, CS2 6/0, ServerScript 12,963 scripts, `mock230_pack --check-only` 8,340/0; flag-off 25/0 |
| 5b | First bounded familiar/pouch cohort | 5 | **done** | Sole admitted Dreadfowl closure: source NPC 6825/pouch 12043 → target NPC 26000/pouch 46000; models 120000–120002, ready/walk seq 23000/23001, animation 23000 and framemap 10000. Its nine-row separate ledger remains `minted`/`unreviewed`, not human signoff; no combat, pet, scroll/special, or audio closure. Structural/staging proof 202/0 preserves `summoning_roster_530` as review-only. Normal real-client proof 110/0 uses the actual menu chain `ifop4=Summon` → action 2231/op 5 → `IF_BUTTONX op=6` → `OPHELD4`, consumes rather than drops the pouch, renders model/head/ready animation plus normal-server `IF_SETNPCHEAD` `npc_head ... applied=1`, and proves persisted 400-tick/drain lifecycle plus real Call/Dismiss |
| 5c | Second bounded familiar/pouch cohort | 5 | **done** | Source NPC 6794/spirit terrorbird + pouch 12007; target NPC 26016/pouch 46016; models 120032–120034; ready/walk seq 23032/23033, animation 23032 and framemap 10032. Its nine-row separate ledger remains `minted`/`unreviewed`; no combat, pet, scroll/special, or audio closure. Structural/staging evidence: generated config/assets added under `ported/scape2009_summoning/`; cohort ledger `port/summoning_spirit_terrorbird_530.map` has nine minted rows and the same boundary checks as 5b |
| 5d | Third bounded familiar/pouch cohort | 5 | **done** | Source NPC 6841/Spirit spider + pouch 12059; target NPC 26032/pouch 46032; models 120064–120066; ready/walk seq 23064/23065, animation 23064 and framemap 10064. Its nine-row separate ledger remains `minted`/`unreviewed`; no combat, pet, scroll/special, or audio closure. Generated config/assets are isolated under `ported/scape2009_summoning/` and the allocation block is disjoint from all earlier admitted cohorts. |
| 5e | Fourth bounded familiar/pouch cohort | 5 | **done** | Source NPC 6806/Thorny snail + pouch 12019; target NPC 26048/pouch 46048; models 120096–120098; ready/walk seq 23096/23097, animation 23096 and framemap 10096. Its nine-row separate ledger remains `minted`/`unreviewed`; no combat, pet, scroll/special, or audio closure. Generated config/assets are isolated under `ported/scape2009_summoning/` with a disjoint allocation block. |
| 5f | Fifth bounded familiar/pouch cohort | 5 | **done** | Granite crab, source `6796/12009` → target `26064/46064`; nine-row asset closure, no audio or gameplay extras. |
| 5g | Sixth bounded familiar/pouch cohort | 5 | **done** | Spirit mosquito, source `7331/12778` → target `26080/46080`; nine-row asset closure, no audio or gameplay extras. |
| 5h | Seventh bounded familiar/pouch cohort | 5 | **done** | Desert wyrm, source `6831/12049` → target `26096/46096`; nine-row asset closure, no audio or gameplay extras. |
| 5i | Eighth bounded familiar/pouch cohort | 5 | **done** | Spirit scorpion, source `6837/12055` → target `26112/46112`; nine-row asset closure, no audio or gameplay extras. |
| 5j | Ninth bounded familiar/pouch cohort | 5 | **done** | Spirit Tz-Kih, source `7361/12808` → target `26128/46128`; nine-row asset closure, no audio or gameplay extras. |
| 5k | Tenth bounded familiar/pouch cohort | 5 | **done** | Albino rat, source `6847/12067` → target `26144/46144`; nine-row asset closure. |
| 5l | Eleventh bounded familiar/pouch cohort | 5 | **done** | Spirit kalphite, source `6994/12063` → target `26160/46160`; nine-row asset closure. |
| 5m | Twelfth bounded familiar/pouch cohort | 5 | **done** | Compost mound, source `6871/12091` → target `26176/46176`; nine-row asset closure. |
| 5n | Thirteenth bounded familiar/pouch cohort | 5 | **done** | Giant chinchompa, source `7353/12800` → target `26192/46192`; nine-row asset closure. |
| 5o | Fourteenth bounded familiar/pouch cohort | 5 | **done** | Vampire bat, source `6835/12053` → target `26208/46208`; nine-row asset closure. |
| 5p | Fifteenth bounded familiar/pouch cohort | 5 | **done** | Honey badger, source `6845/12065` → target `26224/46224`; nine-row asset closure. |
| 5q | Sixteenth bounded familiar/pouch cohort | 5 | **done** | Beaver, source `6808/12021` → target `26240/46240`; nine-row asset closure. |
| 5r | Seventeenth bounded familiar/pouch cohort | 5 | **done** | Void ravager, source `7370/12818` → target `26256/46256`; nine-row asset closure. |
| 5s | Eighteenth bounded familiar/pouch cohort | 5 | **done** | Void spinner, source `7333/12780` → target `26272/46272`; nine-row asset closure. |
| 5t | Nineteenth bounded familiar/pouch cohort | 5 | **done** | Void torcher, source `7351/12798` → target `26288/46288`; nine-row asset closure. |
| 5u | Twentieth bounded familiar/pouch cohort | 5 | **done** | Bronze minotaur, source `6853/12073` → target `26304/46304`; nine-row asset closure. |
| 5v | Twenty-first bounded familiar/pouch cohort | 5 | **done** | Iron minotaur, source `6855/12075` → target `26320/46320`; nine-row asset closure. |
| 5w | Twenty-second bounded familiar/pouch cohort | 5 | **done** | Steel minotaur, source `6857/12077` → target `26336/46336`; nine-row asset closure. |
| 5x | Twenty-third bounded familiar/pouch cohort | 5 | **done** | Mithril minotaur, source `6859/12079` → target `26352/46352`; nine-row asset closure. |
| 5y | Twenty-fourth bounded familiar/pouch cohort | 5 | **done** | Adamant minotaur, source `6861/12081` → target `26368/46368`; nine-row asset closure. |
| 5z | Twenty-fifth bounded familiar/pouch cohort | 5 | **done** | Rune minotaur, source `6863/12083` → target `26384/46384`; nine-row asset closure. |
| 5aa | Twenty-sixth bounded familiar/pouch cohort | 5 | **done** | Bull ant, source `6867/12087` → target `26400/46400`; nine-row asset closure. |
| 5ab | Twenty-seventh bounded familiar/pouch cohort | 5 | **done** | Macaw, source `6851/12071` → target `26416/46416`; nine-row asset closure. |
| 5ac | Twenty-eighth bounded familiar/pouch cohort | 5 | **done** | Evil turnip, source `6833/12051` → target `26432/46432`; nine-row asset closure. |
| 5ad | Twenty-ninth bounded familiar/pouch cohort | 5 | **done** | Spirit cockatrice, source `6875/12095` → target `26448/46448`; nine-row asset closure. |
| 5ae | Thirtieth bounded familiar/pouch cohort | 5 | **done** | Spirit guthatrice, source `6877/12097` → target `26464/46464`; nine-row asset closure. |
| 5af | Thirty-first bounded familiar/pouch cohort | 5 | **done** | Spirit saratrice, source `6879/12099` → target `26480/46480`; nine-row asset closure. |
| 5ag | Thirty-second bounded familiar/pouch cohort | 5 | **done** | Spirit zamatrice, source `6881/12101` → target `26496/46496`; nine-row asset closure. |
| 5ah | Thirty-third bounded familiar/pouch cohort | 5 | **done** | Spirit pengatrice, source `6883/12103` → target `26512/46512`; nine-row asset closure. |
| 5ai | Thirty-fourth bounded familiar/pouch cohort | 5 | **done** | Spirit coraxatrice, source `6885/12105` → target `26528/46528`; nine-row asset closure. |
| 5aj | Thirty-fifth bounded familiar/pouch cohort | 5 | **done** | Spirit vulatrice, source `6887/12107` → target `26544/46544`; nine-row asset closure. |
| 5ak | Thirty-sixth bounded familiar/pouch cohort | 5 | **done** | Pyrelord, source `7377/12816` → target `26560/46560`; nine-row asset closure. |
| 5al | Thirty-seventh bounded familiar/pouch cohort | 5 | **done** | Magpie, source `6824/12041` → target `26576/46576`; nine-row asset closure. |
| 5am | Thirty-eighth bounded familiar/pouch cohort | 5 | **done** | Bloated leech, source `6843/12061` → target `26592/46592`; nine-row asset closure. |
| 5an | Thirty-ninth bounded familiar/pouch cohort | 5 | **done** | Abyssal parasite, source `6818/12035` → target `26608/46608`; nine-row asset closure. |
| 5ao | Fortieth bounded familiar/pouch cohort | 5 | **done** | Spirit jelly, source `6992/12027` → target `26624/46624`; nine-row asset closure. |
| 5ap | Forty-first bounded familiar/pouch cohort | 5 | **done** | Ibis, source `6991/12531` → target `26640/46640`; nine-row asset closure. |
| 5aq | Forty-second bounded familiar/pouch cohort | 5 | **done** | Spirit kyatt, source `7365/12812` → target `26656/46656`; nine-row asset closure. |
| 5ar | Forty-third bounded familiar/pouch cohort | 5 | **done** | Spirit larupia, source `7337/12784` → target `26672/46672`; nine-row asset closure. |
| 5as | Forty-fourth bounded familiar/pouch cohort | 5 | **done** | Spirit graahk, source `7363/12810` → target `26688/46688`; nine-row asset closure. |
| 5at | Forty-fifth bounded familiar/pouch cohort | 5 | **done** | Karamthulhu overlord, source `6809/12023` → target `26704/46704`; nine-row asset closure. |
| 5au | Forty-sixth bounded familiar/pouch cohort | 5 | **done** | Smoke devil, source `6865/12085` → target `26720/46720`; nine-row asset closure. |
| 5av | Forty-seventh bounded familiar/pouch cohort | 5 | **done** | Abyssal lurker, source `6820/12037` → target `26736/46736`; nine-row asset closure. |
| 5aw | Forty-eighth bounded familiar/pouch cohort | 5 | **done** | Spirit cobra, source `6802/12015` → target `26752/46752`; nine-row asset closure. |
| 5ax | Forty-ninth bounded familiar/pouch cohort | 5 | **done** | Stranger plant, source `6827/12045` → target `26768/46768`; nine-row asset closure. |
| 5ay | Fiftieth bounded familiar/pouch cohort | 5 | **done** | Barker toad, source `6889/12123` → target `26784/46784`; ten-row closure including four models. |
| 5az | Fifty-first bounded familiar/pouch cohort | 5 | **done** | War tortoise, source `6815/12031` → target `26800/46800`; nine-row asset closure. |
| 5ba | Fifty-second bounded familiar/pouch cohort | 5 | **done** | Bunyip, source `6813/12029` → target `26816/46816`; nine-row asset closure. |
| 5bb | Fifty-third bounded familiar/pouch cohort | 5 | **done** | Fruit bat, source `6817/12033` → target `26832/46832`; nine-row asset closure. |
| 5bc | Fifty-fourth bounded familiar/pouch cohort | 5 | **done** | Ravenous locust, source `7372/12820` → target `26848/46848`; nine-row asset closure. |
| 5bd | Fifty-fifth bounded familiar/pouch cohort | 5 | **done** | Arctic bear, source `6839/12057` → target `26864/46864`; nine-row asset closure. |
| 5be | Fifty-sixth bounded familiar/pouch cohort | 5 | **done** | Obsidian golem, source `7345/12792` → target `26896/46896`; nine-row asset closure. |
| 5bf | Fifty-seventh bounded familiar/pouch cohort | 5 | **done** | Granite lobster, source `6849/12069` → target `26912/46912`; nine-row asset closure. |
| 5bg | Fifty-eighth bounded familiar/pouch cohort | 5 | **done** | Praying mantis, source `6798/12011` → target `26928/46928`; nine-row asset closure. |
| 5bi | Fifty-ninth bounded familiar/pouch cohort | 5 | **done** | Forge regent, source `7335/12782` → target `26944/46944`; nine-row asset closure. |
| 5bj | Sixtieth bounded familiar/pouch cohort | 5 | **done** | Talon beast, source `7347/12794` → target `26960/46960`; nine-row asset closure. |
| 5bk | Sixty-first bounded familiar/pouch cohort | 5 | **done** | Giant ent, source `6800/12013` → target `26976/46976`; nine-row asset closure. |
| 5bl | Sixty-second bounded familiar/pouch cohort | 5 | **done** | Hydra, source `6811/12025` → target `26992/46992`; nine-row asset closure. |
| 5bm | Sixty-third bounded familiar/pouch cohort | 5 | **done** | Spirit dagannoth, source `6804/12017` → target `27008/47008`; nine-row asset closure. |
| 5bn | Sixty-fourth bounded familiar/pouch cohort | 5 | **done** | Unicorn stallion, source `6822/12039` → target `27024/47024`; nine-row asset closure. |
| 5bo | Sixty-fifth bounded familiar/pouch cohort | 5 | **done** | Wolpertinger, source `6869/12089` → target `27040/47040`; nine-row asset closure. |
| 5bp | Sixty-sixth bounded familiar/pouch cohort | 5 | **done** | Pack yak, source `6873/12093` → target `27056/47056`; nine-row asset closure. |
| 5bq | Sixty-seventh bounded familiar/pouch cohort | 5 | **done** | Fire titan, source `7355/12802` → target `27072/47072`; nine-row asset closure. |
| 5br | Sixty-eighth bounded familiar/pouch cohort | 5 | **done** | Moss titan, source `7357/12804` → target `27088/47088`; nine-row asset closure. |
| 5bs | Sixty-ninth bounded familiar/pouch cohort | 5 | **done** | Ice titan, source `7359/12806` → target `27104/47104`; nine-row asset closure. |
| 5bt | Seventieth bounded familiar/pouch cohort | 5 | **done** | Lava titan, source `7341/12788` → target `27120/47120`; nine-row asset closure. |
| 5bu | Seventy-first bounded familiar/pouch cohort | 5 | **done** | Swamp titan, source `7329/12776` → target `27136/47136`; nine-row asset closure. |
| 5bv | Seventy-second bounded familiar/pouch cohort | 5 | **done** | Geyser titan, source `7339/12786` → target `27152/47152`; nine-row asset closure. |
| 5bw | Seventy-third bounded familiar/pouch cohort | 5 | **done** | Abyssal titan, source `7349/12796` → target `27168/47168`; nine-row asset closure. |
| 5bx | Seventy-fourth bounded familiar/pouch cohort | 5 | **done** | Iron titan, source `7375/12822` → target `27184/47184`; nine-row asset closure. |
| 5by | Seventy-fifth bounded familiar/pouch cohort | 5 | **done** | Steel titan, source `7343/12790` → target `27200/47200`; nine-row asset closure. |
| 5bz | Seventy-sixth bounded familiar/pouch cohort | 5 | **done** | Void shifter, source `7367/12814` → target `27216/47216`; nine-row asset closure. |
| 5bh | Phoenix | 5 | **done** | Source `8575/14623` → target `27232/47232`; nine-row non-audio closure admitted. Unsafe synth sources `5776` and `5753` are deliberately withheld; Phoenix has no source `Familiar` class, so gameplay/audio remains out of scope. |
| 6a | Inventory namespace prerequisite | 6 | **done** | `fields/inv.ini`, `[namespace:inv]`, and feature-on `summoning_bob` (30 slots) are present. `mock230-cache-summoning` bakes the record and the normal container resolver can allocate it; this also removes the documented shop prerequisite. |
| 6b | First Beast of Burden familiar | 6 | **done** | Spirit terrorbird is the selected 12-slot proof familiar. `test-summoning-phase6b` proves real pouch → NPC Store menu → rendered BOB panel → Store, no-cheat relog persistence, real Withdraw, and sidebar Dismiss spill. Rev-239 `OBJ_ADD` state replay now uses the required enclosed-zone route. |
| 7a | Per-account Summoning unlock + Equipment integration | 7 | **done** | Persisted `summoning_unlocked` syncs `content_restrict_summoning_serverside` at login; runtime entries are account-gated; locked/unlocked/relog client acceptance is 42/0. Group 969 mounts inside Worn Equipment rather than a gameframe root, and the real Spirit-terrorbird Store → relog → Withdraw → Dismiss-spill regression passes. |
| 7b | Wolf Whistle unlock writer | 7 | **done** | Upstream completion contract verified: persist unlock, grant 276 Summoning XP and 275 gold charms. Generic charm `12158→40002` was already admitted; the distinct quest-reward copy `12527→40256` has its own audited ledger and cannot enter infusion. `summoning_wolf_whistle_complete` is idempotent, persists the gate, grants 2760 ServerScript XP units and 275 copies, and has real-client/relog harness proof. |
| 7c | Safe shared familiar audio | 7 | **done** | Source synth 188 is ledgered against cache-native `summon_npc` after a byte-identical payload check. The actual sidebar Call path emits SYNTH_SOUND 188 (one loop, zero delay), retained in the permanent real-client log. 4161/4164/4214/4265/4372 remain withheld until transcoded and verified. |
| 7d | First pet lifecycle | 7 | **done** | Clockwork cat (`7771`/`3598`) is admitted through its own nine-row ledger (3 models, 2 seqs, frame archive, framemap) and independent persisted active/type state. The permanent fresh-client target is 35/0: actual inventory Release renders model 123000 + ready seq 25300, no-cheat relog reconstructs type 27300, and the ordinary death teardown clears state (zero varps are intentionally omitted by the save format). It binds Pick-up and Shoo separately and does not reuse familiar points, timers, BoB, growth, or hunger. |
| 7e | Wolf Whistle interaction | 7 | **done** | The feature-on obelisk exposes `Begin Wolf Whistle` as its third ordinary operation, available to locked accounts and wired directly to the idempotent writer. The permanent fresh-save harness sends the normal client `OPLOC3` packet, confirms the reward/state, and verifies the unlocked tab after relog. |
| 7f | Completion audit | 7 | **done** | Audit ledger accounts for all 176 review-only pet NPC rows, 175 pet item rows, and every deferred synth source. Clockwork cat and synth 188 are the only admitted closures; all others have explicit withheld/transcode/behaviour reasons. The stage-enforcement audit is 15/0. `test-summoning-phase7f` now rebuilds `mock230_pack` before validation, avoiding stale-binary false errors; its full content check is 0 errors (warnings are pre-existing diagnostics). |

Phases 5–7 (breadth, Beast of Burden, polish) are scoped in
[`SUMMONING_PORT.md`](SUMMONING_PORT.md) §9. Phase 5 is complete: the preserved generated
experiment remains review-only and is not accepted merely because its importer dry-run succeeds.
Phases 6 and 7 are complete. The preserved broad roster remains review-only; future admissions require a new bounded slice and client proof.

---

## Loop prompt

Phase 7 is complete. Preserve the broad review-only roster; any future audio/pet admission must
start a new bounded slice with a dedicated ledger, real-client proof, `mock230_pack --check-only`,
server-script compilation, and flag-off byte-identity verification.
