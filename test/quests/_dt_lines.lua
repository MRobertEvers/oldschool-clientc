-- Desert Treasure I: lines the first rebuild missed, from RSProx.
--   4621 [2399]: the pyramid's back door before the quest is done, "This passage doesn't seem to
--                lead anywhere..." on the click.
--   3000 [2168] / 3256 [1070]: the Ancient altar, "You feel a strange wisdom fill your mind..."
--   3000 [2173], 3256 [7064], 4629 [5677]: and back, "You feel a strange drain upon your memory..."
return {
    id = "dt_lines",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::setvar varb358_deserttreasure 11",
        "::tele 0_50_45_33_7",
    },

    run = function(t)
        t.ticks(3)
        t.player.click_loc("desert_trasure_back_door_skew", 1)
        t.exec("backdoor.shut", t.msg.expect, "This passage doesn't seem to lead anywhere...")

        t.cheat("::setvar varb358_deserttreasure 15")
        t.cheat("::tele 0_50_145_32_32")
        t.ticks(3)
        t.player.click_loc("dt_zaros_altar", 1)
        t.exec("altar.to_ancient", t.msg.expect, "You feel a strange wisdom fill your mind...")
        local _, book = t.var.server("varb4070_spellbook")
        t.check("altar.ancient_book", book == 1, "spellbook " .. tostring(book))
        t.ticks(2)
        t.player.click_loc("dt_zaros_altar", 1)
        t.exec("altar.back", t.msg.expect, "You feel a strange drain upon your memory...")
        _, book = t.var.server("varb4070_spellbook")
        t.check("altar.standard_book", book == 0, "spellbook " .. tostring(book))
    end,
}
