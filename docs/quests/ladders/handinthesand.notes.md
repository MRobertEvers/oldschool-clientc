# The Hand in the Sand -- driving notes (b55 parity pass)

Positions
- Bert stands at 2550,3100 (handsand_bert). Guard Captain 2551,3078 (pub).
- Sandy: handsand_sandy 2788,3176 and handsand_sandy_looking 2787,3176 are the two base spawns; the guide's handsand_sandy0 is only the client's display swap. Bind and talk to handsand_sandy.
- Mazion (handsand_naziom) 2818,3342 on Entrana.
- Betty is the shared port_sarim betty npc; her Hand in the Sand talk has no menu.

Bell and Rarve
- The bell is the Zogre loc zogre_outdoor_bell (zogre_finish.rs2:41); Rarve is not spawned. A guarded branch hands the bell to handsand_rarve_talk.
- Stages 30-60, 120-150: choose "I have a rather sandy problem that I'd like to palm off on you."
- Stage 70: that choice, then a menu: Can you help me more? / I've lost my magical scrying orb! / Not right now.
- Teleport to Betty needs an empty vial in the pack and works once (varb1531).
- Rarve takes a lost orb at stage 120 (no orb needed).

Items you need (and 2 empty vials): beer, redberries, white berries, bullseye lantern lens, 5 earth runes, bucket of sand. One vial becomes the bottled water, the other Betty puts on the counter.

Betty chain (stage 70)
- Talk (water), use redberries on bottle, white berries on juice, pink dye on lens, talk again with lens (Betty places the vial).
- The lens must be used on the counter standing on 3016,3259 (the doorway). handsand_counter_focus is the proc. The quest driver's use_on walks into the shop first and gets the refusal; prove the doorway with ::hsparity_focus_here.
- Then talk to Betty with Sandy's sand (pickpocket) to finish: stage 80, varb1532 = 5.

Sandy
- Pickpocket is op 3, level 17 Thieving, can fail ("slipped through your fingers").
- Distraction (stage 80): three lies, each works 1 time in 3 at random. Keep trying.
- Use the serum on the coffee mug only while Sandy looks out the window (stage 90). Then activate the orb (held op 1), then ask all three questions.

Lost items: Bert re-gives the hand, beer soaked hand, rota and scroll; the Guard Captain returns the beer soaked hand; Betty re-gives the serum; Mazion the head.

Differences from the guide
- Entrana: no boat or weapon banking gate (handsand_mazion.rs2:9).
- Rarve's sandpit cutscene is not ported (spec pending).
