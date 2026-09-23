# Romeo & Juliet -- pinned wiki brief (2026-09-23, parity pass 2)

Source: https://oldschool.runescape.wiki/w/Romeo_%26_Juliet and
https://oldschool.runescape.wiki/w/Cadava_berries (fetched 2026-09-23).

## Walkthrough (relevant to content parity)

Talk to Romeo in Varrock Square -> climb to Juliet's room (west of Varrock,
level 1) -> take her message back to Romeo -> talk to Father Lawrence
(north-east Varrock) -> talk to the Apothecary (south-west Varrock), who
asks for Cadava berries -> obtain Cadava berries -> return them to the
Apothecary for a Cadava potion -> bring the potion to Juliet -> talk to
Romeo to finish.

## Cadava berries -- sourcing, LostCity vs OSRS

- **LostCity (2004-era, both `LostCity_Content2` rev254 and
  `LostCity_Server` rev289)**: the ONLY source in the content tree is the
  generic imp death-drop table (`drop tables/scripts/imp.rs2`), a 4/128
  chance alongside black/red/white/yellow beads, bolts, eggs, etc. There is
  no bush, no pickable loc, no farming patch anywhere in either LostCity
  checkout.
- **Current OSRS (wiki, confirmed live 2026-09-23)**: Cadava berries have
  three sources today: (1) four wild "cadava bushes... near the south-east
  Varrock mine" (2 berries/bush, ~2 min respawn) -- the primary, fast
  method the wiki recommends; (2) a farming patch from cadavaberry seeds
  (22 Farming); (3) imps STILL drop them at the same 4/128 rate the
  LostCity table already has. The wiki's own Cadava berries page lists all
  three; the quest walkthrough recommends the wild bushes only because
  they're fastest, not because imps stopped dropping them.
- Quest Helper's `RomeoAndJuliet.java` lists `cadavaBerry` only in
  `getItemRequirements()` (a background bring-along tooltip: "You can pick
  some from bushes south east of Varrock"), never as a dedicated
  `NpcStep`/`ObjectStep` in `loadSteps()`. The guide's own step ladder does
  not treat berry-gathering as a driven leg of the quest.

**Verdict**: the port's `drop_tables/scripts/imp.rs2` already carries the
exact LostCity drop table verbatim (4/128 cadavaberries), which is still a
live, correct OSRS source today -- not a stale 2004-only mechanic. The wild
bush is a modern *convenience* OSRS added on top, not a replacement, and
the guide does not dedicate a step to either method. `2026-09-23 parity
pass 2` (agent romeojuliet worker) left the wild bush unimplemented rather
than inventing placement/model data with no grounded source to verify it
against -- adding speculative map content was exactly the failure this
project's `QUEST_CONTENT_AUDIT_2026-09-22.md` flagged for Mort'ton.

**Update, parity pass 1b (2026-09-23, same day, later worker)**: the
"no grounded source" blocker is gone. `oldschool.runescape.wiki/w/
Cadava_bush_(wild)` (raw wikitext fetch) gives exact tiles
(3264,3366) (3268,3370) (3271,3367) (3277,3374), all plane 0, ids
23625/23626/23627 for the two/one/no-berry states, `op1=Pick-from`, and
this pack's own `configs/all.loc` already carries those exact ids under
`fai_varrock_cadavabush_2/_1/_0` PLUS a `fai_varrock_cadavabush_tailored`
carrier for the wiki's documented "south-western bush [that] is always
empty" quirk -- a genuine OSRS-era addition (29 May 2007, "Varrock's New
Look") this cache already ships, not something invented. A probe run
(`build/quest_gate/probe_cadavabush`) confirmed all four are already
PLACED on the live map at those exact tiles; nothing needed spawning.
Implemented in a new file, `server/scripts/areas/varrock/scripts/
cadava_bush.rs2` (pick, deplete, wiki-quoted empty messages, 200-tick
respawn via the same `loc_change` idiom `woodcut.rs2`'s tree stump uses,
and the south-western bush's permanent-dud quirk). Proved live through the
real client (`build/quest_gate/parity_romeojuliet/ledger.tsv`, 13/13 PASS)
-- see `build/parity_state/parity1b/romeojuliet.parity.progress.md` and
`.parity.json` for the full trail. The imp drop remains correct and
untouched; the bush is now ALSO real, working content, not a documented
gap.

## Reward

5 Quest Points only. No coins, no items, no XP (wiki: "The wiki contains no
other tangible rewards listed beyond the quest points"). LostCity's
`~send_quest_complete(questlist:romeojuliet, cadava, 200, ...)` (200 coins)
does NOT carry over -- this is a genuine OSRS-era change, and the port's
`~quest_complete_rewards(quest_romeoandjuliet, "", coins)` (empty reward
string, `coins` used only as the scroll's flavour icon, same idiom as
`quest_lostcity` and `quest_templeofikov`, both other QP-only completions)
is correct.
