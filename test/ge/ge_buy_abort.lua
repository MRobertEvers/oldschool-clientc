-- ge_buy_abort: the Grand Exchange buy offer RSProx index 4426 recorded [82..108],
-- step for step (interface_grandexchange/scripts/ge_offers.rs2's header): the
-- clerk's Exchange, slot 3's Buy, the item search answered with bird snares, the
-- price +5% ten times (each step at least 1: Jagex's guide price 11 went to 21; ours
-- is high alchemy, 3, so 13), the quantity +1 five times (1 -> 6), Confirm (6 x 13
-- coins taken), the offer's details, Abort, and the coins collected from the slot.
--
--   ./src/build_opt/torirsserver --scriptrun test/ge/ge_buy_abort.lua --bots 1 \
--       --name ge0 --ticks 600 --session /tmp/ge --fixture test/quests/fixtures/fresh_lumbridge.ini

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end

local function press(t, label, sym, sub, op)
    local wr, widget = t.ui.widget(sym, sub)
    if wr ~= "ok" then
        t.check(label, false, "no widget " .. tostring(sym) .. " sub " .. tostring(sub) .. ": " .. tostring(widget))
        return
    end
    local r, d = t.ui.invoke(widget, op or 1)
    t.check(label, r == "ok", tostring(d))
    t.ticks(1)
end

return {
    id = "ge_buy_abort",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::give coins 1000" },
    run = function(t)
        t.ticklog.start()
        t.exec("goto", t.player.goto_tile, 3164, 3487, 0)
        t.exec("exchange", t.player.talk_to, "ge_clerk_1", 3)
        t.expect("open", t.ui.await_open("ge_offers", 5))

        press(t, "buy.slot3", "ge_offers:index_3", 3, 1)
        local sr, sd = t.chat.resume_obj(10006)
        t.check("search.answer", sr == "ok", tostring(sd))
        t.ticks(2)
        t.expect("search.item", t.var.await("varp1151_tradingpost_search", 10006, 3))
        for i = 1, 10 do press(t, "price.plus5." .. i, "ge_offers:setup", 13, 1) end
        for i = 1, 5 do press(t, "qty.plus1." .. i, "ge_offers:setup", 3, 1) end
        t.expect("qty", t.var.await("varb4396_ge_newoffer_quantity", 6, 3))

        local wr, confirm = t.ui.widget("ge_offers:setup_confirm", -1)
        t.check("confirm.widget", wr == "ok", tostring(confirm))
        t.exec("confirm", t.ui.invoke, confirm, 1)
        t.ticks(3)
        t.check("confirm.paid", count(t, "coins") == 922, "coins " .. count(t, "coins") .. " == 1000 - 6 x 13")
        t.expect("confirm.last_item", t.var.await("varp4725_ge_last_offer_item", 10006, 3))
        t.expect("confirm.last_price", t.var.await("varp4727_ge_last_offer_price", 13, 3))

        press(t, "details", "ge_offers:index_3", 2, 1)
        t.expect("details.slot", t.var.await("varb4439_ge_selectedslot", 4, 3))
        press(t, "abort", "ge_offers:details_status", 0, 1)
        t.ticks(4)
        press(t, "collect", "ge_offers:details_collect", 2, 2)
        t.ticks(2)
        t.check("collect.refund", count(t, "coins") == 1000, "coins " .. count(t, "coins") .. " == 1000 after the refund")
        t.finish(0)
    end,
}
