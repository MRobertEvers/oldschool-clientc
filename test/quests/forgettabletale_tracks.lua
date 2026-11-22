-- A Forgettable Tale: every track of the three junction groups (forget_tracks.rs2), not only
-- the nine routes the quest needs. The leaves are generated from the track graphs rebuilt
-- from RSProx index 4646 (docs/rsprox-analysis/quests/forgettable_tale.md, "The rides"):
-- every dead end, every junction left empty, every platform -- 80 rides. The three story
-- rooms are ridden by test/quests/forgettabletale.lua.

local LEAVES = {

    { g = 1, lay = "00000000", expect = "blocked", why = "J0" },
    { g = 1, lay = "10000000", expect = "blocked", why = "J1" },
    { g = 1, lay = "11000000", expect = "blocked", why = "J3" },
    { g = 1, lay = "11010000", expect = "P3", why = "(1897, 4974)N" },
    { g = 1, lay = "11020000", expect = "P2", why = "(1897, 4974)S" },
    { g = 1, lay = "12000000", expect = "blocked", why = "J4" },
    { g = 1, lay = "12001000", expect = "P2", why = "(1897, 4961)N" },
    { g = 1, lay = "12002000", expect = "dead", why = "(1897, 4961)S" },
    { g = 1, lay = "20000000", expect = "blocked", why = "J2" },
    { g = 1, lay = "20100000", expect = "P1", why = "(1881, 4948)N" },
    { g = 1, lay = "20200000", expect = "blocked", why = "J5" },
    { g = 1, lay = "20200100", expect = "blocked", why = "J6" },
    { g = 1, lay = "20200110", expect = "ROOM", why = "(1899, 4949)N" },
    { g = 1, lay = "20200120", expect = "blocked", why = "J7" },
    { g = 1, lay = "20200121", expect = "dead", why = "(1908, 4943)N" },
    { g = 1, lay = "20200122", expect = "dead", why = "(1908, 4943)S" },
    { g = 1, lay = "20200200", expect = "dead", why = "(1889, 4939)S" },
    { g = 2, lay = "000000000000", expect = "blocked", why = "J0" },
    { g = 2, lay = "100000000000", expect = "blocked", why = "J1" },
    { g = 2, lay = "110000000000", expect = "blocked", why = "J3" },
    { g = 2, lay = "110100000000", expect = "blocked", why = "J6" },
    { g = 2, lay = "110100100000", expect = "dead", why = "(1899, 4980)N" },
    { g = 2, lay = "110100200000", expect = "blocked", why = "J8" },
    { g = 2, lay = "110100201000", expect = "dead", why = "(1906, 4976)N" },
    { g = 2, lay = "110100202000", expect = "P3", why = "(1906, 4976)S" },
    { g = 2, lay = "110200000000", expect = "blocked", why = "J9" },
    { g = 2, lay = "110200000100", expect = "blocked", why = "J11" },
    { g = 2, lay = "110200000101", expect = "dead", why = "(1911, 4963)N" },
    { g = 2, lay = "110200000102", expect = "ROOM", why = "(1911, 4963)S" },
    { g = 2, lay = "110200000200", expect = "dead", why = "(1904, 4958)S" },
    { g = 2, lay = "120000000000", expect = "blocked", why = "J4" },
    { g = 2, lay = "120010000000", expect = "dead", why = "(1887, 4962)N" },
    { g = 2, lay = "120020000000", expect = "P1", why = "(1887, 4962)S" },
    { g = 2, lay = "200000000000", expect = "blocked", why = "J2" },
    { g = 2, lay = "201000000000", expect = "dead", why = "(1877, 4948)N" },
    { g = 2, lay = "202000000000", expect = "blocked", why = "J5" },
    { g = 2, lay = "202001000000", expect = "blocked", why = "J10" },
    { g = 2, lay = "202001000010", expect = "P2", why = "(1899, 4943)N" },
    { g = 2, lay = "202001000020", expect = "dead", why = "(1899, 4943)S" },
    { g = 2, lay = "202002000000", expect = "blocked", why = "J7" },
    { g = 2, lay = "202002010000", expect = "blocked", why = "J10" },
    { g = 2, lay = "202002010010", expect = "P2", why = "(1899, 4943)N" },
    { g = 2, lay = "202002010020", expect = "dead", why = "(1899, 4943)S" },
    { g = 2, lay = "202002020000", expect = "dead", why = "(1893, 4938)S" },
    { g = 3, lay = "0000000000000000000", expect = "blocked", why = "J0" },
    { g = 3, lay = "1000000000000000000", expect = "blocked", why = "J1" },
    { g = 3, lay = "1100000000000000000", expect = "blocked", why = "J3" },
    { g = 3, lay = "1101000000000000000", expect = "dead", why = "(1881, 4970)N" },
    { g = 3, lay = "1102000000000000000", expect = "blocked", why = "J5" },
    { g = 3, lay = "1102010000000000000", expect = "blocked", why = "J7" },
    { g = 3, lay = "1102010100000000000", expect = "dead", why = "(1892, 4976)N" },
    { g = 3, lay = "1102010200000000000", expect = "blocked", why = "J10" },
    { g = 3, lay = "1102010200100000000", expect = "blocked", why = "J13" },
    { g = 3, lay = "1102010200100100000", expect = "dead", why = "(1903, 4975)N" },
    { g = 3, lay = "1102010200100200000", expect = "blocked", why = "J16" },
    { g = 3, lay = "1102010200100200100", expect = "dead", why = "(1908, 4969)N" },
    { g = 3, lay = "1102010200100200200", expect = "ROOM", why = "(1908, 4969)S" },
    { g = 3, lay = "1102010200200000000", expect = "blocked", why = "J14" },
    { g = 3, lay = "1102010200200010000", expect = "dead", why = "(1901, 4965)N" },
    { g = 3, lay = "1102010200200020000", expect = "dead", why = "(1901, 4965)S" },
    { g = 3, lay = "1102020000000000000", expect = "P1", why = "(1886, 4965)S" },
    { g = 3, lay = "1200000000000000000", expect = "dead", why = "(1875, 4965)S" },
    { g = 3, lay = "2000000000000000000", expect = "blocked", why = "J2" },
    { g = 3, lay = "2010000000000000000", expect = "dead", why = "(1876, 4950)N" },
    { g = 3, lay = "2020000000000000000", expect = "blocked", why = "J4" },
    { g = 3, lay = "2020100000000000000", expect = "dead", why = "(1884, 4945)N" },
    { g = 3, lay = "2020200000000000000", expect = "blocked", why = "J6" },
    { g = 3, lay = "2020201000000000000", expect = "blocked", why = "J8" },
    { g = 3, lay = "2020201010000000000", expect = "blocked", why = "J11" },
    { g = 3, lay = "2020201010010000000", expect = "P2", why = "(1901, 4955)N" },
    { g = 3, lay = "2020201010020000000", expect = "blocked", why = "J15" },
    { g = 3, lay = "2020201010020001000", expect = "dead", why = "(1910, 4949)N" },
    { g = 3, lay = "2020201010020002000", expect = "dead", why = "(1910, 4949)S" },
    { g = 3, lay = "2020201020000000000", expect = "dead", why = "(1896, 4950)S" },
    { g = 3, lay = "2020202000000000000", expect = "blocked", why = "J9" },
    { g = 3, lay = "2020202001000000000", expect = "blocked", why = "J12" },
    { g = 3, lay = "2020202001001000000", expect = "dead", why = "(1903, 4941)N" },
    { g = 3, lay = "2020202001002000000", expect = "blocked", why = "J17" },
    { g = 3, lay = "2020202001002000010", expect = "blocked", why = "J18" },
    { g = 3, lay = "2020202001002000011", expect = "dead", why = "(1913, 4942)N" },
    { g = 3, lay = "2020202001002000012", expect = "dead", why = "(1913, 4942)S" },
    { g = 3, lay = "2020202001002000020", expect = "dead", why = "(1908, 4937)S" },
    { g = 3, lay = "2020202002000000000", expect = "dead", why = "(1896, 4937)S" },
}

