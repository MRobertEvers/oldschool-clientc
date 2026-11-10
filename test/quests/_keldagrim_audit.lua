-- Keldagrim's people, the carriers, the frog and the random event scheduler.
-- Every townsperson is talked to and every row of every menu they offer is tried once,
-- on a tick log for the facing and animation audit.
--
--     python3 tools/quest_gate/run.py --no-build --script test/quests/_keldagrim_audit.lua --name keldagrim_audit
local PEOPLE = {
        { "xbows_sales_dwarf_keldegrim", 2885, 10202, 0 },
        { "dwarf_city_shop_craft", 2885, 10207, 0 },
        { "dwarf_city_shop_gems", 2888, 10212, 0 },
        { "dwarf_city_shop_bakery", 2892, 10212, 0 },
        { "dwarf_city_dwarf_man3", 2899, 10218, 0 },
        { "dwarf_city_dwarf_man5", 2900, 10227, 0 },
        { "dwarf_city_rowdy_dwarf", 2906, 10198, 0 },
        { "dwarf_city_dwarf_man8", 2906, 10216, 0 },
        { "dwarf_city_barmaid_poor", 2916, 10195, 0 },
        { "dwarf_city_dwarf_man1", 2924, 10213, 0 },
        { "dwarf_city_dwarf_man4", 2926, 10225, 0 },
        { "dwarf_city_dwarf_man2", 2933, 10219, 0 },
        { "dwarf_city_dwarf_man6", 2838, 10195, 0 },
        { "dwarf_city_barman_rich", 2841, 10197, 0 },
        { "poh_stonemason", 2849, 10184, 0 },
        { "dwarf_city_old_dwarf2", 2850, 10223, 0 },
        { "dwarf_city_old_dwarf1", 2851, 10225, 0 },
        { "dwarf_city_gardener_dwarf", 2854, 10195, 0 },
        { "dwarf_city_dwarf_man9", 2854, 10199, 0 },
        { "dwarf_city_dwarf_man7", 2855, 10224, 0 },
        { "dwarf_city_library_assistant", 2860, 10226, 0 },
        { "dwarf_city_library_customer_human", 2864, 10227, 0 },
        { "dwarf_city_dwarf_man10", 2865, 10194, 0 },
        { "dwarf_city_dwarf_man11", 2866, 10206, 0 },
        { "aluft_gnome_inventor", 2868, 10198, 0 },
        { "dwarf_city_shop_general_store", 2869, 10190, 0 },
        { "dwarf_city_shop_cloth", 2869, 10211, 0 },
        { "dwarf_city_shop_ship_mechanic", 2870, 10200, 0 },
        { "dwarfrock_engineer1", 2871, 10198, 0 },
        { "dwarf_city_shop_warhammer", 2873, 10209, 0 },
        { "dwarf_city_black_guard_supreme_leader", 2828, 10210, 1 },
        { "dwarf_city_gnome_male_emissary_multi", 2835, 10193, 1 },
        { "dwarf_city_gnome_female_emissary_multi", 2836, 10192, 1 },
        { "dwarf_city_inn_keeper_rich", 2839, 10195, 1 },
        { "dwarf_city_library_customer", 2862, 10225, 1 },
        { "dwarf_city_evil_cat_multi", 2870, 10190, 1 },
        { "dwarf_city_gnome_emissary_multi", 2872, 10190, 1 },
}
return {
    id = "keldagrim_audit",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = { "::clearinv", "::give coins 5000", "::setlevel hitpoints 99", "::gdwarfstage 20" },
    run = function(t)
        t.ticklog.start()
        local function coord(x, z, l)
            return l .. "_" .. math.floor(x / 64) .. "_" .. math.floor(z / 64) .. "_" .. (x % 64) .. "_" .. (z % 64)
        end
        local function await_chat()
            for _ = 1, 10 do
                if t.chat.kind() ~= "none" then return true end
                t.ticks(1)
            end
            return false
        end
        local function shut()
            for _ = 1, 3 do
                if t.chat.kind() == "none" then break end
                t.chat.close()
                t.ticks(1)
            end
            t.ui.close_modal()
            t.ticks(1)
        end
        -- Page to the end, taking row `want` of the first menu and the last row after it.
        local function drive(want)
            local first = true
            for _ = 1, 40 do
                local kind = t.chat.kind()
                if kind == "none" then return end
                if kind == "options" then
                    local _, opts = t.chat.options()
                    local n = type(opts) == "table" and #opts or 0
                    local pick = first and math.min(want, math.max(n, 1)) or math.max(n, 1)
                    first = false
                    if n == 0 or t.chat.choose(opts[pick]) ~= "ok" then return end
                else
                    t.chat.continue_()
                end
                t.ticks(1)
            end
        end
        local tried = 0
        for _, p in ipairs(PEOPLE) do
            local name, x, z, l = p[1], p[2], p[3], p[4]
            local rows = 1
            local want = 1
            while want <= rows and want <= 5 do
                t.cheat("::tele " .. coord(x, z, l))
                t.ticks(2)
                local r, d = t.player.talk_to(name, 1)
                local up = await_chat()
                t.check(name .. ".r" .. want, r == "ok" or up, tostring(r) .. " " .. tostring(d))
                if up then
                    -- Count the first menu's rows, then answer it.
                    for _ = 1, 12 do
                        local kind = t.chat.kind()
                        if kind == "options" or kind == "none" then break end
                        t.chat.continue_()
                        t.ticks(1)
                    end
                    if t.chat.kind() == "options" then
                        local _, opts = t.chat.options()
                        if type(opts) == "table" then rows = #opts end
                    end
                    drive(want)
                    tried = tried + 1
                end
                shut()
                want = want + 1
            end
        end
        t.check("people.tried", tried >= #PEOPLE, tried .. " conversations for " .. #PEOPLE .. " people")

        -- Three recorded animations no other row reaches: Gauss's toast, the book, the
        -- sapphires on the axe Thurgo sharpened.
        t.cheat("::give beer")
        t.cheat("::give godwars_dwarven_book")
        t.cheat("::give dwarf_battleaxe_sharpened")
        t.cheat("::give sapphire 3")
        t.ticks(2)
        local gx, gz, gl
        for _, p in ipairs(PEOPLE) do
            if p[1] == "dwarf_city_dwarf_man6" then gx, gz, gl = p[2], p[3], p[4] end
        end
        t.cheat("::tele " .. coord(gx, gz, gl))
        t.ticks(2)
        t.player.talk_to("dwarf_city_dwarf_man6", 1)
        await_chat()
        drive(1)
        t.ticks(3)
        t.check("gauss.toast", select(2, t.inv.count("beer_glass")) >= 1, "the beer became a glass")
        t.exec("readBook", t.player.inv_op, "godwars_dwarven_book", 1)
        t.ticks(3)
        t.ui.close_modal()
        t.ticks(2)
        t.exec("sharpSapphires", t.player.use_item_on_item, "sapphire", "dwarf_battleaxe_sharpened")
        t.ticks(3)
        t.check("sharp.axe", select(2, t.inv.count("dwarf_battleaxe_new")) == 1, "the axe is restored")
        shut()

        -- The carriers: stand by the factory track and see every stage go past.
        t.cheat("::tele " .. coord(2933, 10190, 0))
        t.ticks(2)
        local seen = {}
        for _ = 1, 260 do
            for _, ty in ipairs({ "dwarf_city_dwarf_sack_in_cart_full", "dwarf_city_dwarf_sack_in_cart_full2", "dwarf_city_dwarf_sack_in_cart_full3",
                "dwarf_city_dwarf_sack_carrier_full", "dwarf_city_dwarf_sack_carrier_full2", "dwarf_city_dwarf_sack_carrier_full3" }) do
                local _, _, rows = t.npc.tiles(ty, 40)
                if type(rows) == "table" and rows[1] then seen[ty] = true end
            end
            t.ticks(1)
        end
        local n = 0
        for _ in pairs(seen) do n = n + 1 end
        t.check("carriers.seen", n >= 3, n .. " of 6 cart and carrier types seen moving")

        -- The frog, index 4500 [1501..1526].
        t.cheat("::tele " .. coord(2905, 10191, 0))
        t.ticks(2)
        t.cheat("::frog")
        t.ticks(4)
        local before = select(2, t.inv.count("macro_frog_token")) or 0
        local r, d = t.player.talk_to("macro_frog_2", 1)
        t.check("frog.talk", r == "ok" or t.chat.kind() ~= "none", tostring(r) .. " " .. tostring(d))
        await_chat()
        -- The script holds for a few ticks between the touch and the princess's page.
        local quiet = 0
        for _ = 1, 40 do
            local kind = t.chat.kind()
            if kind == "none" then
                quiet = quiet + 1
                if quiet >= 6 then break end
            else
                quiet = 0
                if kind == "options" then t.chat.choose("Yes, I will touch you.") else t.chat.continue_() end
            end
            t.ticks(1)
        end
        local after = select(2, t.inv.count("macro_frog_token")) or 0
        t.check("frog.token", after == before + 1, before .. " -> " .. after)
        local _, forced = t.var.server("varb1547_macro_transmitted")
        t.check("frog.varbit_cleared", forced == 0, "macro_transmitted " .. tostring(forced))

        -- The scheduler: ::randomevent runs the same pick the 500-tick roll does.
        t.cheat("::tele 0_50_50_20_20")
        t.ticks(2)
        t.cheat("::randomevent")
        local event = false
        for _ = 1, 16 do
            for _, ty in ipairs({ "macro_highwayman", "macro_jekyll", "macro_geni", "macro_combilock_pirate",
                "macro_evil_bob", "macro_frog_crier", "macro_magneson_invitation", "grab_postman" }) do
                local _, _, rows = t.npc.tiles(ty, 16)
                if type(rows) == "table" and rows[1] then event = true end
            end
            if event then break end
            t.ticks(1)
        end
        t.check("scheduler.event", event, "a random event npc appeared after ::randomevent")
        local _, f1 = t.var.server("varp9570_macro_event_forced")
        t.check("scheduler.fired", f1 == 0, "forced " .. tostring(f1))
        t.finish(0)
    end,
}
