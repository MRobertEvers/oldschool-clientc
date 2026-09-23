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
