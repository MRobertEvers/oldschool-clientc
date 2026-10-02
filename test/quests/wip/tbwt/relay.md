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
