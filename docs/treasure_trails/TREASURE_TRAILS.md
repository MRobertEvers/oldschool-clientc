# Treasure Trails

Port notes for `OSRS-Content/osrs239-content/server/scripts/trail/`.
Queue: [`NEAR_REALITY_CONTENT_QUEUE.md`](../NEAR_REALITY_CONTENT_QUEUE.md) wave A.

---

## 0. The finding that decided the whole design

**The cache is the clue database.** Before writing anything, the audit asked
where the clue data would come from and found it already present in
`cache.osrs239`:

| What | Where | Size |
| --- | --- | --- |
| The clue tables | dbtables 3–28, `cluehelper_*` | 26 tables, ~2,000 rows |
| Which obj is which clue | `param=trail_clue_row` on trail objs | 724 objs |
| Which tier an obj is | `param=trail_is_<tier>` | six exclusive booleans |
| The scroll interface | `trail_cluetext` (203) | 3 components |
| Maps, sextant, puzzles, rewards | `trail_map01`..`24`, `trail_sextant`, `trail_slidepuzzle`, `trail_rewardscreen` | — |

Every emote clue with its emote, outfit, hidey-hole loc and hidey-hole coord;
every cryptic, anagram, cipher, coordinate, map, music, fairy-ring, hot/cold
and skill-challenge clue with its target npc/loc/coord and its text. It is the
data the client's own in-game clue helper reads.

Near-Reality carries the same data as ~13,000 lines of hand-transcribed Java
(`EmoteClue.java` alone is 1,699 lines). **None of it was ported.** Porting it
would have been re-typing, by hand, data this tree already holds and can check —
and it would have created a second copy to keep in step with the cache forever.
What was ported is the *policy* Near-Reality wraps around that data, and even
there the wiki outranks it (§2).

This is the same shape as `toa-cache-ships-the-whole-raid`. Check the cache
before authoring content, every time.

---

## 1. The model

A clue **step** is an obj. Four of the six tiers give every step its own obj id
— 658 of them — so "which clue am I on" is answered by the backpack:

```
trail_clue_easy_emote001  ->  param trail_is_easy = 1
                              param trail_clue_row = 2104
                          ->  dbrow 2104, table cluehelper_clue_emote
                          ->  clue_text, emote, outfit, hidey_hole_loc, target
```

Beginner and master are one stackable obj each (`trail_clue_beginner`,
`trail_clue_master`) with no row param, so their step is a dbrow in a varp.
That asymmetry is the cache's, not a simplification, and it is why
`trail.varp` has two row varps and not six.

What the obj cannot say is how many steps are **left**, because every step of a
trail is drawn from the same pool. That is `%trail_steps_<tier>`, rolled on the
first read.

| File | Holds |
| --- | --- |
| `configs/trail.constant` | tiers, trail lengths, clue-type ids, holding cap |
| `configs/trail.varp` | steps left ×6, current row ×2, completions ×6 |
| `configs/trail.obj` | the two objs the cache leaves uncategorised |
| `configs/trail_steps.enum` | **generated** — the step pool of each tier |
| `scripts/trail_core.rs2` | tier, row, type, text, holding cap |
| `scripts/trail_read.rs2` | `Read` and `Check steps`, bound by category |
| `scripts/trail_step.rs2` | advance a step, hand over the casket |
| `scripts/trail_selftest.rs2` | the five procs the C harness drives |

Regenerate the pool after absorbing a cache:

```sh
python3 tools/gen_trail_steps.py          # writes trail_steps.enum
python3 tools/gen_trail_steps.py --check  # CI: fail if stale
```

---

## 2. Where Near-Reality was wrong

Trap 2 of the port guide, exactly: the shape was right and the numbers were
stale. `ClueLevel.java` declares trail lengths of 1 / 1 / 1–2 / 2–3 / 3–4 / 4–5.
The wiki's table (`sources/Treasure_Trails.wiki`, "Difficulty levels") says
**1–3 / 2–4 / 3–5 / 4–6 / 5–7 / 6–8**. Every tier short, and easy and beginner
reduced to a single step. Zenyte's ranges predate the trail-length rework.

The wiki numbers are what `trail.constant` carries, and the selftest asserts a
master trail reaches **both ends** of 6–8 over 200 rolls — a roll stuck at the
minimum passes a range check and is still wrong.

---

## 3. Traps hit while building this

1. **A challenge scroll has no tier.** 74 objs (`trail_clue_medium_anagram001_challenge`
   and kin) carry `trail_clue_row` and no `trail_is_*` marker. The first draft
   of `~trail_row_of_obj` derived the tier first and gave up when it came back
   `none`, which made every challenge scroll unreadable while looking, from the
   call site, like an obj that was never a clue. Ask the **obj** first.
2. **`db_getfield` is runtime-typed.** `return(db_getfield(...))` from a proc
   declared `(string)` compiles as an int, pushes to the wrong stack and
   underflows the int stack two frames later. It must land in a `def_string`
   first. `quests/scripts/questpoints.rs2` carries the same note.
3. **There is no string-equality opcode.** `if ($text = "")` is not refused by
   the compiler — it compiles as an int comparison and underflows at run time.
   Use `string_length($text) = 0`. See §5.
4. **Bind by category, never by name or wildcard.** 724 step objs is far past a
   name-bound list. `[opheld1,_]` would claim Read on every held obj in the
   game. The cache already groups them; two categories it left unnamed are
   named in `pack/category.pack` and two objs it left uncategorised are given
   one in `trail/configs/trail.obj`.
