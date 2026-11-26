-- Herb tar (RSProx index 4658), against the recording:
--   * a guam leaf on 15 swamp tar with a pestle makes 15 guam tar two ticks
--     later, "You mix the guam leaf into the swamp tar.".
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end

return {
    id = "enchant_tar_teak",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel magic 57",
        "::setlevel herblore 57",
        "::setvar varp80_druidquest ^druid_complete",
        "::setlevel hitpoints 99",
        "::goto 3512 3480 0",
        "::give firerune 100",
        "::give cosmicrune 10",
        "::give strung_topaz_amulet 3",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        -- The enchant itself is not driven here: casting on a held item needs a
        -- client (scriptrun's drive.inv_cast), so that half is checked on the
        -- wire against RSProx instead (doc).
        t.ticklog.mark("tar")
        t.cheat("::give pestle_and_mortar 1")
        t.cheat("::give swamp_tar 30")
        t.cheat("::give guam_leaf 1")
        t.ticks(2)
        t.exec("tar.use", t.player.use_item_on_item, "guam_leaf", "swamp_tar")
        t.ticks(5)
        t.check("tar.made", count(t, "salamander_tar_green") == 15 and count(t, "swamp_tar") == 15,
            "guam tar " .. tostring(count(t, "salamander_tar_green")) .. ", swamp tar " .. tostring(count(t, "swamp_tar")))
        t.check("tar.msg", t.msg.expect("You mix the guam leaf into the swamp tar."))
        t.ticklog.mark("end")
    end,
}
