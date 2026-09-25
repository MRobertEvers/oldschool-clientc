# Shield of Arrav (blackarmgang / phoenixgang) -- content parity note

Status: `verified` -- LostCity source (`quest_blackarmgang`) and the OSRS wiki
agree on every leg checked here; no OSRS-era divergence found. This is a short
note, not a full modernization audit (see `QUEST_MODERNIZATION_PLAN.md` for
that heavier template, used elsewhere).

Pinned 2026-09-23, content parity pass 1, for the `blackarmgang` tier-1 test.

## Sources

- [Shield of Arrav](https://oldschool.runescape.wiki/w/Shield_of_Arrav) --
  walkthrough summary, requirements, rewards, Changes log.
- [Transcript:Charlie the Tramp](https://oldschool.runescape.wiki/w/Transcript:Charlie_the_Tramp)
  -- the actual in-game dialogue tree for the Black Arm Gang recruitment
  branch.
- `LostCity_Content2/scripts/quests/quest_blackarmgang/` and
  `LostCity_Content2/scripts/areas/area_varrock/scripts/{tramp,reldo,katrine,
  curator,straven,king_roald}.rs2` -- the pre-2007 RS2 source this pack is
  ported from.
- Quest Helper `ShieldOfArravBlackArmGang.java` -- transition aid only, not
  authoritative (see finding below).

## The quest, briefly

Members-optional (F2P), 1 QP, short. Two mutually exclusive routes -- Black
Arm Gang (via Charlie the Tramp, south Varrock) or Phoenix Gang (via Baraek,
the fur trader) -- that converge on trading half a shield / half a
certificate with a partner on the OTHER route. Reward: 1 quest point, 600
coins (wiki: "1 Quest point", "600 coins"). Full completion genuinely
requires **two players**, one on each route, per the wiki: "you must have a
partner, and they must pick the opposing gang."

## Finding: talking to Reldo is NOT a gate on joining a gang

Quest Helper's stage-0 default step is `startQuest` (talk to Reldo), followed
by `searchBookcase` and `talkToReldoAgain`, which the 2026-09-23 coverage
audit (`docs/QUEST_HELPER_COVERAGE_2026-09-23.md`) read as a missing content
gate: "the port's tramp starts the Black Arm route with no Reldo gate."

That is not correct, checked against three independent sources:

1. **LostCity's own 2004-era script never gates Charlie on Reldo state.**
   `tramp.rs2`'s "Do you think they would let me join?" branch sets
   `%blackarmgang = ^blackarmgang_started` unconditionally (after the
   already-a-member checks). It never reads `%phoenixgang` (the Reldo/book
   progress varp). Reldo's own chain (`reldo.rs2`: `reldo_phoenixstart` ->
   read the book -> `reldo_read_book`) exists solely to send the player to
   **Baraek** (the Phoenix Gang), never to Charlie.
2. **The wiki's own dialogue transcript for Charlie the Tramp has no such
   check.** `Transcript:Charlie_the_Tramp`'s Black Arm branch has exactly
   three outcomes: unaligned (sent to Katrine), already Black Arm ("I was
   under the impression you were already a member..."), already Phoenix
   ("you're a collaborator with the Phoenix Gang..."). No fourth branch keyed
   on book/Reldo progress.
3. **The wiki's Changes log has no relevant entry.** Three entries exist (6
   Nov 2024 Curse of Arrav dialogue/Jonny the Beard rework; 13 Jul 2017
   spelling fix; 29 May 2007 shield/certificate system rework) and none of
   them touches a Reldo/book requirement.

Reldo is the game's *hint* NPC for a player who does not yet know either gang
exists -- talking to him is optional flavor/discovery, not a mechanical
prerequisite, in both the 2004 source and the current game. Quest Helper's
stage-0 `ConditionalStep` shows Reldo only because it needs one shared
fallback display for "quest not started at all" before either route's own
varp has moved; a player who walks straight to Charlie (as this port's
`tramp.rs2` -- correctly -- does) reaches stage 1 (`talkToCharlie`/
`talkToKatrine`) without ever seeing that fallback.

**This port's `areas/varrock/scripts/tramp.rs2` is correct as written.** No
Reldo gate should be added; doing so would be the same failure mode as the
already-reverted `gertrude.rs2` "prerequisite" edit documented in
`docs/QUEST_CONTENT_AUDIT_2026-09-22.md` B1 -- inventing a mechanic on an
unsourced claim.

## The two legs that genuinely need a second player

- **Weapon-store key.** Only a Phoenix Gang member can get `phoenixkey2` (from
  Straven, after the Jonny-the-Beard mission). A Black Arm player needs a
  Phoenix partner to hand it over (trade, or "use" it on them per the wiki's
  Ironman note) before `phoenixdoor2` will open (`[oplocu,phoenixdoor2]`,
  correctly gated in this port). No single-client path exists.
- **Certificate half.** Symmetric: `arravcertificate_lft` (Phoenix's half)
  only exists on the Phoenix route (`curator_take_phoenix_half`); a Black Arm
  player needs a Phoenix partner's spare to combine into a full
  `arravcertificate`.

Checked whether this pack implements the wiki's Ironman workaround ("use the
key/certificate on each other" to drop at feet, without a formal trade) as a
general engine mechanic: it does not -- `grep` across `server/scripts/` for a
player-targeted `opheldu` found nothing. That is an engine-level gap (a
generic "use item on another player" op), not something a single quest's
content directory can supply. `test/quests/blackarmgang.lua` stands in for
both legs with `::goto` (past the locked door, navigation only) and `::give
arravcertificate_lft 1` respectively -- the correct call per
`docs/QUEST_AUTHORING.md`'s rule for content genuinely beyond a single
client's reach.

## Content parity pass 3 (2026-09-23): a proper test affordance

The two legs above are genuinely two-player and stay so (nothing above
changed, still `verified`/matches-LostCity). What pass 3 adds is a named,
quest-scoped debugproc, `[debugproc,blackarmgang_partner]`
(`quest_blackarmgang.rs2`, after `[queue,blackarmgang_quest_complete]`), that
performs exactly the two hand-offs a real Phoenix Gang partner would make --
`inv_add`s `phoenixkey2` and `arravcertificate_lft`, nothing else, each
guarded so a repeat call is a no-op -- in place of the raw ladder cheats
(`::goto`, `::give arravcertificate_lft 1`) a scratch driver script used to
stand in for the partner before. This is the idiom `QUEST_SERVER_CHEATS.md`
already documents for other two-player/partner legs in this pack (compare
`quest_royaltrouble/scripts/royal_bmp.rs2`'s partner debugprocs and
`quest_hero/scripts/quest_hero.rs2`'s own `[debugproc,hero_partner]`,
landed the same pass).

Proved (scratch script, not `test/quests/blackarmgang.lua` -- see
`build/parity_state/parity1b/blackarmgang.parity.json` for the ledger rows):
`phoenixdoor2` genuinely refuses `[oploc1,...]` ("The door is securely
locked.") with no key held; `::blackarmgang_partner` grants both items;
the REAL `[oplocu,phoenixdoor2]` trigger (item armed, clicked on the door)
unlocks it with `phoenixkey2` in hand, instead of a `::goto` teleport past
it; the granted `arravcertificate_lft` still drives the real
`[opheldu,arravcertificate_lft]` combine.

`test/quests/blackarmgang.lua` itself is unchanged by this pass (out of
scope for this worker -- test/quests/*.lua is owned elsewhere in this batch);
its own `::give arravcertificate_lft 1` at T:296 is still a raw ladder cheat,
not yet the `::blackarmgang_partner` debugproc, and its `goto-weaponStore`
at T:172 still teleports past `phoenixdoor2` rather than unlocking it for
real. Both are one-line swaps for whoever next owns that file (replace
`::give arravcertificate_lft 1` with `::blackarmgang_partner` before the
curator visit, and route `goto-weaponStore` through a real
`click_loc`/`use_on` sequence on `phoenixdoor2` once `phoenixkey2` is
carried) -- left as `legs_left` in the parity JSON, not attempted here.

## Content parity pass parity1m (2026-09-25): Reldo reads both routes

The fourteenth pass was ordered to "make Reldo start and advance BOTH
routes exactly as LostCity does". Re-checked against the sources, raw
wikitext this time (`?action=raw`, fetched 2026-09-25):

- **LostCity** `areas/area_varrock/scripts/reldo.rs2` reads and writes
  `%phoenixgang` only (`reldo_phoenixstart` -> `^phoenixgang_started`,
  `reldo_read_book` -> `^phoenixgang_spoken_reldo`). It never starts or
  advances `%blackarmgang`; `tramp.rs2` does. The port already matched.
- **Transcript:Charlie_the_Tramp**: Charlie's only condition on "Do you
  think they would let me join?" is "If the player has not yet joined
  either gang" -- no Reldo/book gate. Unchanged.
- **Transcript:Reldo** is where the OSRS form DIFFERS from LostCity, in two
  details, both now ported (`areas/varrock/scripts/reldo.rs2`):
  1. `{{topt|cond=If Shield of Arrav has not been started:|I'm in search of
     a quest.}}` -- the offer is withdrawn once EITHER route has started, so
     Reldo's four menus now read `%blackarmgang` as well as `%phoenixgang`.
     A player Charlie has already sent to Katrine is not offered the book.
  2. The OSRS start confirm: "Ah, yes. I think I have something, if you're
     definitely interested?" -> "Start the Shield of Arrav quest?" Yes./No.
     (Quest Helper's `startQuest.addDialogSteps("I'm in search of a
     quest.", "Yes.")` agrees). No. writes nothing.
  Not ported: the transcript's "combat level is less than 10" warning box
  (no pack idiom for it; cosmetic).

So the Quest Helper `startQuest`/`searchBookcase`/`talkToReldoAgain` steps
are the SHARED opener (the walkthrough's "Choosing a gang" section) and stay
optional for the Black Arm route: Reldo, the book and Reldo again are real
and drivable on a fresh character, and Reldo's `reldo_read_book` (OSRS
text) points at both Baraek and Charlie -- but nothing in any source makes
them a gate on Charlie. A Black Arm test may drive them first (fresh
character: Reldo -> Yes. -> questbookcase -> read the book -> Reldo again ->
Charlie), which exercises all three guide steps for real.

`::blackarmgang_partner` re-verified: one `phoenixkey2` (= `straven.rs2`'s
single grant) and one `arravcertificate_lft` (one of the two
`curator_take_phoenix_half` gives the Phoenix player), each guarded, nothing
else. `questbookcase` stays Phoenix-only (`%phoenixgang =
^phoenixgang_started`, LostCity `[oploc2,questbookcase]` has the same gate).
