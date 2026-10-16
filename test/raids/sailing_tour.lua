-- sailing_tour: the Sailing flows a player drives, one after the other, on
-- one account. Content: OSRS-Content/osrs239-content/server/scripts/sailing/
-- (boat_selection, boat_gangplank, boat_customisation, boat_crew,
-- boat_facilities, boat_sidepanel) and quest_pandemonium's Junior Jim.
--
--   1. buy      Junior Jim (the Pandemonium's shipwright) sells a skiff; the
--               boat selector that follows boards it.
--   2. shore    the Pandemonium's gangplank: Disembark, then Board-previous.
--   3. shipyard Junior Jim's "Customise a boat" boards it in the shipyard:
--               the catalogue builds tier-2 keel, hull, sails and helm, then
--               each facility hotspot's marker opens its own catalogue and
--               its first plain-material option is built (materials taken,
--               Construction xp given, the facility recorded on the boat's
--               varbits); the cargo hold is then replaced by an oak one.
--   4. crew     Jobless Jim is recruited and assigned on the Crew Management
--               panel (slot 1, moved to slot 2, back to slot 1), and is on
--               the boat's roster when it is boarded.
--   5. port task the Pandemonium's notice board: a courier task accepted, a
--               second accepted and cancelled; its crate taken at the ledger
--               table and loaded into the boat's hold.
--   6. salvage  out to the Kharidian Sea, whose small-wreck pool is raised on
--               arrival: the nearest wreck is salvaged with the bronze hook
--               until the sea reclaims it (it sinks, another rises), and the
--               salvage is sorted at the Pandemonium's salvaging station.
--   7. voyage   the Pandemonium -> Port Sarim -> Musa Point -> Rimmington ->
--               Port Sarim, each leg boarded by the plank, steered from the
--               helm with the sails set, furled and disembarked; every
--               arrival moves the boat's recorded port to that dock. On the
--               first, a wreck is charted from the deck (Captain's log, once)
--               and at Port Sarim the crate is withdrawn from the hold and
--               delivered at the ledger table for the task's xp. Every gust
--               met under way is trimmed at the mast.
--
-- The part tables are generated from configs/all.dbrow (sailing_boat_skiff's
-- keel/hull/sail/steering options and its seven hotspot rows; schematic-gated
-- parts left out) -- cache data, not hand numbers. `xp` is the whole
-- Construction xp a self-build grants: sailing/scripts/boat_build_xp.rs2's
-- ~sailing_build_xp (tenths) / 10.
--
-- Two bring-alongs inside run, both said in the ledger: materials (::give,
-- as construction_tour gives furniture materials) and Jobless Jim himself
-- (::spawn): no spawn file places him yet, so the recruit has no npc to talk
-- to without it.
--
--   TORIRS_STDERR_UNBUFFERED=1 ./src/build_<you>_opt/torirsserver --scriptrun \
--       test/raids/sailing_tour.lua --bots 1 --name sail --ticks 20000 \
--       --session $S/sail --fixture test/raids/fixtures/fresh_lumbridge.ini

-- Generated (see header): tier 2 of each hull part a new skiff is furnished
-- with at tier 1.
local HULL_PARTS = {
    { part = "sailing_boat_iron_keel_2x5", xp = 200, name = "Iron keel", varbit = "varb19266_sailing_boat_1_keel", value = 1, tab = "Keel",
      mats = { { "sailing_boat_keel_part_iron", 10 } } },
    { part = "sailing_boat_oak_hull_2x5", xp = 238, name = "Oak hull", varbit = "varb19267_sailing_boat_1_hull", value = 1, tab = "Hull",
      mats = { { "sailing_boat_hull_part_oak", 10 }, { "nails_iron", 300 }, { "swamp_tar", 20 } } },
    { part = "sailing_boat_oak_mast_2x5", xp = 36, name = "Oak mast and linen sails", varbit = "varb19268_sailing_boat_1_sail", value = 1, tab = "Sails",
      mats = { { "oak_logs", 10 }, { "nails_iron", 40 }, { "bolt_of_linen", 5 } } },
    { part = "sailing_boat_iron_steering_2x5", xp = 240, name = "Iron helm", varbit = "varb19269_sailing_boat_1_steering", value = 1, tab = "Helm",
      mats = { { "plank_oak", 3 }, { "iron_bar", 6 } } },
}

-- Generated (see header): per skiff hotspot, the first option whose
-- materials are plain items; `option` is its 1-based place in the hotspot's
-- list, which is what the boat's hotspot varbit records. Hotspot 3 offers
-- only chum stations, and a boat takes one (build_max; hotspot 2 has it):
-- its build is the refusal row below. Hotspot 6 is furnished with a basic
-- cargo hold, so it has no marker: in the shipyard the hold's own menu is
-- the one row "Configure" (boat_customisation.rs2 ~sailing_part_place's
-- loc_add_op), which opens its catalogue.
local FACILITIES = {
    { hotspot = 0, option = 1, part = "sailing_boat_facility_anchor_2x5", xp = 176, name = "Anchor",
      mats = { { "steel_bar", 8 }, { "lead_bar", 6 }, { "rope", 1 } } },
    { hotspot = 1, option = 1, part = "sailing_boat_facility_bronze_cannon", xp = 157, name = "Bronze cannon",
      mats = { { "woodplank", 4 }, { "nails_bronze", 16 }, { "bronze_bar", 8 } } },
    { hotspot = 2, option = 1, part = "sailing_boat_facility_chum_station_2x5b", xp = 1522, name = "Chum station",
      mats = { { "plank_mahogany", 10 }, { "nails_mithril", 40 }, { "steel_bar", 2 }, { "fishing_bait", 1000 }, { "knife", 1 } } },
    { hotspot = 4, option = 1, part = "sailing_boat_facility_salvaging_hook_bronze", xp = 151, name = "Bronze salvaging hook",
      mats = { { "woodplank", 4 }, { "nails_bronze", 16 }, { "bronze_bar", 6 }, { "rope", 1 } } },
    { hotspot = 5, option = 1, part = "sailing_boat_facility_inoculation_station_2x5", xp = 765, name = "Inoculation station",
      mats = { { "plank_teak", 8 }, { "nails", 32 }, { "relicyms_balm4", 6 } } },
}
-- Refused: a second chum station.
local SECOND_CHUM = { hotspot = 3, part = "sailing_boat_facility_chum_station_2x5a", name = "Chum station",
    refusal = "This boat already has the maximum number of that facility." }
-- The replacement built over hotspot 6's hold.
local HOLD_UPGRADE = { hotspot = 6, option = 2, configure = "cargo hold", part = "sailing_boat_facility_cargo_hold_oak_2x5", xp = 491, name = "Oak cargo hold",
    mats = { { "plank_oak", 8 }, { "nails_iron", 32 } } }

-- varb19273.. : the boat-1 hotspot varbits, 0..10 contiguous.
local function hotspot_varbit(hotspot)
    return string.format("varb%d_sailing_boat_1_hotspot_%d", 19273 + hotspot, hotspot)
end

-- The docks (configs/all.dbrow sailing_dock_*: dock_id) and their planks.
local DOCKS = {
    pandemonium = { id = 1, plank = "sailing_gangplank_the_pandemonium", berth = { 3074, 2984 } },
    port_sarim = { id = 0, plank = "sailing_gangplank_port_sarim", berth = { 3057, 3189 } },
    musa_point = { id = 3, plank = "sailing_gangplank_musa_point", berth = { 2966, 3147 } },
    rimmington = { id = 5, plank = "sailing_gangplank_rimmington", berth = { 2901, 3221 } },
}

-- Each voyage: from, to, and the legs ({x, z, radius}) between. The
-- Pandemonium <-> Port Sarim legs are pryingtimes' measured route. The rest
-- were charted on the cache's own sea: a breadth-first route over full ocean
-- overlay tiles (configs 442..625, shape 0, no level-0 loc -- the boat map's
-- rule, docs/sailing_collision.md) kept three tiles clear (two in Musa
-- Point's cove), straightened into lines. Rimmington's plank stands at the
-- west end of its pier and a rock lies at 2908,3216, so its berth is reached
-- up the x 2900 lane.
local VOYAGES = {
    { from = "pandemonium", to = "port_sarim", deliver = true, legs = {
        { 3082, 2984, 2 }, { 3082, 3012, 2 },
        -- Six tiles south of the Wreck of the Salty Grouper (3054,3051), whose
        -- water reaches it from the south: charted from the deck.
        { 3053, 3045, 1, chart = "Wreck of the Salty Grouper", chart_sym = "sailing_charting_generic_rowboat_mudskipper_sound",
          chart_varbit = "varb18582_sailing_charting_generic_rowboat_mudskipper_sound_complete" },
        { 3043, 3051, 3 }, { 3037, 3105, 3 }, { 3043, 3158, 3 },
        { 3044, 3181, 3 }, { 3052, 3181, 2 }, { 3057, 3189, 2 } } },
    { from = "port_sarim", to = "musa_point", legs = {
        { 3052, 3178, 2 }, { 3038, 3143, 3 }, { 3012, 3106, 3 }, { 3001, 3095, 3 }, { 2972, 3101, 3 },
        { 2979, 3145, 3 }, { 2978, 3156, 2 }, { 2972, 3153, 2 }, { 2966, 3147, 2 } } },
    { from = "musa_point", to = "rimmington", legs = {
        { 2972, 3153, 2 }, { 2978, 3158, 2 }, { 2972, 3160, 3 }, { 2902, 3204, 3 }, { 2900, 3212, 2 },
        { 2901, 3221, 1 } } },
    { from = "rimmington", to = "port_sarim", legs = {
        { 2900, 3210, 2 }, { 2902, 3204, 3 }, { 2972, 3160, 3 }, { 2979, 3145, 3 }, { 2970, 3103, 3 },
        { 3001, 3095, 3 }, { 3012, 3106, 3 }, { 3038, 3143, 3 }, { 3052, 3178, 2 }, { 3052, 3181, 2 },
        { 3057, 3189, 2 } } },
}

-- The courier task taken at the Pandemonium's notice board: the board's
-- first offer (port_tasks.rs2 ~port_task_board_entry 0 =
-- port_task_the_pandemonium_courier_0, "Port Sarim jewellery delivery":
-- task_id 20, one crate, cargo at the Pandemonium, delivered at Port Sarim,
-- 78 Sailing xp -- ~port_task_xp, the wiki's "Courier tasks" table).
local COURIER = { index = 0, task_id = 20, xp = 78,
    take_ledger = "dock_loading_bay_ledger_table_pandemonium",
    deliver_ledger = "dock_loading_bay_ledger_table_port_sarim" }

local JUNIOR_JIM_TILE = "0_47_46_52_37" -- 3060,2981, the deck beside his counter
local DOCK_TILE = "0_47_46_58_43" -- 3066,2987, the dock at the Pandemonium's plank

return {
    id = "sailing_tour",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::maxstats",
        "::setlevel sailing 99", -- ::maxstats leaves Sailing at 1: every dock, hull and crew slot gates on it
        "::setlevel construction 99",
        "::complete quest_pandemonium",
        "::give coins 5000000",
        "::give hammer 1",
        "::give poh_saw 1",
        "::tele " .. JUNIOR_JIM_TILE,
    },

    run = function(t)
        local H = {}

        function H.count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end
        function H.server(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        function H.state()
            local r, reading = t.sail.state()
            if r ~= "ok" then
                return { aboard = false }, tostring(r)
            end
            return reading, t.sail._describe(reading)
        end
        function H.last_messages(n)
            local _, last = t.msg.last(n or 3)
            local text = {}
            for i = 1, #(last or {}) do
                text[#text + 1] = tostring(last[i].text)
            end
            return table.concat(text, " / ")
        end
        -- A game line among the last few: the verbs that cause one return
        -- after it has landed, so a wait begun after them would never see it.
        function H.said(tag, text)
            local _, last = t.msg.last(8)
            for i = 1, #(last or {}) do
                if string.find(tostring(last[i].text), text, 1, true) then
                    t.check(tag, true, tostring(last[i].text))
                    return
                end
            end
            t.check(tag, false, "no '" .. text .. "' -- last: " .. H.last_messages(4))
        end
        function H.give(mats)
            for i = 1, #mats do
                local have = H.count(mats[i][1])
                if have < mats[i][2] then
                    -- lint: kit-give shipyard materials: the builds' kits are far past 28 slots in non-stackables alone (14 bronze bars, 11 oak planks, 10 mahogany planks, 10 steel bars, 10 oak logs, 8 teak planks, 8 planks, 6 lead bars, 6 iron bars, 6 balms), so each part's kit is handed over at its own build
                    t.cheat("::give " .. mats[i][1] .. " " .. (mats[i][2] - have), false)
                end
            end
            t.ticks(1)
        end
        function H.press(sym, sub)
            local r, w = t.ui.widget(sym, sub)
            if r ~= "ok" then
                return r, "ui.widget " .. tostring(sym) .. " " .. tostring(w)
            end
            local pr, pd = t.ui.invoke(w, 1)
            return pr, "pressed " .. tostring(sym) .. (sub ~= nil and ("[" .. sub .. "]") or "")
                .. (pd ~= nil and (" -- " .. tostring(pd)) or "")
        end
        function H.port()
            return H.server("varb19260_sailing_boat_1_port")
        end

        -- -------------------------------------------------------------- 1. buy
        t.check("buy.jim_present", t.npc.by_symbol("junior_jim") == "ok", "Junior Jim at the Pandemonium")
        local coins_before = H.count("coins")
        t.exec("buy.talk", t.player.talk_to, "junior_jim", 1)
        t.exec("buy.menu", t.chat.play, { "choose:Buy a boat", "choose:/^Skiff/" })
        H.said("buy.ready", "Your new boat is ready at the dock.")
        t.check("buy.paid", coins_before - H.count("coins") == 15000,
            "paid " .. (coins_before - H.count("coins")) .. " want 15000")
        t.check("buy.owned", H.server("varb19258_sailing_boat_1_owned") == 1 and
            H.server("varb19259_sailing_boat_1_type") == 1,
            "owned " .. H.server("varb19258_sailing_boat_1_owned") .. " type " ..
            H.server("varb19259_sailing_boat_1_type") .. " (want 1, skiff 1)")
        t.check("buy.port", H.port() == DOCKS.pandemonium.id, "port " .. H.port())
        t.exec("buy.board", t.sail.select_boat, 1)
        local s = H.state()
        t.check("buy.aboard", s.aboard and s.config_id == 2 and s.client_live, select(2, H.state()))

        -- ------------------------------------------------------------ 2. shore
        t.exec("shore.disembark", t.sail.disembark, DOCKS.pandemonium.plank)
        t.exec("shore.board_previous", t.sail.board, DOCKS.pandemonium.plank, 2)
        t.exec("shore.disembark2", t.sail.disembark, DOCKS.pandemonium.plank)

        -- --------------------------------------------------------- 3. shipyard
        t.cheat("::tele " .. JUNIOR_JIM_TILE, false)
        t.ticks(2)
        t.exec("yard.talk", t.player.talk_to, "junior_jim", 1)
        t.exec("yard.menu", t.chat.play, { "choose:Customise a boat" })
        t.exec("yard.select", t.sail.select_boat, 1)
        t.expect("yard.catalogue", t.ui.await_open("sailing_customisation", 5))
        -- Customising is done at the Shipyard island, a private copy of its
        -- square (wiki "Shipyard"; boat_customisation.rs2 ~sailing_shipyard_go):
        -- the hull is berthed there, far from the Pandemonium's dock.
        local at_yard = H.state()
        t.check("yard.at_shipyard", at_yard.aboard and math.abs((at_yard.hull_x or 0) - DOCKS.pandemonium.berth[1]) > 200,
            select(2, H.state()))

        local function build(tag, p)
            H.give(p.mats)
            local _, before = t.skill.snapshot()
            t.exec(tag .. ".build", t.sail.build, p.part, p.name, p.tab)
            t.expect(tag .. ".xp", t.skill.expect_gain("construction", p.xp, before))
            local left = {}
            for i = 1, #p.mats do
                -- The keg's barrel stand and a chum station's knife are tools
                -- the build keeps; everything else is consumed.
                if p.mats[i][1] ~= "knife" and H.count(p.mats[i][1]) ~= 0 then
                    left[#left + 1] = p.mats[i][1] .. " x" .. H.count(p.mats[i][1])
                end
            end
            t.check(tag .. ".materials", #left == 0, #left == 0 and ("taken: " .. p.name)
                or ("left: " .. table.concat(left, ", ")))
        end

        for i = 1, #HULL_PARTS do
            local p = HULL_PARTS[i]
            local tag = "yard." .. p.part
            build(tag, p)
            t.check(tag .. ".installed", H.server(p.varbit) == p.value,
                p.varbit .. " = " .. H.server(p.varbit) .. " want " .. p.value)
        end

        local function open_hotspot(tag, f)
            t.key("escape")
            t.ticks(1)
            if f.configure then
                t.exec(tag .. ".configure", t.sail._press_deck_row, "Configure", f.configure)
            else
                t.exec(tag .. ".marker", t.sail.open_hotspot, f.hotspot, "sailing_boat_facility_placeholder_2x5_" .. f.hotspot)
            end
            local open_r, open_d = t.ui.await_open("sailing_customisation", 5)
            t.expect(tag .. ".catalogue", open_r, open_r == "ok" and open_d
                or (tostring(open_d) .. " -- last: " .. H.last_messages(3)))
            t.check(tag .. ".hotspot", H.server("varb19524_sailing_boat_customisation_hotspot_id") == f.hotspot,
                "catalogue hotspot " .. H.server("varb19524_sailing_boat_customisation_hotspot_id"))
        end

        local function build_facility(tag, f)
            open_hotspot(tag, f)
            build(tag, f)
            local varbit = hotspot_varbit(f.hotspot)
            t.check(tag .. ".installed", H.server(varbit) == f.option,
                varbit .. " = " .. H.server(varbit) .. " want " .. f.option)
        end

        for i = 1, #FACILITIES do
            build_facility("yard.hotspot" .. FACILITIES[i].hotspot, FACILITIES[i])
        end
        open_hotspot("yard.second_chum", SECOND_CHUM)
        -- The client draws the row blocked and its clientscript shows the
        -- refusal itself (torirs_sailing_custom_build_op: blocked -> an error
        -- popup, nothing sent); the scriptrun lane has no clientscript and
        -- sends the trigger, which the server refuses with its own line.
        -- Either way nothing is built.
        local refused_r, refused_d = t.sail.build(SECOND_CHUM.part, SECOND_CHUM.name, nil, 3)
        t.check("yard.second_chum.refused", refused_r ~= "ok" and (refused_r ~= "refused" or
            string.find(tostring(refused_d), SECOND_CHUM.refusal, 1, true) ~= nil), tostring(refused_d))
        t.check("yard.second_chum.empty", H.server(hotspot_varbit(SECOND_CHUM.hotspot)) == 0,
            hotspot_varbit(SECOND_CHUM.hotspot) .. " = " .. H.server(hotspot_varbit(SECOND_CHUM.hotspot)))
        t.check("yard.hold_furnished", H.server(hotspot_varbit(6)) == 1,
            hotspot_varbit(6) .. " = " .. H.server(hotspot_varbit(6)) .. " (a new skiff's basic hold)")
        build_facility("yard.hold_upgrade", HOLD_UPGRADE)
        t.key("escape")
        t.ticks(1)
        -- The berth's plank is the way home: the captain back beside Junior
        -- Jim, the hull back at the Pandemonium's dock.
        t.exec("yard.leave", t.sail.disembark, "sailing_gangplank_shipyard_disembark")
        H.said("yard.left", "You leave the Shipyard.")
        t.check("yard.boat_home", H.port() == DOCKS.pandemonium.id, "port " .. H.port())
        -- Nothing is built anywhere else: aboard at the dock the catalogue
        -- refuses (the ::sailcustom debugproc is the catalogue's own open).
        t.exec("yard.dock_board", t.sail.board, DOCKS.pandemonium.plank, 2)
        t.cheat("::sailcustom", false)
        t.ticks(1)
        H.said("yard.dock_refused", "Boats can only be customised at the Shipyard.")
        t.check("yard.dock_no_catalogue", t.ui.await_open("sailing_customisation", 1) ~= "ok", "the catalogue stayed shut")
        t.exec("yard.dock_off", t.sail.disembark, DOCKS.pandemonium.plank)

        -- ------------------------------------------------------------- 4. crew
        -- On the open dock beside the Pandemonium's plank: a spawn behind
        -- Junior Jim's counter lands where no path reaches him.
        t.cheat("::tele " .. DOCK_TILE, false)
        t.ticks(2)
        t.note("Jobless Jim has no spawn file yet: ::spawn places him beside the player")
        t.cheat("::spawn sailing_crew_generic_1_dock", false)
        t.ticks(2)
        t.check("crew.jim_present", t.npc.by_symbol("sailing_crew_generic_1_dock") == "ok", "Jobless Jim spawned")
        t.exec("crew.recruit", t.player.talk_to, "sailing_crew_generic_1_dock", 1)
        H.said("crew.recruited", "Jobless Jim joins your recruits.")
        t.check("crew.unlocked", H.server("varb19483_sailing_crew_generic_1_unlocked") == 1,
            "unlocked " .. H.server("varb19483_sailing_crew_generic_1_unlocked"))
        t.expect("crew.panel", t.ui.await_open("sailing_crew", 4))

        local function assign(tag, slot)
            t.exec(tag .. ".recruit_row", H.press, "sailing_crew:recruits_list", 0)
            t.exec(tag .. ".slot_row", H.press, "sailing_crew:crew_list", slot)
            t.exec(tag .. ".assign", H.press, "sailing_crew:assign")
            t.ticks(2)
        end
        assign("crew.slot1", 0)
        t.check("crew.slot1.set", H.server("varb19468_sailing_crew_slot_1") == 1,
            "slot 1 = " .. H.server("varb19468_sailing_crew_slot_1"))
        assign("crew.slot2", 1)
        t.check("crew.slot2.moved", H.server("varb19469_sailing_crew_slot_2") == 1 and
            H.server("varb19468_sailing_crew_slot_1") == 0,
            "slots " .. H.server("varb19468_sailing_crew_slot_1") .. "," .. H.server("varb19469_sailing_crew_slot_2"))
        assign("crew.back", 0)
        t.check("crew.back.set", H.server("varb19468_sailing_crew_slot_1") == 1 and
            H.server("varb19469_sailing_crew_slot_2") == 0,
            "slots " .. H.server("varb19468_sailing_crew_slot_1") .. "," .. H.server("varb19469_sailing_crew_slot_2"))
        t.key("escape")
        t.ticks(1)

        -- ------------------------------------------------------ 5. port task
        local function slot_id(slot)
            local _, slots = t.sail.tasks()
            return slots and slots[slot + 1] and slots[slot + 1].id or -1
        end
        t.exec("task.board", t.sail.task_board, "port_task_board_pandemonium")
        t.exec("task.accept", t.sail.task_accept, COURIER.index)
        t.check("task.slot", slot_id(0) == COURIER.task_id, "slot 0 task " .. slot_id(0) .. " want " .. COURIER.task_id)
        -- A second offer, accepted into the second slot (Sailing 7+) and then
        -- cancelled from the same board: selecting an active task offers
        -- "Cancel task" on the same button.
        t.exec("task.accept_second", t.sail.task_accept, COURIER.index + 1)
        local second = slot_id(1)
        t.check("task.second_slot", second > 0, "slot 1 task " .. second)
        t.exec("task.cancel_second", t.sail.task_accept, COURIER.index + 1)
        t.check("task.second_cancelled", slot_id(1) == 0 and slot_id(0) == COURIER.task_id,
            "slots " .. slot_id(0) .. "," .. slot_id(1))
        t.key("escape")
        t.ticks(1)
        t.exec("task.take", t.sail.cargo_take, COURIER.take_ledger, 3)
        -- A crate is carried in both arms: the cache's crate-carry walk set
        -- (sailing/configs/cargo_crates_bas.obj), back to the bare-handed set
        -- once it is stowed.
        t.ticks(1)
        t.exec("task.carry_walk", t.sail.walk_set, { ready = "dttd_carrying_crate_ready",
            walk = "dttd_carrying_crate_walk", run = "dttd_carrying_crate_walk_fast" })
        t.exec("task.board_boat", t.sail.board, DOCKS.pandemonium.plank, 2)
        t.exec("task.load", t.sail.cargo_load, "cargo hold")
        t.ticks(1)
        t.exec("task.empty_walk", t.sail.walk_set, { ready = "human_ready", walk = "human_walk_f", run = "human_running" })
        t.exec("task.ashore", t.sail.disembark, DOCKS.pandemonium.plank)

        -- ------------------------------------------------------- 6. salvage
        -- Out of the Pandemonium into the Kharidian Sea, whose square lays out
        -- its small-wreck pool (sailing_wrecks.rs2): the nearest raised wreck
        -- is salvaged with the bronze hook built at hotspot 4 until the sea
        -- reclaims it. The salvage lands in the boat's hold (boat_facility_
        -- actions.rs2 ~sailing_store_product), so it is withdrawn there and
        -- sorted at the Pandemonium's salvaging station.
        t.exec("salvage.board", t.sail.board, DOCKS.pandemonium.plank, 2)
        t.exec("salvage.helm", t.sail.helm, "Helm")
        t.exec("salvage.sails", t.sail.sails, true)
        t.exec("salvage.out", t.sail.sail_to, 3082, 2984, 2)
        local wreck_r, wreck = t.world.loc_near("sailing_small_shipwreck", 80)
        t.check("salvage.wreck_raised", wreck_r == "ok", tostring(wreck_r) .. " "
            .. (type(wreck) == "table" and (wreck.tile_x .. "," .. wreck.tile_z) or tostring(wreck)))
        local salvage_xp_before = nil
        if wreck_r == "ok" then
            -- Four tiles north of the wreck: every site is open water three
            -- tiles round (the cache's ocean overlays), and the hook reaches
            -- seven tiles (boat_salvaging.rs2 ~sailing_wreck_range).
            t.exec("salvage.approach", t.sail.sail_to, wreck.tile_x, wreck.tile_z + 4, 2)
            t.exec("salvage.furl", t.sail.sails, false)
            salvage_xp_before = select(2, t.skill.snapshot())
            t.exec("salvage.deploy", t.sail._press_deck_row, "Deploy", "salvaging hook")
            t.expect("salvage.deployed", t.msg.await("You deploy the hook into the shipwreck.", 4))
            t.expect("salvage.reclaimed", t.msg.await(
                "You salvage all you can from the shipwreck before it is reclaimed by the sea.", 300))
            local sunk_r = t.world.loc_near("sailing_small_shipwreck_stump", 80,
                { at = { wreck.tile_x, wreck.tile_z, 0 }, slack = 0 })
            t.check("salvage.site_sunk", sunk_r == "ok", "the salvaged site is sunken: " .. tostring(sunk_r))
            local other_r, other = t.world.loc_near("sailing_small_shipwreck", 80)
            t.check("salvage.pool_rose", other_r == "ok" and (other.tile_x ~= wreck.tile_x or other.tile_z ~= wreck.tile_z),
                "another wreck up: " .. (type(other) == "table" and (other.tile_x .. "," .. other.tile_z) or tostring(other)))
        end
        -- Home either way. Deploying took the captain off the helm
        -- (~sailing_facility_release_helm).
        t.exec("salvage.helm_again", t.sail.helm, "Helm")
        t.exec("salvage.sails_again", t.sail.sails, true)
        t.exec("salvage.home1", t.sail.sail_to, 3082, 2984, 2)
        t.exec("salvage.home2", t.sail.sail_to, 3074, 2984, 1)
        t.exec("salvage.home_furl", t.sail.sails, false)
        if salvage_xp_before then
            t.exec("salvage.from_hold", t.sail.hold_withdraw, "sailing_small_shipwreck_salvage", "cargo hold")
            t.key("escape")
            t.ticks(1)
        end
        t.exec("salvage.ashore", t.sail.disembark, DOCKS.pandemonium.plank)
        local got = H.count("sailing_small_shipwreck_salvage")
        if salvage_xp_before then
            t.check("salvage.salvage", got > 0, "small shipwreck salvage x" .. got)
            -- 10 xp a piece: ~sailing_wreck_data (100 tenths), the wiki's 10.
            t.expect("salvage.xp", t.skill.expect_gain("sailing", 10 * got, salvage_xp_before))
        end
        if got > 0 then
            local _, sort_before = t.skill.snapshot()
            -- Sorting answers with no line until the backpack is sorted, so
            -- click_loc's settle reports no answer: the rows below judge it.
            local press_r, press_d = t.player.click_loc("sailing_port_salvaging_station", 1)
            t.note("salvage.sort press: " .. tostring(press_r) .. " " .. tostring(press_d))
            -- Two ticks a piece once at the station; the client walks there
            -- first (click_loc answers on the map flag), so wait on the
            -- backpack, not a fixed count.
            for _ = 1, 2 * got + 24 do
                if H.count("sailing_small_shipwreck_salvage") == 0 then
                    break
                end
                t.ticks(1)
            end
            t.ticks(1)
            t.check("salvage.sorted", H.count("sailing_small_shipwreck_salvage") == 0,
                "salvage " .. got .. " -> " .. H.count("sailing_small_shipwreck_salvage"))
            -- 5.5 xp a piece (~sailing_sort_recipe 55 tenths; the wiki's 5.5).
            t.expect("salvage.sort_xp", t.skill.expect_gain("sailing", 5.5 * got, sort_before))
        end

        -- ----------------------------------------------------------- 7. voyage
        -- Every gust sailed through is trimmed (sail.lua QD.sail.auto_trim: the
        -- mast's "Work" pressed only while the trim window is open).
        t.sail.auto_trim = true
        t.sail.trims = {}
        local _, trim_xp_before = t.skill.snapshot()
        for v = 1, #VOYAGES do
            local voyage = VOYAGES[v]
            local from, to = DOCKS[voyage.from], DOCKS[voyage.to]
            local tag = "voyage." .. voyage.from .. "_to_" .. voyage.to
            t.exec(tag .. ".board", t.sail.board, from.plank, 2)
            if v == 1 then
                t.check("crew.aboard", H.server("varb19243_sailing_sidepanel_crew_slot_1") == 1,
                    "sidepanel crew slot 1 = " .. H.server("varb19243_sailing_sidepanel_crew_slot_1"))
            end
            t.exec(tag .. ".helm", t.sail.helm, "Helm")
            t.exec(tag .. ".sails", t.sail.sails, true)
            for l = 1, #voyage.legs do
                local leg = voyage.legs[l]
                -- No gust is trimmed on the harbour approach (the last three
                -- legs): a burst carries the hull into the berth's piers.
                -- Nor on the way out or on a short leg: leaving Rimmington, a
                -- trimmed burst ran an 8-tile leg 4 tiles past its target
                -- onto the shore before the stop landed (client lane).
                local prev = voyage.legs[l - 1]
                local span = prev and math.max(math.abs(leg[1] - prev[1]), math.abs(leg[2] - prev[2])) or 0
                t.sail.auto_trim = l > 2 and l <= #voyage.legs - 3 and span >= 12
                t.exec(tag .. ".leg" .. l, t.sail.sail_to, leg[1], leg[2], leg[3])
                if leg.chart then
                    -- Furled to chart, as a captain stops to look; the Chart
                    -- row is pressed from the deck (sail.lua QD.sail.chart).
                    local ctag = tag .. ".chart"
                    t.exec(ctag .. ".furl", t.sail.sails, false)
                    local _, before = t.skill.snapshot()
                    t.exec(ctag, t.sail.chart, leg.chart, leg.chart_sym)
                    t.expect(ctag .. ".xp", t.skill.expect_gain("sailing", 35, before))
                    t.check(ctag .. ".logged", H.server(leg.chart_varbit) == 1,
                        leg.chart_varbit .. " = " .. H.server(leg.chart_varbit))
                    local again_r, again_d = t.sail.chart(leg.chart, leg.chart_sym)
                    t.check(ctag .. ".once", again_r == "refused" and
                        string.find(tostring(again_d), "already charted", 1, true) ~= nil, tostring(again_d))
                    t.exec(ctag .. ".sails", t.sail.sails, true)
                end
            end
            t.exec(tag .. ".furl", t.sail.sails, false)
            local at = H.state()
            t.check(tag .. ".berth", at.aboard and math.abs((at.hull_x or 0) - to.berth[1]) <= 4 and
                math.abs((at.hull_z or 0) - to.berth[2]) <= 4,
                select(2, H.state()) .. " (want within 4 of " .. to.berth[1] .. "," .. to.berth[2] .. ")")
            if voyage.deliver then
                t.exec(tag .. ".withdraw", t.sail.cargo_withdraw, "cargo hold")
            end
            t.exec(tag .. ".disembark", t.sail.disembark, to.plank)
            t.check(tag .. ".port", H.port() == to.id, "boat port " .. H.port() .. " want " .. to.id)
            if voyage.deliver then
                local _, before = t.skill.snapshot()
                t.exec(tag .. ".deliver", t.sail.cargo_deliver, COURIER.deliver_ledger, 10)
                t.expect(tag .. ".deliver.xp", t.skill.expect_gain("sailing", COURIER.xp, before))
                t.check(tag .. ".deliver.done", slot_id(0) == 0, "slot 0 task " .. slot_id(0) .. " (cleared on completion)")
                H.said(tag .. ".deliver.mes", "You complete the task: Port Sarim jewellery delivery.")
            end
        end

        t.sail.auto_trim = false
        local trimmed, refused = 0, {}
        for i = 1, #t.sail.trims do
            if t.sail.trims[i].result == "ok" then
                trimmed = trimmed + 1
            else
                refused[#refused + 1] = tostring(t.sail.trims[i].detail)
            end
        end
        t.check("trim.caught", trimmed > 0, trimmed .. " gust(s) trimmed in " .. #t.sail.trims
            .. " window(s): " .. tostring(t.sail.trims[1] and t.sail.trims[1].detail))
        t.check("trim.clean", #refused == 0, #refused == 0 and "every press caught its gust"
            or table.concat(refused, " | "))
        t.finish(0)
    end,
}
