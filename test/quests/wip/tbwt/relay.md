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

## seam matthew-mbp-m4-b49-seam2 (tbwt_completion_rewards) -- the XP is CLAIMED FROM THE BROTHERS, not missing
- The b49 review's premise was wrong: Tai Bwo Wannai Trio's completion awards NO XP in the real game. The four
  XP rewards are claimed by talking to each brother in the village AFTER completion, and this port already does
  that. Sources: OSRS wiki "Tai Bwo Wannai Trio" oldid 15265886, Rewards ("claimed upon speaking to Tinsay /
  Tiadeche / Tamayu after quest completion"); Quest Helper TaiBwoWannaiTrio.java talkToTimfrakuEnd ("you will
  need to talk to each of the brothers individually ... to receive experience rewards"); LostCity_Server
  area_karamja tbwt_{tinsay,tiadeche,tamayu}_final.rs2 = the port's same files (stat_advance cooking 50000,
  fishing 50000, attack 25000 + strength 25000, pack tenths). No stat_advance was added to the completion
  (it would double-award).
- What the test is missing is the three claim legs. Add them after `quest.points` (proved: build/quest_gate/
  seam2_tbwt_copy2, the working tbwt.lua + these rows, SUMMARY 211 PASS pass=211 fail=0; claimTinsay.xp
  `cooking: +5000 xp (whole units)`, claimTiadeche.xp `fishing: +5000`, claimTamayu.xp-attack/-strength
  `+2500` each, claimTamayu.spear `tbwt_rune_spear_kp 1`). The npcs are the `*_multinpc_house` symbols
  (m43_47.spawn), drawn once varp320 >= 4. `t.skill.snapshot()` answers `(ok, table)`: take the SECOND value
  (your line 713 `local snap = t.skill.snapshot()` holds "ok"; drop it or fix it). Tamayu needs one free slot.
```lua
            t.exec("goto-claimTinsay", t.player.goto_tile, 2790, 3052, 0)
            local _, s1 = t.skill.snapshot()
            t.exec("claimTinsay", t.player.talk_to, "tbwt_tinsay_multinpc_house", 1)
            t.exec("claimTinsay-dialog", t.chat.play, {
                "npc:Braaar! Your meals are truly excellent", "player:I'm glad to hear it.",
                "npc:but there are some areas", "player:Such as?", "npc:Let me show you.",
                "mesbox:Tinsay trains you in cooking technique.", "mesbox:Tinsay teaches you how to cook Karambwan",
                "player:Thanks.", "npc:Oh, and before you go", "player:Ok...",
            })
            t.ticks(2)
            t.exec("claimTinsay.xp", t.skill.expect_gain, "cooking", 5000, s1)
            t.exec("goto-claimTiadeche", t.player.goto_tile, 2781, 3055, 0)
            local _, s2 = t.skill.snapshot()
            t.exec("claimTiadeche", t.player.talk_to, "tbwt_tiadeche_multinpc_house", 1)
            t.exec("claimTiadeche-dialog", t.chat.play, {
                "npc:Hello, Bwana! We three have finally returned", "player:So I see.", "npc:I wish to reward you",
                "npc:I have refined my Karambwan fishing technique", "mesbox:Tiadeche trains you in fishing technique.",
                "npc:Thanks to you, Lubufu's Karambwan monopoly is over", "player:Thanks.",
            })
            t.ticks(2)
            t.exec("claimTiadeche.xp", t.skill.expect_gain, "fishing", 5000, s2)
            t.exec("goto-claimTamayu", t.player.goto_tile, 2800, 3055, 0)
            local _, s3 = t.skill.snapshot()
            t.exec("claimTamayu", t.player.talk_to, "tbwt_tamayu_multinpc_house", 1)
            t.exec("claimTamayu-dialog", t.chat.play, {
                "npc:Welcome back to Tai Bwo Wannai, stranger.", "player:I no longer feel a stranger here.",
                "npc:And neither do we", "npc:I have not much of worth to give",
                "mesbox:Tamayu trains you in fighting technique.", "player:Thanks.", "npc:This is the one thing I can give",
                "mesbox:Tamayu hands you some kind of spear.", "npc:Take care, Bwana.",
            })
            t.ticks(2)
            t.exec("claimTamayu.xp-attack", t.skill.expect_gain, "attack", 2500, s3)
            t.exec("claimTamayu.xp-strength", t.skill.expect_gain, "strength", 2500, s3)
            t.exec("claimTamayu.spear", t.inv.await, "tbwt_rune_spear_kp", 1, 5)
            t.finish(0)
```
- The reward SCROLL: the real one (wiki File:Tai_Bwo_Wannai_Trio_reward_scroll.png) reads `2 Quest Points / 5,000
  Fishing XP / 5,000 Cooking XP / 2,500 Attack XP / 2,500 Strength XP`, karambwan model, no coins line. The port's
  reads `2 Quest Points | 2000 coins | The three brothers return to Tai Bwo Wannai`, and that exact string is
  pinned by tools/check_quest_combat_contract.py:2543. LANDED by the seam2 closer (content string + pin in one
  commit, [seam:matthew-mbp-m4-b49-seam2]): the scroll now reads `5000 Fishing XP | 5000 Cooking XP | 2500
  Attack XP | 2500 Strength XP` (karambwan model) and `t.scroll.reward_xp` returns 5000/5000/2500/2500.
  REWRITE your `quest.scroll_rewards` row: drop the "2000 coins" test and add the four
  `t.scroll.reward_xp(<Skill>)` rows (literal 5000/5000/2500/2500); the scroll pays nothing, so the skill
  deltas are asserted only at the three claims above.
  The 2,000 coins stay a backpack delta (`talkToTimfrakuEnd.coins`), which is right either way.
- Vessel loading (content changed, uncommitted, quest_tbwt.rs2 tbwt_load_karambwan_vessel): it now takes ONE raw
  karambwanji (wiki "Raw karambwanji" oldid 15184350, "load a raw karambwanji into a karambwan vessel"; this
  cache's karambwanji is stackable=1, LostCity's is not, which is why LostCity's inv_delslot was one fish).
  Proved: fillVessel `lost tbwt_raw_karambwanji 3->2`, both use orders. Your leg 3 loop still passes as written
  (copy2: 3 tries, 3 raw karambwan) but its comment "loading the vessel takes the whole karambwanji stack, so
  each try re-nets" is now false: leftover karambwanji stay in the pack, so the re-net each try is optional.
- getMonkeyCorpse is luck: in copy run 1 the monkey wandered off after 14 wind strikes and the whole tail
  cascaded (167/211). Copy run 2 needed 15 casts. Raise the cap (`while casts < 30`) -- 14 is not enough.

## orchestrator note (matthew-mbp-m4-b49 round 2)
- test/quests/tbwt.lua is seam 2's proven copy (wip/tbwt/seam2_proven.lua, 211/0 end to end): it adds the three claim legs (the XP is claimed from each brother after completion, not at completion) and literal XP rows. The scroll now matches the wiki (no coins line) -- check whether the completion still pays 2000 coins (talkToTimfrakuEnd.coins at ~739) against quest_tbwt.rs2 and the wiki, and keep or drop that row accordingly. Remove the two stale CHECK markers from the header.