local function sv(t, sym) return select(2, t.var.server(sym)) end
local function here(t)
    local r, tl = t.world.tile()
    local _, lvl = t.world.level()
    if r ~= "ok" or type(tl) ~= "table" then return nil, tostring(tl) end
    local x, z = tl.x, tl.z
    if x >= 6400 then x, z = 1856 + x % 64, 4928 + z % 64 end
    return { x = x, z = z, level = lvl }, x .. "," .. z .. "," .. tostring(lvl)
end
local function at(t, x, z, level)
    local tl = here(t)
    return tl ~= nil and tl.x == x and tl.z == z and tl.level == level
end
local function play(t, row, list, ticks)
    local kind, text = list[1]:match("^(%a+):(.*)$")
    t.await({
        level = function()
            local k = t.chat.kind()
            if k == "none" then return false end
            if kind == "choose" then return k == "options" end
            local raw = t.read._presented_text(t.read._text_symbols)
            if raw == nil then return false end
            return t.read._strip_tags(tostring(raw)):find(text, 1, true) ~= nil
        end,
        note = row .. ": waiting for '" .. list[1] .. "'",
    }, ticks or 12)
    t.exec(row, t.chat.play, list)
end
local function await_hub(t, row)
    t.await({ level = function() return at(t, 1863, 4956, 1) end, note = row .. ": the hub" }, 30)
    local _, txt = here(t)
    t.check(row .. "-hub", at(t, 1863, 4956, 1), "at " .. txt .. " (want the hub 1863,4956,1)")
