-- ge_fill: two players trade through the Grand Exchange (--bots 2). Players only:
-- the seller lists 10 bird snares at 5 each, the buyer bids 7 each for 6; the
-- trade goes at the seller's price, so the buyer pays 30 and gets 12 back, and the
-- seller's offer stands 6 of 10 sold (ge_matching.rs2's rules, the engine's
-- torirs_server_ge.c). Then each collects. UNVERIFIED against a recording: no
-- recorded session fills an offer.
--
--   ./src/build_opt/torirsserver --scriptrun test/ge/ge_fill.lua --bots 2 \
--       --name gefill0 --ticks 900 --session /tmp/gefill --fixture test/quests/fixtures/fresh_lumbridge.ini

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

local function confirm(t, label)
    local wr, widget = t.ui.widget("ge_offers:setup_confirm", -1)
    t.check(label .. ".widget", wr == "ok", tostring(widget))
    t.exec(label, t.ui.invoke, widget, 1)
    t.ticks(3)
end

return {
    id = "ge_fill",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv" },
    run = function(t)
        local role = t.party.role()
        if role == 1 then
            t.ticklog.start()
            t.cheat("::give hunting_ojibway_bird_snare 10")
        else
            t.cheat("::give coins 100")
        end
        t.ticks(2)
        t.exec("goto", t.player.goto_tile, role == 1 and 3164 or 3164, role == 1 and 3487 or 3488, 0)
        t.exec("exchange", t.player.talk_to, "ge_clerk_1", 3)
        t.expect("open", t.ui.await_open("ge_offers", 5))

        if role == 1 then
            -- The snares from the side inventory: a sell offer for all 10.
            press(t, "sell.item", "ge_offers_side:items", 0, 1)
            t.expect("sell.qty", t.var.await("varb4396_ge_newoffer_quantity", 10, 3))
            press(t, "sell.price.1", "ge_offers:setup", 9, 1)
            press(t, "sell.price.2", "ge_offers:setup", 9, 1)
            confirm(t, "sell.confirm")
            t.check("sell.taken", count(t, "hunting_ojibway_bird_snare") == 0, "snares " .. count(t, "hunting_ojibway_bird_snare"))
        end
        local br, bd = t.party.barrier("listed", 300)
        t.check("party.listed", br == "ok", tostring(bd))

        if role == 2 then
            press(t, "buy.slot0", "ge_offers:index_0", 3, 1)
            t.chat.resume_obj(10006)
            t.ticks(2)
            for i = 1, 4 do press(t, "buy.price." .. i, "ge_offers:setup", 9, 1) end
            for i = 1, 5 do press(t, "buy.qty." .. i, "ge_offers:setup", 3, 1) end
            confirm(t, "buy.confirm")
            t.check("buy.paid", count(t, "coins") == 58, "coins " .. count(t, "coins") .. " == 100 - 6 x 7")
            press(t, "buy.details", "ge_offers:index_0", 2, 1)
            press(t, "buy.collect.items", "ge_offers:details_collect", 2, 2)
            press(t, "buy.collect.refund", "ge_offers:details_collect", 2, 2)
            t.check("buy.snares", count(t, "hunting_ojibway_bird_snare") == 6, "snares " .. count(t, "hunting_ojibway_bird_snare"))
            t.check("buy.refund", count(t, "coins") == 70, "coins " .. count(t, "coins") .. " == 58 + 6 x (7 - 5)")
        end
        br, bd = t.party.barrier("bought", 300)
        t.check("party.bought", br == "ok", tostring(bd))

        if role == 1 then
            press(t, "sell.details", "ge_offers:index_0", 2, 1)
            press(t, "sell.collect", "ge_offers:details_collect", 2, 2)
            t.check("sell.takings", count(t, "coins") == 30, "coins " .. count(t, "coins") .. " == 6 x 5")
        end
        t.finish(0)
    end,
}
