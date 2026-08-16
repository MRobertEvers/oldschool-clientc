# Giving the spawned roster real stats

A plan to take the npcs that actually stand in the rev-239 world, read what the
OSRS Wiki says about each one, and write that down as content the server reads —
levels, bonuses, attack speed, aggression, and the per-npc behaviour the wiki
describes but a stat block cannot.

Everything in §1 was measured against the tree on 2026-08-12 before any of the
rest was written. **§0 is the result** — implemented the same day, against the
plan below with one architectural change (§0.3) the plan did not anticipate.

---

## 0. What actually shipped

1,278 of the 1,700 attackable spawned npcs now carry real hitpoints, levels,
attack/defence bonuses per combat style, attack speed and (for the ones the
wiki calls aggressive) a hunt range — sourced from the OSRS Wiki, joined by
cache npc id, not by name. 145 more already had hand-authored stats and were
left untouched; 263 have a wiki page that doesn't happen to list that specific
variant's id; 14 have no wiki page or an irreducibly ambiguous one (ids the
wiki lists under two different combat states at once — Vardorvis, Duke
Sucellus, the Leviathan, the Whisperer, all genuinely bespoke encounters
outside this plan's scope regardless).

### 0.1 The tools, roughly matching §7's order

| tool | does |
|---|---|
| `tools/wiki_npc_roster.py` | builds the 1,700-row roster (id, gameval, display name, combat level, size) from `dump_stats` + every `*.spawn` file → `wiki/npc_roster.csv` |
| `tools/wiki_fetch.py` | fetches wiki pages **by title, in batches of 50** — not the category crawl §3 originally proposed, see §0.2 — plus a disambiguation-resolution pass |
| `tools/wiki_infobox.py` | a real template/wikilink parser (brace-depth counting, not regex-over-the-page) → one dict per version block per `Infobox Monster` template, each carrying its own `id`/`idN` list |
| `tools/gen_npc_stats.py` | the join, the validation report, the ledger, and the compiled config — see §0.3 for why the config isn't `npc_stats.generated.npc` |

### 0.2 Fetch: targeted, not a category crawl

The roster names exactly which npcs matter — 552 distinct display names for
1,700 ids — so `wiki_fetch.py` fetches those titles directly
(`action=query&titles=...`, 50 per request, redirects resolved by the API)
rather than crawling `Category:Monsters` + `Category:Non-player characters`
and their 76 subcategories. ~12 requests instead of ~130, same 1 req/sec
etiquette, and it found every title on the first pass except the true
disambiguation pages.

That last case — "Cave goblin", "Warrior", "Soldier", 19 others — got its own
pass rather than being written off as residue: `wiki_fetch.py` extracts every
link off the disambiguation page, fetches all of them (460 candidate pages,
one more batch), and `gen_npc_stats.py` accepts a candidate only when its own
`id`/`idN` list literally contains the npc id being resolved — the same
id-exact rule as the main join, so a disambiguation guess can never bind the
wrong monster. That turned 21 dead ends into 16 resolved names.

### 0.3 The bug the plan didn't anticipate, and why the config is `combat_stats.generated.npc`

The plan (§4) assumed a second generated `.npc` file could sit beside
`npc_anims.generated.npc` the way `gen_npc_combat.py`'s own ledger already
coexists with hand-authored files. It cannot, safely, without more care than
"write a new file":

- `mock230_content_npc()` (mock230_content.c) resolves an npc id to the
  **first** `[gameval]` block found across every `.npc` file in the tree — two
  files naming the same npc do not merge, the second is simply never reached.