end
local function enter(t, row)
    t.exec("goto-" .. row, t.player.goto_tile, 2919, 10166, 0)
    t.exec(row, t.player.click_loc, "keldagrim_train_cart", 1, { at = { 2919, 10164, 0 } })
    t.await({ level = function() return at(t, 1863, 4956, 1) end, note = row .. ": the hub" }, 60)
    local _, txt = here(t)
    t.check(row .. "-hub", at(t, 1863, 4956, 1), "at " .. txt)
end
local function leave(t, row)
    t.exec(row, t.player.click_loc, "forget_train_cart", 2)
    play(t, row .. "-dialog", { "choose:Yes." }, 12)
    t.await({ level = function() local tl = here(t); return tl ~= nil and tl.level == 0 end, note = row }, 20)
end

local function ride(t, k, leaf)
    local row = "g" .. leaf.g .. "-" .. k .. "-" .. leaf.expect
    for i = 1, #leaf.lay do
        t.cheat("::setvar varb" .. (841 + i) .. "_forget_if" .. i .. " " .. leaf.lay:sub(i, i))
    end
    t.cheat("::setvar varp7868_forget_route 0")
    t.exec(row, t.player.click_loc, "forget_train_cart", 1)
    if leaf.expect == "blocked" then
        play(t, row .. "-line", { "player:This junction is blocked. I'd better go back to the start" }, 90)
        await_hub(t, row)
    elseif leaf.expect == "dead" then
        play(t, row .. "-line", { "player:I've come to a dead end, I'd better go back to the start." }, 90)
        await_hub(t, row)
    else
        local want = tonumber(leaf.expect:sub(2))
        t.await({ level = function()
            local tl = here(t)
            return tl ~= nil and tl.level == 1 and sv(t, "varp7868_forget_route") == want
        end, note = row .. ": the platform" }, 90)
        local _, txt = here(t)
        t.check(row .. "-platform", (here(t) or {}).level == 1 and sv(t, "varp7868_forget_route") == want,
            "at " .. txt .. ", box " .. tostring(sv(t, "varp7868_forget_route")) .. " (" .. leaf.why .. ")")
        t.exec(row .. "-back", t.player.click_loc, "forget_train_return_cart", 1)
        await_hub(t, row .. "-back")
    end
end

local function group(g, stage)
    return { name = "group" .. g, run = function(t)
        t.cheat("::setvar varb822_forget_quest " .. stage)
        enter(t, "enter" .. g)
        local k = 0
        for _, leaf in ipairs(LEAVES) do
            if leaf.g == g and leaf.expect ~= "ROOM" then
                k = k + 1
                ride(t, k, leaf)
            end
        end
        leave(t, "leave" .. g)
    end }
end

return {
    id = "forgettabletale_tracks",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::complete quest_giantdwarf",
        "::complete quest_fishingcontest",
        "::setvar varb825_forget_boarding_removed 1",
        "::setvar varb839_forget_tunnel_cutscene1 1",
        "::setvar varb840_forget_tunnel_cutscene2 1",
        "::setvar varb841_forget_tunnel_cutscene3 1",
        "::tele 0_45_158_39_54",
    },
    legs = { group(1, 100), group(2, 110), group(3, 118) },
}
