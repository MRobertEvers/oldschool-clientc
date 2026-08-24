# CS2 opcode groups — audit against the Java deob and the NXT decompile

> **Status: plan, nothing applied.** This records what the two reference
> clients say the CS2 opcode space is *shaped* like, what this tree does
> instead, and the changes that would close the gap. No generator, table or
> header has been touched.

CS2 opcodes are not a flat 8000-entry namespace. Both reference clients
dispatch them through a **ladder of century-wide groups**, and the group an
opcode lives in decides its name prefix, whether it has a `.`-form twin, and
whether it takes its target component off the stack or from the active slot.
This tree reproduces the *names* faithfully and the *grouping* not at all —
there is no group table anywhere in `src/cs2vm2/`, `3rd/rscache/src/cs2/` or
either generator. Everything below follows from that.

---

## 1. What the references say the groups are

### 1.1 NXT — the group names are in the binary

`osclient-216-mac.c` (Ghidra dump of the NXT Mach-O, symbols intact) carries
`jag::oldscape::ScriptRunnerImpl` member names. The dispatch functions are
named after their ranges, so Jagex's own grouping is readable directly:

```sh
grep -ao "ExecuteCommand[0-9]*To[0-9]*\(Or[0-9]*To[0-9]*\)\?" \
    ~/Documents/git_repos/osclient_decompile/osclient-216-mac.c | sort -u
```

45 handlers, of which six are explicitly *paired* — one function serves a
`cc_` century and the `if_` century 1000 above it:

| NXT handler | serves |
| --- | --- |
| `ExecuteCommand100To999` | 100–999 |
| `ExecuteCommand1000To1099Or2000To2099` | 1000–1099 **and** 2000–2099 |
| `ExecuteCommand1100To1199Or2100To2199` | 1100–1199 **and** 2100–2199 |
| `ExecuteCommand1200To1299Or2200To2299` | 1200–1299 **and** 2200–2299 |
| `ExecuteCommand1300To1399Or2300To2399` | 1300–1399 **and** 2300–2399 |
| `ExecuteCommand1400To1499Or2400To2499` | 1400–1499 **and** 2400–2499 |
| `ExecuteCommand1900To1999Or2900To2999` | 1900–1999 **and** 2900–2999 |
| `ExecuteCommand1500To1599` … `1800To1899` | one century each |
| `ExecuteCommand2500To2599` … `2800To2899` | one century each |
| `ExecuteCommand3100To3199` … `4200To4299` | one century each |
| `ExecuteCommand5000To5099`, `5300To5399`, `5500To5599`, `5600To5699` | one century each |
| `ExecuteCommand6200To6299`, `6500To6599` … `7200To7299` | one century each |
| `ExecuteCommand7400To7499`, `7500To7599`, `7600To7699`, `7800To7899`, `8000To8099` | one century each |
| `ExecuteOtherCommand` | everything else |

The paired names are the load-bearing part: **the `cc_` form and the `if_`
form of the same command are one handler**, selecting active-component vs
stack-component from the operand byte. That is the same fact `RENAMES.md`
records for the Java client as `activeComponentCc` / `activeComponentIf`.

### 1.2 The Java deob — the same ladder, merged where empty

`Deobfuscator/src_osrs239_rl1_12_33/deob/Statics.java:28060`,
`method6889` (`dispatchOpcodeGroup` in `instr/RENAMES.md`), re-derived rather
than read by eye:

```sh
sed -n '28060,28450p' Statics.java |
  grep -oE "var0 < [0-9]+|return method[0-9]+" | paste - -
```

52 rungs. Each rung's range is `[previous bound, this bound)`. It agrees with
NXT everywhere; where NXT has a per-century function the deob sometimes merges
several adjacent centuries into one handler because the centuries between them
are **empty**:

| deob rung | NXT equivalent | note |
| --- | --- | --- |
| 100–999 → `method4548` | `ExecuteCommand100To999` | identical |
| 1000–1099 → `method5842`, 2000–2099 → `method5842` | one paired NXT function | *same handler for both* — pairing confirmed independently |
| 1100/2100 → `method4754`, 1200/2200 → `method5661`, 1300/2300 → `method12438`, 1400/2400 → `method4487` | paired | " |
| 1900/2900 → `method1005` | paired | " |
| 3000–3199 → `method6397` | only `3100To3199` exists in NXT | 3000–3099 is empty in both tables |
| 4300–5099 → `method11780` | only `5000To5099` | 4300–4999 empty |
| 5100–5399 → `method9060` | only `5300To5399` | 5100–5299 empty |
| 5400–5599 → `method4488` | only `5500To5599` | 5400–5499 empty |
| 5700–6299 → `method6341` | only `6200To6299` | 5700–6199 empty |
| 6300–6599 → `method5150` | only `6500To6599` | 6300–6499 empty |
| 7200–7499 → `method12357` | `7200To7299` + `7400To7499` | 7300–7399 empty |
| 7700–7999 → `method11128` | only `7800To7899` | implements 7900/7901 only |
| 7600–7699 → `method10020` | `7600To7699` | **bare `return 2`** — unimplemented in the Java client |
| 8100–8599 → `method4514`, 8600–13999 → `method6817` | absent | no opcode in this tree reaches either |

**The two references never contradict each other.** The century is the unit;
the merged deob rungs are exactly the ranges whose interior centuries are
empty. So the century is the right granularity for a group table here.

### 1.3 The group table both references imply

Verified against our own two tables (every id we carry falls inside one of
these, and no id falls in a gap):

| group | prefix convention | what it is |
| --- | --- | --- |
| 100–999 | `cc_`/`if_`/`overlay_` | component construction and addressing (`cc_create`, `cc_find`, `if_find`, the param receivers 204–222) |
| 1000–1099 / 2000–2099 | `cc_set…` / `if_set…` | geometry (position, size, hide, clickthrough) |
| 1100–1199 / 2100–2199 | `cc_set…` / `if_set…` | appearance (colour, graphic, model, text, the `input_` family) |
| 1200–1299 / 2200–2299 | `cc_setobject…` | object/head slots |
| 1300–1399 / 2300–2399 | `cc_setop…` | ops, drag, opkeys |
| 1400–1499 / 2400–2499 | `cc_seton…` | listener registration (CLIENTSCRIPT kind) |
| 1500–1599 / 2500–2599 | `cc_get…` / `if_get…` | geometry reads |
| 1600–1699 / 2600–2699 | `cc_get…` / `if_get…` | appearance reads |
| 1700–1799 / 2700–2799 | `cc_get…` / `if_get…` | inventory-slot and identity reads, component params |
| 1800–1899 / 2800–2899 | `cc_get…` / `if_get…` | op/target reads |
| 1900–1999 / 2900–2999 | `cc_` / `if_` | component actions (`callonresize`, `triggerop`) |
| 3000–3099 | — | **empty** |
| 3100–3199 | bare | dialog resume, local player, mobile, notifications |
| 3200–3299 | bare | sound, client/device/game options |
| 3300–3399 | bare | clock, inv, stat, coord, map, mouse |
| 3400–3499 | `enum` | enum lookups |
| 3500–3599 | `key…` | key state |
| 3600–3699 | `friend_`/`ignore_`/`clan_` | social |
| 3700–3799 | bare | (three ids, all unidentified) |
| 3800–3899 | `activeclan…`/`affinedclan…` | clan settings/channel |
| 3900–3999 | `tradingpost_`/`stockmarket_` | GE |
| 4000–4099 | bare | integer maths and bit ops |
| 4100–4199 | bare | string maths |
| 4200–4299 | `oc_` | obj config |
| 4300–4999 | — | **empty** |
| 5000–5099 | `chat…` | chat |
| 5100–5299 | — | **empty** |
| 5300–5399 | bare | window mode |
| 5400–5499 | — | **empty** |
| 5500–5599 | `cam_` | camera |
| 5600–5699 | bare | logout, federated login |
| 5700–6199 | — | **empty** |
| 6200–6299 | `viewport_`/`uizoom_`/`safearea_` | viewport metrics |
| 6300–6499 | — | **empty** |
| 6500–6599 | `worldlist_`/`nc_`/`lc_`/`oc_`/`struct_`/`mobile_` | world list, config params, device |
| 6600–6699 | `worldmap_`/`mec_` | world map |
| 6700–6799 | `clientop_`/`nc_` | client-side op overrides |
| 6800–6899 | bare | (mostly unidentified; `loc_find`) |
| 6900–6999 | bare | local player / login |
| 7000–7099 | `highlight_` | entity highlighting |
| 7100–7199 | `minimenu_` | minimenu introspection |
| 7200–7299 | `overlay_`/`minimap_` | scripted entity overlays |
| 7300–7399 | — | **empty** |
| 7400–7499 | bare | (19 ids, all unidentified) |
| 7500–7599 | `db_` | DB tables |
| 7600–7699 | `loot_` | loot tracker — **the Java client does not implement this group** |
| 7700–7799 | — | **empty** |
| 7800–7899 | bare | (20 ids, all unidentified) |
| 7900–7999 | bare | 7900/7901 only |
| 8000–8099 | `array_` | string/array helpers |