- `walk_configs()` visited directory entries in **`readdir()` order**, which
  makes no promise about anything — not alphabetical, not creation order,
  filesystem-dependent. A comment a few hundred lines away already assumed
  sorted order ("areas/ sorts before general/, which is precisely the order
  that happens"); nothing enforced it.
- The moment `combat_stats.generated.npc` (then still named
  `npc_stats.generated.npc`) landed beside `npc_anims.generated.npc` in the
  same directory, ~1,000 npcs that appear in both files started losing
  whichever file's fields lost the unordered race — observed as npcs falling
  back to human-default swing/block/death animations, or (for `bat`) losing
  its combat sounds entirely. `make test-mock230` caught this immediately: 7
  failing selftests, one of them a blunt `mock230_content_error_count() != 0`.

Three fixes, all necessary together:

1. **`walk_configs` now sorts** (`scandir` + `alphasort` instead of raw
   `readdir`) — src/net/mock/mock230_content.c. Makes the ordering every
   existing comment already assumed actually true, rather than true by
   filesystem coincidence.
2. **The compiled config is named `combat_stats.generated.npc`**, which sorts
   alphabetically before `npc_anims.generated.npc` in the same directory —
   so for every npc both files cover, this one is now the one the loader
   reaches first.
3. **It carries the union.** `gen_npc_stats.py` reads
   `npc_anims.generated.npc`'s own compiled lines for a gameval and folds them
   into its own block ahead of its own fields, so winning the load-order race
   doesn't cost that npc its animations and sounds — the block is a strict
   superset, not a second competitor.

The same discovery also found `find_authored_hitpoints`'s block-detection
regex (`[a-z0-9_]+`) was narrower than the real loader
(`mock230_content_section_header` accepts anything between `[` and `]`) — it
missed 63 npcs with hand-authored anim/sound/param overlays that state no
`hitpoints=`, and two npcs whose gameval contains a literal `+`
(`mmsnailround_red+black`). Broadened to match the real parser and renamed
`find_existing_blocks` — it now excludes **any** existing block, not just ones
with hitpoints, and is the reason 145 npcs are skipped in §0 rather than 80.

One pre-existing, unrelated gap remains after all of this:
`quest_mortton.varp`'s `wholeread=allow` key isn't implemented by the varp
text-config parser yet. Confirmed pre-existing (I never touched that file,
and it fails identically on a tree with none of this work applied) and out of
scope — a quest-varp whole-read feature has nothing to do with npc combat
stats. `make test-mock230` fails on that one line alone; everything this plan
touches is clean.

### 0.4 Style wiring: melee got it for free, ranged and magic did not

`~npc_meleeattack` (skill_combat/combat_stats.rs2), the default swing every
npc falls through to, already reads `npc_param(damagetype)` — so setting
`param=damagetype,<style>` from the wiki's `attack style` field was enough
for every stab/slash/crush npc. Ranged and magic are a different story: the
same default proc rolls accuracy off `npc_stat(attack)` and max hit off
`npc_stat(strength)` regardless of `damagetype` — reading the code confirmed
exactly what plan §5.2 predicted from the outside, that a Ranged- or
Magic-style npc left on the engine default fights with the wrong stat
entirely.

- **Ranged** already had a fully generic, param-driven proc —
  `~npc_rangeattack` (npc_combat_ranged.rs2) — needing only a
  `[ai_opplayer2,<gameval>]` binding. 57 npcs got one.
- **Magic** had no generic equivalent — every existing magic npc casts a
  named spell via a dbrow (`dark_wizard.rs2`, `melzar_the_mad.rs2`, ...), which
  a wiki-driven npc with no game-visible spell has no way to name. Added
  `[proc,npc_generic_magicattack]` (npc_combat_magic.rs2), structurally a
  mirror of `npc_rangeattack` rather than the dbrow-based casters: no spell
  table, just the npc's own `magicattack`/`magicdefence` params for the roll
  and a new flat `magic_maxhit` param (npc_combat.param) for the damage,
  sourced directly from the wiki's stated `max hit` rather than derived from a
  formula the wiki gives no ingredients for. 60 npcs got one.
- **10 ranged/magic npcs already have a bespoke `[ai_opplayer2,...]` or
  `[ai_applayer2,...]` script** (dark_wizard, chaos_druid, barbarian, ...) —
  `gen_npc_stats.py` detects and skips these; a generated binding would either
  collide with or silently shadow hand-written combat AI.
- `magic_maxhit` also needed registering in the *engine's* param whitelist
  (`apply_param` in mock230_content.c hand-lists every valid `param=` name
  separately from the compile-time param registry sscompile checks against) —
  a second, easy-to-miss place a new param has to be declared, found only
  because the selftest's content-error count didn't go back to zero after the
  first fix.
- 508 npcs' wiki page lists more than one attack style (hybrid attackers).
  Only the primary is wired; true multi-style AI is exactly the kind of
  bespoke, human-reviewed work §5.3 scoped out.

### 0.5 Aggression: already correct

Plan §5's biggest stated risk — "turning `huntmode=aggressive` on across the
roster without [a level cap] makes every low-level monster in the world
permanently hostile to a maxed player" — turned out to be based on an
assumption the plan never actually verified. `maybe_aggress()`
(mock230_combat.c, landed 2026-08-04) already implements the exact OSRS rule:
`if (mock230_combat_level(player) > npc_level * 2) return;`. No code change
was needed; `huntmode=aggressive` + `param=huntrange,5` (the wiki gives no
aggro-range number, so this is a stated engine default, not a wiki fact) went
out for every npc the wiki calls aggressive without further changes. The
~10-minute "goes passive if the player camps the spot" timeout is *not*
implemented and remains a real, separate, minor gap.

### 0.6 What's still open

- **263 "not_in_any_candidate" npcs** — a real wiki page exists (Zombie,
  Skeleton, Guard, Dagannoth, ...) but none of its version blocks' id lists
  include this specific spawned variant, usually because it's a
  quest/minigame reskin at a much higher combat level than the page
  documents. Not resolved by guessing; each is recorded in its own ledger
  file with the candidates that were considered and rejected.
- **Poison severity** is deliberately not wired — the wiki's `poisonous` field
  is a yes/no, not the numeric severity `poison_severity` (combat.param)
  wants, and the two aren't reliably derivable from each other. Recorded as a
  note in every affected ledger, per plan §2's "no destination yet" rule.
- **Attributes beyond Undead** (Demon, Dragon, Fiery, ...) have no server
  field to route to yet — same treatment, noted not dropped.
- **Drop tables, slayer data, bespoke boss scripts** — out of scope per plan
  §8, unchanged.

---

## 1. Where the roster stands today

| | count | how it was measured |
|---|---|---|
| distinct npc symbols spawned anywhere | **5,941** | first column of every `*.spawn` under `server/scripts` |
| …that resolve to a cache npc id | 5,502 | joined to `out/tmp5/npc_rigs.csv` (`gameval` → `npc_id`) |
| …that are **attackable** (`op` = Attack **and** `combat_level` > 0) | **1,700** | joined to `dump_stats --rev osrs239 cache.osrs239` |
| …of those, with `hitpoints=` authored anywhere | **80** | `[block]` scan of all 42 `*.npc` configs |
| …of those, with any `[ai_*,<name>]` script | 350 | trigger scan of all 1,819 `*.rs2` |
| …of those, named in `pack/npc.server` | 1,531 | `pack/npc.server` |

**1,620 of the 1,700 attackable npcs in the world run on engine defaults.** They
have the animation and sound work from `tools/gen_npc_combat.py`
(`npc_anims.generated.npc` covers 2,472 spawned npcs) and nothing else: no
hitpoints, no levels, no bonuses, no attack speed, no aggression. A level-995
Sotetseg and a level-2 goblin are the same fight.

Combat levels of the 1,700: 446 at 1–20, 425 at 21–50, 466 at 51–100, 288 at
101–200, 75 above 200.

## 2. What the wiki gives, and why the join is exact

`Infobox Monster` carries every field the server wants, and — decisively — it
carries **`id1 =`, `id2 =` … lists of cache npc ids per version block**:

```
|id1 = 3028,3029,3030,...,5203      |hitpoints1 = 5   |att1 = 1  |str1 = 1  |def1 = 1
|id2 = 5192,5193,5204,...,5208      |hitpoints2 = 5   |att2 = 3  ...
|id3 = 3045,3073,3074,3075,3076     |hitpoints3 = 12
|id4 = 3046                         |hitpoints4 = 16
```

So the mapping is **npc id → version block**, not name → page. No fuzzy
matching, no "which goblin is this", and the four goblin variants land on four
different stat blocks the way the game has them. Verified live against
`https://oldschool.runescape.wiki/api.php?action=parse&page=Goblin&prop=wikitext`.

The fields, and where each one goes:

| wiki field | destination | notes |
|---|---|---|
| `hitpoints`, `att`, `str`, `def`, `mage`, `range` | `hitpoints=` `attack=` `strength=` `defence=` `magic=` `ranged=` | first-class `.npc` keys, already in `content_fields.c` |
| `attbns`, `strbns` | `param=stabattack/slashattack/crushattack`, `param=strengthbonus` | split by `attack style` |
| `amagic`, `mbns` | `param=magicattack`, magic damage | `npc_magic_attack_roll` reads `magicattack` |
| `arange`, `rngbns` | `param=rangeattack`, `param=rangebonus` | `npc_ranged_maxhit` reads both |
| `dstab dslash dcrush dmagic drange` | `param=stabdefence/slashdefence/crushdefence/magicdefence/rangedefence` | all five declared in `combat.param` |
| `attack speed` | `param=attackrate` | ticks; engine default is 4 |
| `attack style` | `param=damagetype` | also picks which `*attack` bonus `attbns` fills |
| `aggressive` | `huntmode=aggressive` + `param=huntrange` | see §5 on the level-based aggro rule |
| `respawn` | `respawnrate` | wiki states ticks |
| `size` | **validate only** — the cache states it | a disagreement is a finding, not an edit |
| `combat` | **validate only** against `combat_level` | ditto; it is also how a wrong id list is caught |
| `max hit` | **validate only** | compare to what `~combat_maxhit(str, strbns)` produces; §6 |
| `poisonous`, `immunepoison`, `immunevenom` | `param=poison_severity`, new immunity params | |
| `attributes` (demon/dragon/undead/…) | `param=undead` exists; rest are new params | gate for slayer/salve/demonbane later |
| `slayxp`, `cat`, `assignedby` | slayer content | out of scope here, captured in the ledger |
| `examine` | **validate only** — cache opcode 3 | |

Fields with no destination are still recorded in the ledger. "The wiki says this
and nothing reads it yet" is a finding worth keeping on disk.

## 3. Fetching, and the shape of the crawl

Per-page fetching 1,700 pages is the wrong crawl: many ids share one page, and
the API will hand over 50 full wikitexts per request through a category
generator. Measured live:

```
action=query&generator=categorymembers&gcmtitle=Category:Monsters
  &gcmlimit=50&prop=revisions&rvprop=content&rvslots=main
→ 50 pages, 50/50 carrying Infobox Monster
```

`Category:Monsters` is 1,627 pages + 44 subcategories;
`Category:Non-player characters` is 4,486 + 32. Crawling both recursively is
roughly **130 requests** for the whole corpus — a few minutes at 1 req/sec, well
inside etiquette. A per-page fallback (`action=parse&page=<title>`) covers
anything a spawned id resolves to but the category sweep missed.

Rules the crawler follows:

- Descriptive `User-Agent` with a contact address; ≤1 request/second; no
  parallelism. The wiki asks for this and it costs nothing here.
- **Cache the raw wikitext on disk, one file per page, with its `revid` and
  fetch date.** Every later run parses the cache, not the network. A re-crawl is
  then a deliberate act with a visible diff, and the parse is reproducible
  offline — the same reason `gen_spawns.py` pins its source commit.
- The crawl output is a *corpus*, not a decision. Nothing under
  `server/scripts` is written by the fetch step.

Storage: `OSRS-Content/osrs239-content/wiki/monsters/<page>.wikitext` plus a
`manifest.tsv` of page → revid → fetch date. ~4 MB of text; it belongs in the
tree for the same reason `npc_combat/` does — a decision nobody can re-derive
offline is a decision nobody can review.

## 4. The ledger, then the config

This tree has already solved "16,292 per-npc decisions, most of them uncertain,
some of them hand-pinned" once, and the answer is
`tools/gen_npc_combat.py`'s two layers. This follows it exactly, for the reasons
that file's header states (cachepack's 1,024-source ceiling; a block is not free
because `npc_default.npc` overlays it; the ledger holds what a config cannot
express).

