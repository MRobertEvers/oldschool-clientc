# Chrome panels: XP Tracker, Hiscores, Loot Tracker

> Third round of the discovery pass (`docs/PORTING_GUIDE.md` §5.3), companion
> to `docs/questlist_chatmenu_levelup.md`, `docs/shop_server_reqs.md`, and
> `docs/friends_pm_chat_server_reqs.md`. Unlike the first two rounds, all
> three interfaces here are **modern (2018+) OSRS client features with no
> LostCity precedent** — confirmed per-interface below, not assumed. That
> changes the shape of the finding: mostly these need little or no new
> *server* work, and the real gaps are either client-side CS2 host-op
> implementations or corpus gaps in the decompiled script dump.

## 0. Status at a glance

| interface | id | status | what's missing |
|---|---|---|---|
| `xptracker` | 729 | **needs almost nothing from the server** | one small `.varp` overlay for "Set Goal"; everything else already rides on landed `STAT_XP`/`STAT_BASE`/timer machinery |
| `hiscores` | 894 | **not a server feature at all** | real client does an out-of-band HTTP lookup; the actual gap is 9 unimplemented CS2 host ops in `cs2vm2.c` |
| `loottools` | 650 | **corpus gap blocks the real question** | population routines (`script7166`/`script7133`) are missing from the decompile; on current evidence no new packet is needed, but that's inferred, not confirmed |

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
case** in this decompile's handler (`script_5463.cs2`'s switch jumps from
case 5 straight to case 8) — same corpus-gap class as
`docs/questlist_chatmenu_levelup.md` §1.3's missing `questlist_draw`.
Re-decompile before assuming the client half is genuinely absent.

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
