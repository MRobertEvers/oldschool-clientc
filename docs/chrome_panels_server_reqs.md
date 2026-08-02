# Chrome panels: XP Tracker, Hiscores, Loot Tracker

> **PARTLY BUILT — 2026-08-02.** All three panels now *open* in the headless
> client and two of the three *draw*. See §7 for exactly what landed, what it
> cost, and what is still missing. Read §7 before acting on anything below it:
> the sections that follow §0 are the discovery pass and several of their
> headline claims did not survive measurement.
>
> Three corrections to the previous header, all re-measured (PORTING_GUIDE §7):
>
> 1. **The ceiling was never the problem and was already bumped.**
>    `CS2VM2_OPCODE_STACK_MAX` measures **8023**, not 7602
>    (`src/cs2vm2/cs2vm2_opcode_stack.gen.h:5`; the generator constant is
>    `MAX_OPCODE = 8023` at `gen_opcode_stack.py`, whose own comment documents
>    the 7602→8023 move). Every opcode this doc named was already in range —
>    and it changed nothing, because the rows arrived zeroed with `known = 0`,
>    which is what `CS2VM2_Op_StackMetaStub` asserts on. The real gap was that
>    **the two signature tables in this repo could not see each other**; that
>    is what §7.1 fixes.
> 2. **"Mirror ~15 rows" understated it by 20×.** 335 opcodes had a signature
>    in `3rd/rscache/src/cs2/cs2_command.gen.h` that the client VM could not
>    inherit. Hand-mirroring fifteen would have left the other 320.
> 3. **"Set Goal" — the switch cases are 8, 9 and 10.** The previous header
>    already corrected "case 5 straight to case 8" in passing; stated plainly
>    here because the wrong number is the memorable one. There is no case 6
>    either, so the button is dead in this cache's clientscripts and no server
>    or engine change can make it fire.

> Third round of the discovery pass (`docs/PORTING_GUIDE.md` §5.3), companion
> to `docs/questlist_chatmenu_levelup.md`, `docs/shop_server_reqs.md`, and
> `docs/friends_pm_chat_server_reqs.md`. Unlike the first two rounds, all
> three interfaces here are **modern (2018+) OSRS client features with no
> LostCity precedent** — confirmed per-interface below, not assumed. That
> changes the shape of the finding: mostly these need little or no new
> *server* work, and the real gaps are either client-side CS2 host-op
> implementations or corpus gaps in the decompiled script dump.

## 0. Status at a glance

Re-measured 2026-08-02; the "measured" column is a client run, not a reading.
See §7.5 for the runs.

| interface | id | status | measured | what's missing |
|---|---|---|---|---|
| `xptracker` | 729 | **needs almost nothing from the server** | **opens and draws, exit 0, zero stubbed opcodes** | the `xpdrops_*` varps for the goal *display*; the goal *button* is dead in the cache (§1.1) |
| `hiscores` | 894 | **not a server feature at all** | **opens and draws, exit 0, one stubbed opcode (7809)** | host implementations for 7809/7811 — and the lookup itself is out of band, so "lookup failed" is the honest end state |
| `loottools` | 650 | **blocked on tier-B arities, not on a corpus gap** | **opens, then aborts at opcode 7601** | 16 opcodes with no arity in either signature table (§7.6); no new packet is needed — that question is now settled |

---

## 1. `xptracker` (729) — needs almost nothing

`xptracker.if` onload (`i:5448`) chains into `script_5449` (rebuild) and, from
the sidebar tab that hosts it, two listeners armed once:
`if_setonstattransmit("script5451", ...)` and
`if_setontimer("script5452(...)", ...)` (`script_5356.cs2`/`script_7571.cs2`,
case `struct_3742`).

**Exhaustive grep of every proc reachable from the onload finds nothing
beyond `stat_xp`/`stat_base` (already-transmitted) and `clientclock`
(client-native tick counter).** `STAT_BASE`/`STAT_XP` are landed
(`docs/combat_hud.md` §2); `IF_SETONSTATTRANSMIT` dispatch is landed
(`src/game/rs_cs2_dispatch.c:228-241`, confirmed); `IF_SETONTIMER`/
`CC_SETONTIMER` dispatch is landed (`src/app.c:3874-3898`, confirmed — fires
once per client tick for every component with a timer hook).

