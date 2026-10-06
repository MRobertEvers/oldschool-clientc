Ribbiting Tale notes (what the ladder cannot know)

Source: OSRS wiki + Quest Helper (LostCity has no such quest).
Everything is at the guide's tiles, around the Locus Oasis (1683,2973).

The chest in Marcellus' house (1676,2974) opens interface 809, the cache's
combination lock (ribbitingtale_locs.rs2, ribbit_chest_dial).
- Five dials, random start letters. Read each dial's middle letter and turn
  it until the word is NALIA. Then press Confirm.
- Dials from left to right: RLSNTHYDKE, ELDAOSKNRT, SERILANUTO, APORILCETN,
  LSWBPFMDTA. N=3 A=3 L=4 I=4 A=9 steps from the first letter.
- The client answers with six digits (the sixth is always 0): NALIA is
  334490. Close answers maxint and leaves the chest shut.
- A wrong word says "The chest remains locked." and closes the lock.
- The bed's love letter is only a hint (it names Nalia). Holding it is not
  a gate.
- In a driver: t.ui.widget("combination_lock:lock", 1+7*(dial-1)+1) is the
  dial's middle letter, +3 is the turn button for that dial.

Stage 12 -> 14: the tree chop and the lily pad are instant, with no
swing and no check that Dave and Jane are looking. Sabotage only works once
the tree is down (stage 12).

Stage 14 hop-off: the real game has a cutscene. Here the commentary is a
dialogue from Sue/Gary (ribbitingtale.rs2). The cutscene spec is pending
(docs/quests/cutscenes/).

Cuthbert: planting the plushy in the dung (stage 24) spawns him at
(1688,2977) at once, and he attacks. Kill him with Attack (op 2).
