# The Garden of Death -- what the ladder cannot know

Source: OSRS wiki (no LostCity version). Driven end to end 0 -> 56 by a scratch driver, 364 rows.

Stages the port writes (cache ladders gate every Search op): 4 journal read, 6 in dungeon 1,
13/27/37/47 tablet taken, 14/28/38/48 tablet read, 16/30/40/50 the dungeon's words known,
22 vines inspected, 24 cut, 52 final reread, 54 warning note taken, 56 done.

Tiles that differ from the guide:
- Every rope/hole puts you on a tested tile beside it: dungeon 1 1309,9869; dungeon 2 1374,10032;
  dungeon 3 1295,10033; dungeon 4 1457,9822. Surface exits 1308,3468 / 1364,3635 / 1314,3615 / 1449,3511.
- Boaty has four docks (Molch, Molch Island, Battlefront, Shayzien); it lands you on the open
  tile beside the destination boat (1342,3645 / 1367,3640 / 1384,3665 / 1408,3612).
- Dungeon 2: the vines (1375,10024) split it. The tablet table is south of them; squeeze back
  north (op1 Squeeze-through) to reach the rope. The rune-diagram rubble (1375,10012) is only
  clickable from the south end (1375,10010).

Gates between steps:
- Hole 1 refuses until the journal is read ("no reason to go down there").
- Holes 2-4 are "Tree" / "Huge Mushroom" / "Forest Mushrooms" with no op until the stage opens.
- Vines: Inspect (-> 22), Cut with secateurs (-> 24), Search for the dirty note, Squeeze-through.

The translation puzzle (the part a driver gets wrong):
- A word can be typed only after it was SEEN: inspect a carving (the interface text is the word
  list), read a chest label/the compass, or read a tablet. Unseen -> "has potential" message.
- Any meaning of a word counts. Stage sets (Quest Helper): 1 island water time vessel north;
  2 west poison body food earth; 3 make yes no move arrive east south;
  4 few big sun moon life death mind home air fire. Finishing a set ends the loop with the
  "translated enough" message and writes the next stage.
- Taking a carving does not teach its words; inspecting it does.
- Tablets: green = meaning fits the sentence, blue = only another meaning known, plain = untouched.
  "below" (osto) is seen by reading tablet 1, so it can be typed any time after.
- Fully translated (varbit 14675): 21 more meanings after the quest, ending with "below".
- Attempt Translation is a resume button; after each answer the "Attempt another translation?"
  menu reopens the name prompt (the chat verbs read that reopen as "stale reopen": use
  name_entry then chat.play the mesbox).
- The note on the back of tablets 1 and 4 is offered AFTER the tablet is closed (a queue).
  Reading the warning note ends the quest after the book is closed.

Different from the wiki/guide: word-to-carving seen sets come from the cache interface text;
the poison chest label is "Achi Toka" (wiki transcript). Music unlock "The Old Ones" is not done.
