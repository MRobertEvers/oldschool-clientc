-- rick_turpentine: the Rick Turpentine random event for the tick log, to diff
-- against RSProx index 4456 [2000..2011] (talked to: a reward, talked to again,
-- gone after the page) and 4216 [3909..3930] (a reminder at S+10).
-- docs/rsprox-analysis/quests/troll_romance.md, "Rick Turpentine".
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/rick_turpentine.lua --bots 1 \
--       --name rick0 --ticks 1500 --session /tmp/rick --fixture test/quests/fixtures/fresh_lumbridge.ini
local function first_row(t, sym)
    local r, _, rows = t.npc.tiles(sym, 20)
    if r == "ok" and rows and rows[1] then return rows[1] end
    return nil
end

local REWARDS = { "coins", "kebab", "spinach_roll", "uncut_sapphire", "uncut_emerald", "uncut_ruby",
    "uncut_diamond", "cosmic_talisman", "keyhalf1", "keyhalf2" }

local function rewarded(t)
    for _, obj in ipairs(REWARDS) do
        local r, n = t.inv.count(obj)
        if r == "ok" and n > 0 then return obj end
    end
    return nil
end

return {
    id = "rick_turpentine",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
    },
    run = function(t)
        t.ticklog.start()

        -- 1. Talked to: the reward line, the item, the page; talked to again he
        -- must leave; gone the tick after that page closes.
        t.ticklog.mark("rick.talk")
        t.cheat("::rick", false)
        t.ticks(3)
        t.check("talk.here", first_row(t, "macro_highwayman") ~= nil, "Rick within 20")
        local r, d = t.player.talk_to("macro_highwayman", 1)
        t.check("talk.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("talk.page", t.chat.play, {
            "npc:Today is your lucky day, sirrah!",
        })
        t.expect("talk.reward", t.msg.expect("Your reward is:"))
        local got = rewarded(t)
        t.check("talk.item", got ~= nil, "a reward from the table: " .. tostring(got))
        t.ticks(3)
        t.check("talk.gone", first_row(t, "macro_highwayman") == nil, "Rick left after the page")

        -- 2. Dismissed (unverified): gone the next tick.
        t.ticklog.mark("rick.dismiss")
        t.cheat("::rick", false)
        t.ticks(4)
        r, d = t.player.talk_to("macro_highwayman", 5)
        t.note("dismiss.op: " .. tostring(r) .. " " .. tostring(d))
        t.ticks(4)
        t.check("dismiss.gone", first_row(t, "macro_highwayman") == nil, "Rick left on Dismiss")

        -- 3. Ignored: a reminder at S+10, gone by ~S+110 (the Genie's schedule; unverified).
        t.ticklog.mark("rick.ignore")
        t.cheat("::rick", false)
        t.ticks(60)
        t.check("ignore.stays", first_row(t, "macro_highwayman") ~= nil, "Rick still waiting at ~S+60")
        t.ticks(50)
        t.check("ignore.gone", first_row(t, "macro_highwayman") == nil, "Rick left by ~S+110")

        t.finish(0)
    end,
}
