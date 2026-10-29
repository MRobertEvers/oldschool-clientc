# RFD Start (index 4400): the generic content

Recording: RSProx index 4400 "RFD Start" (2026-10-28, rev 241.1, w495). The
Recipe for Disaster rows (the Cook, `hundred_*`, Aris, the banquet, the journal,
ashes on the fruit blast) are in [rfd_start.md](rfd_start.md). This file covers
every OTHER row of `rsprox_actions.py`, with ticks cited from 4400.

Test: `test/quests/_gnome_cocktails.lua`

    python3 tools/quest_gate/run.py --script test/quests/_gnome_cocktails.lua --name gnome_cocktails --no-publish

The last run passed 40 of 40 rows. The tick log shows the gate and Femi's boxes
on the recorded ticks (see "Femi and the gate" below).

## 1. Gnome cocktails (ticks 801..1280)

Our version before this work was the 2004 LostCity system. You used each
ingredient on the shaker, varps 6570/6571 counted them, and pouring made
`unfinished_*` items. Nothing bound the shaker's Mix-cocktail. OSRS uses the
7 August 2006 "Gnome Cuisine" system. Mix-cocktail opens interface 49
`aluft_cooking`, whose panel the client draws from `enum_2323` (clientscript
3779 `gnome_cuisine_init`). Create turns the shaker into `aluft_shaker_<drink>`,
and Pour then needs a glass and the garnish. The wiki's Cocktail shaker page
says: "Ingredients can not be used on the shaker directly".

Rebuilt in `skill_cooking/scripts/gnome_cooking/gnome_cocktail_shaker.rs2`.

| row | recorded | ours before | verdict |
|---|---|---|---|
| Mix-cocktail (inv op 2 = opheld1) [844] [865] [877] [901] [1175] | `toplevel_mainmodal_open` 2524 (-1,-1); `if_opensub` mainmodal `aluft_cooking`; `if_setevents cuisine_clickzone 0..7 OP1`; busy 1 | "You look into the cocktail shaker..." mesbox (a guess) | fixed |
| dish click `cuisine_clickzone` sub 0 [850] [903] [1177] | nothing on the wire (the client shows the ingredients itself) | unbound | fixed: the pick is stored in varbit 2517 `gnome_chosen_cuisine`, which the panel's vartransmit hook reads. Sub 0 with the varbit already 0 sends nothing, as recorded |
| Create without the level [905] (all three fruit, Cooking 4) | mesbox echo, `if_closesub aluft_cooking`, messagebox "You either don't have all the ingredients, or the cooking level required (or both) to make this." (2 rows, lineheight 31); busy 0 on continue | unbound | fixed, the same order (`~gnome_cuisine_mesbox`); test `mix.low.*` |
| Create, Fruit blast [1178] | pineapple, lemon, orange and the shaker removed; `aluft_shaker_fruit_blast` in the first free slot (slot 9, where the orange was); 30 xp; panel closed; mesbox "You just need to pour this into an empty cocktail glass and garnish with sliced lemon before serving." | none | fixed; test `mix.inv`, `mix.xp` (+30) |
| other 6 cocktails | not recorded | 2004 use-on flow | ingredients and levels from the cache's structs 594..600; mix 30 xp each (the wiki recipes). The mix lines are modelled on the Fruit blast line: UNVERIFIED |
| Pour without the garnish [1205] | game message "You need a slice of lemon to finish this cocktail." | none | fixed; test `pour.noslice` |
| Pour [1269] | the drink takes the mixed shaker's slot, `cocktail_shaker` takes the glass's slot, the slices go; 20 xp; mesbox "You pour the cocktail and add the finishing touches." (lineheight 0); busy 1 | none | fixed; test `pour.inv`, `pour.xp` (+20) |
| Pour, the others | not recorded | none | the wiki's garnishes and finish xp (punch 40, blizzard 80, sgg 90, special 150). The drunk dragon and choc saturday pour into `_prefinish` / `_preheat` at 0 xp. Add-ingreds and the range step (new `cooking_generic` rows: saturday 0 xp, dragon 130 xp) are UNVERIFIED |
| glass on mixed shaker (use-on) | not recorded | (old pour) | the same pour (the wiki says it works), UNVERIFIED |
| ingredient on the shaker | not recorded | added the ingredient | "Nothing interesting happens." (the wiki: not possible). The old `@add_cocktail_ingredient` label is kept as that answer for its 5 callers |
| knife on lemon [1260..1263] | skillmulti 270 `skillmulti_setup(12, "How would you like to cut the lemon?\|Slice it\|Dice it", 1, lemon_slices, lemon_chunks, ..., 1)`, `skillmulti:a/b` armed; the pick closes it on its own tick; next tick spam "You cut the lemon into slices." and the slices in the lemon's slot; no anim, no sound | `~p_choice2("Slice the lemon.", "Dice the lemon.")`, `mes` | fixed (`cutting_fruit.rs2`): the panel, mode 12, the cap = lemons held, `mes_spam`, the cut on pick+1; test `cut.*`. Lime, orange and pineapple use the same title with their own name, and batches cut every 2 ticks: UNVERIFIED |
| Hudo Talk-to [824..833] | 5 pages ("...I have a large selection." with a full stop), then the shop opens on "Great stuff."'s continue | "...large selection" with no full stop; "Hudo has nothing to trade with yet." | fixed; test `hudo.*` |
| Hudo Trade (opnpc3) [853..897] | "Grand Tree Groceries", gnomeshop_hudo: stock rows 10/11 are `tbw_spider_on_stick_cooked` (6297) and `_shaft_cooked` (6299), count 0; prices shaker 2, knife 6, fruit 2 | stock rows 10/11 raw spiders | fixed (`grand_tree_groceries.inv`); prices matched; "You don't have enough inventory space." [875] is generic |
| gnome waiter Talk-to [804..815] | 6 pages (the 4-row page ends "...that they will", the next is "not be accepted..."), then Gianne's Restaurant opens on the last continue | forced `\|` breaks; "The gnome waiter has nothing to trade with yet." | fixed (one string, our pager splits it as Jagex does); test `waiter.*` |

