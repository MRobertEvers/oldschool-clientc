# zombiequeen (Shilo Village) -- driver notes

Source: LostCity quest_zombiequeen, ported leg for leg. Run: parity driver, 160 rows green.

Setup needs: junglepotion done (Trufitus), agility 40, crafting 30, prayer 20, hp 90, atk/def/str 80+
(Nazastarool is lvl 91), rune armour + scimitar, spade, lit torch, rope, chisel, bronze wire, papyrus,
charcoal, 3 bones, food.

Positions the ladder point is not enough for:
- Cairn rocks zqrocks (2762,2990): stand at (2762,2988). (2764,2990) is unreachable.
- Waterfall rocks zqwaterfallrocks at (2940,9349) and (2940,9353): tele to (2939,9349).
- Palms zqquest_hidytree (2915/2916,3092); hidden doors at (2916,3091).
- Hillside doors are a MULTILOC: press by the multi symbol (hillsidedoorl_multi), not the closed loc.
  They are per-player (varbit zqdoor_multi 0 jungle/1 closed/2 open) and hide again after 50 ticks
  ("trees spring back"). Re-search the palms, then press. Repeated searches stack queues.
- Tomb: gate zombiequeengateclosedl (2928,9516) needs the beads WORN (else Rashiliyia). Rash rocks
  zq_rashrocks (2927,9511..9515). Tomb door thzq_tombrooml1 (2892,9480). Dolmen zqrashdolmen (2892,9487).
- Exit door hillsideexitclosedl (2929,9527): use the bone key from inside.

Dialogue gates:
- Cairn crawl asks "Yes Please, I can think of nothing nicer!" every time (leg 16 and 29).
- Search palms / gallows / rubble each have a Yes option; the "No" row leaves the stage alone.
- Scrolls (tattered, crumpled, notes) open a chatmenu; read = op 1 "Yes please.", close with escape.
- Burial (opheld1 zqzadimusbones at Tai Bwo Wannai centre) is a multi-page chain: loop ticks + drain
  until the boneshard lands.
- Dolmen corpse hand-in: objbox, 2 queen npc pages, mes, then queue(zombiequeen_quest_complete).
  Loop ticks(6)+drain until %zombiequeen = 15, then expect_complete.

Fights:
- Nazastarool forms 2 and 3 spawn on their own after 1 dies. With auto-retaliate on they may already
  be dead when the driver looks; treat no_row as done and read bits 9/10/11 of %zq_map_mechanisms.
- Take the corpse (opobj3 zqcorpse) after form 3: stage 14.

Ported differently:
- Tomb door bones: the state write ~set_zqtombdoorstate(1) is BEFORE the loc_change guard
  (quest_zombiequeen.rs2:1454). loc_change on the interacted loc ends the script in this engine, so
  the third bone never wrote stage 12 otherwise. LC writes after.
- Chisel on pommel / bone shard is dispatched from uncut_gem.rs2 (chisel preempts opheldu).
- Scroll interface: zq_scroll_blank (rs2:1739) does if_close first or the chatmenu lingers.
- Mound dig hook lives in general_use/scripts/spade.rs2; Shilo arms in trufitus.rs2.
- Not driven: raft exit (zqtableraft, rs2:671), fake_coins opobj3 (rs2:1361), Shanks sail.