**Row order, pause state, "tracking since," and the 10-slot drop-sample
ring are all `%varcint`** — confirmed outside the content register entirely:
no `varc` namespace in `content.ini`, no `fields/varc.ini`, and no wire
packet ever writes a client varc (`grep -rniE "varc" src/net/rev/` matches
only the unrelated `RESET_CLIENT_VARCACHE`/varp-sync alias). Client-side,
`VarCManager_ResetAll` (`src/varc/varc_manager.c:172`) is called nowhere in
`app.c` or `src/game` — confirmed, only from its own test — so this state
lives in-process for the session and never touches the wire in either
direction. **Nothing to declare, nothing to transmit, nothing for the server
to own.**

### 1.1 The one exception: "Set Goal"

Several row procs call `~xpdrops_data_get` (`script_1002.cs2`), which reads
**real varps** — `xpdrops_<skill>_start`/`_end`
(`configs/all.varp.compack:1227-1275,4964-4965`, confirmed present, e.g.
`1228=xpdrops_total_start`, `1253=xpdrops_attack_end`) — shared with the
unrelated XP-drops number-panel feature. These already ride the generic
`.varp` transmit wire (`src/net/mock/mock230_content.c:1686-1694`), the same
mechanism `%qp` needs in `docs/questlist_chatmenu_levelup.md` §1.2. **Nothing
declares them**: `grep -rn "xpdrops_.*_start\|xpdrops_.*_end" server/scripts/`
is empty.

The "Set Goal" button itself (op 6 on the row context menu) has **no bound
case** in the handler. Re-measured 2026-08-02 against `cache.osrs239`:
`[clientscript,script5461]` arms `cc_setop(6, "Set Goal")` and hooks
`script5463`, whose `switch_int` has cases **8, 9 and 10 only**. There is no
case 6 and no case 5 — the earlier "jumps from case 5 straight to case 8" is
wrong about the mechanism and right about the conclusion. This is **not** a
corpus gap like `docs/questlist_chatmenu_levelup.md` §1.3's missing
`questlist_draw`: the script is present and complete, and the op is simply
dead in this cache. Nothing server- or engine-side can make it fire.

### 1.2 Server obligations

| state | needed for | delivery | mock230 status |
|---|---|---|---|
| `STAT_XP`/`STAT_BASE`, timer dispatch | the whole tracking/rate display | already-landed transports | **landed, sufficient** |
| `xpdrops_<skill>_start`/`_end` `.varp` overlay, `transmit=yes` (likely `scope=perm`) | "Set Goal" only | generic varp wire | **not declared** — small, isolated gap |
| Goal-setting click handler | wiring op 6 end-to-end | — | **corpus gap** — re-decompile `script_5463`'s real target before implementing |

### 1.3 LostCity precedent

**None, confirmed** (`grep -ril "xptracker\|xp_tracker" LostCity_Server/` —
zero hits) — expected, since LostCity is a 2004 snapshot and the real XP
tracker postdates it by over a decade.

---

## 2. `hiscores` (894) — not a server feature

**Headline: this is a real name-keyed cross-player lookup, not "my own stats
reformatted"** — confirmed by `script_7548.cs2` printing *"Rank for
`<name>`"* (rank is meaningless for your own single account) and
`script_7532.cs2` iterating up to 5 *named* Group Ironman members
individually. There is no rank-ordered scrollable list anywhere in the 31
components, so this is bounded name lookups, not a full paginated
leaderboard render.

**And it needs no game packet at all.** `grep -rn "hiscore" src/net/rev/` is
zero hits across every revision's `packetin.h`. The proof this is
intentional, not an oversight: a sibling script in the same UI family,
`script_4234.cs2` (`clan_members_op`), answers "show hiscores for this
name" by calling `~openurl_raw("https://secure.runescape.com/m=hiscore_oldschool/…")`
— the **real client pops the OS browser to the live hiscores website**, fully
outside the game protocol. The in-panel async path (interface 894) almost
certainly does the equivalent as a background HTTP call, not a `PKT_*`.

