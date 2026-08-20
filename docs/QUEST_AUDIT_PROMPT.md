# Quest audit loop — prompt

Paste everything below the line into the agent (or run it with /loop). One
quest per iteration.

---

You are auditing and fixing ported quests in this repo, ONE QUEST PER
ITERATION, in original release-date order. Your goal is that each audited
quest is completable and wiki-accurate end to end, with proven tests.

## Before doing ANYTHING else, in this order
1. Read `docs/QUEST_PORTING_FIELD_GUIDE.md` in full. It is the operating
   manual for this task. Its rules override your instincts, especially:
   - the §1 build/test loop (never rebuild the whole tree; rebuild sscompile
     immediately before every content compile; absolute output paths;
     confirm the "compiled N scripts" line),
   - the §2 pre-compile grep sweep,
   - §7 verification discipline (every fix gets a check you have PROVEN can
     fail, by mutation).
2. Read `docs/QUEST_AUDIT_LEDGER.md`. If it does not exist, create it this
   iteration (see "Ledger" below) and do nothing else.
3. Other agents work in this tree concurrently. Use a private
   `PLATFORM_OBJ_BASE` scratch dir for every binary you build. Never
   `git stash`, never `git checkout --` shared files. Save your diff to your
   scratchpad after each edit.

## Ledger (durable state — this is how the loop resumes)
`docs/QUEST_AUDIT_LEDGER.md`: one row per quest — release date, quest name,
content dir (`server/scripts/quests/quest_*`), status
(`unaudited | in-progress | audited-clean | fixed | blocked:<reason>`),
date, one-line note. Build it ONCE from the OSRS wiki's quest list with
release dates (fetch raw wikitext: `?action=raw`; cross-check dir names
against `ls server/scripts/quests/`). Quests with no dir get
`blocked:not-ported` — do not implement missing quests in this loop, only
audit existing ones. Update the row at the START (in-progress) and END of
every iteration.

## Each iteration: pick the FIRST row that is `unaudited`, then
1. **Gather truth.** Fetch the quest's wiki page, its `/Quick guide`, and
   its `/Transcript` as raw wikitext. Fetch the Quest Helper class for this
   quest from github.com/Zoinkwiz/quest-helper — its `steps.put(N, ...)`
   ladder is the authoritative varbit/varp progression and the gameval names
   resolve in our pack unchanged. Check the wiki page's **Changes** section:
   anything 2009scape-derived drifts there.
2. **Static audit** of `server/scripts/quests/quest_<name>/`:
   - Map every Quest Helper step N to the script that advances our progress
     var to N. Any step with no writer = a dead end; any writer with no
     trigger path = unreachable.
   - Every `[opnpc*/oploc*/opheld*]` binding: is it on the multinpc SHELL
     (not a rung)? Does a name binding shadow a category or engine verb it
     must re-issue (`p_opnpc(2)`)? Ground `op` vs inventory `iop` correct?
   - Every quest fight npc: run the field guide's §4 checklist (huntmode,
     retaliate, maxrange, ai_timer/npc_settimer, hitpoints on every form,
     death_drop, opplayer2).
   - Every teleport/cutscene: no same-tick `map_blocked`/`loc_find`/`p_walk`
     after the move; camera/lock restored on death and logout paths.
   - Every parked dialogue has a resume or discard path.
   - Rewards, quest points, and journal text verbatim against the wiki's
     rewards table.
   - Run the §8 pre-flight greps over the quest dir.
3. **Dynamic audit.** Rebuild sscompile, compile the pack (absolute paths),
   confirm the compiled line, then drive the quest: prefer an existing
   selftest/debugproc; otherwise add a `::questaudit_<name>` debugproc (or C
   stanza if it needs world ticks — `p_delay` in a debugproc parks forever
   under --selftest) that walks the progress var through every step,
   asserting state, items, and rewards at each. Echo a pass line for every
   check (ASCII only).
4. **Fix what you found.** Minimal diffs; fix bugs, do not refactor. Every
   fix gets an assertion, and you must mutate the fix to show the assertion
   go red, then restore it. Record which mutation killed which assertion.
5. **Prove no collateral.** `ToriRSServer --selftest` (no env, `src/build_opt/`
   binary) A/B against a pre-change run on the same binary; normalized
   failure sets must be identical apart from your new checks. If the run-live
   lane pack exists, rebuild it too (`tools/tob_build_packs.sh`).
6. **Close out.** Update the ledger row (`audited-clean` or `fixed` + note;
   `blocked:<reason>` only for missing engine features — state the exact
   opcode/feature, verified missing per §7.7, not assumed from a doc).
   Report: quest, bugs found (each: symptom → root cause → fix → the
   assertion that now pins it), what remains approximate, evidence lines.

## Hard rules
- The wiki is the single source of truth; a `done` in any queue doc is a
  claim to verify, not a fact.
- If the first compile error is "is not a command": your sscompile is stale.
  Rebuild it. Do not diagnose further.
- Never mark `audited-clean` on static reading alone — step 3 is mandatory.
- Never fix a selftest failure you did not cause (see field guide §1 on RNG
  false regressions); note it and move on.
- One quest per iteration. Stop after closing out the ledger row.
