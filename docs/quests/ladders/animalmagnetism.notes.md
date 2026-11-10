# Animal Magnetism: what the ladder cannot know

Source: OSRS wiki (Transcript:Animal_Magnetism oldid 15263367, every page) + Quest
Helper. LostCity has no copy, so there is no LostCity table. No RSProx recording of
the quest yet; lines the transcript does not cover are marked UNVERIFIED in the scripts.

Malcolm (anma_ghost_farmer_multi) is a shell spawn at 3618,3526 from anma.spawn.
Alice is the world spawn at 3627,3526. Malcolm needs the ghostspeak amulet worn
("Woo woo!" without it). The Old crone does not.
The farm is four messages: Malcolm 10 -> 20 (he misses his wife), Alice 20 -> 30
(she loves him, where are the savings), Malcolm 30 -> 40 (in the bank), Alice
40 -> 50 (she needs his bank pass), Malcolm 50 -> 60 (he won't give it), Alice
60 -> 70 (a witch west of here alters ghostspeak amulets). Alice's quest pages are
row 4 of the farming shops' menu, "I'm here about a quest."; between messages she
and Malcolm answer with one line each and no menu.
The Old crone (ahoy_crone) is shared with Ghosts Ahoy. If both quests are live
she asks which one you mean: pick "I'm here about the farmers east of here."
Crone 70 -> 73 takes a hair (a mesbox); 73 -> 76 needs a ghostspeak amulet (worn
or in the pack) and a free slot, and hands over the Crone-made amulet.
Malcolm takes it (choice "Okay, you need it more than I do, I suppose.") 76 -> 80.
The next talk is the chicken scene (pages only, no actors or camera) and ends at
100. Malcolm sells on the talk after: "Could I buy those chickens now, then?" then
"Could I buy 2 chickens?" (or one at a time). 10 ecto-tokens each; bring 20.
Ava takes the chickens one at a time or both (%varp9594_anma_progress bit 0).
Alice's shop is option 1 of her menu, or Trade (op 3). Both open farming_shop_4.
The witch wants 5 iron bars, hands back one Selected iron (anma_iron_bar).
Hammer it inside the Rimmington mine box (anma.constant anma_rimm_mine_*).
The guide says face north; the port does not check facing (no facing query).
Ava's magnet hand-in gives 50 Crafting XP.
Undead trees: state 150 swaps the multinpc to the choppable form. The first
chop with a mithril-or-better axe bounces (150 -> 160). Turael (Burthorpe; Aya after
While Guthix Sleeps, under "More options...") needs two talks from his own menu:
"I'm here about a quest." (bit 1), then "Hello, I'm here about those trees again."
and "I'd love one, thanks."; mithril axe and holy symbol are consumed (170 -> 180).
Twigs: 30 percent fail chance per chop.
Ava takes the twigs (190 -> 200) and hands the notes over on the NEXT talk (bit 2).
Research notes: nine toggles in the notes interface; they open all on (511); the
solution is a mesbox, "It suddenly all makes sense."
Pattern + hard leather + polished buttons: use-on in anma_notes.rs2.
Stage 240 gives 1 qp and 1000/1000/1000/2500 xp as the scroll shows.