---

## 2. What this tree does today

**Two independent name tables, generated by two independent generators, from
one shared vendor file plus two separate local overlays.**

| | opcode ids | placeholders (`_NNNN`) | generator | local overlay |
| --- | --- | --- | --- | --- |
| `src/cs2vm2/cs2_opcode.h` (+ `cs2_opcode_meta.c`, `dat2a_cs2_opcode_decode.c`) | 880 | 105 | `tools/cs2_gen_opcodes/gen_opcodes.py` | `local_opcodes.py` |
| `3rd/rscache/src/cs2/cs2_command.gen.h` | 1071 | 401 | `3rd/rscache/tools/cs2/gen_cs2_tables.py` | `local_commands.py` |

`gen_cs2_tables.py:38-42` deliberately reads
`tools/cs2_gen_opcodes/vendor/Opcodes.kt` "keeping one source of truth for
what is opcode 105" — but it reads only the **vendored** file, never
`local_opcodes.py`. So the sharing covers upstream's names and none of ours.

Neither generator has a group concept:

- `gen_opcodes.py:emit_header` walks the ids in ascending order and emits one
  `#define` each. The only structure is `SECTION_COMMENTS`, an 11-entry
  free-form banner map (`86, 1430, 1436, 2133, 2430, 2704, 3170, 4030, 6600,
  6910, 7500`) — of which exactly one, 6600, is a group banner.
- `gen_cs2_tables.py` emits one flat `[id] = { … }` row per opcode.
- `src/cs2vm2/cs2vm2.c:362` is a single `switch( opcode )` over 12 000 lines.
- `CS2_OpCode_String` (`cs2_opcode_meta.c:7535`) answers `"_unknown"` for
  anything off the end of the table, losing the group.

The one place a group boundary is encoded at all is a heuristic:
`gen_cs2_tables.py:374`, `dot = 100 <= opcode < 2000`.

---

## 3. Discrepancies

### D1 — no group table exists; the grouping is not expressible

Nothing in the tree can answer "which group is opcode 6221 in", so nothing can
check that a name belongs to its group, that a `cc_` op has an `if_` twin, or
that a new id lands in a century the reference clients actually dispatch. Every
discrepancy below is downstream of this one.

### D2 — the two name tables have drifted 105 ids apart

**103 ids the VM names and the compiler still calls `_NNNN`.** A script using
one of these decompiles to `_7003(…)` and cannot be recompiled by name, even
though `cs2_opcode.h` knows it is `HIGHLIGHT_NPC_GET`:

| group | count | ids |
| --- | --- | --- |
| 100–199 | 2 | 103, 104 |
| 200–299 | 2 | 202, 203 |
| 1100/2100 | 2 | 1128, 2128 |
| 1300/2300 | 3 | 1308, 1309, 2309 |
| 1600–1699 | 1 | 1613 |
| 3100–3199 | 4 | 3129, 3138, 3139, 3140 |
| 3200–3299 | 2 | 3209, 3210 |
| 6200–6299 | 6 | 6210, 6212, 6220–6223 |
| 6600–6699 | 12 | 6600, 6615, 6618–6620, 6623–6627, 6638, 6698 |
| 6700–6799 | 10 | 6700–6709 |
| 6800/6900 | 2 | 6803, 6951 |
| 7000–7099 | 24 | the `HIGHLIGHT_*` SETUP/GET/CLEAR rungs |
| 7100–7199 | 9 | 7100–7105, 7108–7110 |
| 7200–7299 | 13 | 7200–7214, 7252 |
| 7600–7699 | 3 | 7603, 7609, 7623 |
| 8000–8099 | 8 | 8000, 8003, 8007, 8018, 8019, 8022–8024 |

**Two ids where both tables have a name and the names disagree:**

