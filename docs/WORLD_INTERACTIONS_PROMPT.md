# World-interaction implementation loop — agent prompt

Paste the block below as a single message. It implements the remaining rows in
`tools/world_interaction_worklist.tsv`, ONE FAMILY PER ITERATION.

Unlike `QUEST_AUDIT_PROMPT.md` this loop needs **no ledger file**:
`--worklist` derives every row's status by re-resolving it against the script
tree on each run, so a row cannot claim done while its binding is absent, and a
regression re-appears as TODO by itself. The tool is the ledger.

---

You are implementing missing world interactions in this repo, ONE FAMILY PER
ITERATION, in worklist order. A family is done when every one of its rows
resolves to a live binding and a test you have proven can fail says so.

## Before doing ANYTHING else, in this order
1. Read `docs/QUEST_PORTING_FIELD_GUIDE.md` in full. Its rules override your
   instincts, especially the §1 build/test loop (never rebuild the whole tree;
   rebuild sscompile immediately before every content compile; absolute output
   paths; confirm the "compiled N scripts" line), the §2 pre-compile grep
   sweep, and §7 verification discipline.
2. Read `docs/WORLD_INTERACTIONS.md` — the defect taxonomy in §1, and §4 in
   full. §4 is the list of false-positive classes that have already cost
   verification rounds; re-deriving them is wasted work.
3. Run `python3 tools/world_interaction_scan.py --worklist --todo` and take the
   FIRST family listed. That is your one family for this iteration.
4. Other agents work in this tree concurrently. Use a private
   `PLATFORM_OBJ_BASE` scratch dir for every binary you build. Never
   `git stash`, never `git checkout --` a shared file. Save your diff to your
   scratchpad after each edit — HEAD moves mid-task here.

## Each iteration
1. **Gather truth.** Fetch the wiki page for the interaction (raw wikitext via
   `?action=raw` where useful). Establish: the exact option wording, the
   required item(s), skill level and XP, the product, the animation, and any
   timer/chance. Check the page's **Changes** section.
   - **If the wiki says the interaction does not exist in the form the cache
     implies, DELETE THE ROW from the worklist with a comment saying why.**
     Do not invent content. Precedent: ordinary fountains and wells have no
     `Drink-from` at all (`WORLD_INTERACTIONS.md` §3.6) — a generic drink
     handler would have been fabricated mechanics.
2. **Read the cache record before writing anything.** For every symbol in the
   family, dump its block from `configs/all.loc` / `all.npc` and confirm the
   op NUMBER per record. Op numbers differ between records of the same concept
   — `fat_cow` puts Milk on op1 while `fairy_fat_cow` puts it on op2. Never
   normalise; bind what each record declares.
3. **Decide symbol vs category binding.** `pack/category.pack` is the
   compile-time authority for category ids — NOT `port/categories_loc.map`,
   which is porting triage and disagrees. Prefer a category binding when every
   member carries the id; fall back to explicit symbols for members that carry
   no category (that is exactly why the Keldagrim furnaces were dead).
4. **Implement.** Minimal diffs, matching the surrounding file's idiom. Put
   generic interactions in `server/scripts/general_use/scripts/` or the owning
   `skill_*` dir; do not create a lane. Reuse existing labels and procs rather
   than duplicating a body across records.
5. **Compile.** Rebuild sscompile first, absolute paths, private objdir, and
   CONFIRM the "compiled N scripts" line before believing any test.
6. **Verify with a check you have proven can fail.** Add an assertion that the
   interaction produces its effect, then MUTATE the fix — put the op number
   back, or drop one trigger head — and watch the assertion go red. Restore,
   and record which mutation killed which assertion. A green test you have
   never seen fail proves nothing.
7. **Prove no collateral.** `./src/build_opt/torirsserver --selftest` (no env),
   A/B against a pre-change run on the same binary; normalise before diffing
   (`sed -E 's/-?[0-9]+/N/g'`). Failure sets must be identical apart from your
   new checks. Never "fix" a failure you did not cause — the selftest shares
   one RNG stream and unrelated failures shift (field guide §1).
8. **Close out.** Re-run `--worklist --todo` and show the family now reads
   DONE. Report: family, what the wiki said, what you bound and why
   symbol-vs-category, the mutation that proved each assertion, the A/B result,
   and anything still approximate.

## Hard rules
- The wiki is the single source of truth. A row in the worklist is a claim to
  verify, not a fact.
- If the first compile error is "is not a command", your sscompile is stale.
  Rebuild it. Do not diagnose further.
- **Duplicate trigger names are a hard compile error**, and a name binding
  shadows a category binding it may need to re-issue (`p_oploc(N)`). Grep for
  an existing owner before adding a head; if one exists, route through a hub
  rather than redeclaring.
- Ground ops are `op`, inventory ops are `iop`/`ifop` — and this cache lists
  only NON-DEFAULT inventory options, so obj rows are unreliable
  (`WORLD_INTERACTIONS.md` §4.8). The worklist is loc/npc only; keep it that
  way.
- Never mark a family done on static reading. Step 6 is mandatory.
- One family per iteration. Stop after the family reads DONE.

---

## Running it unattended

For several families back to back, wrap it:

```
/loop implement the next TODO family per docs/WORLD_INTERACTIONS_PROMPT.md
```

To scope a single one instead, name it:

```
Implement the `fruit_tree` family per docs/WORLD_INTERACTIONS_PROMPT.md
```

`fruit_tree` (40 rows, six species, one shape) and `altar` (39 rows) are the
two large families; the other twelve are 1-7 rows each. Start with `churn` if
you want a short first pass that exercises the whole loop.
