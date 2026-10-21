-- gearup_quiver_pouch: the Archive and Tombs gearups, Dizana's quiver's own
-- ammo slot, and the rune pouch's Open.
--
--   ::tfa gearup   Ancient Magicks, the barrage pouch, dragon arrows in the
--                  quiver's slot, diamond ascension bolts (e) in the pack
--   a twisted bow fires from the quiver's slot (the ammo slot holds bolts),
--   and the ammo slot's bolts are untouched
--   Open on the divine rune pouch opens rune_pouch (190)
--   ::toa gearup   Piety's slot not taken by Zeal, thralls castable
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/gear/gearup_quiver_pouch.lua --bots 1 \
--       --name gq0 --ticks 600 --session /tmp/gq --fixture test/raids/fixtures/fresh_lumbridge.ini
local function vb(t, name)
    local r, v = t.var.varbit(name)
    return v
end

local function vp(t, name)
    local r, v = t.var.varp(name)
    return v
end

return {
    id = "gearup_quiver_pouch",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::goto 3222 3218 0",
    },
    run = function(t)
        t.ticks(2)
        t.cheat("::tfa gearup", false)
        t.ticks(2)
        t.check("tfa.spellbook", vb(t, "varb4070_spellbook") == 1, "spellbook " .. tostring(vb(t, "varb4070_spellbook")))
        t.check("tfa.pouch", vb(t, "varb29_rune_pouch_type_1") == 2 and vb(t, "varb14285_rune_pouch_type_4") == 13,
            "pouch " .. tostring(vb(t, "varb29_rune_pouch_type_1")) .. "/" .. tostring(vb(t, "varb14285_rune_pouch_type_4")))
        t.check("tfa.quiver", vp(t, "varp4142_dizanas_quiver_temp_ammo") == 11212
            and vp(t, "varp4141_dizanas_quiver_temp_ammo_amount") == 1000,
            "quiver " .. tostring(vp(t, "varp4142_dizanas_quiver_temp_ammo")) .. " x" .. tostring(vp(t, "varp4141_dizanas_quiver_temp_ammo_amount")))
        local _, bolts = t.inv.count("ascension_bolts_diamond_enchanted")
        t.check("tfa.bolts", bolts == 500, "diamond ascension bolts " .. tostring(bolts))

        -- The pouch's Open.
        t.player.inv_op("divine_rune_pouch", 1)
        t.check("pouch.open", t.ui.await_open("rune_pouch", 5))
        t.ticks(1)
        t.ui.close_modal()
        t.ticks(2)

        -- The twisted bow fires the quiver's dragon arrows. The pack is full,
        -- and a two-hander over the book needs a slot for it.
        t.player.drop("4dose2combat")
        t.ticks(2)
        t.player.equip("twisted_bow")
        t.ticks(2)
        local _, bow = t.inv.count_in("worn", "twisted_bow")
        t.check("tbow.worn", bow == 1, "twisted bow worn " .. tostring(bow))
        local before = vp(t, "varp4141_dizanas_quiver_temp_ammo_amount")
        local _, worn_before = t.inv.count_in("worn", "dragon_bolts_enchanted_ruby")
        t.player.attack("man", 2, 10)
        t.ticks(8)
        local after = vp(t, "varp4141_dizanas_quiver_temp_ammo_amount")
        local _, worn_after = t.inv.count_in("worn", "dragon_bolts_enchanted_ruby")
        t.check("quiver.fired", after < before, "quiver " .. tostring(before) .. " -> " .. tostring(after))
        t.check("quiver.bolts_kept", worn_after == worn_before, "ruby bolts " .. tostring(worn_before) .. " -> " .. tostring(worn_after))

        -- Tombs.
        t.cheat("::toa gearup", false)
        t.ticks(2)
        t.check("toa.piety", vb(t, "varb3909_kr_knightwaves_state") == 8, "knight waves " .. tostring(vb(t, "varb3909_kr_knightwaves_state")))
        t.check("toa.zeal", vb(t, "varb16578_prayer_zeal_unlocked") == 0 and vb(t, "varb16577_prayer_zeal") == 0, "zeal set")
        t.check("toa.arceuus", vb(t, "varb4070_spellbook") == 3 and vb(t, "varb9631_arceuus_spellbook_unlocked") == 1, "spellbook")
        t.check("toa.akd", vb(t, "varb12296_akd") == 150, "akd " .. tostring(vb(t, "varb12296_akd")))
        t.check("toa.pouch", vb(t, "varb29_rune_pouch_type_1") == 4 and vb(t, "varb1622_rune_pouch_type_2") == 8
            and vb(t, "varb1623_rune_pouch_type_3") == 9, "pouch")
        t.expect("gearup_quiver_pouch.run", "ok", "done")
    end,
}
