# Enlightened Journey relay: where every hand-in comes from (seam matthew-mbp-m4-b50-seam1)

The round-1 file was rejected for `::give`s of the gathered kit. Every item now has an
in-game source in the pack, and each route below has been driven. The proof copy is
`build/seam_state/matthew-mbp-m4-b50-seam1/ej_copy.lua`: round1_rejected.lua with every
mid-run `::give` replaced by these legs. It ran 221/221 PASS to `quest.varp_complete` 200
(`build/quest_gate/s1_ej_copy`). Its setup gives only the guide's brought-along kit (papyrus 3,
ball of wool, unlit candle, tinderbox) plus `::give coins 500`. The coins pay for the shops,
Aggie and the silk.

Sources:
- Wiki Enlightened Journey oldid 15292357 ("items": trip 1 and trip 2 lists, "Ballooning" note).
- Wiki Willow branch oldid 15184331.
- Wiki Willow tree (Farming) oldid 15287151.
- Wiki Auguste's sapling oldid 15185627.
- Wiki Potatoes oldid 15183483.
- Wiki Empty sack oldid 15183845.
- Quest Helper EnlightenedJourney.java @5ea99d5e. It lists all of these items as
  `getItemRequirements` (brought along). None of them is marked `canBeObtainedDuringQuest`.
  The willow tooltip says "using secateurs on a willow tree you've grown. Auguste will give you
  a sapling".

## Content added for this (OSRS-Content, skill_farming)

- **Willow trees in tree patches.** `farming_trees` rows `farming_tree_willow` (willow sapling)
  and `farming_tree_willow_auguste` (`zep_plantpot_willow_sapling`):
  - Farming level 30, six stages of 40 minutes.
  - Patch values: 15 is the seedling, 16 to 20 are growing, 21 is check-health, 22 is the chop
    morph with no branches, 192 to 197 are the chop morph with 1 to 6 branches, 23 is the stump.
  - Check-health, chop and clear are now bound for the willow morphs.
- **Willow branches.** After check-health, one branch grows every 5 minutes, up to 6.
  - Use `secateurs` on the patch to cut every branch the tree has, 2 ticks each.
  - The branch count is caught up from the deadline, so a 30-minute skip gives all 6.
  - Once the tree is full, the first cut restarts the 5-minute clock.
  - Chopping the tree to a stump destroys the branches.
- **Fill on vegetable sacks** (`farming_sacks.rs2`). Op1 on an empty sack fills it with 10
  vegetables, taking cabbage first, then onion, then potato. Op1 on a part-filled sack tops it
  up to 10. This is the only source of `sack_potato_10` ("Potatoes(10)").

## Trip 1: before the first boat (all of it fits: the pack is exactly 28 when the silk is in)

The order matters. Pick the potatoes while 10 slots are still free, and buy the silk last.

| Item | Where | How (verbs as driven) |
| --- | --- | --- |
| 9 empty sacks (8 sandbags + 1 potato sack) | Sarah, `farming_shopkeeper_1`, 3038,3292 (south Falador farm) | `goto_tile 3036,3290,0`; `shop.open("farming_shopkeeper_1", 3, "farming_shop_1")`; `shop.buy("sack_empty", 9)`; `shop.close()` |
| 3 redberries | Wydin, `wydin`, 3014,3204 (Port Sarim food store). Stock is 1, so he restocks between buys | `goto_tile 3014,3206,0`; 3 times: `shop.open("wydin", 3, "wydinstore")`, `shop.buy("redberries", 1)`, `shop.close()`, `t.ticks(110)` |
| 2 onions | Fred's field, loc `onion`, 3188-3189,3266-3268 | `goto_tile 3187,3265,0`; `click_loc("onion", 2)` twice; `inv.await("onion", i, 10)` |
| red dye, yellow dye | Aggie, `aggie`, 3086,3259 (Draynor). 3 redberries or 2 onions, plus 5 coins | `goto_tile 3086,3257,0`; `use_on("redberries", by_symbol("npc","aggie"))`, then `chat.play{"player:Okay, make me some red dye please.","mesbox:You hand the berries"}`; then `use_on("onion", aggie)`, `chat.play{"*"}` |
| sack of potatoes (Potatoes(10)) | loc `potato`, 3138-3140,3272-3282 (north of Draynor). Also on Entrana at 2820-2822,3360-3361 | `goto_tile 3141,3276,0`; `click_loc("potato", 2)` x10, each followed by `inv.await("potato", i, 10)`; then `t.player.inv_op("sack_empty", 1)`. It answers `timeout` because the sack becomes another obj, so read `inv.count("sack_potato_10") == 1` back |
| 10 silk | silk trader, `silk_trader`, 3299,3204 (Al Kharid). One per talk, 3 gp | `goto_tile 3298,3206,0`; 10 times: `talk_to("silk_trader", 1)`, then `chat.play{"npc:Do you want to buy any fine silks?","choose:How much are they?","player:How much are they?","npc:3 gp.","choose:Okay, that sounds good.","player:Okay, that sounds good.","mesbox:You buy some silk for 3 gp."}` |

