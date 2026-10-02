## leg 1
- Leg 1 (steps goToTimfrakuLadder..getRum) is driven and green through fillVessel; getRum is BLOCKED as a content_bug: Zembo has no spawn/shop (m45_49.spawn has no zembo; nothing in server/scripts mentions him), so karamja_rum cannot be bought. Leg 2 cannot start until content places Zembo + a shop selling karamja_rum.
- Player ends at Musa Point 2925,3143,0 (no checkpoint written, t.blocked ends the run). Quest var varp320_tbwt_main = 3 (started); varp6052_tbwt_lubufu = 31 (apprentice).
- Backpack: small net, 500 coins, ~3 raw karambwanji left, 6 raw shrimp (bycatch), 1 tbwt_karambwan_vessel + 1 tbwt_karambwan_vessel_loaded_with_karambwanji.
- Setup gives: fishing 5, firemaking 30, agility 15, cooking 30, Jungle Potion complete, net, coins 500.
- Surprises: Timfraku's talk closes the chat for ~4 ticks (if_close, p_delay) mid-dialogue, so chat.play is split; Lubufu is only reachable from 2771,3169 (2770,3167 says "can't reach"); the ladder click works from goto 2781,3089; fishing spot press 0_43_47_karambwanji gives 23 karambwanji in ~165 ticks.

## seam matthew-mbp-m4-b49-seam1 (tbwt_zembo_spawn_and_shop) -- getRum is unblocked
- Content (uncommitted at hand-off, OSRS-Content quests/quest_tbwt/): Zembo is placed at 2925,3143,0
  (configs/tbwt_zembo.spawn), sells beer / karamja_rum / jug_wine (configs/tbwt_zembo.inv
  [boozeshop]), talks and trades (scripts/tbwt_zembo.rs2). Source: LostCity_Server
  areas/area_karamja zambo.rs2 + karamja.npc [zambo] + karamja.inv [boozeshop], maps/m45_49.jm2.
- Replace the parked file's `getRum.zembo` check + `t.blocked(...)` with these rows (proved by
  build/quest_gate/seam1_tbwt_copy, the parked file with this tail: SUMMARY pass=47 fail=0 up to
  the copy's own end-of-leg marker; rows 41-47 PASS, coins 500 -> 470):
```lua
            local zr = t.npc.nearest("zembo", 15)
            t.check("getRum.zembo", zr == "ok", "zembo within 15 tiles of Musa Point 2925,3143: " .. tostring(zr))
            t.exec("getRum-talk", t.player.talk_to, "zembo", 1)
            t.exec("getRum-dialog", t.chat.play, {
                "npc:Hey, are you wanting to try some of my fine wines and spirits?",
                "choose:Yes please.",
            })
            t.exec("getRum-shop", t.shop.attach, "boozeshop")
            t.exec("getRum", t.shop.buy, "karamja_rum", 1)
            do local r, d = t.shop.close(); t.check("getRum-shop-close", r == "ok", tostring(r) .. ": " .. tostring(d)) end
            t.exec("getRum.has", t.inv.expect_has, "karamja_rum", 1)
```
- `getRum-shop` and `getRum.has` wrote `[frame unchanged]` with no shot; take the shop shot from
  `getRum` (148-getRum.png shows the shop, 470 coins and the rum in the pack).
