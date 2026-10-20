-- _toa_wardens_debug: `::toawardens` into each phase and the vault, each run
-- a few ticks; a smoke test for the cheat, not a tell comparison.
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
return {
    id = "_toa_wardens_debug",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::setlevel hitpoints 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toawardens 4", false)
        t.ticks(30)
        t.ticklog.mark("debug.vault")
        t.cheat("::toawardens 6", false)
        t.ticks(8)
        t.expect("debug.vault", said(t, "burial chamber") and "ok" or "fail")
        local sr, sd = t.player.click_loc("toa_vault_sarcophagus_parent", 1)
        t.expect("debug.sarc", sr == "ok" and "ok" or "fail", tostring(sr) .. " " .. tostring(sd))
        t.ticks(22)
        t.expect("debug.special", said(t, "found something special") and "ok" or "fail")
        local or2, od = t.player.click_loc("toa_vault_sarcophagus_open", 1)
        t.expect("debug.search", or2 == "ok" and "ok" or "fail", tostring(or2) .. " " .. tostring(od))
        t.expect("debug.interface", t.ui.await_open("toa_chests", 20) == "ok" and "ok" or "fail")
        t.ticklog.mark("debug.p3")
        t.cheat("::toawardens 4", false)
        t.ticks(30)
        t.expect("debug.enrage", "ok")
    end,
}