Open (cocktails):
- The packets inside a tick: ours sends `busy` after 2524 (Jagex sends it before). The engine sets it.
- Our `if_addresumebutton` arms skillmulti cells 0..28; Jagex arms 0..max (0..1 here). This is engine-generic.
- The interface's other three tabs (battas, crunchies, bowls; varbit 698 `aluft_cooking_type` 1..3) still use the 2004 half-baked flow. No recording covers them.
- Drunk dragon heating: the wiki says it can fail to give xp below level 38. Not modelled.

## 2. Every other generic row

| tick | action | recorded | ours before | verdict |
|---|---|---|---|---|
| 60, 94 | `banker1_east`/`banker2_east` opnpc3 | bank opens | `[opnpc3,_bank_teller]` (category 249) | matched (bound; generic bank) |
| 77, 98, 416, 566-568, 976-1020, 1117, 1123, 1172, 1500-1564 | bankmain/bankside ops | withdraw/deposit | the bank interface | engine-generic, not re-diffed |
| 104, 145, 1041, 1591 | Varrock/Lumbridge tablet Break | C+1 `poh_smash_magic_tablet` d16 + synth 965 d15; C+3 `poh_absorb_tablet_*`, tablet -1; C+5 teleport (3212,3423 / 3214,3424; 3222,3219 / 3221,3217) | `[opheld1,poh_tablet_*]` `@break_poh_tablet` (already rebuilt to this shape in the Tree Gnome Village work) | matched |
| 119, 128 | `pillory_rotten_tomato_crate` oploc1 + Buy | L+1 face + shop "Pelters' Veg Stall" (`pillory_rottenfruitshop`, 1 cell, rotten tomato 2,000); one tomato for 100 coins; restocked a tick later | unbound (Buy did nothing); inv generated as `stock=empty` | fixed: `shop/varrock/scripts/pelters_veg_stall.rs2` + `configs/pelters_veg_stall.inv`; the generated block now has only `allstock=no`. Test `pelters.*` (100 paid). The stall's buy-back price is not recorded |
| 159.. | Cook | | | RFD (rfd_start.md) |
| 396, 402 | `spiralstairsbottom_3` op1, `spiralstairsmiddle` op2 | L+1 face (sizex/z 2), L+2 teleport same tile plane+1; from standing, the teleport on C | category `climb_up` / `climb_spiral_middle` | matched (the skill's 4286 shape) |
| 408, 409, 969, 1091 | `aide_bankbooth(_multi)`, `bankbooth`, `exchange_bank_wall_bank` oploc2 | bank | `bank_booths.rs2` | matched (bound) |
| 419, 433, 443 | `logs` opobj4 (Light) | L+1 spam "You attempt to light the logs." + `human_createfire`; again at +3; on success a step (seq -1) and the next tick "The fire catches..." + face + obj_del + fire loc + synth `fire_lit` (2596). NO sound on any attempt | the same timing, plus `tinderbox_strike` (2599) on every attempt | fixed: the strike sound removed. 2599 is in 0 of the index's 4401 sessions, 2596 in 110 |
| 516 | logs on tinderbox (inventory) | the same, the log dropped first | `@light_logs_inv` | matched (sound fixed as above) |
| 512, 531, 1423 | opobj3 logs/ashes/cooked chicken | take | engine take | engine-generic |
| 575, 966 | wield rune scimitar, wear glory (inv op 3) | appearance, category varbit, `equip_sword` | `~equip` | engine-generic |
| 582..585 | necklace of passage Rub -> The Outpost (an `if_subop`, op 6 subop 1; not in the actions list, found from the [585] teleport) | C spam "You rub the necklace...", `human_castteleport` + `teleport_casting` h92, sound_area `teleport_all` range 5; C+3 purple "Your necklace of passage has three uses left.", teleport to 2429,3348,0, (4)->(3) in the same slot | **unbound**: no necklace of passage script | fixed: `general/scripts/enchanted_jewellry/necklace_of_passage.rs2` (`last_subop`, `~player_teleport_normal`). Test `passage.*`. Wizards' Tower (3114,3179 r2), Eagles' Eyrie (3405,3158), the other charge lines, "crumbles away" are the wiki's, UNVERIFIED; Wyrmscraig is refused; the worn Rub is OPEN |
| 612, 618 | world map orb / close | client | client | client-generic |
| 635 | `oaktree` oploc1 (no axe; axe banked [567]) | walk, face [642], reset [643], no message | woodcutting | not diffed (no answer recorded; a no-axe message would be expected) -- OPEN |
| 651 | `boxes3` oploc1 | L+1 face + "There is nothing interesting in these boxes." | not checked | OPEN (not diffed) |
| 659, 678, 711 | `gnome_areagate` oploc1 | Femi beside it and her boxes not helped: her pages at L+1 (below). Crossing: O face (5x2), O+1 step onto the entrance tile, O+2 face through, O+3 the gate open (`loc_del`, `_open_right` 2462,3383 r0, inviswall 2461,3383 r3, `_open_left` 2459,3383 r2, inviswall 2461,3384 r1), O+4..O+6 three steps, O+7 the gate back (4 ticks). No sound | opened on O+2, steps from O+2, 5 ticks | fixed (`gnome_gate.rs2`); the tick log matches on every tick |
| 661..702 | Femi's boxes | pages "Hello there." / "Hi!" / "Could you help me lift these boxes? They are really<br>heavy!" / menu "Sorry, I'm a bit busy.\|Okay then." / "Okay then." / "Thanks traveller!"; from the last continue C: steps C+1, C+2, face C+5, `human_pickupfloor` C+6, the boxes `loc_del` (2460,3380) + face 2460,3379 C+7, pick-up C+8, `cart_boxes` 2458,3378 r3 C+9, "Thanks again friend!" C+10 | forced break after "boxes?", "OK then.", the boxes turned into an inviswall, face 2458,3378, everything a tick early | fixed (`quest_grandtree/scripts/femi.rs2`); test `femi.*` and the tick log |
| 672, 674, 706 | Femi opnpc1 | "The little gnome is too busy to talk." | the same | matched |
| 745 | 3-dose stamina (drink) | spam lines, `human_eat`, synth `liquid` 2401, varbit `stamina_active` | consumption db | engine-generic, not re-diffed |
| 752 | `stronghold_ent` **op2** (Talk-to) | mesbox "The tree doesn't feel like talking." at L+1 with the face (Tree Gnome Village done, The Grand Tree not started) | only op1 bound, gated on Tree Gnome Village (the Talk-to click did nothing; op1 Travel exists only at Grand Tree 160) | fixed: `[oploc2,stronghold_ent]`, gated on varp 150 < 160 (the multiloc's own var). Test `ent.*`. Travel/Last-destination (menu 217) are OPEN |
| 776 | `treedoorr` oploc1 | L+1 the open door + synth 62 (loops 1); L+2, L+3 steps; face back L+4; door back at L+6 | the same, synth loops 0 | fixed (loops 1); the face back is not sent (OPEN, minor) |
| 783 | Narnode opnpc1 | "Welcome Traveller. I am King Narnode. It's nice to<br>see an outsider." (chathap2) / "Hi! It seems to be a busy settlement." / "For now." (chatscared1); the recorder left for the ladder | forced break after "Narnode." | fixed (`king_narnode.rs2`) |
| 787 | `grandtree_ladderbottom` oploc1 | L+1 face, L+2 `human_reachforladder`, L+3 teleport plane 1, same tile | `climb_up_ladder` -> `~climb_ladder_anim` (p_delay 0) | matched |
| 801..1280 | gnome waiter, Hudo, cocktails | section 1 | | fixed |
| 887 | eat lobster | spam "You eat the lobster.", `human_eat`, synth `eat` | food.rs2 | engine-generic |
| 914..956 | stats / skill guide / quest list tabs | interface | interface scripts | client/interface-generic, not diffed |
| 1095..1257 | GE clerk opnpc3 and the whole offer flow (`ge_offers`, objdialog, price/quantity, collect) | the full GE | **unbound**: no `[opnpc3,ge_clerk_*]`, `interface_grandexchange` holds only matching procs | OPEN (a whole system) |
| 1126, 1136.. | raw chicken on `forestry_fire` (category 152 = cooking_fire) | menu "How many would you like to cook?\|Raw chicken" (mode 6, cap = count); then each cook: `human_firecooking` + synth `fry` + the item + ONE spam line ("You cook some chicken." / "You accidentally burn the chicken.") on the SAME tick, every 4 ticks (1139, 1142, 1146, 1150, 1154, 1158) | reached via `[oplocu,_cooking_fire]`; but each cook prints "You cook the chicken on the fire..." with the anim and "The chicken is now nicely cooked." two ticks later with the item | OPEN: the whole cooking skill's per-item shape and its per-food lines (the 2004 db text); one recording only covers chicken. Tutorial Island (4289 [265..268]) shows a different single-item shape (item at C+3) |
| 1165, 1348, 1586 | drop burnt/cooked/raw chicken (op 7) | `obj_add` + `put_down` | engine drop | engine-generic |
| 1275 | ashes on fruit blast | | | RFD |
| 1621.. | quest scroll, `hundred_*`, journal | | | RFD |
| 2220 | logout | | | engine-generic |

## Teleports

| tick | cause | verdict |
|---|---|---|
| 109, 150, 1045, 1595 | teleport tablets | matched |
| 400, 402 | spiral stairs | matched |
| 585 | necklace of passage Rub -> The Outpost (`if_subop` at [582]) | fixed (was unbound) |
| 791 | Grand Tree ladder | matched |
| 1660..2213 | RFD | RFD doc |

## Open

- GE clerk / the GE offer flow (unbound; a whole system).
- Cooking on a fire or range: the item, anim and line all on one tick every 4
  ticks, and the per-food OSRS lines. Ours prints the 2004 pre-message and the
  item 2 ticks after the anim.
- The Stronghold spirit tree's Travel / Last-destination (menu 217) and the
  spirit trees in general (LostCity flow).
- Necklace of passage: the worn Rub, the plain Rub (no submenu entry), and
  the three unrecorded destinations' tiles.
- Gnome battas, crunchies and bowls (`aluft_cooking_type` 1..3) are still the
  2004 flow.
- `oaktree` with no axe and `boxes3` were not diffed.
- treedoorr: Jagex faces the door again one tick after the last step; ours does not.
- Engine order inside a tick (busy before 2524; resume-button ranges 0..28
  against 0..max).