- Leg 2 next: sliceBanana needs a knife and a banana (the guide picks one from a plantation tree;
  Luthas' plantation is just east, 2939,3154). Proved in scratch seam1_zembo_b:
  `t.player.use_item_on_item("knife", "banana")` -> 'You deftly chop the bananas into slices.';
  `t.player.use_item_on_item("tbwt_sliced_banana", "karamja_rum")` -> 'You add the banana slices to
  the Karamjan rum.' (tbwt_sliced_banana_in_karamja_rum). The order matters: (a, b) fires [opheldu,b].
- The two stale `-- CHECK` words in parked.lua's header (lines 3 and 6) are still there: yours to remove.

## leg 1 (b49 relay, final)
- Leg 1 (goToTimfrakuLadder..getRum) is green: tbwt run 49 rows pass=49 fail=0, checkpoint 1 written. getRum now buys the rum from Zembo (talk, boozeshop attach, buy).
- End: Musa Point 2925,3143,0, varp320_tbwt_main = 3, quiet (shop closed, no dialogue).
- Backpack: karamja_rum x1, net, coins 470, 1 plain tbwt_karambwan_vessel, 1 tbwt_karambwan_vessel_loaded_with_karambwanji, 6 raw shrimp (bycatch). No raw karambwanji left in the pack at the end (check before leg 2 needs paste).
- Setup gives: fishing 5, firemaking 30, agility 15, cooking 30, Jungle Potion complete, net, coins 500.
- Leg 2 starts with sliceBanana (knife + banana: banana from Luthas' plantation 2939,3154; knife must be given or found).
- Surprises: Timfraku's talk closes the chat mid-dialogue (split chat.play); Lubufu reachable only from 2771,3169; the ladder click works from goto 2781,3089.

## leg 2
- Leg 2 (sliceBanana..pickupBurntBones) is green: tbwt full run 83 rows pass=83 fail=0, checkpoint 2 written; lint clean.
- End: 2912,3112,0 (beside Tiadeche, quiet: goto away from the aggressive jogres at 2924,3060), varp320_tbwt_main = 3; varp321_tbwt_tiadeche = request_manual (Tiadeche asked the player to take the vessel to Tinsay).
- Backpack: tbwt_burnt_jogre_bones, seaweed, tbwt_sliced_banana_in_karamja_rum, 1 plain tbwt_karambwan_vessel, tbwt_raw_karambwan (Tiadeche's gift), knife, tinderbox, net, coins 470, 8 sharks, 6 raw shrimp. The loaded vessel is gone (given to Tiadeche). Worn: rune_scimitar (equipped by row getJogreBones-wield).
- Setup now also gives knife, tinderbox, rune_scimitar, 8 sharks and levels attack 60 / strength 60 / defence 40 / hitpoints 70 (jogre fight).
- Surprises: giveVessel's chat closes for a 10-tick p_delay, so it is two chat.play calls with t.ticks(14) between; burnt bones appear only 23-48 ticks after the fire catches (t.await on obj_near, 80 ticks); a second aggressive jogre refuses the checkpoint ("in combat") until the player goto's away; t.world.tile() returns (ok, {x,z,level}).

## leg 3
- Leg 3 (makeKarambwanjiPaste..giveSpear) is green: `--from-leg 3` 85 rows pass=85 fail=0, checkpoint 3 written at 2844,3041,0; lint clean.
- End: 2844,3041,0 beside Tamayu, quiet; varp320_tbwt_main = 3; varp6053_tbwt_tamayu = 3 (watched_cutscene); Tamayu has taken the 4-dose agility potion and the tbwt_iron_spear_kp (varp6051 flags set), so leg 4's goOnHuntToKill is the killing hunt.
- Pack: marinated burnt jogre bones (tbwt_burnt_jogre_bones_marinated_in_karambwanji), banana rum with slices, seaweed, pestle_and_mortar, plain karambwan vessel, 1-2 raw karambwan spare, net, knife, tinderbox, coins 470, 8 sharks, rune_scimitar still worn. Setup unchanged; leg 3 gives pestle, iron_spear, 4dose1agility, ::complete quest_druidicritual and fishing 65 INSIDE the leg (t.cheat).
- Surprises: pestle needs Herblore unlocked (Druidic Ritual); loading the vessel eats the WHOLE karambwanji stack (quest_tbwt.rs2:98-109 inv_delslot), so each karambwan try re-nets; karambwan fishing and cooking are luck (7 tries once; loops are bounded 10/5); the karambwanji spot copy sits ON 2791,3019, stand at 2791,3021; the 4th potion page comes after a delay (split chat.play); the hunt cutscene teleports to 2846,3041.

## leg 4
- Leg 4 (goOnHuntToKill..talkToTimfrakuEnd) is green; the quest is complete: full run 221 rows pass=221 fail=0, gate green, lint clean, helper_coverage FULL (40/40), no GUIDE-GAP.
- End: Timfraku's hut 2780,3087,1, varp320_tbwt_main = 6 (complete); scroll closed, quiet. Reward rows are the literal amounts: 2 Quest Points (7 from 5), +2000 coins (the scroll lists no XP).
- Setup now also gives magic 10 + air_rune 60 + mind_rune 60 (wind strike on the monkey).
- Surprises: the monkey takes 1-2 per wind strike, so getMonkeyCorpse re-casts in a loop until the corpse is down (await_dead_engaged falls back to melee, which the monkey dodges); Tamayu's option is "Take me on your next hunt for the Shaikahan" (no period; choose by /pattern/); Tinsay's chat closes at each p_delay (split chat.play + t.ticks); Cairn Isle and Tiadeche reached by goto_tile; completing queues Timfraku's "see my sons" chat, dismissed before the scroll; expect_complete's quest.journal row times out, so the four rows are hand-written (gaps-combat).