| id | `cs2_opcode.h` | `cs2_command.gen.h` | why |
| --- | --- | --- | --- |
| 4030 | `SETBIT_RANGE_VALUE` | `setbit_range_toint` | `LOCAL_ALIASES` keeps both in C; only the vendor spelling reaches the compiler. Two spellings for one opcode is a coin flip for a reader. |
| 7250 | `MINIMAP_SETZOOMABLE` | `setminimaplock` | the **only** id where `local_opcodes.py` overrides a *real* vendor name rather than a `_NNNN` placeholder, and it does so with no doc comment saying what was measured. |

**194 ids the compiler table carries and `cs2_opcode.h` does not**, so the VM
cannot route them by name and `CS2_OpCode_String` prints `_unknown`:
`0:1, 100:2, 200:13, 1100:10, 1200:5, 1400:2, 1500:1, 1600:2, 1700:2, 2200:2,
2300:1, 2500:1, 2600:1, 2700:2, 3100:32, 3200:7, 3300:5, 3900:2, 4000:1,
4100:3, 4200:2, 6500:1, 6700:4, 6800:10, 7200:2, 7400:19, 7600:24, 7800:20,
7900:2, 8000:15` (group:count). One id goes the other way: **6901**, in
`cs2_opcode.h` only.

### D3 — upstream `Opcodes.kt` maps two names to opcode 202

```kotlin
const val _202 = 202
const val _203 = 202   // <- should be 203
```

The single duplicate in the whole vendor file. `opcodeNames` is built by
value, so 202 prints as `_203` and 203 has no vendor name at all — visible in
`cs2_command.gen.h` as `[202] = { "_203" …}, [203] = { "_203" …}`. Our VM
table sidesteps it by naming both (`OVERLAY_FIND` / `OVERLAY_CC_FIND`) but the
vendored typo is still in the tree and still feeds the compiler.

### D4 — `dot_capable` comes from a range heuristic, not from the group table

`gen_cs2_tables.py:374` sets `dot = 100 <= opcode < 2000` for every opcode
whose signature came from the stack table, and CLIENTSCRIPT-kind opcodes get
`dot=False` unconditionally (`put(name, "CLIENTSCRIPT")`, line 328). Both
diverge from the reference dispatch:

- **43 `cc_`-named opcodes are marked not-dot.** 35 of them are the listener
  family — 1400–1412, 1414–1425, 1427–1431, 1433, 1436–1439 — which NXT
  dispatches through **one** function with its `if_` twins
  (`ExecuteCommand1400To1499Or2400To2499`) precisely because the `cc_` form
  *is* the dot form. The other eight are `cc_deleteall` (102), six
  `cc_input_set*` ops (1137, 1139, 1140, 1141, 1143, 1145) whose neighbours in
  the same century are dot, and `cc_triggerop` (1928).
- **Two `if_` opcodes are marked dot**: `if_find` (201) — which the
  `100 <= opcode < 2000` rule catches by accident — and `if_callonresize`
  (2927). An `if_` op is by definition the *named* form.

A wrong `dot_capable` is not a loud failure: it changes how many values the
compiler expects on the stack for that call.

### D5 — invented names that break their group's convention

All of these come from `local_opcodes.py::LOCAL_NAMES` and none carry an
`opcode_docs.py` entry, so the header emits a bare `#define` with no evidence:

| id | name | problem |
| --- | --- | --- |
| 1309 | `CC_OP1309` | placeholder in a spelling the tree uses nowhere else. The established convention for "identified id, unknown meaning" is `_NNNN` (7040–7044 do it correctly, with a comment saying why). |
| 2309 | `IF_OP2309` | same, and it is the twin of 1309 — so the pair encodes "unknown" twice, two different ways from `_1309`/`_2309`. |
| 6231 | `SAFEAREA_GETMAXY_ALT` | `_ALT` is not a suffix any other name in the tree uses. It claims 6231 duplicates 6223 without saying how that was established. |
| 6232 | `CAM_GETYAW` | puts a `CAM_*` name in the 6200 viewport group; every other `CAM_*` op (5504–5506, 5530–5531) is in the 5500 camera group. Either the name or the reading is wrong. |
| 6698 | `WORLDMAP_ELEMENTCOORD1` | numeric suffix on the *lower* id of a pair whose upper id (6699) is the unsuffixed `WORLDMAP_ELEMENTCOORD`. Reads as "the first one" when it is the second. |
| 6910 | `LOGIN_INT24` | names the opcode after its C type. It is the only name in the table that does. |

### D6 — twin names whose suffixes disagree

