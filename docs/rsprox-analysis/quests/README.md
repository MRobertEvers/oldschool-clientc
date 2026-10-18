# Quest completions in the RSProx archive

Written 2026-10-18. Covers 29 transcribed sessions with 47 completions of 39
different quests. `completions.tsv` has one row per completion: session, date,
revision, world activity, tick, quest, quest points before and after, and the
scroll's reward lines.

## How the completions were found

There are 38 sessions in the archive (out of 4,283) with completions. Two
archive searches each return the same 38: interface 153 (`questscroll`) and the
game message "completed a quest". The speedrun scroll (interface 781) has no
hits.

In a transcript, each completion shows up in a single tick:

- `varp qp (101)` goes up by the quest's points;
- `message_game "Congratulations, you've completed a quest: <name>"`;
- `if_settext questscroll:quest_title (153:4)` and the reward lines (`153:9`-`153:15`);
- `if_opensub ... id=questscroll (153), type=modal`.

`tools/rsprox/rsprox_quests.py <id>.txt...` extracts the table. In every
session, the number of scrolls opened matches the number of messages.

## Normal worlds: 19 sessions, 33 completions

The rows are ordered by the quest point total. The totals link up from 280 to
339, with gaps only between recordings: 286 (Oct 2024), 319 -> 321, 327 -> 328.
So every session except **3651** is most likely one account, recorded from
October 2024 to June 2026. Session 3651 is a different account (304 -> 305).

| Quest | Session | Rev | Date | QP | QP total |
|---|---|---|---|---|---|
| Sins of the Father | 1637 | 226 | 2024-10-20 | 2 | 280 → 282 |
| Children of the Sun | 1637 | 226 | 2024-10-20 | 1 | 282 → 283 |
| Below Ice Mountain | 1636 | 226 | 2024-10-20 | 1 | 283 → 284 |
| Defender of Varrock | 1636 | 226 | 2024-10-20 | 2 | 284 → 286 |
| Twilight's Promise | 1695 | 226 | 2024-10-30 | 1 | 286 → 287 |
| The Heart of Darkness | 1695 | 226 | 2024-10-30 | 2 | 287 → 289 |
| At First Light | 1695 | 226 | 2024-10-30 | 1 | 289 → 290 |
| Death on the Isle | 1695 | 226 | 2024-10-30 | 2 | 290 → 292 |
| Ethically Acquired Antiquities | 1695 | 226 | 2024-10-30 | 1 | 292 → 293 |
| Meat and Greet | 1695 | 226 | 2024-10-30 | 1 | 293 → 294 |
| The Ribbiting Tale of a Lily Pad Labour Dispute | 1695 | 226 | 2024-10-30 | 1 | 294 → 295 |
| A Night at the Theatre | 1695 | 226 | 2024-10-30 | 2 | 295 → 297 |
| The Garden of Death | 1692 | 226 | 2024-10-31 | 1 | 297 → 298 |
| Getting Ahead | 1692 | 226 | 2024-10-31 | 1 | 298 → 299 |
| A Porcine of Interest | 1693 | 226 | 2024-10-31 | 1 | 299 → 300 |
| Perilous Moons | 1693 | 226 | 2024-10-31 | 2 | 300 → 302 |
| Temple of the Eye | 1693 | 226 | 2024-10-31 | 1 | 302 → 303 |
| Land of the Goblins | 1694 | 226 | 2024-10-31 | 2 | 303 → 305 |
| The Path of Glouphrie | 1694 | 226 | 2024-10-31 | 2 | 305 → 307 |
| Secrets of the North | 1694 | 226 | 2024-10-31 | 2 | 307 → 309 |
| Desert Treasure II - The Fallen Empire | 1703 | 226 | 2024-10-31 | 5 | 309 → 314 |
| While Guthix Sleeps | 1696 | 226 | 2024-10-31 | 5 | 314 → 319 |
| Scrambled! | 3072 | 231 | 2025-07-23 | 1 | 321 → 322 |
| The Final Dawn | 3128 | 232 | 2025-08-13 | 3 | 322 → 325 |
| Shadows of Custodia | 3127 | 232 | 2025-08-13 | 2 | 325 → 327 |
| Pandemonium | 3448 | 235 | 2025-11-19 | 1 | 328 → 329 |
| Prying Times | 3455 | 235 | 2025-11-19 | 1 | 329 → 330 |
| Current Affairs | 3467 | 235 | 2025-11-19 | 1 | 330 → 331 |
| Troubled Tortugans | 3456 | 235 | 2025-11-20 | 1 | 331 → 332 |
| The Red Reef | 4152 | 238 | 2026-05-21 | 2 | 332 → 334 |
| The Ides of Milk | 4153 | 238 | 2026-05-21 | 1 | 334 → 335 |
| The Blood Moon Rises | 4272 | 239 | 2026-06-30 | 4 | 335 → 339 |
| Pandemonium (second account) | 3651 | 235 | 2025-11-22 | 1 | 304 → 305 |

## Leagues and beta worlds: 10 sessions, 14 completions

These were already on the NAS mirror. Leagues relics speed up quests, so use
these sessions for quest text and step order, not for timing.

| Quest | Session | Rev | World | QP |
|---|---|---|---|---|
| Tai Bwo Wannai Trio | 75 | 227 | Leagues V | 2 |
| Throne of Miscellania | 91 | 227 | Leagues V | 1 |
| Royal Trouble | 91 | 227 | Leagues V | 1 |
| Sleeping Giants | 96 | 227 | Leagues V | 1 |
| Contact! | 101 | 227 | Leagues V | 1 |
| Beneath Cursed Sands | 101 | 227 | Leagues V | 2 |
| A Night at the Theatre | 101 | 227 | Leagues V | 2 |
| Sins of the Father | 102 | 227 | Leagues V | 2 |
| Shadow of the Storm | 572 | 227 | Leagues V | 1 |
| Pandemonium | 381 | 231 | Sailing Beta | 1 |
| The Ribbiting Tale of a Lily Pad Labour Dispute | 1102 | 237 | Leagues VI | 1 |
| Scrambled! | 1106 | 237 | Leagues VI | 1 |
| Death on the Isle | 1106 | 237 | Leagues VI | 2 |
| At First Light | 1108 | 237 | Leagues VI | 1 |

## Not transcribed yet

There are nine Leagues V, Grid Master and Sailing Alpha sessions with 14 more
completions: 1710, 1718, 1768, 1788, 1843, 1846, 2644, 3275 and 3333.

## Caveats

- **A recording covers only the end of a quest.** The tick column is minutes
  into the recording, not how long the quest took. Most sessions start partway
  through a quest. Read the transcript before the completion tick to see the
  final steps.
- **Rev 236+ needs a newer RSProx build.** The checkout at
  `~/Documents/git_repos/rsprox` (Dec 2025) has decoders only up to osrs-235,
  so rev 237-239 sessions fail with
  `NoSuchElementException: Key RevisionKey(revision=239...)`. Build upstream
  master (osrs-236 to osrs-241) instead. Transcribing also needs
  `~/.rsprox/settings/` to exist.
