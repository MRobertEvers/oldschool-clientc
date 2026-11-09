-- The non-quest clicks of RSProx 4503: a gardener's Pay at the Varrock tree patch
-- (nothing planted, the protection, "already looking after", the 200-coin chop,
-- 4503 [236..244]), the Gnome Stronghold tree patch ([346..364]), the spirit
-- tree's list ([724..731]), a booth's Collect ([838]), the Rogues' Den fire's
-- Cook op ([1290]).
local function count(t, name)
    local _, n = t.inv.count(name)
    return n
end

return {
    id = "gardener_pay",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel farming 99",
        "::setlevel cooking 99",
        "::give spade 1",
        "::give coins 1000",
        "::give plantpot_maple_sapling 2",
        "::give basket_orange_5 2",
        "::give raw_trout 1",
        "::setvar varp111_treequest ^tree_complete",
        "::setvar varb702_varbit_702 3",
        "::setvar varb2953_varbit_2953 3",
    },
    run = function(t)
        t.ticks(2)
        -- ---- Varrock's tree patch, Treznor ----
        t.cheat("::tele 0_50_54_27_2")
        t.ticks(3)
        t.exec("empty.pay", t.player.talk_to, "farming_gardener_tree_3", 3)
        t.exec("empty.page", t.chat.play, { "npc:You don't have a sapling planted in that patch.", "end" })

        local patch = t.player.by_symbol("loc", "farming_tree_patch_3")
        t.exec("plant", t.player.use_on, "plantpot_maple_sapling", patch)
        t.exec("plant.mes", t.msg.expect, "You plant the maple sapling in the tree patch.")
        t.ticks(5)

        t.exec("protect.pay", t.player.talk_to, "farming_gardener_tree_3", 3)
        t.exec("protect.menu", t.chat.play, {
            "choose:Yes.",
            "npc:That'll do nicely",
            "end",
        })
        t.exec("protect.mes", t.msg.expect, "You pay the gardener one basket of oranges to protect the patch.")
        t.check("protect.paid", count(t, "basket_orange_5") == 1, "baskets left " .. tostring(count(t, "basket_orange_5")))

        t.exec("again.pay", t.player.talk_to, "farming_gardener_tree_3", 3)
        t.exec("again.page", t.chat.play, { "npc:I'm already looking after that patch for you.", "end" })

        -- The chop state of a maple (RuneLite TREE: check 32, chop 33).
        t.cheat("::setvar varb702_varbit_702 33")
        t.ticks(2)
        local coins_before = count(t, "coins")
        t.exec("chop.pay", t.player.talk_to, "farming_gardener_tree_3", 3)
        t.exec("chop.menu", t.chat.play, { "choose:Yes.", "end" })
        t.check("chop.coins", count(t, "coins") == coins_before - 200, "coins " .. tostring(coins_before) .. " -> " .. tostring(count(t, "coins")))
        t.expect("chop.state", t.var.await_server("varb702_varbit_702", 3, 5))

        -- ---- The Gnome Stronghold's tree patch, Prissy Scilla ----
        t.cheat("::tele 0_38_53_0_7")
        t.ticks(3)
        local gnome = t.player.by_symbol("loc", "farming_tree_patch_5")
        t.exec("gnome.plant", t.player.use_on, "plantpot_maple_sapling", gnome)
        t.exec("gnome.plant.mes", t.msg.expect, "You plant the maple sapling in the tree patch.")
        t.ticks(5)
        t.exec("gnome.pay", t.player.talk_to, "farming_gardener_tree_gnome", 3)
        t.exec("gnome.menu", t.chat.play, { "choose:Yes.", "npc:That'll do nicely", "end" })
        t.check("gnome.paid", count(t, "basket_orange_5") == 0, "baskets left " .. tostring(count(t, "basket_orange_5")))

        -- ---- The Grand Exchange's young spirit tree ----
        t.cheat("::tele 0_49_54_47_53")
        t.ticks(3)
        t.exec("spirit.open", t.player.click_loc, "spirittree_small", 1)
        t.expect("spirit.list", t.ui.await_open("menu_new", 10))
        t.exec("spirit.pick", t.ui.press_row, { sym = "menu_new:text", sub = 0 })
        t.ticks(5)
        local _, at = t.world.tile()
        t.check("spirit.landing", at ~= nil and at.x == 2542 and at.z == 3170, "landed " .. tostring(at and at.x) .. "," .. tostring(at and at.z))

        -- ---- Varrock west bank's booth: Collect ----
        t.cheat("::tele 0_49_53_50_60")
        t.ticks(3)
        t.exec("collect", t.player.click_loc, "fai_varrock_bankbooth_multi", 3)
        t.expect("collect.open", t.ui.await_open("ge_collect", 10))
        t.ui.close_modal()
        t.ticks(2)

        -- ---- The Rogues' Den fire: Cook, one trout, no menu ----
        t.cheat("::tele 1_47_77_35_44")
        t.ticks(3)
        t.exec("cook", t.player.click_loc, "fire_cook", 1)
        t.ticks(8)
        t.check("cook.done", count(t, "raw_trout") == 0, "raw_trout " .. tostring(count(t, "raw_trout")))
        -- ---- Gnormadium Avlafrim before One Small Favour (4503 [4779..4800]) ----
        t.cheat("::tele 0_39_46_47_31")
        t.ticks(3)
        t.exec("gnormadium.talk", t.player.talk_to, "gnormadium_avlafrim", 1)
        t.exec("gnormadium.dialog", t.chat.play, {
            "npc:Hello! Don't get in the way",
            "choose:Is there anything I can do?",
            "player:Is there anything I can do?",
            "npc:Not really, I'm afraid",
            "choose:Okay, thanks.",
            "player:Okay, thanks.",
            "end",
        })
        t.finish(0)
    end,
}