**Layer 1 — the ledger.** `OSRS-Content/osrs239-content/npc_stats/<shard>/<gameval>.stats`,
one file per attackable spawned npc, written by `tools/gen_npc_stats.py`:

```
// Goblin (npc 3028) — level 2
// Source: OSRS Wiki "Goblin" revid 14783221, fetched 2026-08-12, version block 1
// Generated by tools/gen_npc_stats.py. Re-running rewrites this file.
// To pin a value by hand: edit it, set `source = authored`, and later runs
// read this file back and leave it exactly as it stands.

source        = generated
wiki_page     = Goblin
wiki_revid    = 14783221
wiki_version  = 1

hitpoints     = 5
attack        = 1
strength      = 1
defence       = 1
attackrate    = 4                    // wiki `attack speed`
damagetype    = crush                // wiki `attack style`
crushattack   = -21                  // wiki `attbns`, routed by style
strengthbonus = -15
stabdefence   = -15
...
aggressive    = no
respawnrate   = 35

// Checks — stated, never written to the config
cache_combat_level = 2               // wiki says 2, agrees
cache_size         = 1               // wiki says 1, agrees
wiki_max_hit       = 1               // computed from str/strbns: 1, agrees

// Behaviour the infobox does not hold. See §5.
behaviour     = none
```

