-- Divine potions (skill_herblore/scripts/divine_potions.rs2): crushed crystal
-- on a super strength(4) opens the upgrade menu and makes a divine super
-- strength(4) for 4 crystal (RSProx 1104/1105/3257). Nothing answered the
-- pair before 2026-11-17.
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end
return {
    id = "divine_potions",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::complete quest_druidicritual",
        "::setlevel herblore 99",
        "::give 4dose2strength 2",
        "::give prif_crystal_shard_crushed 8",
    },
    run = function(t)
        t.ticks(3)
        t.player.use_item_on_item("prif_crystal_shard_crushed", "4dose2strength")
        t.exec("menu", t.ui.make, "a")
        t.exec("made", t.inv.await, "4dosedivinestrength", 1, 10)
        t.check("dust", count(t, "prif_crystal_shard_crushed") == 4, "crystal left " .. count(t, "prif_crystal_shard_crushed"))
        t.check("line", t.msg.expect("You mix 4 pieces of crushed crystal into your potion.") == "ok", "line")
    end,
}
