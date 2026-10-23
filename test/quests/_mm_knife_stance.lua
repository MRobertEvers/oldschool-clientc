-- Misthalin Mystery's killer's knife carries its own stance. RSProx index 4336
-- [1924]: wielding mistmyst_cutscene_knife sends [bas] ready=2911
-- (elid_choc_ice_seller_ready), walk=7327 (hween16_knife_walk),
-- run=2322 (human_toadcannon_running). Read back through the client.
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_knife_stance.lua --name mm_knife_stance
return {
    id = "mm_knife_stance",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give mistmyst_cutscene_knife",
    },

    run = function(t)
        t.ticks(2)
        local wield = t.cheat("::wield mistmyst_cutscene_knife")
        t.ticks(3)
        local result, detail = t.sail.walk_set({
            ready = "elid_choc_ice_seller_ready",
            walk = "hween16_knife_walk",
            run = "human_toadcannon_running",
        })
        t.check("knife.stance", wield == "ok" and result == "ok",
            "::wield -> " .. tostring(wield) .. "; " .. tostring(detail))
    end,
}