**Layer 2 — the config.** `server/scripts/npc/configs/npc_stats.generated.npc`,
compiled from the ledger, one `[gameval]` block per npc that has an answer. An
npc with no wiki answer gets a ledger file saying so and **no block**, because a
block it does not need would inherit `npc_default.npc`'s overlay.

Ordering against the existing files matters: an authored block in
`lumbridge.npc` or `dragon.npc` already wins over generated output (the goblin
ledger in `npc_combat/g/goblin.combat` records exactly this and says
`NOT COMPILED`). The generator must do the same — read the authored configs
first, skip any npc they already state, and say so in the ledger file rather
than silently losing to load order.

Membership: every npc gaining a server field must be named in
`pack/npc.server`, or the loader hard-errors. `gen_npc_combat.py`'s
`claim_server_membership()` already does this correctly (comments first, names
after, sorted) — reuse it, do not re-implement it. 169 of the 1,700 are not
currently listed.

## 5. Special behaviour

The infobox holds a stat block. What the wiki *prose* says — "attacks with
melee and magic", "drains prayer", "heals when it reaches its lair" — has no
field, and the only honest way to carry it is per-npc RuneScript. The engine's
seam is the `[ai_*]` triggers: default melee is handled in
`mock230_combat.c`'s npc tick, and anything else needs an `ai_opplayer2` /
`ai_applayer2` script (`dark_wizard.rs2` is the worked example — two spells,
`~npc_cast_spell`, a random gate).

