-- Zul-Andra and the row out, as RSProx index 3259 records the recorder's first
-- visit ([8240..8325]): the High Priestess to "I accept", the ritual at the
-- boat, the row (stage 3), the rise; then the shrine left, and the master
-- scroll book's open and store (index 4574 [430..444]).
-- docs/rsprox-analysis/zulrah/README.md
--
--     python3 tools/quest_gate/run.py --no-build --script test/bosses/zulandra.lua --name zulandra
return {
    id = "zulandra",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::setlevel hitpoints 99",
        "::give bookofscrolls_empty 1",
        "::give teleportscroll_zulandra 4",
    },

    run = function(t)
        t.ticklog.start()
        local function stage(name, want)
            local _, v = t.var.server("varb4391_snakeboss_info")
            t.check("stage." .. name, v == want, "snakeboss_info " .. tostring(v) .. " want " .. tostring(want))
        end
        local function await_page(n)
            for _ = 1, n or 15 do
                if t.chat.kind() ~= "none" then return end
                t.ticks(1)
            end
        end

        t.check("tele", t.cheat("::tele 0_34_47_18_48") == "ok", "beside the High Priestess")
        t.ticks(3)
        stage("start", 0)
        t.player.talk_to("snakeboss_highpriest", 1)
        await_page()
        t.exec("harcinqa", t.chat.play, {
            "npc:Why do you bother the High Priestess of Zulrah",
            "choose:Tell me about Zulrah.",
            "player:Tell me about Zulrah.",
            "npc:Zulrah is the all-powerful snake god",
            "npc:Long ago, our forebears came to this place",
            "npc:But Zulrah, in his mercy",
            "npc:Then my mother, oldest and wisest",
            "npc:Zulrah granted us the right",
            "npc:All he asks in return",
            "npc:Willing volunteers are never hard to find",
            "choose:How do people get chosen as a sacrifice to Zulrah?",
            "player:How do people get chosen",
            "npc:Volunteers offer themselves gladly",
            "npc:The next sacrifice is a brave gnome",
            "choose:I won't let you sacrifice him - take me instead!",
            "player:I won't let you sacrifice him",
            "npc:You would offer yourself as a sacrifice",
            "npc:... and I accept.",
            "choose:Have you any advice for surviving against Zulrah?",
            "player:Have you any advice",
            "npc:I think you have misunderstood",
        })
        t.ticks(2)
        stage("volunteered", 2)

        t.player.click_loc("snakeboss_boat", 1)
        await_page(30)
        t.exec("ritual", t.chat.play, {
            "player:The High Priestess said I could offer myself",
            "npc:It is rare for a traveller to make such an offer!",
            "npc:Once I have brought you to the shrine",
            "npc:You can use your magical powers",
            "npc:When Zulrah inevitably kills you",
            "npc:Are you ready to face Zulrah?",
            "choose:Yes, let's go.",
            "player:Yes, let's go.",
            "npc:I shall now transport you",
        })
        t.ticks(1)
        stage("rowed", 3)
        -- O+3 the move into the copy; O+4 the box with a continue.
        for _ = 1, 15 do
            local r, tl = t.world.tile()
            if r == "ok" and tl.x > 6000 then break end
            t.ticks(1)
        end
        t.ticks(2)
        t.chat.continue_()
        local present = t.npc.await_present("snakeboss_boss_ranged", 30, 8)
        t.check("rise", present == "ok", "Zulrah rose: " .. tostring(present))
        local _, active = t.var.server("varp6553_zulrah_active")
        t.check("active", active == 1, "zulrah_active " .. tostring(active))
        t.ticks(8)

        -- Out by the debug path (~zulrah_leave), into the 3x3 on 2196,3056.
        t.cheat("::zulrah")
        t.ticks(4)
        local r, tl = t.world.tile()
        t.check("landed", r == "ok" and tl.x >= 2195 and tl.x <= 2197 and tl.z >= 3055 and tl.z <= 3057,
            "tile " .. tostring(tl and tl.x) .. "," .. tostring(tl and tl.z))

        -- Zul-Gwenwynig at stage 3, [R] index 819 [10630].
        t.player.talk_to("snakeboss_priest", 1)
        await_page()
        t.exec("gwenwynig", t.chat.play, {
            "npc:You left the shrine. Did Zulrah reject your sacrifice?",
            "choose:I'm off.",
            "player:I'm off.",
        })
        t.ticks(2)

        -- The master scroll book (index 4574 [430], [435]).
        t.player.inv_op("bookofscrolls_empty", 3)
        t.ticks(2)
        t.player.use_item_on_item("teleportscroll_zulandra", "bookofscrolls_empty")
        t.ticks(2)
        local _, stored = t.var.server("varb5683_bookofscrolls_zulandra")
        t.check("book.stored", stored == 4, "zulandra scrolls in the book " .. tostring(stored))
    end,
}