The actual gap is nine CS2 host ops the panel calls directly
(`_7801` rank, `_7802` score, `_7809` request-status, `_7810` cancel,
`_7811` error-string, `_7812` select-data-source, `_7819` group-aggregate,
`_7823` group-member-name, `_7824` select-member) plus `openurl` —
**confirmed zero cases for any of them** in `src/cs2vm2/cs2vm2.c`
(`grep -n "case 78[0-2][0-9]:"` → nothing), so any is reached they hit
`CS2VM2_ReportUnimplementedOpcode`. This is a **client VM gap**, not
something mock230's server code addresses.

> **Superseded in part, 2026-08-02 (§7).** "Nine" was an undercount — the
> measured closure is 20+ opcodes, and the panel's reached path today is
> narrower still: it opens and draws with exactly **one** stubbed opcode,
> 7809, because §7.1's bridge gave the whole family real signatures. They no
> longer abort; they report themselves as `cs2-stub` and answer zeros. The
> conclusion is unchanged and now demonstrated rather than inferred.

### 2.1 Server obligations

| requirement | needed? | why |
|---|---|---|
| Game-protocol packet | **No** | real client uses out-of-band HTTP; confirmed zero wire hits |
| Cross-player rank/score data source | Only if the lookup is to return real data | a single/few-player mock has no real leaderboard to answer against |
| CS2 host ops (`_7801`…`_7824`, `openurl`) | Yes, client-side | avoid the unimplemented-opcode abort; the honest options, in order of effort, are: (1) stub to a graceful "lookup failed" response, (2) answer only for the logged-in player's own name/stats as a shim, (3) actually implement `openurl` to shell to a real browser |
| `~script7529`'s body (issues the actual request) | Unknown | missing from this corpus — re-decompile before implementing |

### 2.2 LostCity precedent

**No `.rs2` proc, confirmed** (`grep -rniE "hiscore" LostCity_Server/content/scripts` — zero). What LostCity *does* have is
entirely outside the game engine: `engine/src/server/login/LoginServer.ts`
writes a player's levels/xp into a separate SQL table (`hiscore`/
`hiscore_large`) on every autosave, for an out-of-band website to query —
**write-only, no read path anywhere in that checkout**, and structurally the
same "separate service, not a RuneScript trigger" shape
`docs/PORTING_GUIDE.md` §5.2 already names for LostCity's `FriendServer`. If
mock230 ever wants real hiscore data, the LostCity-shaped move is a
persistence-sync service, not content.

---

## 3. `loottools` (650) — corpus gap blocks the real question

`loottools.if` (85 components) is **two independent panels sharing chrome**:
the actual Loot Tracker (per-source grouping of received drops, Drops/
Sources/Overview views) and a separate Ground-Item highlighter (free-text
`*wildcard*` lists, unrelated to kill attribution). **Collection log is a
separate interface** (`collection.if`/`collection_overview.if`, confirmed
present, untouched by this call graph) — don't conflate the three.

`script_7128`'s onload does zero data work — it wires cosmetic chrome and
delegates to **`~script7166`/`~script7133`, both confirmed missing from this
decompile** (`ls` on both ids fails — verified directly). These are called
from 14+ sites across the loot-tracker call graph, so this is the single
load-bearing gap, same class as `docs/questlist_chatmenu_levelup.md` §1.3's
missing `questlist_draw`.

**Three hypotheses for how a kill's loot reaches the tracker were checked
against the corpus** (native history-ring like PM chat's, server-pushed
per-line records, or a static drop-table viewer):
- No loot-specific getter op was found (the only "loot"-named entry in
  `cs2_command.gen.h` is an unrelated clan-lootshare setting).
- No `db_getfield`/`db_find` call appears anywhere in the loot-tracker call
  graph — ruling out "static drop-table viewer."
