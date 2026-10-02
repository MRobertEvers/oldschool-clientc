## leg 1
- Leg 1 (steps goToTimfrakuLadder..getRum) is driven and green through fillVessel; getRum is BLOCKED as a content_bug: Zembo has no spawn/shop (m45_49.spawn has no zembo; nothing in server/scripts mentions him), so karamja_rum cannot be bought. Leg 2 cannot start until content places Zembo + a shop selling karamja_rum.
- Player ends at Musa Point 2925,3143,0 (no checkpoint written, t.blocked ends the run). Quest var varp320_tbwt_main = 3 (started); varp6052_tbwt_lubufu = 31 (apprentice).
- Backpack: small net, 500 coins, ~3 raw karambwanji left, 6 raw shrimp (bycatch), 1 tbwt_karambwan_vessel + 1 tbwt_karambwan_vessel_loaded_with_karambwanji.
- Setup gives: fishing 5, firemaking 30, agility 15, cooking 30, Jungle Potion complete, net, coins 500.
- Surprises: Timfraku's talk closes the chat for ~4 ticks (if_close, p_delay) mid-dialogue, so chat.play is split; Lubufu is only reachable from 2771,3169 (2770,3167 says "can't reach"); the ladder click works from goto 2781,3089; fishing spot press 0_43_47_karambwanji gives 23 karambwanji in ~165 ticks.