The plan does **not** attempt to LLM-summarise 1,700 prose pages into scripts.
It does three tractable things:

1. **Extract the structured behaviour flags the infobox does hold** —
   `aggressive`, `poisonous`, `immunepoison`, `immunevenom`, `immunecannon`,
   `immunethrall`, `attributes`, `max hit`, `attack style` when it lists more
   than one. Those become params and are mechanical.
2. **Classify the attack style from the fields, not the prose.** An npc whose
   `attack style` names Magic or Ranged needs an `ai_opplayer2` script, because
   the C melee path is the only thing it would otherwise get. Count these; they
   are a bounded list, and they are the npcs where "stats implemented" without a
   script produces a monster that fights *wrong* rather than fights weakly.
   A shared `[proc,npc_generic_ranged_attack]` / `_magic_attack` driven entirely
   by params covers most of them without a bespoke script each.
3. **Write the prose behaviour section into the ledger as a `behaviour =` note
   with its wiki section quoted**, and produce a ranked worklist of npcs whose
   prose describes a mechanic no param can express. That list is reviewed by a
   human and scripted deliberately, boss by boss, exactly as `giantmole_ai.rs2`
   and the QBD encounter were.

**Aggression needs one engine question answered before it is set on 400+ npcs.**
OSRS aggression stops when the player's combat level exceeds twice the npc's
(plus one), and it times out after ~10 minutes in an area. `maybe_aggress()` in
`mock230_combat.c` implements neither today. Turning `huntmode=aggressive` on
across the roster without that rule makes every low-level monster in the world
permanently hostile to a maxed player. So: **implement the level rule first**,
then set the flag. This is the one code change the plan requires.