- Five **mutator** ops do exist and are loot-tracker-specific — clear-tracker,
  clear-one-source, ignore-add/remove, clear-ignore-list (opcodes
  7613/7614/7616/7617/7621, `cs2_command.gen.h:1051-1056`) — consistent with
  a native store existing, but they don't show how it's populated.

**Best-supported conclusion, held as inference, not fact**: the tracker
likely reads a client-native structure fed by the client's own combat/
ground-item code when a kill or pickup actually happens — implying no new
packet — but this is inferred from call-site shape, not confirmed by reading
`script7166` itself. **Re-decompile it before treating this as settled.**

Separately confirmed: even where the five mutator opcodes above are named in
the CS2 op table, **this client build's own VM has no opcode-stack entry for
any of them** (`src/cs2vm2/cs2vm2_opcode_stack.gen.h` — confirmed no match),
so they'd hit the unimplemented-opcode stub — a client-engine gap, distinct
from any server gap.

> **Superseded, 2026-08-02 (§7).** Two corrections. (a) The five mutators
> 7613/7614/7616/7617/7621 now have signatures via §7.1's bridge — and none of
> them is on the reached path anyway; the populate path is a *different*
> family this section never found (7601/7602/7605/7606/7630, plus 1624, 2624,
> 7406-7408). (b) The inference in the paragraph above is now **settled, not
> held**: decompiling script 7166 under trial overrides yields
> `$n = _7605(0, ~script1046(_7601, 10), 1); while ($i < $n) { $id = _7606($i);
> $name = _7602($id); $val = _7604($name); … }` — a client-native list store
> queried by begin→count / index→id / id→name. **No new game packet is
> needed.** What still blocks the panel is that 7601 and friends have no arity
> in *either* signature table, so the bridge cannot reach them: see §7.6.

### 3.1 Server obligations — loot-tracker-specific vs. pre-existing

| what | scope | mock230 status |
|---|---|---|
| A new packet/dbtable/RUNCLIENTSCRIPT push for tracker data | loot-tracker-specific | **not evidenced as needed** — pending `script7166` re-decompile |
| `.varp`/`.varbit` overlay for the view/ignore/value-mode toggles (`settings_varp_ehc_5`, `loottools_varp1`) | loot-tracker-specific, minor | **not declared** (`grep` confirms zero hits under `server/scripts/`) — same class as `bank_closing`/`shop_quantity` in `docs/shop_server_reqs.md` §5 |
| CS2 opcodes 7613/7614/7616/7617/7621 | loot-tracker-specific, client-side | **not implemented** in this build's VM |
| Re-decompile `script_7166`/`script_7133` | prerequisite to everything else here | **corpus gap**, confirmed missing |
| NPC-death drop-roll mechanism | **pre-existing, already documented** — NOT loot-tracker-specific; the tracker has nothing to show without it, same as it has nothing to show without NPCs existing | **mostly landed** — `mock230_world_npc_died` (`mock230_world.c:3360-3382`, confirmed) runs `[ai_queue3,<npc>]` drop scripts with a `param=death_drop` fallback; 71/71 LostCity `drop tables/scripts` confirmed present, 69 compiling (97.2%, `docs/LOSTCITY_PORT_TRIAGE.md`), blocked on npc-category plumbing for the rest |

### 3.2 LostCity precedent

