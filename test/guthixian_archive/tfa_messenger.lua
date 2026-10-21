-- tfa_messenger: the Alliance's message, as RSProx index 4292 [68..98]
-- recorded it (docs/rsprox-analysis/guthixian_archive/README.md):
--
--   the quest done, a Guardian of Armadyl appears ~63 ticks on and follows
--   Talk-to: "Hello there. I have a message for you.", then the letter
--   Read: the way into the Archive opens, the messenger goes, the long scroll
--
--   ./src/build_opt/torirsserver --scriptrun test/guthixian_archive/tfa_messenger.lua --bots 1 \
--       --name tfam --ticks 600 --session /tmp/tfam --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function present(t, sym)
    local r = t.npc.nearest(sym, 24)
    return r == "ok"
end

local function unlocked(t)
    local r, v = t.var.varbit("varb16354_wgs_tfa_shortcut_unlocked")
    return r == "ok" and v == 1
end

return {
    id = "tfa_messenger",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
    },
    run = function(t)
        t.ticklog.start()
        t.check("locked.before", not unlocked(t), "the way in was open before the letter")
        local ok = t.await({ level = function() return present(t, "tfa_messenger") end, note = "the messenger" }, 90)
        t.check("messenger.arrives", ok == "ok", "no messenger within 90 ticks")
        -- Standing still: it walks up to the player (4292 [69..74]).
        t.ticks(5)
        t.ticklog.mark("talk")
        local r, d = t.player.talk_to("tfa_messenger", 1)
        t.note("talk: " .. tostring(r) .. " " .. tostring(d))
        t.check("page.hello", t.chat.expect_text("Hello there. I have a message for you."))
        t.ui.press_pause("chat_left:continue", -1)
        t.check("page.objbox", t.chat.expect_text("You've been given a message."))
        t.ui.press_pause("objectbox:universe", 0)
        t.ticks(2)
        t.check("locked.unread", not unlocked(t), "the way in opened before the letter was read")
        t.ticklog.mark("read")
        r, d = t.player.inv_op("tfa_start_message", 1)
        t.note("read: " .. tostring(r) .. " " .. tostring(d))
        t.check("scroll.open", t.ui.await_open("longscroll", 5))
        t.check("unlocked", unlocked(t), "wgs_tfa_shortcut_unlocked still 0 after the read")
        t.ticks(1)
        t.check("messenger.gone", not present(t, "tfa_messenger"), "the messenger is still here")
        local wr, close = t.ui.widget("longscroll:close")
        t.check("scroll.close_widget", wr == "ok", tostring(wr))
        t.ui.invoke(close, 1)
        t.check("scroll.closed", t.ui.await_close("longscroll", 5))
        t.ticks(4)
        t.ticklog.mark("end")
        t.expect("tfa_messenger.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
