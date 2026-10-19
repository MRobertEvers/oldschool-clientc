-- autocast: the combat tab's Spell buttons and the autocast panel (interface 201,
-- skill_combat/scripts/player/auto_cast.rs2). The client draws the grid from
-- varp 664's layout key and reads the pick from varbit 276, so both are read off
-- the CLIENT's copy: an untransmitted carrier fails here. Covers the standard
-- layout (-1), the Ancient one (4675, RSProx), a pick the layout did not draw,
-- and a weapon change that can no longer cast the pick. com_mode follows RSProx:
-- 4 while a spell is autocast (no style button lit), the prior style after; and
-- com_stance follows the style (staff Bash 1, Pound 2, autocast 1).
--   ./src/build_<you>_opt/torirsserver --scriptrun test/magic/autocast.lua --name s1 \
--       --ticks 300 --session $S/run --fixture test/quests/fixtures/fresh_lumbridge.ini
local function varp(t, name)
    local _, v = t.var.varp(name)
    return v
end

local function varbit(t, name)
    local _, v = t.var.varbit(name)
    return v
end

local function press(t, sym, sub)
    local wr, w = t.ui.widget(sym, sub)
    local pr, pd = t.ui.invoke(w, 1)
    t.ticks(2)
    return wr == "ok" and pr == "ok", tostring(sym) .. "[" .. tostring(sub) .. "] " .. tostring(wr)
        .. " press " .. tostring(pr) .. " " .. tostring(pd)
end

return {
    id = "autocast",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = { "::setlevel magic 99", "::setlevel attack 50", "::give staff_of_air", "::give staff_of_zaros" },
    run = function(t)
        t.exec("equip.air", t.player.equip, "staff_of_air")
        t.ui.tab("combat")
        t.ticks(2)
        press(t, "combat_interface:style_slot_1")

        local ok, d = press(t, "combat_interface:autocast_normal")
        t.check("open.standard", ok and varp(t, "varp664_spitfire_coord") == -1,
            d .. "; varp664 " .. tostring(varp(t, "varp664_spitfire_coord")) .. " (want -1)")
        ok, d = press(t, "autocast:spells", 1)
        t.check("pick.wind_strike", ok and varbit(t, "varb276_autocast_spell") == 1
            and varbit(t, "varb275_autocast_set") == 1,
            d .. "; autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " set "
            .. tostring(varbit(t, "varb275_autocast_set")) .. " (want 1, 1)")
        t.check("com_mode.autocast", varp(t, "varp43_com_mode") == 4 and varp(t, "varp46_com_stance") == 1,
            "com_mode " .. tostring(varp(t, "varp43_com_mode")) .. " com_stance "
            .. tostring(varp(t, "varp46_com_stance")) .. " (want 4, 1)")

        -- Smoke Rush is not on the standard grid: the client never drew it.
        press(t, "combat_interface:autocast_normal")
        ok, d = press(t, "autocast:spells", 31)
        t.check("pick.undrawn", varbit(t, "varb276_autocast_spell") == 1,
            d .. "; autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " (want 1, kept)")
        press(t, "autocast:spells", 0)
        t.check("cancel", varbit(t, "varb276_autocast_spell") == 0 and varp(t, "varp43_com_mode") == 1
            and varp(t, "varp46_com_stance") == 2,
            "autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " com_mode "
            .. tostring(varp(t, "varp43_com_mode")) .. " com_stance " .. tostring(varp(t, "varp46_com_stance"))
            .. " (want 0, 1: the style before the pick, 2: Pound)")

        t.cheat("::setvar varb4070_spellbook 1", false)
        t.exec("equip.zaros", t.player.equip, "staff_of_zaros")
        t.ticks(2)
        ok, d = press(t, "combat_interface:autocast_defensive")
        t.check("open.ancient", ok and varp(t, "varp664_spitfire_coord") == 4675
            and varbit(t, "varb2668_autocast_defmode") == 1,
            d .. "; varp664 " .. tostring(varp(t, "varp664_spitfire_coord")) .. " defmode "
            .. tostring(varbit(t, "varb2668_autocast_defmode")) .. " (want 4675, 1)")
        ok, d = press(t, "autocast:spells", 46)
        t.check("pick.ice_barrage", ok and varbit(t, "varb276_autocast_spell") == 46,
            d .. "; autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " (want 46)")

        -- A plain staff casts no Ancient Magicks: the pick goes.
        t.exec("equip.air2", t.player.equip, "staff_of_air")
        t.ticks(2)
        t.check("weapon.revalidate", varbit(t, "varb276_autocast_spell") == 0 and varp(t, "varp43_com_mode") == 1,
            "autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " com_mode "
            .. tostring(varp(t, "varp43_com_mode")) .. " (want 0, 1)")

        -- The pick casts, and the swing's ~player_combat_stat keeps the mode at 4
        -- rather than clamping it into the staff's style row.
        t.cheat("::setvar varb4070_spellbook 0", false)
        t.cheat("::give mindrune 50", false)
        press(t, "combat_interface:autocast_normal")
        press(t, "autocast:spells", 1)
        local _, before = t.skill.read("magic")
        t.cheat("::spawn man", false)
        t.ticks(2)
        t.player.attack("man", 2, 30)
        t.ticks(12)
        local _, after = t.skill.read("magic")
        local gained = (type(after) == "table" and type(before) == "table") and (after.experience - before.experience) or nil
        t.check("autocast.casts", gained ~= nil and gained > 0 and varp(t, "varp43_com_mode") == 4,
            "magic xp +" .. tostring(gained) .. " com_mode " .. tostring(varp(t, "varp43_com_mode"))
            .. " (want > 0, 4)")

        -- A style button ends autocast and is the style.
        press(t, "combat_interface:style_slot_0")
        t.check("style.ends", varbit(t, "varb276_autocast_spell") == 0 and varp(t, "varp43_com_mode") == 0
            and varp(t, "varp46_com_stance") == 1,
            "autocast_spell " .. tostring(varbit(t, "varb276_autocast_spell")) .. " com_mode "
            .. tostring(varp(t, "varp43_com_mode")) .. " com_stance " .. tostring(varp(t, "varp46_com_stance"))
            .. " (want 0, 0, 1: Bash)")
    end,
}