**None for the tracker UI itself, confirmed** (`grep -ril "loottrack\|loot_track" LostCity_Server/` and `grep -rli loot LostCity_Server/engine/src` — both zero) — expected, the
feature postdates rev 254 by roughly fifteen years. LostCity's `drop
tables/` (71 scripts, confirmed count) is a real, already-tracked dependency
— but it's the drop tables' own dependency, already queued as the next
content slice after shops per `docs/LOSTCITY_PORT_TRIAGE.md`, not something
this feature adds.

---

## 4. What this doc does not cover

- `script_5463`'s real "Set Goal" target, `~script7529`'s hiscores-request
  body, and `script7166`/`script7133`'s bodies — all missing from this
  decompiled corpus; re-decompile each from the live cache before
  implementing against this doc.
- The Ground Items highlighter's persistence story (§3, panel B) — confirmed
  separate from the loot log, not traced further.
- Full parity of the loot tracker's GE-value/HA-value computation — the
  native 7-tuple field layout was inferred from usage, not confirmed against
  a definition.

---

## 7. What landed, 2026-08-02 (the CS2-VM half)

Everything in this section was verified in the headless client, not in a log.
The commands are at the end.

### 7.1 The signature bridge — the change that matters

This repo has **two** CS2 signature tables and, until now, no path between
them:

| table | who reads it | what it knows |
|---|---|---|
| `src/cs2vm2/cs2vm2_opcode_stack.gen.h` | the **client VM** at run time | whatever `cs2_opcode.h` doc comments, `MANUAL_STACK` and the name heuristics say |
| `3rd/rscache/src/cs2/cs2_command.gen.h` | the **decompiler** (`cs2 decompile`) | vendored RuneStar `Command.kt` layered with `3rd/rscache/tools/cs2/local_commands.py`, i.e. ~120 arities this repo established by corpus inference |

`gen_opcode_stack.py` read only the first two of `cs2_opcode.h`,
`cs2_opcode_meta.c`. And `cs2_opcode_meta.c` declares
`cs2_opcode_meta_table_size = 7511`, so **every opcode ≥ 7511 had no name for
the heuristics to match** and fell out as `known = 0` — which is precisely the
range the three chrome panels live in. `[7604]` is the clean proof: the
decompiler gives it `(STRING)->(INT)`, and the client VM still aborted on it.

`gen_opcode_stack.py` now reads `cs2_command.gen.h` as a fourth source. It
resolves each row's `arg_off/arg_count` and `def_off/def_count` against
`cs2_proto_pool`, counts STRING-stack prototypes against everything else, and
takes only `RSCACHE_CS2_CMD_BASIC` rows — the other kinds (DB_FIND,
DB_GETFIELD, CLIENTSCRIPT, PARAM) are variadic by construction, so their
counts there are placeholders rather than signatures, and all of them already
have dedicated dispatch. The STRING-proto set is read out of `cs2_types.c`
rather than transcribed, because `RSCache_CS2_TypeStack` is a one-liner
(`cs2_types.c:109`: only `TYPE_STRING` is a string-stack type) and a new named
string prototype must not be silently counted as an int.

**335 opcodes gained a real signature.** Including every one this doc named,
plus `abs`, plus the shared-chrome tooltip family (6751/6752/6753, 6801/6802,
6851/6852, 6900, 7003, 7031-7033) that is nine hops from *every* panel's
onload and was therefore a latent abort on hover paths well beyond these three.

### 7.2 Two deliberate deviations from the obvious design

**(a) The bridge is additive, not an override.** It fills only rows that
nothing else established (`known == 0`). It is ranked below the name
heuristics, not above them. The reason is measured: **53 opcodes disagree
between the two tables**, and nearly all of them are `CC_SET*`/`IF_SET*` rows
whose `(0,0,0,0)` heuristic value is a *marker* rather than a claim — those
opcodes have dedicated dispatch in `cs2vm2.c` that owns the stack, so the
table row only feeds the debug trace. Rewriting them from a table the runtime
does not consult would change nothing at best and silently re-shape a working
UI path at worst. The asymmetry is the whole argument: filling a `known = 0`
row can only replace a hard abort with a signature; overriding a `known = 1`
row can replace working behaviour with corruption.

Every disagreement is instead recorded in `BRIDGE_CONFLICTS_OK` with its
class, and **generation fails** on any new or stale entry. That is the
cross-check guard this work needed — a disagreement means one of the two
tables is wrong about a real opcode, which is exactly the silent-wrong-arity
bug `local_commands.py:16-18` warns about.

Three classes of conflict, for the record: (i) the ~40 `CC_SET*`/`IF_SET*`
marker rows; (ii) five rows where *ours* is the measured one and the vendored
table is stale or era-wrong — `4201`/`4202` (the op slot is the **operand**,
not a stack pop), `3800`/`3850` (no-arg in this cache), `8000` (arrays are
STRING-stack handles at rev 239, per `docs/cs2-arrays-are-handles.md`); and
(iii) ~10 where the vendored table looks right and ours is a heuristic guess,
left alone because nothing reachable exercises them and each needs its own
witness before it moves.

**(b) `known` is now tri-state, and the new state reports itself.** A bridged
row is written `known = 2`: the signature is real, but *nothing in this repo
implements the opcode*, so `StackMetaStub` balances the stack and pushes zeros
and empty strings. That is a **plausible wrong answer**, which is a harder
failure to find than an abort — so the runtime prints one line per opcode, the
first time it reaches each, **ungated by any env var**:

```
cs2-stub: opcode 7809 has an inherited signature (0,0,1,0) but no implementation
          — stack balanced, results faked (script 7469 pc 38)
