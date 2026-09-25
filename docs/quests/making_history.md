# Making History -- pinned wiki brief (2026-09-23, parity pass 1b)

Source: https://oldschool.runescape.wiki/w/Making_History ,
https://oldschool.runescape.wiki/w/Transcript:The_Mysterious_Adventurer ,
https://oldschool.runescape.wiki/w/Transcript:The_History_of_the_Outpost ,
https://oldschool.runescape.wiki/w/Transcript:The_Times_of_Lathas (fetched
2026-09-23). LostCity has no `quest_makinghistory` in either
`LostCity_Content2` or `LostCity_Server` (this is a November 2005 release,
after both checkouts' 2004 cutoff) -- source = wiki throughout.

## Walkthrough (relevant to content parity)

Talk to Jorral at the Outpost (west of East Ardougne) -> he sends the
player to find a trader's journal (buried near the outpost, found with a
spade), then to a ghost (Droalak, outside Port Phasmatys' general store,
needs a ghostspeak amulet) for a scroll, then to Blanin and Dron in
Rellekka (Dron gates his knowledge behind a 12-question quiz about himself
that Blanin already briefed the player on) -> return the journal and scroll
to Jorral -> carry his letter to King Lathas in Ardougne Castle -> Lathas
promises to preserve the outpost and gives a reply letter -> hand that
letter back to Jorral to complete the quest.

**Completion**: Jorral says there is now something that may interest the
player in his museum -- the outpost's storage rooms become a small museum,
open from then on. Rewards: 3 Quest Points, 1,000 Crafting XP, 1,000 Prayer
XP, 750 coins, an enchanted key (Ardougne monastery cellar), and Master
clue scrolls start including a hot/cold digging step.

## The museum (the leg this pass implements)

The Outpost building's crates/sacks/bed become five named display cases the
moment the quest completes (cache-native multiloc swap on the
`makinghistory_objloc` varbit, `configs/all.loc:113988-114110`):

- **Shield Display** (`op1=Study`) -- real Study text on the wiki: a huge
  shield from the Fourth Age.
- **Large Display x2** (`op1=Study`) -- ground floor: Wally's helmet
  (Demon Slayer), Arrav's axe (Shield of Arrav), Randas's boots. First
  floor: a Fourth Age Saradomin banner, an iron mace (matching Dron's own
  mace), a Zamorak banner -- the "Dreaded Years of Tragedy" case, both
  paired with the quest's own Great Battle backstory told in the outpost's
  own bookcase (see below).
- **Bookcase** (`op1=Study`) -- three readable books, verbatim per the
  Transcript: pages above:
  - *The History of the Outpost*: the outpost's founding ~140 years prior,
    the Zamorak occupation ("The Dreaded Years of Tragedy"), the two
    childhood friends split by religion whose armies clashed in "The Great
    Battle", their reconciliation under Guthix, and the founding of the
    kingdom and the marketplace that followed.
  - *The Times of Lathas*: the Ardignas royal line, 68 years old, five
    kings, current King Lathas. **Post-Song of the Elves** the wiki
    documents a distinct retitled text: *The Times of Ardignas*, six kings,
    current King Thoros -- gated on `quest_songoftheelves`'s own carrier
    (`%sote >= ^sote_complete`).
  - *The Mysterious Adventurer*: a lore book about "a brave and intelligent
    adventurer" listing eleven example achievements (Demon Slayer,
    Restless Ghost, Cook's Assistant, Creature of Fenkenstrain, Between a
    Rock, Biohazard, Sheep Shearer, Black Knights' Fortress, Ernest the
    Chicken, Fishing Contest, Gertrude's Cat). This list is **static in the
    source** -- the same fixed eleven quests regardless of what the reading
    player has actually completed -- so no cross-quest completion tracking
    is needed to render it faithfully; it is flavour text, not a real quest
    log.
- **Catapult** (`makinghistory_catapult`, `configs/all.loc`) -- confirmed
  from the cache directly: this loc has **no `op1` at all**, unlike the
  other four. It is pure scenery with no click option in the real game.
  `PARITY.tsv`'s "Catapult Study has no handler" was never a real content
  gap; there is nothing to write, and nothing was.

## Prior-pass note

Parity pass 2 (2026-09-23, sha `fc44e385e0`) first wrote the
`makinghistory_objloc` completion write and the Shield/Large Display Study
text, but left the Bookcase and Catapult unhandled after the pass's closer
struck two uncited invented lines for them. This pass (1b) sources and
implements the Bookcase's three real books and confirms the Catapult has no
real content to add.