## On Entrana, before the hand-ins (stage 70)

| Item | Where | How |
| --- | --- | --- |
| bowl | spawn `bowl_empty` 2816,3351 **level 1** (m44_52.spawn:50). It is in Auguste's own house (2816-2822,3350-3356), up the ladder: the wiki's "house just east of Auguste, up the ladder" | `goto_tile 2817,3352,0` (inside the house); `click_loc("ladder", 1)`; await level 1; `click_obj("bowl_empty", 3)`; `click_loc("laddertop", 1)` to come down |
| 8 sandbags | `sandpit` 2817,3342 | as the round-1 file: `use_on("sack_empty", sandpit)` x8 |

After the last hand-in (the bowl), Auguste gives `basket_apple_5` and
`zep_plantpot_willow_sapling` (ej_shared.rs2:289-290).

## Trip 2: willow branches and logs

1. **Leave Entrana.** `goto_tile 2832,3336,0`; `talk_to("shipmonk2", 1)`;
   `chat.play{"npc:Do you wish to leave holy Entrana?","choose:Yes, I'm ready to go.","player:Yes, I'm ready to go.","npc:Okay, let's board"}`;
   `t.ticks(8)`; `click_loc("ship_to_entrana_off", 1)` (the Port Sarim gangplank, level 1).
2. **Tools from Sarah.** `shop.open("farming_shopkeeper_1", 3, "farming_shop_1")`, then buy
   `rake`, `spade` and `secateurs`, 1 each.
3. **Falador park tree patch.** The patch is `farming_tree_patch_2` at 3003,3372; its state is
   varbit `varb701_varbit_701`.
   - `goto_tile 3002,3376,0`.
   - If the state is not 3: `click_loc("farming_tree_patch_2", 1)` (Rake), then
     `var.await_server("varb701_varbit_701", 3, 80)`.
   - `use_on("zep_plantpot_willow_sapling", by_symbol("loc","farming_tree_patch_2"))`, then await
     the state at 15.
4. **Grow.** Six times: `t.clock.skip(40)`, then
   `var.await_server("varb701_varbit_701", 15+stage, 560)`.
   - Ordinary farming re-arms from now, so each skip is one stage.
   - The growth softtimer is 500 ticks, so each await takes about 499 ticks.
5. **Check health.** `click_loc("farming_tree_patch_2", 1)`. The state goes to 22.
6. **Branches, twice.** Each round:
   - `t.clock.skip(30)`.
   - `var.await_server(..., 197, 560)` (6 branches).
   - `use_on("secateurs", patch)`.
   - `inv.await("willow_branch", 6*round, 40)`.
   - Mind the pack: 3 weeds from raking plus the tools fill it.
7. **Logs.**
   - Before chopping, drop the 3 weeds, the empty plant pot, the rake and the spade. The pack is
     23 full otherwise and stops at 5 logs.
   - Bob: `talk_to("bob", 3)`, then `chat.play{"player:Have you anything to sell?","npc:Yes! I buy and sell axes!"}`,
     `shop.attach("axeshop")` and `shop.buy("bronze_axe", 1)`. Bob's Trade speaks two pages
     first, so `shop.open` times out.
   - Chop with `click_loc("tree", 1, {at = {x,z}})` over the trees west of Lumbridge castle:
     3168,3233 3171,3236 3178,3238 3181,3237 3180,3224 3173,3216 3170,3213 3179,3212 3177,3208
     3161,3208 3158,3209 3156,3214 3154,3217 3154,3224 3146,3226 3145,3222.
   - Name the tile. A bare "nearest tree" once walked over the river into the goblins and the
     10-hitpoint character died.
   - `drop("bronze_axe")`. The wiki says axes cannot be taken to Entrana.
8. **Back to Entrana.** Use the monk `shipmonk1_c` and the gangplank `ship_from_entrana_off`,
   as on trip 1. `goto_tile 2808,3355,0`, then the round-1 file's basket and flight rows as they
   are.

## Not built / not checked

- Paying a gardener the basket of apples to protect the tree. The wiki says it is optional, and
  disease is stubbed in this pack.
- The "Remove-one"/"Empty" sack ops and using a single potato on a sack.
- Willow stump regrowth. The stump stays until it is cleared, as the oak's does.
- Pick the Entrana potatoes instead if you like. That needs a 10th empty sack on Entrana.
- The branch and cut chat lines ("You cut a willow branch from the tree.", "There are no branches
  on this tree to cut yet.") are the port's own text. The wiki pages above give no message
  text.