```

Unlike `TORIRS_CS2_SURVEY` this is not opt-in, because a silent wrong answer
is not something you should have to go looking for. The list a run prints is
the honest inventory of what it faked its way through.

### 7.3 `abs` (4035) — the whole of xptracker's client-side gap

`abs` had **no case anywhere in `src/cs2vm2/`** (`grep CS2_OP_ABS` hit only
the `#define`). Before the bridge it aborted; *after* the bridge it would have
answered 0 for every input — the exact failure mode §7.2(b) exists to make
visible. So it is implemented (`CS2VM2_Op_Abs`, beside `BitCount`) and listed
in `MANUAL_STACK` so it reads `known = 1` ("we know what it does") and not the
bridge's 2 ("we only know its shape"). `INT_MIN` wraps rather than saturating,
matching the Java client, and is written as an explicit unsigned negation
rather than UB. Four cases added to `make -C src test-cs2-math`.

### 7.4 Content: the panels are reachable at all now

`OSRS-Content/osrs239-content/server/scripts/interface_chrome/scripts/chrome_panels.rs2`
adds `[debugproc,xptracker]`, `[debugproc,loottools]`, `[debugproc,hiscores]`,
each opening its panel into `toplevel_osrs_stretch:mainmodal` type 0 — the
same slot and type the bank, skill guide, slayer rewards and farming view use,
which is what gives them a working X and Escape with no code. Every id is
resolved **by name** through the pack. Nothing is added to
`player/configs/gameframe.enum`: none of the three is a login-mounted slot in
the real client, correctly.

This is not cosmetic. Before it, **all 36 aborting opcodes were latent** — a
clean boot printed zero `cs2-survey` lines and exited 0 purely because nothing
could reach the panels. No claim about the VM work was testable until the
panels could be opened.

### 7.5 Measured result, per panel

| panel | opens | draws | exit | what the run says |
|---|---|---|---|---|
| `xptracker` 729 | yes | yes (71k px differ vs no-panel) | 0 | **zero** `cs2-stub` lines — its open path reaches no un-implemented opcode at all |
| `hiscores` 894 | yes | yes (104k px differ) | 0 | one `cs2-stub` line: opcode **7809**, `(0,0,1,0)`, in script 7469 |
| `loottools` 650 | yes | — | **134** | aborts at opcode **7601**, which has no arity in *either* table (tier B, §7.6) |

The hiscores row is the proof the bridge does work, and it was obtained the
way PORTING_GUIDE §2.5 demands — by making the assertion fail. A control
binary built with all 335 bridged rows flipped back to `known = 0` **aborts
(exit 134) at opcode 7809** on the identical run. With the bridge it exits 0
and prints the one honest line. A green run against a panel nothing opens
would have proved nothing; this is the same run, differing only in the table.

Note what the xptracker row does *and does not* say: the panel opens and draws
cleanly, and `abs` is implemented, but **the open path does not reach `abs`**
in a plain open — script5380 is the actions-to-goal estimator and needs a goal
set. `abs` is verified by unit test, not by that BMP. Saying otherwise would
be claiming verification that was not run.

### 7.6 Still missing — the honest list