The `cc_`/`if_` pairing rule says id *N* and *N*+1000 are one command in two
forms, so their suffixes should match. Three pairs in `cs2_opcode.h` do not:

| pair | names | verdict |
| --- | --- | --- |
| 1309 / 2309 | `CC_OP1309` / `IF_OP2309` | D5; the ids are baked into the names, so the suffixes can never match |
| 1702 / 2702 | `CC_GETID` / `IF_HASSUB` | **not a discrepancy** — the pairing stops at 1700; both references give 2702 its own meaning |
| 1704 / 2704 | `CC_SETCOMPONENTPARAM` / `IF_SETPARAM` | genuinely different commands that happen to be 1000 apart. 2704 also carries a legacy alias `IF_HASCHILD_MODAL` at the same id (`cs2_opcode.h:1556`), left from a rev-634 misreading. |

And `CC_CLEAROPSUBMENU` (1310) has no `IF_` twin at 2310 in either table,
while every other 1300-group `cc_` op does.

### D7 — the tree carries names for a group the reference clients do not run

`cs2_command.gen.h` has 27 ids in 7600–7699 and `cs2_opcode.h` names three of
them `LOOT_*`. `RENAMES.md` records that the Java client's handler for that
range (`method10020`) is a bare `return 2` — i.e. `throw new
IllegalStateException()`. NXT has an `ExecuteCommand7600To7699`, so the group
is real, but any behaviour we implement there is unverifiable against the Java
client. The same applies to 7700–7999, where the Java handler implements only
7900 and 7901. Nothing in the tree says so at the point of use.

### D8 — nothing checks any of this

There is no test, no generator assertion and no CI step that compares
`cs2_opcode.h` with `cs2_command.gen.h`. `tools/cs2_gen_opcodes/validate_cache.py`
scans a cache for opcodes the *meta* table lacks; it never looks at the
compiler table. Every drift above accumulated silently.

---

## 4. Necessary changes

Ordered so each step's gate exists before the step that needs it.

### C1 — one group table, generated, shared

Add `tools/cs2_gen_opcodes/opcode_groups.py`: an ordered list of
`(lo, hi, slug, twin_lo, summary)` transcribed from §1.3, with the derivation
commands from §1.1 and §1.2 in the module docstring so it can be re-derived
rather than trusted.

Emit from it, in `gen_opcodes.py`:

- a banner comment above the first `#define` of each group, replacing the ten
  ad-hoc `SECTION_COMMENTS` entries that are really group banners;
- `enum CS2_OpcodeGroup` plus `CS2_OpcodeGroupOf(int opcode)` and
  `CS2_OpcodeGroupName(enum CS2_OpcodeGroup)` in `cs2_opcode_meta.h`;
- have `CS2_OpCode_String` fall back to the group slug plus the id
  (`"worldmap/6641"`) instead of `"_unknown"`, so a trace of an unhandled
  opcode still says which family it was.

Export the same table as JSON next to the generated headers so
`gen_cs2_tables.py` and the JS side (`tools/cs2dom`) read it rather than
re-encoding it.

**Gate:** a generator assertion that every id in either table falls inside a
declared group, and that no two groups overlap.

### C2 — one local-name layer

Make `gen_cs2_tables.py` read `tools/cs2_gen_opcodes/local_opcodes.py::LOCAL_NAMES`
the way it already reads that directory's `vendor/Opcodes.kt`
(`gen_cs2_tables.py:38-42`), and delete the two entries in
`3rd/rscache/tools/cs2/local_commands.py::LOCAL_NAMES` that duplicate it
(210, 2704) after moving anything they know into the shared layer. That closes
103 of the 105 mismatches in D2 in one regeneration.

Resolve the two real disagreements first, because the merge has to pick one
spelling:

- **4030** — keep the vendor `SETBIT_RANGE_TOINT` as canonical and demote
  `SETBIT_RANGE_VALUE` to the alias it already is, or drop the alias and fix
  its two C call sites. Do not keep both as peers.
- **7250** — either restore vendor `SETMINIMAPLOCK`, or keep
  `MINIMAP_SETZOOMABLE` **and** add the `opcode_docs.py` entry recording what
  established it. An override of a named vendor entry with no evidence is the
  one shape of change this table cannot absorb.

**Gate:** a check (test or generator assertion) that for every id present in
both tables the names are equal up to case. This is the test D8 says is
missing; write it before C2 so it fails first.

