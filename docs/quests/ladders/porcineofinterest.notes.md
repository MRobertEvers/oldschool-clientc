# A Porcine of Interest - author notes

Positions (read from the placed locs by a client probe)
- Notice board 3086,3251. Sarah 3033,3293. Spria 3092,3266 (slayer_master_9 / porcine_spria).
- Strange hole 3150,3347 (2x2). Climb-down lands at 3157,9713 (not the cave zone corner).
- Cart 3120,3300; cabbage 3130,3314; potatoes 3132,3313; carrot 3146,3342; tree 3148,3344.
- Exit rope 3157,9714 goes up to 3151,3346 (own trigger; the shared climb_up default is wrong).
- Blockage 3156,9704: Climb-over walks you 3157,9705 <-> 3157,9703. Skeleton 3163,9676.

Gates
- Stage 10 rope: use a rope ON the hole (consumed). The hole alone says it needs a rope.
- Trail props are optional: a mesbox each (carrot and potatoes add a player line); the cart
  sets porcine_inspected_cart but nothing gates on it.
- Stage 25 needs the goggles WORN at the hole and at the blockage. Carried is refused.
- Blockage at stage 25, heading south: line, then Yes/No. Yes raises the quest Sourhog
  at 3157,9697, owned by you, set to attack. Repeat clicks do not raise a second.
- Dialogue gates: Sarah "Talk about the bounty." (else shop). Notice board "Yes.".

Fight
- Sourhog hp 30, level 37. About 1 in 4 swings is acid spit: goggles worn = nothing;
  else "Argh! My eyes!", 6-16 damage (combat_damage_player) and -5 Attack/Defence.
  Numbers approximate (wiki gives none).
- Blockage prompt: "Climb over the blockage?" Yes / No (no full stops).
- After the kill, the exit rope refuses until a foot is cut ("Before I leave, I should go
  and collect a foot...").
- Bring food. Whip works to fight; it is not accepted for the foot.

Foot
- The cache places no corpse; the kill (or crossing the blockage at stage 30) adds
  porcine_dead_sourhog at 3157,9697. Cut-foot needs a knife or a wielded slash weapon
  (bronze scimitar proven); whip, tentacle, noxious halberd, dragon claws refused.
- Lost foot: climbing down with none held/banked resets the corpse.

Different from the guide
- Pig Thing cutscene is a text blackout then teleport to Spria (porcineofinterest_locs.rs2
  poi_investigate_skeleton). Spec pending with the cutscene session.
- Music Safety in Numbers unlocks on climb-down (varp2237 bit 24).