5. **A selftest on the shared player must put the backpack back.** The first
   draft `inv_clear`ed it and got away with it only because of stanza ordering.

---

## 4. What A1 does not do

A1 is the model, the read path and the casket chain. Still open, in queue order:

- **A2** emote clues — the 126 emote rows, outfits, hidey-holes, Uri.
- **A3** cryptic / anagram / cipher / map / coordinate — including the 74
  challenge scrolls, which are readable now and answer nothing.
- **A4** light box, puzzle box, sextant, hot/cold.
- **A5** the five reward tables. `~trail_reward` hands over a casket today; the
  casket's `Open` is not answered.
- **A6** Mimic, Watson, Uri, Sherlock, STASH units, scroll boxes, and the
  holding cap's reach beyond the inventory (see `~trail_can_receive`).

Clue scrolls are also not yet on any drop table — nothing gives one out.

---

## 5. Engine changes this slice needed

- `ToriRSServer_Pack` did not link (three symbols). `torirs_server_dbinfo.c` and
  `torirs_server_healthbarinfo.c` joined `TORIRSSERVER_PACK_SRCS`; `ToriRSServer_WorldSetVarpOn`
  joined `torirs_server_pack_stubs.c`.
- **`cachepack`'s merge disagreed with the runtime about param precedence.**
  `param` is a map key whose sub-key identifies the slot, but the "is this a
  list?" test answered yes for it, so two files stating one param both survived
  and `merged_value` returned the first. The runtime assigns per line, so the
  *last* file wins there. Three npcs ended up with a server band saying
  `vile_ashes`/`big_bones` while the world said "drops nothing", and no re-pack
  could converge them. Fixed in `cp_merge.c` by ranking three layers instead of
  two — cache 0, `*.generated.*` 1, hand-authored 2 — which is what
  `torirs_server_content.c` has always done with its three-pass npc load. This took
  `ToriRSServer_Pack` from 1 error to **0** for the first time.
- A content bug the above surfaced: `[skeletonmage]` was authored in two files
  with different `attack_anim`, one of them the giant skeleton's against a base
  skeleton rig.

**Still open** (filed, not fixed here): 8 npcs where `combat_stats.generated.npc`
states `attackrate` twice with different values, and 56 where a hand-authored
God Wars or Theatre anim is overridden by `npc_anims.generated.npc` purely
because `npc/` sorts after `areas/`. Both readers now agree about all of them,
which is what makes them visible; deciding which value is *right* is a content
pass of its own. `cachepack` prints one line per case.

---

## 6. Emote clues (A2)

`cluehelper_clue_emote`, 126 rows. Each states the emote index or indices, a
`cluehelper_target_coord` row, an optional `cluehelper_outfit` row, the
hidey-hole, and — on 42 of them — a `combat_encounter`, which is the double
agent. 25 rows carry **two** emotes; that is the medium-and-up "do X then Y".

**The emote column is the client's emote-tab index.** `cluehelper_emote_beginner_0`
is "Blow a raspberry at Aris" with emote 19, and `interface_emote`'s
`^emote_raspberry` is 19. There is no translation table and the selftest asserts
there needs to be none.

The hook is one line at the end of `~emote_perform`, called for **every** emote.
Which emotes matter is a property of the clue in the player's backpack; asking
it the other way round would need a reverse index to rebuild whenever the cache
changed. It is silent on every path but success.

### 6.1 `param_258` is the trail item group

400 objs across 55 groups. The cache names the groups itself:
`cluehelper_requirement_obj_param_trail_item_god_book` is `item_group=7`, and
the twelve objs carrying `trail_item_group=7` are the twelve god books. Every
ring of dueling charge state reads 18.

This is how `cluehelper_outfit`'s `wearpos_param_*` columns say "any rune
heraldic shield" without listing eleven ids, and it is why the outfit check has
two lists per slot: specific objs, and item groups.

### 6.2 Two traps in the outfit check

1. **The cache's outfit columns are not wear positions.** They skip the three
   *body* positions — arms (6), head (8), jaw (11) — because those are parts of
   a player rather than things to equip. So column 7 is `wearpos_legs`, which is
   wear position 7 by coincidence, and column 8 is `wearpos_hands`, which is
   wear position **9**. Passing the column as the position checks the wrong slot
   for four of the eleven and passes by luck for the rest.
2. **`worn` is addressed by wear position**, so `inv_add(worn, gold_ring, 1)`
   fills the first free slot — the hat. Use `inv_setslot`.

A column index cannot be a variable in this dialect (`table:column` compiles to
a packed constant), so the check is eleven near-identical procs rather than a
loop. That is the compiler checking that every column named exists and holds
what it is read as, which a loop would give up.

### 6.3 Area

The cache gives one coord per clue; Near-Reality gives a hand-authored polygon
per clue. `^trail_emote_radius = 5` is **ours** — one constant, chosen to cover
the room a clue usually names without accepting a neighbouring one. If a
specific clue needs its own area that is a row in a table, not a second
constant; inventing 126 areas by hand is how NR's file reached 1,699 lines.

### 6.4 Uri

Six records, one per tier (`trail_beginner_uri` … `trail_master_uri`), plus
three double agents. The wiki's two gates are both implemented: performing the
emote summons him, and **talking** to him re-checks the outfit, so a player who
strips after summoning is refused. `combat_encounter` on the row — not the tier
— decides whether an agent comes first; the tier only picks which agent record.