### C3 — the 194 compiler-only ids

For each, either add a `LOCAL_NAMES`/`opcode_docs.py` entry (when we know what
it is) or let it stay `_NNNN` — but emit the `#define` either way, so
`cs2_opcode.h` and `cs2_command.gen.h` have the *same id set*. `_7400` is a
name; `_unknown` is not. The 6901 asymmetry disappears with them.

**Gate:** extend C2's check to id-set equality, not just name equality.

### C4 — `dot_capable` from the group table

Replace `dot = 100 <= opcode < 2000` (`gen_cs2_tables.py:374`) with
`group.twin_lo is not None and group.lo <= opcode < group.hi`, and stop
special-casing CLIENTSCRIPT kind: whether a command takes a `.` form is a
property of its **group**, not of its kind. That fixes the 41 `cc_seton*` ops
and `cc_deleteall`.

`if_find` (201) and `if_callonresize` (2927) then fall out as not-dot
automatically. Both should still be checked by hand against
`ExecuteCommand100To999` / `ExecuteCommand1900To1999Or2900To2999` in the NXT
dump before the change lands, because a wrong `dot_capable` changes the
compiler's stack expectation silently (§D4).

**Gate:** an assertion that `dot_capable` implies the name starts `cc_` or
`overlay_` or `_`, and that every `cc_`-named op in a twinned group has it.

### C5 — rename the six off-convention names

`CC_OP1309` → `_1309`, `IF_OP2309` → `_2309`, `SAFEAREA_GETMAXY_ALT` → `_6231`,
`CAM_GETYAW` → `_6232`, `WORLDMAP_ELEMENTCOORD1` → `_6698`, `LOGIN_INT24` →
`_6910`, each with a `local_opcodes.py` comment saying what is and is not known
— exactly the shape 7040–7044 already use. Any of the six may keep its name
instead **if** an `opcode_docs.py` entry is written that says what established
it; the rule to enforce is *a name or a placeholder, never a guess wearing a
name*. `CAM_GETYAW` in particular needs its group checked before either
outcome: a `CAM_*` op outside the 5500 camera group is a claim about the
client, not a spelling choice.

Keep the `IF_HASCHILD_MODAL` alias at 2704 but move the "previously
misidentified" note from the doc comment into a one-line `LOCAL_ALIASES`
comment, so the alias carries its own reason.

**Gate:** an assertion that every name is either `_<id>` or has an
`OPCODE_DOCS` entry.

### C6 — mark the groups the references do not implement

Add an `implemented_in` field to the group table (`nxt`, `deob`, `both`,
`neither`) and emit it into the banner. 7600–7699 is `nxt` only; 7700–7999 is
`nxt` only and partial. Cite `RENAMES.md`'s sixteen ids that reach no handler
in the Java client (6758, 6803, 6859, 6951, 7043, 7451, 7600, 7627, 7800,
7803, 7804, 7805, 7807, 7813, 7814, 7820) in the same place, so anyone
implementing one knows there is no Java behaviour to compare against.

### C7 — fix the vendored typo, at the vendor boundary

`vendor/Opcodes.kt`'s `const val _203 = 202` is upstream's bug. Do not edit the
vendored file — add 202/203 to `LOCAL_NAMES` (they already are, in the VM
layer) and make the vendor parser **fail** on a duplicate value rather than
silently letting the later name win. Today the parser's dict-build hides it;
after C2 the same typo in a future vendor bump would silently rename one of our
opcodes.

---

## 5. What this plan does not touch

- **`cs2vm2.c`'s flat `switch`.** A jump table is the right shape for
  execution; grouping it buys nothing at runtime. The group table is for
  naming, generation and validation.
- **`tools/cs2dom`.** Its `OPS` vocabulary (`src/ops.js`) was checked against
  `cs2_command.gen.h` — all 27 command names resolve, and it emits only
  `if_*` (never-dot) forms, so C4 cannot change what it generates.
- **`src/engine/cs2_opcode_dialect.{c,h}`.** The RS2-on-dat2 remaps (51 →
  `SWITCH`, 4500 → `STRUCT_PARAM`) are per-id translations that cross group
  boundaries by design; they are correct as they stand and the group table
  should not be used to constrain them.
- **Signatures.** Pop/push counts and prototypes are a separate audit
  (`docs/CS2_REV239_ROUNDTRIP_QUEUE.md`). Nothing here changes an arity.
