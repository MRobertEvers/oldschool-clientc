Read docs/PORTING_GUIDE.md before doing anything — it is the entry point for
this work; follow the section it names for your task, and read the docs its
§8 lists for that task. Hard rules, non-negotiable:
- No game-facing strings, item/npc/interface ids, or config-shaped constants
  in C. If you feel forced to hardcode, the namespace or opcode surface is
  the bug — stop and report it instead (PORTING_GUIDE §2.4).
- Before implementing any behavior, grep the LostCity reference
  (~/Documents/git_repos/LostCity_Server) per §2.2 and state in one sentence
  where LostCity puts it. If it's content there, it's content here.
- Never copy ids between trees; resolve names through the pack (§4.1 step 4).
- Re-measure instead of trusting numbers in prose (§7).
- Done means: verified in the headless client, mock230_pack --check-only at
  0 errors, existing tests green, and the topic doc updated.