- **Tier-B arities: 2624, 7407, 7408, 7601, 7602, 7605, 7606, 7630, 7800,
  7803, 7805, 7807, 7813, 7814, 7816, 7820.** These have no signature in
  *either* table, so the bridge cannot reach them and they still abort. They
  are what blocks loottools, and they also break `cs2 decompile` — which is
  why the closures measured for these panels are **lower bounds**: 7 hiscores
  scripts and 5 loottools scripts still fail to decompile, so their callees
  were never walked. Establishing them is `local_commands.py`'s documented
  method (override sweeps, call-site reading, `cs2 infer-arity` rounds) and is
  not done here. Two of them (`7800` = `(STRING, INT)`, `7408` =
  `(INT, STRING, INT, INT)->(INT)`) cannot even be *tried* via
  `cs2 decompile --override`, which can only express ints-then-strings.
- **Host implementations for the bridged families.** 7809/7811 (hiscores
  request status and result string), the 7600 loottools store family, and
  `NC_PARAM`/`LC_PARAM` (6513/6514) all now have signatures and no behaviour.
  They report themselves via `cs2-stub`.
- **The hiscores lookup cannot be made to work and should not be faked.**
  `grep -rn hiscore src/net/rev/` is zero in every revision: the real client
  goes out of band over HTTP. The correct end state is a panel that opens and
  reports "lookup failed" through `_7809`/`_7811`, with the string coming from
  content (PORTING_GUIDE §2.4 item 2), not a literal in C. Fabricating ranks
  would be inventing data.
- **Populating loottools** is greenfield client-engine work (a native loot
  store fed from the kill/pickup path) — but §3's open question is now
  settled: decompiling script 7166 under trial overrides reads as
  `begin→count / index→id / id→name` against a **client-native list store**, so
  **no new game packet is needed**.
- **The varps** (`xpdrops_*` 1228-1275, `loottools_varp1` 3798,
  `settings_varp_ehc_5` 3795) are still undeclared under `server/scripts/`.
  They earn the tracker's goal *display* only; the Set Goal *button* stays
  dead for the reason in the header.
- **`cs2_opcode_meta_lookup` returns entry [0] for any opcode ≥ 7511** —
  `PUSH_CONSTANT_INT`, operand INT32. Every opcode in these three panels'
  graphs happens to be INT32-operand so it does not bite here, but an opcode
  ≥ 7511 whose real operand width is INT8 would mis-advance the bytecode
  reader. A latent decoder hazard, independent of this work, left as-is.

### 7.7 Reproducing

```sh
# regenerate the table (never hand-edit the .gen.h)
python3 src/cs2vm2/gen_opcode_stack.py
#   -> "937 opcodes with metadata, 933 known; 335 inherited from
#       cs2_command.gen.h [table size 8023], 53 acknowledged conflicts"

make -C src PLATFORM_OBJ_BASE=/tmp/wfobj_vm EMBED_SERVER=1 -j4
make -C src mock230-scripts        # the debugprocs live in the script pack

for p in xptracker loottools hiscores; do
  SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=1500 TORIRS_NET_CHEAT="$p" \
    TORIRS_NET_DEBUG=1 TORIRS_EXIT_BMP=/tmp/$p.bmp \
    ./src/torirs --manifest manifest_osrs230_embed.ini --user testbl --pass test
done
```

`TORIRS_NET_DEBUG=1` is what prints the client-side `if-opensub: mount
iface=<id>` line that proves the panel actually mounted; without it the run is
silent about that and a failed mount looks identical to a successful one.
`TORIRS_CS2_SURVEY` must be **off** — its own comment says a survey run is not
trustworthy.

To prove the assertion can still fail, flip the bridged rows back:

```sh
python3 - <<'PY'
import re; p="src/cs2vm2/cs2vm2_opcode_stack.gen.h"; t=open(p).read()
open(p,"w").write(re.sub(r"(\[\d+\] = \{ \d+, \d+, \d+, \d+, )2( \},)", r"\g<1>0\g<2>", t))
PY
# rebuild, rerun hiscores -> exit 134, "unimplemented opcode 7809"
python3 src/cs2vm2/gen_opcode_stack.py   # restore
```
