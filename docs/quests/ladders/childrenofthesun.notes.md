# Children of the Sun: author notes (learned by driving)

Every line is Transcript:Children_of_the_Sun (closer b52 replaced the
worker's paraphrase). Drive by the transcript's own text.

Actors: Alina 3225,3426 and Noah 3224,3427 (spawn rows carry the BASE symbol
vmq1_alina / vmq1_noah; talk_to the base). Tobyn 3211,3437 (base
vmq1_guard_sergeant); roof Tobyn 3202,3473 plane 2 (vmq1_guard_sergeant_roof);
Itzla 3204,3473 (vmq1_itzla); cell bandit 3200,3473.

Start: the Alina/Noah intro, then "Start the Children of the Sun quest?"
Yes. (-> 2), then the menu: "When will this delegation arrive?" plays the
delegation lines (-> 4) and the bag-guard exchange, and the tail starts (-> 6).
"No." and "Sorry, but I need to head off." write nothing further.

Tail (state 6): an owner-private vmq1_bag_guard walks the guide's line points
and stops 6 ticks at five markers (3233,3427 / 3240,3417 / 3241,3403 /
3236,3392 / 3247,3397). Spotted = he is stopped, you are within 8 tiles and in
line of sight: overhead "Hey! What are you doing?" + mesbox "You failed to stay
hidden from the guard.". Lost = more than 20 tiles away: mesbox "You failed to
stay close enough to the guard.". Either deletes him; talking to Alina/Noah
again (Noah: "That was quite a large bag...") restarts him. Keep 12-18 tiles
behind; the client's guard row lags. He ends at the door 3259,3400 (-> 8).

House (state 8): click vmq1_bandit_door (3259,3400): the eavesdrop lines,
-> 10. The camera cutscene is spec-pending (not ported).

Tobyn at 10: "Move along, citizen." then the report (-> 12, guards appear).
Marking (12): Mark / Unmark (op 1) on any of ten; a fifth mark is the mesbox
"You've already marked enough guards.". Impostors are guards 1-4 (3208,3422 /
3221,3430 / 3246,3429 / 3237,3427). They wander a few tiles (a press can land
on the floor: press again), and the pool only holds npcs within 15 tiles:
goto near each. Four marked with a genuine one: Tobyn "Are you sure?...".
Exactly the four -> 14; Tobyn arrests, escorts you to the roof (teleport to
3206,3473,2) -> 16.

Roof (16-22): one conversation with Itzla or roof Tobyn (or the cell bandit):
intro -> 18 at "Excellent! Let's get started then.", interrogation -> 20 at
"Come, <name>.", aftermath -> 22 at "Itzla departs.", Tobyn's close -> 24.
A re-talk resumes at the stage reached. Completion sets vmq2_first_travel = 1.
Walking back up: stairs 3212,3474, castle_door 3219,3472 plane 1, ladder
3224,3472; the plane-2 doors 3218,3472 and 3207,3472-3473 were not hittable by
the driver (covered) -- prefer the escort.

Different from the guide: the guard stops are server rules (ai_timer in
childrenofthesun.rs2 [ai_timer,vmq1_bag_guard]), not tile-overlay guidance.
