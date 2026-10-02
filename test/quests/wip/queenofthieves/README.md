# queenofthieves -- work in progress

`sent_back_b53.lua` is the green file the matthew-mbp-m4-b53 sampler reverted (1420d9ba2,
73/73 PASS). Everything before the Queen is sound. The fault: `goto-talkToQueenOfThieves`,
`goto-exitWarrens2` and `goto-talkToShauna` teleport in and out of the Queen's tent past
`piscquest_tentdoor` (1765,10149, "Doorway", op1 Go-through), and that doorway has no script.
Script the door in `quest_queenofthieves/scripts/queenofthieves_locs.rs2` (open from
`^qot_queen`), then `click_loc` it from 1765,10148 in, and from inside out
(docs/quest_authoring/sampler-findings.md, Sample matthew-mbp-m4-b53 (a)). The stew is bought,
not cooked (content-gaps: The Queen of Thieves: an uncooked stew).
