-- kharedst_memoirs: Client of Kourend's reward book and its castle teleport,
-- as RSProx recorded them (docs/rsprox-analysis/quests/client_of_kourend.md,
-- section 8):
--   - a page's Inspect, the player's page (index 4418 [1032]);
--   - Reminisce with an entry missing: "There is nothing here to remember."
--     (4418 [903]);
--   - a page on the book: its varbit, 20 memories, the objbox (4416 [1201],
--     4418 [1041]), and the book on a page the same;
--   - Reminisce from the menu (4416 [1218], 4418 [907]) and from the
--     right-click entry, each one memory and the page's landing;
--   - the Kourend Castle teleport tablet, within 2 of 1641,3673 (10 recorded
--     landings, 4418 [36] among them);
--   - all five places, the Kourend & Kebos medium task: each city's bit, and with
--     the fifth the task's bit on kourend_achievement_diary and the count (index
--     4426 [2828..2860]).
--
--   ./src/build_opt/torirsserver --scriptrun test/magic/kharedst_memoirs.lua --bots 1 \
--       --name memoirs0 --ticks 600 --session /tmp/memoirs --fixture test/raids/fixtures/fresh_lumbridge.ini

local function near(t, label, x, z, r)
    local res, v = t.world.tile()
    local ok = res == "ok" and type(v) == "table" and v.level == 0
        and math.abs(v.x - x) <= r and math.abs(v.z - z) <= r
    t.check(label, ok, "want within " .. r .. " of " .. x .. "," .. z .. ", read "
        .. ((type(v) == "table" and v.x) and (v.x .. "," .. v.z .. "," .. v.level) or tostring(res)))
end

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end

return {
    id = "kharedst_memoirs",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", "::give veos_kharedsts_memoirs 1", "::give veos_memoirs_pisc_page 1",
        "::give veos_memoirs_hos_page 1", "::give poh_tablet_kourendteleport 1",
        "::give veos_memoirs_shay_page 1", "::give veos_memoirs_lova_page 1", "::give veos_memoirs_arc_page 1",
    },
    run = function(t)
        t.ticklog.start()
        t.ticks(3)
        t.expect("start.charges", t.var.expect("varb6035_veos_memoir_charges", 0))

        t.ticklog.mark("inspect")
        t.exec("inspect.op", t.player.inv_op, "veos_memoirs_pisc_page", 1)
        t.exec("inspect.chat", t.chat.play, {
            "player:This looks like one of the pages to Kharedst's Memoirs. Maybe I should try adding it to the book.", "end",
        })

        t.ticklog.mark("missing")
        t.exec("missing.op", t.player.inv_op, "veos_kharedsts_memoirs", 1)
        t.exec("missing.chat", t.chat.play, { "options", "choose:/second entry is missing/", "end" })
        t.ticks(2)
        t.expect("missing.free", t.var.expect("varb6035_veos_memoir_charges", 0))

        t.ticklog.mark("add.pisc")
        t.exec("add.pisc.use", t.player.use_item_on_item, "veos_memoirs_pisc_page", "veos_kharedsts_memoirs")
        t.exec("add.pisc.chat", t.chat.play, { "objbox:You add an entry to Kharedst's Memoirs.", "end" })
        t.expect("add.pisc.page", t.var.expect("varb6031_piscarilius_page", 1))
        t.expect("add.pisc.charges", t.var.expect("varb6035_veos_memoir_charges", 20))
        t.check("add.pisc.gone", count(t, "veos_memoirs_pisc_page") == 0, "pages " .. count(t, "veos_memoirs_pisc_page"))

        t.ticklog.mark("remember.pisc")
        t.exec("remember.pisc.op", t.player.inv_op, "veos_kharedsts_memoirs", 1)
        t.exec("remember.pisc.chat", t.chat.play, { "options", "choose:'The Fisher's Flute' - Piscarilius", "end" })
        t.ticks(6)
        near(t, "remember.pisc.landed", 1804, 3748, 2)
        t.expect("remember.pisc.charges", t.var.expect("varb6035_veos_memoir_charges", 19))

        t.ticklog.mark("add.hos")
        t.exec("add.hos.use", t.player.use_item_on_item, "veos_kharedsts_memoirs", "veos_memoirs_hos_page")
        t.exec("add.hos.chat", t.chat.play, { "objbox:You add an entry to Kharedst's Memoirs.", "end" })
        t.expect("add.hos.page", t.var.expect("varb6030_hosidius_page", 1))
        t.expect("add.hos.charges", t.var.expect("varb6035_veos_memoir_charges", 39))

        t.ticklog.mark("remember.hos")
        t.exec("remember.hos.op", t.player.inv_op, "veos_kharedsts_memoirs", 1, { subop = 1 })
        t.ticks(6)
        near(t, "remember.hos.landed", 1713, 3612, 2)
        t.expect("remember.hos.charges", t.var.expect("varb6035_veos_memoir_charges", 38))

        t.ticklog.mark("check")
        t.exec("check.op", t.player.inv_op, "veos_kharedsts_memoirs", 4)
        t.exec("check.chat", t.chat.play, {
            "objbox:On the inside of the cover a message is displayed in dark ink. It reads: 38/40 memories remain.", "end",
        })

        t.ticklog.mark("castle")
        t.exec("castle.break", t.player.inv_op, "poh_tablet_kourendteleport", 1)
        t.ticks(6)
        near(t, "castle.landed", 1641, 3673, 2)

        -- The diary: Piscarilius and Hosidius done above, the other three now.
        t.expect("diary.pisc", t.var.expect("varb7921_kourend_diary_pisc_teleport", 1))
        t.expect("diary.hos", t.var.expect("varb7918_kourend_diary_hos_teleport", 1))
        t.expect("diary.not_yet", t.var.expect("varb7934_kourend_med_count", 0))
        local rest = {
            { "veos_memoirs_shay_page", 3, "varb7920_kourend_diary_shay_teleport", 1478, 3576 },
            { "veos_memoirs_lova_page", 4, "varb7919_kourend_diary_lova_teleport", 1545, 3761 },
            { "veos_memoirs_arc_page", 5, "varb7917_kourend_diary_arc_teleport", 1681, 3747 },
        }
        for _, r in ipairs(rest) do
            t.ticklog.mark("diary." .. r[1])
            t.exec("diary.add." .. r[1], t.player.use_item_on_item, r[1], "veos_kharedsts_memoirs")
            t.exec("diary.add." .. r[1] .. ".chat", t.chat.play, { "objbox:You add an entry to Kharedst's Memoirs.", "end" })
            t.exec("diary.remember." .. r[1], t.player.inv_op, "veos_kharedsts_memoirs", 1, { subop = r[2] })
            t.ticks(6)
            near(t, "diary.landed." .. r[1], r[4], r[5], 2)
            t.expect("diary.bit." .. r[1], t.var.expect(r[3], 1))
        end
        t.expect("diary.started", t.var.expect("varb7924_started_kourend_diary", 1))
        t.expect("diary.count", t.var.expect("varb7934_kourend_med_count", 1))
        t.finish(0)
    end,
}
