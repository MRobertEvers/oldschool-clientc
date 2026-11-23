-- A Forgettable Tale of a Drunken Dwarf (quest_forgettabletale), driven through the content
-- rebuilt from RSProx index 4637 and 4646 (docs/rsprox-analysis/quests/forgettable_tale.md).
-- Every page below is the recording's, as the .rs2 files carry it:
--   forget_veldaban.rs2 (the start, the flyover, the report), forget_keldagrim.rs2 (the Drunken
--   Dwarf, Rowdy, Gauss, Khorvak, the tale), forget_farming.rs2 (Rind, the patch),
--   forget_brewing.rs2 (Blandebir, the vat, the barrel), forget_tunnels.rs2 (tickets, carts,
--   conductor, director, the tunnel entry pans), forget_puzzle.rs2 (stones, machinery, routes),
--   forget_story.rs2 (listening room, library, the last scene), forget_pub.rs2 (the ending).
-- Legs (docs/quest_authoring/relay.md): each leg is self-contained; the file-level helpers below
-- take `t` and share no state. The puzzle machinery resets every junction when it is opened
-- (forget_puzzle.rs2 [oploc1,keldagrim_track_junction_control_box]), so each route is set in one
-- opening, and a leg never ends with a route half set.

-- ---------------------------------------------------------------- helpers
local GREEN = "turnleft_trackjunction_control_card"   -- forget_puzzle.rs2: the green stone
local YELLOW = "turnright_trackjunction_control_card" -- the yellow stone
local SWITCHES = "abcdefghijklmnopqrs"

local function n(t, sym) return select(2, t.inv.count(sym)) or 0 end
local function sv(t, sym) return select(2, t.var.server(sym)) end
local function here(t)
    local r, tl = t.world.tile()
    local _, lvl = t.world.level()
    if r ~= "ok" or type(tl) ~= "table" then return nil, tostring(tl) end
    local x, z = tl.x, tl.z
    -- The tunnels are the player's own copy (forget_tunnels.rs2 ~forget_enter_tunnels),
    -- 64-aligned: read a tile there as the template square's (29_77).
    if x >= 6400 then
        x, z = 1856 + x % 64, 4928 + z % 64
    end
    return { x = x, z = z, level = lvl }, x .. "," .. z .. "," .. tostring(lvl)
end
-- A template tile of the tunnel square (29_77) in the player's copy, for an exact click.
local function tc(t, x, z)
    local r, tl = t.world.tile()
    if r ~= "ok" or type(tl) ~= "table" or tl.x < 6400 then return x, z end
    return tl.x - tl.x % 64 + (x - 1856), tl.z - tl.z % 64 + (z - 4928)
end
local function at(t, x, z, level)
    local tl = here(t)
    return tl ~= nil and tl.x == x and tl.z == z and tl.level == level
end
local function await_at(t, note, x, z, level, ticks)
    t.await({ level = function() return at(t, x, z, level) end, note = note }, ticks)
end

-- Wait for the page `list[1]` names (its text up, or a menu for "choose:"), then read the list
-- with chat.play. A page the content opens after a delay (a fade, a timed scene, a box's
-- p_delay) is waited for here, so chat.play never grades the gap before it.
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

-- A camera sequence read from the mark taken before the click that started it. `quiet` covers
-- the longest gap between two camera packets (the last scene's 80 ticks, the pub's 100).
local function scene(t, row, mark, expect)
    if mark == nil then return end
    t.exec(row, t.cutscene.await, row, { since = mark, quiet = 200, timeout = 200, expect = expect })
end

local function stones(t)
    return "carried green " .. n(t, GREEN) .. " yellow " .. n(t, YELLOW) .. ", slab green "
        .. tostring(sv(t, "varb861_forget_num_left")) .. " yellow " .. tostring(sv(t, "varb862_forget_num_right"))
end