## 6. Validation, which is most of the value

Three sources describe these npcs and they can be made to check each other.
Every check below is a report, not an edit.

| check | what a mismatch means |
|---|---|
| wiki `combat` vs cache `combat_level` | the id list on that page is wrong, or the page describes a different revision. **This is the primary detector of a bad join** — a wrong id lands on a stat block whose combat level will not match. |
| wiki `size` vs cache `size` | same |
| wiki `examine` vs cache opcode 3 | same, weaker signal |
| wiki `max hit` vs `~combat_maxhit(str, strbns)` | either the bonuses are wrong or the server's formula is. Both are worth knowing; the reference formula lives in `combat_stats.rs2`. |
| wiki stats vs LostCity's `.npc` blocks, where both exist | LostCity is rev-254; a disagreement is usually era drift and should be recorded, not resolved by fiat |
| ids the wiki claims that the cache does not have | the page is newer than rev 239 — drop that version block |
| spawned attackable npcs with no wiki page at all | the honest residue; count it and list it |

The `--validate` mode measures all of this and writes nothing, the same contract
`gen_npc_combat.py --validate` has.

## 7. Order of work

1. **`tools/wiki_fetch.py`** — the category crawl of §3 into
   `OSRS-Content/osrs239-content/wiki/monsters/`, with the manifest. Idempotent,
   rate-limited, resumable. No content is written.
2. **`tools/wiki_infobox.py`** — wikitext → per-version dict, including the
   `idN` lists. Templates nest and fields carry wiki markup (`[[Crush]]`,
   `{{plink}}`), so this is a real parser with unit tests over a handful of
   pinned pages, not a regex. Pinned fixtures live beside it.
3. **The join and the report.** id → version block for all 1,700; run every §6
   check; publish counts. **Stop here and read the report** — if the combat-level
   check disagrees on more than a small tail, the join is wrong and everything
   downstream is wrong with it.
4. **`tools/gen_npc_stats.py --validate`**, then `--write`: the ledger of §4.
   Review a sample by hand against the wiki pages before compiling anything.
5. **The config**, `npc_stats.generated.npc`, plus the `pack/npc.server` claims.
   `mock230_pack -v` must load with 0 errors; `make -C src test-mock230` must
   pass.
6. **The aggression level rule** in `maybe_aggress()`, with a test, *then*
   `huntmode=aggressive` from the wiki flag.
7. **Generic ranged/magic npc attack procs** driven by params, bound to the
   npcs §5.2 classifies. Bespoke boss scripts stay a separate, human-reviewed
   worklist.
8. **In-game verification** on a sample spanning the level bands — the headless
   harness from `docs/` (`SIM_OPNPC` + `SPLAT_DEBUG`) can fight an npc and read
   back its hitpoints and splats, so "the goblin has 5 hp and hits a 1" is a
   test rather than a screenshot.

## 8. Scope, stated

**In:** the 1,700 attackable spawned npcs. Stats, bonuses, attack speed,
aggression flag, poison/immunity flags, and the params behind them.

**Out, deliberately:** drop tables (the wiki has them, `drop_tables/` is a
ported LostCity slice with its own conventions, and mixing the two imports is
how a bad drop rate becomes invisible); slayer assignment data; the 3,802
spawned non-combat npcs; bespoke boss encounter scripts beyond the classification
in §5.3.

**Blocked on a decision:** whether the ledger and the wiki corpus live in
`OSRS-Content` (consistent with `npc_combat/`, ~4 MB) or outside it.
