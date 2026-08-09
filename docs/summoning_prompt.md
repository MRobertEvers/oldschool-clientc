Implement the Summoning port (2009scape rev-530 → OSRS-Content osrs239), behind a feature flag.

REPOS
  target: /Users/matthewevers/Documents/git_repos/3draster  (+ OSRS-Content/ submodule)
  source: /Users/matthewevers/Documents/git_repos/2009scape (reference only — never modify)

READ FIRST, IN THIS ORDER — do not start work until you have:
  1. docs/SUMMONING_PORT.md          — the plan. Findings, decisions, all 7 phases with
                                       checkbox todos, flag architecture, content tree layout,
                                       risk register, verification commands.
  2. docs/SUMMONING_PORT_QUEUE.md    — the slice queue + opcode-gap table. This is loop state.
  3. docs/summoning_port/AGENT_REDTEAM.md — the adversarial review. It is the TIEBREAKER: the
                                       design docs contradict each other and it caught 8 factual
                                       errors in them. Read it before trusting AGENT_DESIGNS.md.
  4. docs/PORTING_GUIDE.md §2.2, §2.4/§2.5, §4, §4.5, §7 — the binding process rules.
  Reference as needed: docs/summoning_port/AGENT_RECON.md (12 recon reports),
  AGENT_DESIGNS.md (4 designs), pouches_530.json (the 82-entry port manifest).

DECISIONS ALREADY SETTLED — do not re-litigate or re-derive:
  - Summoning is stat 24, NOT 23. Stat 23 is Sailing and is live end-to-end in osrs239.
  - Sailing is kept. Summoning is display slot 25 (enum_681 val=25,24); stats panel goes 3x9.
    Prefer a dedicated clientscript for the Summoning cell over repositioning all 25.
  - Full roster: all 82 familiars. NPC ids are NOT a constraint — separate work removes the cap.
    Do not tier, budget, or scope around npc ids. Do not re-derive a ceiling from the NPC_INFO
    v5 reader; it is being changed.
  - The feature flag already exists in the cache: script_8950 + a content_restrict_*_serverside
    varbit. Do NOT touch src/features/features.h — that is a client-era table, wrong layer.
  - Ported content lives in a marked lane: ported/scape2009_summoning/ and
    server/scripts/ported_scape2009_summoning/. §11 of the plan lists facts already confirmed —
    trust them, don't re-measure.

SCOPE THIS PASS: Phase 0 (remaining slices) → Phase 1 → Phase 2 → Phase 3, stopping for review
after the Spirit wolf vertical slice. Maps are out of scope; the obelisk goes in via runtime
loc_add. Work one queue slice at a time and update SUMMONING_PORT_QUEUE.md after each.

TRAPS — these have already burned time, don't rediscover them:
  - Any 530 measurement taken with --rev rs643 is SUSPECT (it pins a rev-610 frame format).
    Re-measure against the real 530 profile.
  - cachepack ships base-cache bytes when a CS2 script fails to compile and only a counter says
    so. 95 of 9,368 committed .cs2 already fail, including script_1904 (the skill-guide builder).
    Gate every bake on "compiled N, failed 0".
  - Framemap V3→V1 downgrade is a confirmed SILENT no-op. Fix it with a test that fails on
    today's code before relying on any ported animation.
  - Fixing the RS2 sequence codec touches 634 and 727 too — A/B both before and after.
  - A skipped test suite reads as a pass. Every summoning target must assert a non-zero check count.
  - MOCK230_SAVES=$(mktemp -d) on EVERY headless run. Never bare `pkill -f build/mock230`.
  - Never `git stash` in this repo. No ASAN on this Mac. embed_test decode is broken pre-existing.
  - Never name a ported record exactly `summoning` — trigger subjects resolve first-match-wins
    across namespaces and mis-resolution is silent. Prefix everything summoning_*.

DEFINITION OF DONE per slice: verified in the headless client, mock230_pack --check-only at 0
errors, existing tests green, the flag-off bake proven byte-identical, and the queue doc updated.
Don't commit unless I ask.