-- A storage box: the op, the mesbox three ticks on, the stones it held added to the pack.
-- `box` names a platform's box by its tile, template-equivalent in the copy: a platform zone
-- is copied to two places in group one, so the nearest box can be the other platform's
-- (4646 --raw: the recorder's boxes at 34,25 / 49,42 / 42,25 / 50,17 / 34,35 / 50,34).
local function search_box(t, row, line, green, yellow, box)
    local g0, y0 = n(t, GREEN), n(t, YELLOW)
    local opts = nil
    if box then
        local bx, bz = tc(t, box[1], box[2])
        opts = { at = { bx, bz, 1 } }
    end
    t.exec(row, t.player.click_loc, "keldagrim_track_junction_card_box", 1, opts)
    play(t, row .. "-dialog", { "mesbox:" .. line }, 12)
    local g1, y1 = n(t, GREEN), n(t, YELLOW)
    t.check(row .. "-stones", g1 - g0 == green and y1 - y0 == yellow,
        "green " .. g0 .. " -> " .. g1 .. " (want +" .. green .. "), yellow " .. y0 .. " -> " .. y1
        .. " (want +" .. yellow .. ")")
end

-- One opening of the machinery: every junction set as the route wants (forget_route_expected,
-- 1 green / 2 yellow), Ok pressed, the route checked.
local function junction(t, i) return sv(t, "varb" .. (842 + i) .. "_forget_if" .. (i + 1)) end
local function set_route(t, start_row, iface, steps, ok_row, route)
    t.exec(start_row, t.player.click_loc, "keldagrim_track_junction_control_box", 1)
    t.exec(start_row .. "-open", t.ui.await_open, iface, 30)
    t.check(start_row .. "-loaded", sv(t, "varb861_forget_num_left") == n(t, GREEN)
        and sv(t, "varb862_forget_num_right") == n(t, YELLOW), stones(t))
    for _, s in ipairs(steps) do
        local i, want, name = s[1], s[2], s[3]
        local _, w = t.ui.widget(iface .. ":switch_" .. SWITCHES:sub(i + 1, i + 1))
        if w == nil then
            t.check(name, false, iface .. " has no switch for junction " .. i)
            return
        end
        for _ = 1, 3 do
            if junction(t, i) == want then break end
            -- op 1, as Jagex's client sends it (4646: `[if_buttonx] com=forget_puzzle1:switch_a
            -- (248:35), op=1`); the op-less press is an IF1 button's, which these are not.
            t.ui.invoke(w, 1)
            t.ticks(2)
        end
        local v = junction(t, i)
        t.check(name, v == want, "junction " .. i .. " is " .. tostring(v) .. " want " .. want .. " (" .. stones(t) .. ")")
    end
    local _, okw = t.ui.widget(iface .. ":ok_button")
    if okw == nil then
        t.check(ok_row, false, iface .. " has no ok_button")
        return
    end
    t.ui.invoke(okw, 1)
    local closed = t.ui.await_close(iface, 30)
    -- The ride follows the junctions themselves (forget_tracks.rs2); Ok only closes.
    t.check(ok_row, closed == "ok", "interface closed: " .. tostring(closed) .. " (route " .. route .. ")")
end

-- The hub cart to a platform (routes 1 2 4 5 7 8), and the platform's cart back.
local function ride_to_platform(t, row, small)
    t.exec(row, t.player.click_loc, "forget_train_cart", 1)
    -- A platform: plane 1 of the copy (its zones are the template's plane 3), its box remembered (~forget_cart_platform); which
    -- box is the platform's: 1 for the first route of a group, 2 for the second.
    local want = small and 1 or 2
    t.await({ level = function()
        local tl = here(t)
        return tl ~= nil and tl.level == 1 and sv(t, "varp7868_forget_route") == want
    end, note = row .. ": the platform" }, 60)
    local _, txt = here(t)
    t.check(row .. "-platform", (here(t) or {}).level == 1 and sv(t, "varp7868_forget_route") == want,
        "at " .. txt .. ", box " .. tostring(sv(t, "varp7868_forget_route")) .. " (want platform box " .. want .. ")")
end
-- `cart` is the platform's own cart tile (template-equivalent in the copy), for the same
-- reason as search_box's `box` (--raw 3059, 3152, 3798, 3939, 4416, 4543).
local function ride_back(t, row, cart)
    local cx, cz = tc(t, cart[1], cart[2])
    t.exec(row, t.player.click_loc, "forget_train_return_cart", 1, { at = { cx, cz, 1 } })
    await_at(t, row .. ": the hub", 1863, 4956, 1, 20)
    local _, txt = here(t)
    t.check(row .. "-hub", at(t, 1863, 4956, 1), "at " .. txt .. " (want the hub 1863,4956,1)")
end

return {
    id = "forgettabletale",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv", -- the fixture's tutorial slots
        -- Requirements: The Giant Dwarf + Fishing Contest (forget_meets_quest_reqs), Cooking 22 and
        -- Farming 17 (forget_meets_requirements, the planting gate)
        "::complete quest_giantdwarf",
        "::complete quest_fishingcontest",
        "::setlevel cooking 22",
        "::setlevel farming 17",
        -- The Giant Dwarf leaves the player in a company; the recorder's was the Green Gemstone
        -- (4646 [2629..2656]), whose director takes the boarding down at stage 80.
        "::setvar varb578_giantdwarf_current_company 4",
        -- Rowdy dwarf names one of 24 items on the first ask (forget_rowdy_talk); the recording's
        -- was a fake beard (4637 [1174], index 12). A request already set is kept, so the test
        -- gives the recorded one and brings it.
        "::setvar varp7869_forget_rowdy_item 12",
        "::give feud_karidian_fakebeard 1",
        -- Brought along: two beers (the Drunken Dwarf's, and the one drunk at the end), a beer
        -- glass (Gauss's toast, then the stout), a kebab (held for the ending), the brewing
        -- ingredients, the patch tools and coins (tickets 2x50, yeast 25).
        "::give beer 2",
        "::give beer_glass 1",
        "::give kebab 1",
        "::give barley_malt 2",
        "::give bucket_water 2",
        "::give dibber 1",
        "::give rake 1",
        "::give coins 300",
        -- A weapon in hand, so the rides' stash is exercised: every Keldagrim cart takes the
        -- weapon off for the ride and gives it back on landing (4494 [2820..2838], 4637
        -- [1508..1527]; keldagrim_travel.rs2 ~keldagrim_cart_seat).
        "::give bronze_dagger 1",
        "::wield bronze_dagger",
    },
    bind = {
        varp = "varb822_forget_quest",
        constants = {
            not_started = 0, talk_drunkdwarf = 10, bring_beer = 20, collect_seeds = 30,
            grow_kelda = 40, brew_stout = 50, give_stout = 60, hear_more = 65,
            ask_conductor = 70, ask_director = 80, tunnels_first = 100, listening_room = 105,
            tunnels_second = 110, library = 115, tunnels_third = 118, last_cutscene = 119,
            report_veldaban = 120, eat_kebab = 130, complete = 140,
        },
        row = "quest_forgettabletale",
        display = "Forgettable Tale...",
        points = 2,
    },
    legs = {
        { name = "keldagrim_seeds", run = function(t)
            -- LEG 1: the start and the flyover, the Drunken Dwarf, and the four seeds
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- The Grand Exchange trapdoor's Travel: the forced ride to the station (keldagrim_travel.rs2)
            t.exec("goto-travelToKeldagrim", t.player.goto_tile, 3141, 3504, 0)
            t.exec("travelToKeldagrim", t.player.click_loc, "ge_keldagrim_trapdoor", 2, { at = { 3140, 3504, 0 } })
            -- Seated, the dagger off (4494 [2820]); back in hand with the landing ([2838]).
            local seated = t.await({ level = function()
                local r, c = t.ui._worn_count("bronze_dagger")
                return r == "ok" and c == 0
            end, note = "the dagger off on the cart" }, 20)
            t.check("travelToKeldagrim-unarmed", seated == "ok", "dagger off on the cart: " .. tostring(seated))
            await_at(t, "the ride to the station", 2909, 10174, 0, 40)
            local _, ride = here(t)
            local wr, wc = t.ui._worn_count("bronze_dagger")
            t.check("travelToKeldagrim-arrived", at(t, 2909, 10174, 0), "at " .. ride .. " (want the station 2909,10174,0)")
            t.check("travelToKeldagrim-rearmed", wr == "ok" and wc == 1 and n(t, "bronze_dagger") == 0,
                "worn dagger " .. tostring(wr) .. " " .. tostring(wc) .. ", in the pack " .. n(t, "bronze_dagger"))
            t.exec("goto-veldabanDoor", t.player.goto_tile, 2827, 10219, 0)
            t.exec("veldabanDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door",
                open = "dwarf_keldagrim_door_open", at = { 2827, 10218, 0 }, near = { 2827, 10219 }, far = { 2827, 10217 } })

            -- forget_veldaban_start, 4637 [521..763]; the stage and the flyover go with the last click
            local fly = t.cutscene.mark()
            t.exec("talkToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
            play(t, "talkToVeldaban-dialog", {
                "npc:Have you heard?",
                "player:You mean, half the world has suddenly become a sailor?",
                "npc:No, not that. It's the Red Axe!",
                "player:What have those grumpy little guys done now?",
                "npc:They've left the city!",
                "player:Oh dear, is that serious?",
                "npc:It's never happened before!",
                "player:So what happens now?",
                "npc:Well... I may require your services again.",
                "choose:Very interested!",
                "player:Very interested!",
                "npc:We need some more information about what the Red Axe is up to.",
                "player:Are you asking me to... spy on them?",
                "npc:The other directors of the Consortium",
                "player:So where would you like me to go?",
                "npc:That's the problem.",
                "player:Yes?",
                "npc:There is a dwarf, all the way to the northeast",
                "choose:Yes.",
                "player:Yes, I do believe I have.",
                "npc:That's him, poor fellow.",
                "npc:But he seems to know something about the Red Axe",
                "npc:Problem is, no one believes him.",
                "npc:But if he does indeed know something",
                "npc:Do you want to help the Black Guard with this?",
                "choose:Yes.",
                "player:Sounds like just the job for me!",
                "npc:Excellent, good luck to you in your quest!",
            })
            t.expect("quest.stage.talk_drunkdwarf", t.quest.expect_stage("talk_drunkdwarf"))
            -- The flyover (forget_opening_scene, 4637 [771..845]): the city under the camera, the
            -- title card, then the player at the Drunken Dwarf's door with his line.
            play(t, "openingFlyover-dialog", { "player:Well, here I am... at the drunken dwarf's relative's residence" }, 100)
            scene(t, "openingFlyover.cutscene", fly, {
                { op = "moveto", coord = "0_45_159_8_0", height = 1200 },
                { op = "lookat", coord = "0_45_159_0_2", height = 700 },
                { op = "moveto", coord = "0_45_159_31_6", height = 1200 },
                { op = "lookat", coord = "0_45_159_34_44", height = 700 },
                { op = "moveto", coord = "0_45_159_29_38", height = 1200 },
                { op = "reset" },
            })
            local _, door = here(t)
            t.check("openingFlyover-door", at(t, 2913, 10218, 0), "after the flyover at " .. door .. " (want 2913,10218,0)")

            -- The Drunken Dwarf: every talk opens his menu; the Red Axe row (4637 [1034..1051])
            t.exec("talkToDrunkDwarf.houseDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2913, 10219, 0 }, near = { 2913, 10219 }, far = { 2913, 10220 } })
            t.exec("talkToDrunkDwarf", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            play(t, "talkToDrunkDwarf-dialog", {
                "choose:I need to know about the Red Axe...",
                "player:I need to know about the Red Axe...",
                "npc:THE RED AXE WILL COME FOR YOU!",
                "player:What, what do you mean?",
                "npc:I c-c-c-can't think shtraight",
                "player:You don't look very sober to me!",
                "npc:DON'T contradict me!",
                "player:Have you ever considered to stop drinking?",
                "npc:I'll shtop, right after you get me shome of the good shtuff!",
            })
            t.expect("quest.stage.bring_beer", t.quest.expect_stage("bring_beer"))

            -- goTalkDrunkDwarfAgain: the beer from the pack and the first seed (4637 [1094..1129])
            local beers = n(t, "beer")
            t.exec("goTalkDrunkDwarfAgain", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            play(t, "goTalkDrunkDwarfAgain-dialog", {
                "choose:I need to know about the Red Axe...",
                "player:I need to know about the Red Axe...",
                "npc:Did you get me shome of the good stuff yet?",
                "player:Yes, here you go.",
                "npc:What wash that?",
                "player:What's the REALLY good stuff then?",
                "npc:Some of them original Kelda Stout!",
                "player:Hey, er, I don't think that's good for you.",
                "npc:I know, but get me shome of it anyway.",
                "player:What am I supposed to do with a seed?",
                "npc:You silly human!",
                "npc:Hop hop hop!",
                "player:Where do I get the other seeds then?",
                "npc:Some of me drinking buddies might have 'em.",
                "npc:Drinking buddies. Yes, find me some drinking buddies.",
                "player:Right, I think what you're trying to say is...",
                "npc:You're awfully shmart for a human!",
                "player:Maybe that's just because I don't drink as much as you.",
            })
            t.expect("quest.stage.collect_seeds", t.quest.expect_stage("collect_seeds"))
            t.check("goTalkDrunkDwarfAgain-seed", n(t, "kelda_hop_seed") == 1 and n(t, "beer") == beers - 1,
                "kelda seeds " .. n(t, "kelda_hop_seed") .. " (want 1), beers " .. beers .. " -> " .. n(t, "beer"))

            -- talkToRowdyDwarf: the ask names the item (forced to the recorded fake beard in
            -- setup), the second talk takes it and gives the seed (4637 [1150..1390])
            t.exec("talkToRowdyDwarf.houseDoorOut", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2913, 10219, 0 }, near = { 2913, 10220 }, far = { 2913, 10218 } })
            t.exec("goto-talkToRowdyDwarf", t.player.goto_tile, 2914, 10198, 0)
            t.exec("talkToRowdyDwarf", t.player.talk_to, "dwarf_city_rowdy_dwarf", 1)
            play(t, "talkToRowdyDwarf-dialog", {
                "player:Hold on a second there, I need your help!",
                "npc:Help, from me?",
                "player:No, I try not to get drunk",
                "npc:Mmmm, beer.",
                "player:I need to brew Kelda stout",
                "npc:Mmmm, Kelda stout.",
                "player:Do you know where I can find any seeds?",
                "npc:I might... oh yesh",
                "player:But you're not going to just give it to me, are you?",
                "npc:No.",
                "player:Alright then, what do you want for it?",
                "npc:Get me... a fake beard.",
                "player:What?? Where am I going to get that!",
                "npc:You can at least make the effort.",
            })
            t.check("talkToRowdyDwarf-request", sv(t, "varp7869_forget_rowdy_item") == 12
                and sv(t, "varb829_forget_seed2_told") == 1,
                "forget_rowdy_item=" .. tostring(sv(t, "varp7869_forget_rowdy_item")) .. " (want 12, the fake beard), seed2_told="
                .. tostring(sv(t, "varb829_forget_seed2_told")))
            t.exec("talkToRowdyDwarf-trade", t.player.talk_to, "dwarf_city_rowdy_dwarf", 1)
            play(t, "talkToRowdyDwarf-trade-dialog", {
                "player:Do you have any Kelda seeds?",
                "npc:Oh yesh... a fake beard.",
                "player:Look, I've got it here!",
                "npc:Shplendid! Give that to me please!",
                "player:Now how about those seeds?",
                "npc:Well, I've got a Kelda hop seed right here!",
                "player:Wow, really?",
                "npc:Nah, I'm just kidding!",
                "player:I suppose you wouldn't ever have a proper job",
                "npc:What wash that?",
                "player:Nothing...",
            })
            t.check("talkToRowdyDwarf-seed", n(t, "kelda_hop_seed") == 2 and n(t, "feud_karidian_fakebeard") == 0,
                "kelda seeds " .. n(t, "kelda_hop_seed") .. " (want 2), fake beards left " .. n(t, "feud_karidian_fakebeard"))

            -- getWWMTicket: track 4's Travel op, White Wolf Mountain with Fishing Contest done
            -- (keldagrim_travel.rs2 [opnpc3,dwarf_city_train_conductor4], 4637 [1470..1480])
            t.exec("goto-getWWMTicket", t.player.goto_tile, 2906, 10171, 0)
            t.exec("getWWMTicket", t.player.talk_to, "dwarf_city_train_conductor4", 3)
            play(t, "getWWMTicket-dialog", { "choose:White Wolf Mountain (50 coins)" })
            t.exec("getWWMTicket-ticket", t.inv.await, "dwarf_minecart_ticket_kelda_whitewolf", 1, 5)
            -- travelToWWM: the track-4 cart (forget_tunnels.rs2 [oploc1,keldagrim_train_cart])
            t.exec("goto-travelToWWM", t.player.goto_tile, 2919, 10170, 0)
            t.exec("travelToWWM", t.player.click_loc, "keldagrim_train_cart", 1, { at = { 2919, 10169, 0 } })
            -- The ride, RSProx 4637 [1485..1530]: the mountain's station at 2876,9869.
            await_at(t, "the cart to White Wolf Mountain", 2876, 9869, 0, 40)
            local _, wwm = here(t)
            t.check("travelToWWM-arrived", at(t, 2876, 9869, 0) and n(t, "dwarf_minecart_ticket_kelda_whitewolf") == 0,
                "at " .. wwm .. " (want 2876,9869,0), tickets left " .. n(t, "dwarf_minecart_ticket_kelda_whitewolf"))

            -- talkToKhorvak: the borrow row, the seed with the last page (4637 [1684..1776])
            t.exec("talkToKhorvak", t.player.talk_to, "dwarfrock_engineer2", 1)
            play(t, "talkToKhorvak-dialog", {
                "player:Hello there. I'm looking for some Kelda hop seeds.",
                "npc:Oh, we've got an expert here!",
                "player:It sounds ghastly to me.",
                "npc:Oh, I've got a sheed you might like to borrow!",
                "player:Borrow??",
                "npc:Then we've got no deal!",
                "choose:No wait, I want to borrow it after all.",
                "player:No wait, I want to borrow it after all.",
                "npc:You are going to give it back",
                "player:Of course, of course!",
                "npc:That'sh very deshent of you, human.",
                "player:Only too happy to help!",
                "npc:Hey, err... wait...",
                "player:Err, yes?",
                "npc:How are you goin' to give me back my sheed",
                "npc:You'll be too drunk to find your way back here!",
                "player:Oh, don't worry...",
                "npc:That'sh alright then, here you go.",
            })
            t.check("goGiveKhorvakBeer", n(t, "kelda_hop_seed") == 3,
                "kelda seeds " .. n(t, "kelda_hop_seed") .. " (want 3: Khorvak lends his, no stout needed)")

            -- getKeldaTicket and takeCartFromWWMToKelda (4637 [1975..1977])
            t.exec("goto-getKeldaTicket", t.player.goto_tile, 2874, 9870, 0)
            t.exec("getKeldaTicket", t.player.talk_to, "dwarf_city_train_conductor6", 3)
            play(t, "getKeldaTicket-dialog", { "choose:Keldagrim (50 coins)" })
            t.exec("getKeldaTicket-ticket", t.inv.await, "dwarf_minecart_ticket_whitewolf_kelda", 1, 5)
            t.exec("takeCartFromWWMToKelda", t.player.click_loc, "whitewolfmountain_train_cart", 1)
            -- Back, 4637 [1979..2004]: off the cart at track 5, 2909,10174.
            await_at(t, "the cart back to Keldagrim", 2909, 10174, 0, 40)
            local _, back = here(t)
            t.check("takeCartFromWWMToKelda-arrived", at(t, 2909, 10174, 0) and n(t, "dwarf_minecart_ticket_whitewolf_kelda") == 0,
                "at " .. back .. " (want 2909,10174,0), tickets left " .. n(t, "dwarf_minecart_ticket_whitewolf_kelda"))

            -- talkToGauss: the toast (a beer or a glass in the pack, neither spent), the seed three
            -- ticks after the chat closes (4637 [2141..2171])
            t.exec("goto-talkToGauss", t.player.goto_tile, 2838, 10195, 0)
            t.exec("talkToGauss", t.player.talk_to, "dwarf_city_dwarf_man6", 1)
            play(t, "talkToGauss-dialog", {
                "npc:A good day to you, sir!",
                "player:And to you!",
                "player:Say, you know a lot about beer, right?",
                "npc:A fair bit if I say so myself",
                "player:Do you know anything about Kelda stout?",
                "npc:Ah, very rare that is!",
                "player:Yes, that's what I heard",
                "npc:Then you'll probably need some seeds!",
                "player:Have you got any?",
                "npc:Have a toast to our good health with me",
                "player:Drinking is bad for you, but I'll make an exception this time.",
                "npc:Hurrah for the human!",
                "player:Cheers!",
                "npc:Cheers!",
            })
            play(t, "talkToGauss-seed-dialog", { "npc:That was fun! Here, I've got one Kelda seed left." }, 10)
            t.check("talkToGauss-seed", n(t, "kelda_hop_seed") == 4 and n(t, "beer_glass") == 1,
                "kelda seeds " .. n(t, "kelda_hop_seed") .. " (want 4), beer glass " .. n(t, "beer_glass") .. " (want 1: kept)")
            local _, txt = here(t)
            t.check("leg.1.state", sv(t, "varb822_forget_quest") == 30 and n(t, "kelda_hop_seed") == 4,
                "at " .. txt .. ", forget_quest=" .. tostring(sv(t, "varb822_forget_quest")) .. ", seeds " .. n(t, "kelda_hop_seed"))
        end },
        { name = "kelda_patch", run = function(t)
            -- LEG 2: Rind's leave, the patch, the wait, the harvest, up to the brewery
            t.exec("goto-talkToRind", t.player.goto_tile, 2854, 10197, 0)
            t.exec("talkToRind", t.player.talk_to, "dwarf_city_gardener_dwarf", 1)
            play(t, "talkToRind-dialog", {
                "player:I need some help.",
                "npc:What can a humble gardener like myself help you with?",
                "player:I have four Kelda seeds here, but need some advice on growing them.",
                "npc:Marvelous, those seeds are quite rare",
                "npc:First of all, you must remember that these seeds only grow underground.",
                "npc:There is a patch in the palace garden",
                "player:Thanks! Is there anything else I need to know?",
                "npc:Just take care of the weed in the patch",
                "player:Thank you! Your advice is much appreciated!",
            })
            t.expect("talkToRind-stage", t.quest.expect_stage("grow_kelda"))

            -- plantKelda: rake the weeds, then use the seeds on the weeded patch
            t.exec("goto-plantKelda", t.player.goto_tile, 2854, 10201, 0)
            t.exec("plantKelda-rake", t.player.click_loc, "farming_hops_patch_keldagrim", 1)
            t.exec("plantKelda-weeded", t.var.await, "varb823_forget_farming", 3, 200)
            local patch = t.player.by_symbol("loc", "farming_hops_patch_keldagrim")
            t.exec("plantKelda", t.player.use_on, "kelda_hop_seed", patch)
            t.exec("plantKelda-growing", t.var.await, "varb823_forget_farming", 4, 12)
            t.check("plantKelda-planted", select(2, t.inv.count("kelda_hop_seed")) == 0,
                "kelda seeds left: " .. tostring(select(2, t.inv.count("kelda_hop_seed"))))

            -- waitForKelda: the farming clock (RSProx index 4637 [2217..3884]): a stage on each
            -- five-minute mark after the planting, four of them, so at most 20 minutes; the
            -- last one says "Perhaps I should take a look and see if my Kelda hops have grown..."
            -- The documented fast-forward for a real-time wait is t.clock.skip; the quest's own
            -- forget_tick softtimer then runs the catch-up.
            local _, before = t.chat.lines(0)
            local kelda_since = 0
            for _, row in ipairs(before or {}) do
                if row.serial > kelda_since then kelda_since = row.serial end
            end
            t.exec("waitForKelda-skip", t.clock.skip, 20)
            t.exec("waitForKelda", t.var.await, "varb823_forget_farming", 8, 110)
            local kelda_seen = false
            local _, after = t.chat.lines(kelda_since)
            for _, row in ipairs(after or {}) do
                if tostring(row.text):find("Perhaps I should take a look and see if my Kelda hops have grown...", 1, true) then
                    kelda_seen = true
                end
            end
            t.check("waitForKelda-line", kelda_seen, "the grown line (4637 [3884])")

            -- harvestHops: a hand pick, no spade, no line (forget_farming.rs2 [oploc1,kelda_hops_fullygrown], 4646 [287..293])
            t.exec("harvestHops", t.player.click_loc, "farming_hops_patch_keldagrim", 1)
            t.exec("harvestHops-hops", t.inv.await, "kelda_hops", 1, 20)
            t.expect("quest.stage.brew_stout", t.quest.expect_stage("brew_stout"))

            -- goUpstairsPub: the Laughing Miner's doorway (2914,10196), then its staircase
            -- (forget_brewing.rs2 [oploc1,dwarf_keldagrim_stairs_lower] -> ^forget_pub_upstairs_coord 2914,10196,1)
            t.exec("goto-goUpstairsPub", t.player.goto_tile, 2914, 10198, 0)
            t.exec("goUpstairsPub.pubDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2914, 10196, 0 }, near = { 2914, 10198 }, far = { 2914, 10195 } })
            t.exec("goUpstairsPub", t.player.climb, { loc = "dwarf_keldagrim_stairs_lower", op_name = "Climb-up",
                at = { 2915, 10196, 0 }, dest = { 2914, 10196, 1 } })
            local _, txt = here(t)
            t.check("leg.2.state", select(2, t.world.level()) == 1 and n(t, "kelda_hops") == 1,
                "at " .. txt .. ", forget_quest=" .. tostring(sv(t, "varb822_forget_quest")) .. ", kelda hops " .. n(t, "kelda_hops"))
        end },
        { name = "brewing", run = function(t)
            -- LEG 3: the pot, the yeast, the vat, the wait, the valve, the barrel (4646 [684..2217])
            t.exec("goto-pickupPot", t.player.goto_tile, 2916, 10192, 1)
            t.exec("pickupPot", t.player.click_obj, "pot_empty", 3)
            t.exec("pickupPot-held", t.inv.await, "pot_empty", 1, 5)
            -- buyYeast: Blandebir's menu loops until "That's all I need to know, thanks."
            local coins0 = n(t, "coins")
            t.exec("buyYeast", t.player.talk_to, "blandebir", 1)
            play(t, "buyYeast-dialog", {
                "player:Hello, I wonder if you could help me on this whole brewing thing...",
                "npc:I might be able to - what do you need to know?",
                "choose:Do you have any spare ale yeast?",
                "player:Do you have any spare ale yeast?",
                "npc:Well, as a matter of fact I do",
                "choose:That's a good deal - please fill my pot with ale yeast for 25GP.",
                "player:That's a good deal - please fill my pot with ale yeast for 25GP.",
                "choose:That's all I need to know, thanks.",
                "player:That's all I need to know, thanks.",
            })
            t.check("buyYeast-held", n(t, "ale_yeast") == 1 and n(t, "pot_empty") == 0 and n(t, "coins") == coins0 - 25,
                "ale yeast " .. n(t, "ale_yeast") .. ", pots " .. n(t, "pot_empty") .. ", coins " .. coins0 .. " -> " .. n(t, "coins"))

            -- the vat (forget_brewing.rs2 [oplocu,brewing_vat_1]): each step an objectbox
            local vat = t.player.by_symbol("loc", "brewing_vat_1")
            t.exec("addWater", t.player.use_on, "bucket_water", vat)
            play(t, "addWater-dialog", { "objbox:You add some water to the vat." })
            t.check("addWater-v", sv(t, "varb736_brewing_vat_varbit_1") == 1 and n(t, "bucket_water") == 0 and n(t, "bucket_empty") == 2,
                "vat " .. tostring(sv(t, "varb736_brewing_vat_varbit_1")) .. " (want 1), buckets of water " .. n(t, "bucket_water")
                .. ", empty buckets " .. n(t, "bucket_empty"))
            t.exec("addMalts", t.player.use_on, "barley_malt", vat)
            play(t, "addMalts-dialog", { "objbox:You add some barley malt to the vat." })
            t.check("addMalts-v", sv(t, "varb736_brewing_vat_varbit_1") == 2 and n(t, "barley_malt") == 0,
                "vat " .. tostring(sv(t, "varb736_brewing_vat_varbit_1")) .. " (want 2), barley malt " .. n(t, "barley_malt"))
            t.exec("addKelda", t.player.use_on, "kelda_hops", vat)
            play(t, "addKelda-dialog", { "objbox:You add some kelda hops to the vat." })
            t.check("addKelda-v", sv(t, "varb736_brewing_vat_varbit_1") == 68 and n(t, "kelda_hops") == 0,
                "vat " .. tostring(sv(t, "varb736_brewing_vat_varbit_1")) .. " (want 68), kelda hops " .. n(t, "kelda_hops"))
            t.exec("addYeast", t.player.use_on, "ale_yeast", vat)
            play(t, "addYeast-dialog", { "objbox:You add a pot of ale yeast into the vat and the contents begin to ferment." })
            t.check("addYeast-v", sv(t, "varb736_brewing_vat_varbit_1") == 69 and n(t, "ale_yeast") == 0 and n(t, "pot_empty") == 1,
                "vat " .. tostring(sv(t, "varb736_brewing_vat_varbit_1")) .. " (want 69), ale yeast " .. n(t, "ale_yeast")
                .. ", empty pots " .. n(t, "pot_empty") .. " (want 1: the pot comes back)")

            -- waitBrewing: two stages on the farming clock's five-minute marks (4646 [1367], [1864])
            t.exec("waitBrewing-skip", t.clock.skip, 10)
            t.exec("waitBrewing", t.var.await_server, "varb736_brewing_vat_varbit_1", 71, 110)

            t.exec("turnValve", t.player.click_loc, "vat_valve_1", 1)
            play(t, "turnValve-dialog", { "mesbox:You turn the valve, releasing one pint of Kelda Stout into the barrel." })
            t.check("turnValve-v", sv(t, "varb738_brewing_barrel_varbit_1") == 3 and sv(t, "varb736_brewing_vat_varbit_1") == 0,
                "barrel " .. tostring(sv(t, "varb738_brewing_barrel_varbit_1")) .. " (want 3), vat "
                .. tostring(sv(t, "varb736_brewing_vat_varbit_1")) .. " (want 0)")
            local barrel = t.player.by_symbol("loc", "brewing_barrel_1")
            t.exec("useGlassOnBarrel", t.player.use_on, "beer_glass", barrel)
            play(t, "useGlassOnBarrel-dialog", { "objbox:You fill the glass with Kelda Stout. The barrel is now empty." })
            t.check("useGlassOnBarrel-stout", n(t, "kelda_stout") == 1 and n(t, "beer_glass") == 0,
                "kelda stout " .. n(t, "kelda_stout") .. ", beer glasses " .. n(t, "beer_glass"))
            t.expect("quest.stage.give_stout", t.quest.expect_stage("give_stout"))

            -- goDownFromPub: forget_brewing.rs2 [oploc1,dwarf_keldagrim_stairs_upper] -> 2917,10196,0
            t.exec("goDownFromPub", t.player.climb, { loc = "dwarf_keldagrim_stairs_upper", op_name = "Climb-down",
                at = { 2915, 10196, 1 }, dest = { 2917, 10196, 0 } })
            local _, txt = here(t)
            t.check("leg.3.state", at(t, 2917, 10196, 0) and n(t, "kelda_stout") == 1,
                "at " .. txt .. ", forget_quest=" .. tostring(sv(t, "varb822_forget_quest")))
        end },
        { name = "tale_and_tunnel", run = function(t)
            -- LEG 4: the stout and the tale, the conductor, the director, the secret cart
            t.exec("giveStout.pubDoorOut", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2914, 10196, 0 }, near = { 2914, 10195 }, far = { 2914, 10198 } })
            t.exec("goto-giveStout", t.player.goto_tile, 2913, 10218, 0)
            t.exec("giveStout.houseDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2913, 10219, 0 }, near = { 2913, 10219 }, far = { 2913, 10220 } })
            -- 4646 [2327..2356]: the stout and 65 go with "Here you go then."; "First of all..." starts the tale
            t.exec("giveStout", t.player.talk_to, "dwarf_city_drunken_dwarf", 1)
            play(t, "giveStout-dialog", {
                "choose:I need to know about the Red Axe...",
                "player:I need to know about the Red Axe...",
                "npc:I want my Kelda shtout!",
                "player:I've got some Kelda stout right here.",
                "npc:Now there'sh a good human, gimme that!",
                "player:Are you sure? This is pretty bad stuff.",
                "npc:You want to hear about the Red Kebabsh or what?",
                "player:Here you go then.",
                "npc:Ooooh, THAT'sh the good shtuff!",
                "npc:The Red Axe? You want to know about them, do you?",
                "npc:A long time ago, quite a while ago, yes...",
                "player:The what? The Magenta Accordion?",
                "npc:It is, it is!",
                "player:Alright, if you say so...",
                "npc:Right, so I had just left my company.",
                "npc:I explored this old mine",
                "npc:First of all...",
            })
            t.check("giveStout-stout", n(t, "kelda_stout") == 0, "kelda stout held " .. n(t, "kelda_stout") .. " (want 0: he drank it)")
            t.expect("quest.stage.hear_more", t.quest.expect_stage("hear_more"))
            -- The tale (4646 [2356..2405]): four fades, three timed pages with no continue, then
            -- the last page with one; 70 goes out with the Drunken Dwarf's last page.
            play(t, "hearTale-dialog", {
                "npc:...and I don't really remember anything after that.",
                "player:That was very interesting!",
                "player:But I shall investigate that tunnel immediately!",
                "npc:If you shay sho...",
            }, 80)
            t.expect("quest.stage.ask_conductor", t.quest.expect_stage("ask_conductor"))

            -- talkToCartConductor: the southern conductor's tunnel row (4646 [2559..2586])
            t.exec("talkToCartConductor.houseDoorOut", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2913, 10219, 0 }, near = { 2913, 10220 }, far = { 2913, 10218 } })
            t.exec("goto-talkToCartConductor", t.player.goto_tile, 2922, 10168, 0)
            t.exec("talkToCartConductor", t.player.talk_to, "dwarf_city_train_conductor8", 1)
            play(t, "talkToCartConductor-dialog", {
                "choose:Ask about closed off tunnel.",
                "player:Excuse me, can you tell me what's behind that boarded up tunnel to the south?",
                "npc:Why, that part of the mines hasn't been used in a long time.",
                "player:I need to go through there.",
                "npc:I don't have the authority to do that kind of thing.",
                "player:Then who do I talk to?",
                "npc:I don't know. Perhaps you have an influential friend?",
            })
            t.expect("quest.stage.ask_director", t.quest.expect_stage("ask_director"))

            -- goUpToDirector / talkToDirector: the player's own company's director, the Green
            -- Gemstone's (forget_tunnels.rs2 forget_director_talk, 4646 [2629..2656])
            t.exec("goto-goUpToDirector", t.player.goto_tile, 2893, 10209, 0)
            t.exec("goUpToDirector", t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_lower", op = 1, op_name = "Climb-up",
                at = { 2894, 10209, 0 }, src = { 2893, 10209 }, dest = { 2896, 10209, 1 } })
            t.exec("talkToDirector.present", t.npc.await_present, "dwarf_city_director_green_gemstone", 30, 15)
            t.exec("talkToDirector", t.player.talk_to, "dwarf_city_director_green_gemstone", 1)
            play(t, "talkToDirector-dialog", {
                "npc:Have you seen the statue?",
                "choose:Can you help me with a boarded up tunnel?",
                "player:Can you help me with a boarded up tunnel?",
                "npc:Ah, and what would be lurking behind this particular tunnel?",
                "player:I think it leads to something that might be related to the Red Axe.",
                "npc:The Red Axe!",
                "npc:This is indeed an important issue.",
                "player:Can you help me get the boarding removed then?",
                "npc:I'll order the removal at once!",
                "player:Thanks! I'll investigate the tunnel immediately!",
            })
            t.expect("quest.stage.tunnels_first", t.quest.expect_stage("tunnels_first"))
            t.check("talkToDirector-boarding", sv(t, "varb825_forget_boarding_removed") == 1,
                "forget_boarding_removed=" .. tostring(sv(t, "varb825_forget_boarding_removed")))
            -- goDownFromDirector: the same stairs' upper copy (maplink to 2893,10209,0)
            t.exec("goto-goDownFromDirector", t.player.goto_tile, 2896, 10209, 1)
            t.exec("goDownFromDirector", t.player.climb, { loc = "dwarf_keldagrim_wide_stairs_upper", op = 1, op_name = "Climb-down",
                at = { 2895, 10209, 1 }, src = { 2896, 10209 }, dest = { 2893, 10209, 0 } })

            -- takeSecretCart: the ride, the fade, the hub, the first-entry pan and the player's
            -- line (forget_tunnels.rs2 forget_secret_cart / forget_enter_tunnels, 4646 [2950..2980], [2807..2826])
            t.exec("goto-takeSecretCart", t.player.goto_tile, 2919, 10166, 0)
            local pan = t.cutscene.mark()
            t.exec("takeSecretCart", t.player.click_loc, "keldagrim_train_cart", 1, { at = { 2919, 10164, 0 } })
            play(t, "tunnelEntry-dialog", { "player:I'm deep underground now, but how am I supposed to get across here?" }, 40)
            -- the first pan drifts its eye only: no second look (4646 [2808])
            scene(t, "tunnelEntry.cutscene", pan, { { op = "moveto", height = 1000 }, { op = "lookat", height = 500 },
                { op = "moveto", height = 1000 }, { op = "reset" } })
            local _, txt = here(t)
            t.check("takeSecretCart-hub", at(t, 1863, 4956, 1) and sv(t, "varb837_forget_region_status") == 1,
                "at " .. txt .. " (want the hub 1863,4956,1), region_status " .. tostring(sv(t, "varb837_forget_region_status")))
        end },
        { name = "puzzle_group_one", run = function(t)
            -- LEG 5: the first group's three routes (forget_route_expected 1..3), the listening room
            search_box(t, "searchBox1", "You find a green stone and a yellow stone in the storage box.", 1, 1)
            set_route(t, "startPuzzle1", "forget_puzzle1", { { 0, 2, "puzzle1P1" }, { 2, 1, "puzzle1P2" } }, "puzzle1Ok", 1)
            ride_to_platform(t, "takePuzzle1Cart", true)
            search_box(t, "searchPuzzle1Box", "You search the box and find another stone with a green marking on it.", 1, 0, { 1890, 4953 })
            ride_back(t, "returnFromPuzzle1", { 1889, 4955 })
            set_route(t, "startPuzzle2", "forget_puzzle1",
                { { 0, 1, "puzzle2P1" }, { 1, 2, "puzzle2P2" }, { 4, 1, "puzzle2P3" } }, "puzzle2Ok", 2)
            ride_to_platform(t, "takePuzzle2Cart", false)
            search_box(t, "searchPuzzle2Box", "You search the box and find another stone with a yellow marking on it.", 0, 1, { 1905, 4970 })
            ride_back(t, "returnFromPuzzle2", { 1905, 4968 })
            set_route(t, "startPuzzle3", "forget_puzzle1",
                { { 0, 2, "puzzle3P1" }, { 2, 2, "puzzle3P2" }, { 5, 1, "puzzle3P3" }, { 6, 1, "puzzle3P4" } }, "puzzle3Ok", 3)
            -- route 3: the listening room (forget_story.rs2 forget_enter_listening_room / forget_listen,
            -- 4646 [3362..3464]): thirteen timed pages, then the player's own
            t.exec("takePuzzle3Cart", t.player.click_loc, "forget_train_cart", 1)
            play(t, "listenToConversation-dialog", { "player:Pesky human huh? Well, I think I have enough info now." }, 160)
            t.expect("quest.stage.listening_room", t.quest.expect_stage("listening_room"))
            -- The room is a copy of its own, the template's plane 2 on its plane 0 (4646 [3365]).
            t.check("listenToConversation-room", select(2, t.world.level()) == 0 and n(t, GREEN) == 0 and n(t, YELLOW) == 0
                and sv(t, "varb869_forget_room1_once") == 1,
                "level " .. tostring(select(2, t.world.level())) .. ", " .. stones(t) .. ", room1_once="
                .. tostring(sv(t, "varb869_forget_room1_once")))
            -- No running in the listening room (4646 [3412..3422]: the recorder's clicks walked a
            -- tile a tick with run on and full energy, no message): every step of a six-tile walk
            -- east is one tile (a run steps two).
            do
                t.ticklog.start()
                t.ticklog.mark("listen.norun")
                local _, marks = t.ticklog.rows({ kind = "mark" })
                local since = marks[#marks].serial
                local _, a = t.world.tile()
                t.player.walk_to(a.x + 6, a.z)
                local _, rows = t.ticklog.rows({ kind = "player_tile", since = since })
                local px, pz, widest, moved = a.x, a.z, 0, 0
                for _, r in ipairs(rows) do
                    local d = math.max(math.abs(r.x - px), math.abs(r.z - pz))
                    if d > widest then widest = d end
                    if d > 0 then moved = moved + 1 end
                    px, pz = r.x, r.z
                end
                t.check("listenToConversation-norun", widest == 1 and moved >= 6,
                    "widest step " .. widest .. " tile(s) over " .. moved .. " moving ticks (want 1, a walk)")
            end
            -- leaveListeningRoom1: 110, the fade, the hub, the second group's pan (4646 [3571..3593])
            local pan = t.cutscene.mark()
            t.exec("leaveListeningRoom1", t.player.click_loc, "forget_story_exit_next", 1)
            play(t, "tunnelEntry2-dialog", { "player:Looks like another chasm I need to cross." }, 40)
            scene(t, "tunnelEntry2.cutscene", pan, { { op = "moveto", height = 650 }, { op = "lookat", height = 400 },
                { op = "moveto", height = 650 }, { op = "lookat", height = 400 }, { op = "reset" } })
            t.expect("quest.stage.tunnels_second", t.quest.expect_stage("tunnels_second"))
            local _, txt = here(t)
            t.check("leg.5.state", at(t, 1863, 4956, 1), "at " .. txt .. ", " .. stones(t))
        end },
        { name = "puzzle_group_two", run = function(t)
            -- LEG 6: the second group's routes 4..6 and the library
            search_box(t, "searchBox2", "You find a green stone and two yellow stones in the storage box.", 1, 2)
            set_route(t, "startPuzzle4", "forget_puzzle2",
                { { 0, 1, "puzzle4P1" }, { 1, 2, "puzzle4P2" }, { 4, 2, "puzzle4P3" } }, "puzzle4Ok", 4)
            ride_to_platform(t, "takePuzzle4Cart", true)
            search_box(t, "searchPuzzle4Box", "You search the box and find another stone with a green marking on it.", 1, 0, { 1898, 4953 })
            ride_back(t, "returnFromPuzzle4", { 1898, 4955 })
            set_route(t, "startPuzzle5", "forget_puzzle2",
                { { 0, 2, "puzzle5P1" }, { 2, 2, "puzzle5P2" }, { 5, 1, "puzzle5P3" }, { 10, 1, "puzzle5P4" } }, "puzzle5Ok", 5)
            ride_to_platform(t, "takePuzzle5Cart", false)
            search_box(t, "searchPuzzle5Box", "You search the box and find yet another stone with a green marking on it.", 1, 0, { 1906, 4945 })
            ride_back(t, "returnFromPuzzle5", { 1906, 4947 })
            set_route(t, "startPuzzle6", "forget_puzzle2",
                { { 0, 1, "puzzle6P1" }, { 1, 1, "puzzle6P2" }, { 3, 2, "puzzle6P3" }, { 9, 1, "puzzle6P4" }, { 11, 2, "puzzle6P5" } },
                "puzzle6Ok", 6)
            -- route 6: the library (forget_story.rs2 forget_enter_library, 4646 [4085..4266])
            t.exec("takePuzzle6Cart", t.player.click_loc, "forget_train_cart", 1)
            -- The route 6 run is ~40 ticks with the bridge (4646 [4038..4077]).
            -- Its own copy, plane 0 (4646 [4077] rebuild_region: template planes 2/3 onto 0/1).
            await_at(t, "the library", 1899, 4965, 0, 90)
            t.expect("quest.stage.library", t.quest.expect_stage("library"))
            local _, lib = here(t)
            t.check("takePuzzle6Cart-library", at(t, 1899, 4965, 0), "at " .. lib .. " (want 1899,4965,0 of the library's copy, 4646 [4077])")
            -- The records' bookcase (4646 [4090]), in the copy.
            local bx, bz = tc(t, 1906, 4970)
            t.exec("searchBookcase", t.player.click_loc, "dwarf_keldagrim_metal_bookcase", 1, { at = { bx, bz, 0 } })
            play(t, "searchBookcase-dialog", { "mesbox:You find an interesting book that seems to contain a lot of data about Red Axe employees." })
            t.exec("searchCrate2", t.player.click_loc, "forget_metal_crate_withpapers2", 1)
            play(t, "searchCrate2-dialog", { "mesbox:You find something that seems to shed some light on what happened to the drunken dwarf" })
            t.exec("searchCrate1", t.player.click_loc, "forget_metal_crate_withpapers1", 1)
            play(t, "searchCrate1-dialog", { "mesbox:You find a copy of a letter, addressed to the boatman" })
            t.check("library-read", sv(t, "varb833_forget_room2_bookcase") == 1 and sv(t, "varb834_forget_room2_paper1") == 1
                and sv(t, "varb835_forget_room2_paper2") == 1,
                "bookcase=" .. tostring(sv(t, "varb833_forget_room2_bookcase")) .. " paper1=" .. tostring(sv(t, "varb834_forget_room2_paper1"))
                .. " paper2=" .. tostring(sv(t, "varb835_forget_room2_paper2")))
            -- leaveLibrary: "Move deeper into the mines?" Yes; 118, the fade, the third group's pan
            local pan = t.cutscene.mark()
            t.exec("leaveLibrary", t.player.click_loc, "forget_story_exit_next", 1)
            play(t, "leaveLibrary-dialog", { "choose:Yes." })
            play(t, "tunnelEntry3-dialog", { "player:What the Guthix is this? Another chasm? And even more tracks!" }, 40)
            scene(t, "tunnelEntry3.cutscene", pan, { { op = "moveto", height = 700 }, { op = "lookat", height = 500 },
                { op = "moveto", height = 700 }, { op = "lookat", height = 500 }, { op = "reset" } })
            t.expect("quest.stage.tunnels_third", t.quest.expect_stage("tunnels_third"))
            local _, txt = here(t)
            t.check("leg.6.state", at(t, 1863, 4956, 1) and n(t, GREEN) == 0 and n(t, YELLOW) == 0, "at " .. txt .. ", " .. stones(t))
        end },
        { name = "puzzle_group_three", run = function(t)
            -- LEG 7: the third group's routes 7..9 and the last scene
            search_box(t, "searchBox3", "You find two green stones and two yellow stones in the storage box.", 2, 2)
            set_route(t, "startPuzzle7", "forget_puzzle3",
                { { 0, 1, "puzzle7P1" }, { 1, 1, "puzzle7P2" }, { 3, 2, "puzzle7P3" }, { 5, 2, "puzzle7P4" } }, "puzzle7Ok", 7)
            ride_to_platform(t, "takePuzzle7Cart", true)
            search_box(t, "searchPuzzle7Box", "You search the box and find another two stones with markings on them: one green, one yellow.", 1, 1, { 1890, 4963 })
            ride_back(t, "returnFromPuzzle7", { 1890, 4960 })
            set_route(t, "startPuzzle8", "forget_puzzle3",
                { { 0, 2, "puzzle8P1" }, { 2, 2, "puzzle8P2" }, { 4, 2, "puzzle8P3" }, { 6, 1, "puzzle8P4" },
                  { 8, 1, "puzzle8P5" }, { 11, 1, "puzzle8P6" } }, "puzzle8Ok", 8)
            ride_to_platform(t, "takePuzzle8Cart", false)
            search_box(t, "searchPuzzle8Box", "You search the box and find yet another two stones with markings on them.", 1, 1, { 1906, 4962 })
            ride_back(t, "returnFromPuzzle8", { 1905, 4960 })
            set_route(t, "startPuzzle9", "forget_puzzle3",
                { { 0, 1, "puzzle9P1" }, { 1, 1, "puzzle9P2" }, { 3, 2, "puzzle9P3" }, { 5, 1, "puzzle9P4" },
                  { 7, 2, "puzzle9P5" }, { 10, 1, "puzzle9P6" }, { 13, 2, "puzzle9P7" }, { 16, 2, "puzzle9P8" } }, "puzzle9Ok", 9)
            -- route 9: the last scene (forget_story.rs2 forget_room_six_scene, 4646 [4696..4875]): 120
            -- three ticks in, ~180 ticks of camera and timed pages, then the player awake by the
            -- southern cart station
            local last = t.cutscene.mark()
            t.exec("takePuzzle9Cart", t.player.click_loc, "forget_train_cart", 1)
            play(t, "watchCutscene-dialog", { "player:Er, what? Where am I? Errrrr... I must tell Veldaban... something." }, 260)
            -- In the copy the camera tiles are the copy's: op and height only.
            scene(t, "watchCutscene.cutscene", last, {
                { op = "moveto", height = 1000 },
                { op = "lookat", height = 500 },
                { op = "moveto", height = 450 },
                { op = "lookat", height = 300 },
                { op = "moveto", height = 800 },
                { op = "lookat", height = 675 },
                { op = "lookat", height = 675 },
                { op = "moveto", height = 1000 },
                { op = "lookat", height = 500 },
                { op = "reset" },
            })
            t.expect("quest.stage.report_veldaban", t.quest.expect_stage("report_veldaban"))
            -- the region mark goes with the last line's close, twelve ticks after it (4646 [4887])
            t.await({ level = function() return sv(t, "varb837_forget_region_status") == 0 end,
                note = "the last line's close" }, 20)
            local _, txt = here(t)
            t.check("leg.7.state", at(t, 2917, 10163, 0) and n(t, GREEN) == 0 and n(t, YELLOW) == 0
                and sv(t, "varb837_forget_region_status") == 0,
                "at " .. txt .. " (want 2917,10163,0), " .. stones(t) .. ", region_status " .. tostring(sv(t, "varb837_forget_region_status")))
        end },
        { name = "finale", run = function(t)
            -- LEG 8: Veldaban at 120 (4646 [5012..5022]), then the beer drunk in the Laughing Miner
            -- with the kebab in the pack (forget_pub.rs2 [opheld1,beer], 4646 [5135..5301])
            t.exec("goto-goReturnToVeldaban", t.player.goto_tile, 2827, 10219, 0)
            t.exec("goReturnToVeldaban.hqDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door",
                open = "dwarf_keldagrim_door_open", at = { 2827, 10218, 0 }, near = { 2827, 10219 }, far = { 2827, 10217 } })
            t.exec("goReturnToVeldaban", t.player.talk_to, "dwarf_city_black_guard_leader", 1)
            play(t, "goReturnToVeldaban-dialog", {
                "player:Veldaban, I must tell you something incredibly important!",
                "npc:What is it,",
                "player:I must tell you about... about...",
                "player:...about my incredible craving for a kebab.",
                "npc:Errr, are you alright?",
                "player:Mmm, yes, I'll be perfectly alright...",
                "player:Watch out kebabs, I come for you!",
                "npc:I think you've been bewitched.",
            })
            t.expect("quest.stage.eat_kebab", t.quest.expect_stage("eat_kebab"))
            -- out of the HQ, the street to the Laughing Miner, in by its doorway, to a tile inside
            -- the pub zone (^forget_pub_zone_south_west..north_east, 2907..2919 x 10187..10197)
            t.exec("eatKebab.hqDoorOut", t.player.pass_door, { closed = "dwarf_keldagrim_door",
                open = "dwarf_keldagrim_door_open", at = { 2827, 10218, 0 }, near = { 2827, 10217 }, far = { 2827, 10219 } })
            t.exec("goto-eatKebab", t.player.goto_tile, 2914, 10198, 0)
            t.exec("eatKebab.pubDoorIn", t.player.pass_door, { closed = "dwarf_keldagrim_door_poor",
                open = "dwarf_keldagrim_door_poor_open", at = { 2914, 10196, 0 }, near = { 2914, 10198 }, far = { 2914, 10195 } })
            t.exec("walk-eatKebab", t.player.walk_route, { { 2912, 10192 } }, { level = 0 })
            t.check("eatKebab-held", n(t, "beer") >= 1 and n(t, "kebab") == 1,
                "beer " .. n(t, "beer") .. ", kebab " .. n(t, "kebab") .. " (forget_pub.rs2: the drink needs both)")
            local _, snap = t.skill.snapshot()
            -- eatKebab (the guide's last step): the recorded ending is the BEER, drunk with the kebab
            -- in the pack; the crowd scene plays in a copy and the kebab goes at D+160
            t.exec("eatKebab", t.player.inv_op, "beer", 1)
            t.check("eatKebab-drunk", n(t, "kebab") == 1 and n(t, "beer_glass") == 1,
                "kebab " .. n(t, "kebab") .. " (want 1: kept until D+160), beer glass " .. n(t, "beer_glass")
                .. " (want 1: the beer's slot)")
            -- The first camera (D+6, 4646 [5141]) lands under the black: its frame is solid black,
            -- which the gate reads as a pre-login shot. The sequence is read from after the fade-in
            -- (D+9), before the next camera at D+11 (the drink row returns about D+3); the D+6 camera
            -- is in the wire audit (docs/rsprox-analysis/quests/forgettable_tale.md).
            t.ticks(6)
            local pub = t.cutscene.mark()
            -- The scene's camera resets at D+160 under the black, so the sequence is read here
            -- (its keyframe shots are taken live) and its row is written after the scroll, when
            -- the frame is the pub again rather than the fade. 140 and the scroll go out five ticks
            -- after the copy is left (D+166).
            local pub_r, pub_d = "unsupported", "no camera reader"
            if pub ~= nil then
                pub_r, pub_d = t.cutscene.await("pubEnding.cutscene", { since = pub, quiet = 200, timeout = 200, expect = {
                    { op = "moveto", height = 330 }, { op = "moveto", height = 300 },
                    { op = "lookat", height = 175 }, { op = "moveto", height = 450 }, { op = "reset" } } })
            end
            t.await({ level = function() return sv(t, "varb822_forget_quest") == 140 end, note = "the scene's end" }, 40)
            -- D+160 [5295]: the glass and the kebab both taken as the player gets up
            t.check("eatKebab-spent", n(t, "kebab") == 0 and n(t, "beer_glass") == 0,
                "kebab " .. n(t, "kebab") .. ", beer glass " .. n(t, "beer_glass") .. " (want 0 and 0)")
            t.quest.expect_complete()
            if pub ~= nil then
                t.check("pubEnding.cutscene", pub_r == "ok", tostring(pub_d))
            end
            t.check("reward.cooking", t.skill.expect_gain("cooking", 5000, snap) == "ok", "5000 Cooking XP (forget_shared.rs2, 50000 in tenths)")
            t.check("reward.farming", t.skill.expect_gain("farming", 5000, snap) == "ok", "5000 Farming XP (forget_shared.rs2, 50000 in tenths)")
            t.check("reward.stout", n(t, "mature_dwarven_stout") == 2, "mature dwarven stout " .. n(t, "mature_dwarven_stout") .. " want 2")
        end },
    },
}
