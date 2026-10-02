Client of Kourend notes (driven 2026-10-02, source: wiki transcript, no LostCity).

Veos (Piscarilius) is a multinpc shell: it only resolves while varb12146_veos_pisc_vis = 1.
X Marks the Spot completion writes it; `::complete quest_xmarksthespot` does NOT, so a
driver must `::setvar varb12146_veos_pisc_vis 1` in setup.
Veos stands at 1825,3691. 1825,3694 is a ship deck ("I can't reach that!"); stand at 1824,3689.
Port Sarim Veos (state 0) only ferries you across; the offer is made on the Piscarilius docks.

Dialogue gates:
- Veos menu: "Have you got any quests for me?" then "Yes." (the "No." arm declines).
- Each store keeper offers "Can I ask you about <city>?" only at stage 1 and only until that
  house is written down. Without BOTH scroll and quill a mesbox says so and nothing is saved.
- Topic menus: the first topic loops back, the second topic records the house.
- The fifth house writes stage 2 (whisper box). Veos at stage 2 takes scroll+quill (stage 3),
  then hands the orb (stage 4) only into a free slot.
- Orb Activate within 8 tiles of 1712,3883, stage 4 only: stage 5, orb removed.
- Veos at 5: the possession ("ARGH!") writes 6; rewards need 3 free slots (memoirs + 2 lamps),
  else a mesbox and the quest stays at 6.

Storekeepers (no wandering seen): Leenz 1807,3723; Regath 1720,3724; Munty 1551,3749;
Jennifer 1519,3591; Horace 1773,3588. Each has a trade option from the same greeting.

Different from the guide: the lamp is veos_lamp (Rub opens the shared xpreward picker, kind 7,
quest_atailoftwocats/scripts/twocats.rs2 twocats_lamp_obj; 500 XP, any skill, no level).
Veos's non-quest menu lines (Where am I / take me somewhere / advice) are not ported:
the transcript gives no replies. Stage 2/3/6 resume points exist (clientofkourend.rs2:178,207,257).
