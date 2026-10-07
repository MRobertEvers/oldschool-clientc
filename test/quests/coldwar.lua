-- Cold War (quest_coldwar), a relay: one author per leg (docs/quest_authoring/relay.md).
-- Leg 1: Larry at the Ardougne Zoo, the bird hide on the iceberg, the penguin watch, the boat to Relleka.
--
-- RE-DRIVE b72 (door rule). goto_tile is used only for plain overland travel between open tiles; every
-- door and gate between the player and a target is pressed by its verb, in and out:
--   * Lumbridge/Rimmington <-> Ardougne/Relleka: the only walk on foot is the members' gate south of
--     Taverley (membergater/membergatel 2933-2934,3320, gates.rs2 [label,member_fencegate_try], a
--     walk-through gate): reach.py 3206,3233 -> 2933,3318 REACH closed-doors len=389; 2933,3321 ->
--     2599,3268 REACH len=823 at margin 250; 2707,3732 (the Relleka landing) -> 2933,3321 REACH len=943
--     at 250; 2933,3319 -> 2953,3222 (Rimmington) REACH len=117.
--   * the zoo's penguin enclosure door (peng_ardougne_enclosure_door 2594,3266, north edge; doors.loc
--     door_closed) in and out;
--   * the Lumbridge sheep pen's east gate (qip_sheep_shearer_fencegate_l/_r 3213,3261-3262, west edge:
--     the pen is x <= 3212), Fred's yard gate (3188-3189,3279) and house door (qip_sheep_shearer_poordoor
--     3189,3275), the cow field's rustic_fencegate_l (3176,3315, north edge);
--   * in the outpost (maps/m41_162.jl2): the KGP room's peng_base_door 2654,10382 (west edge: the room
--     is x <= 2653), the agility room's peng_base_door_agility 2640,10382 (west edge), Ping and Pong's
--     peng_base_door_bard 2662,10396 (east edge: the room is x >= 2663).
-- The POH is staged in setup (::coldwarpoh, a prerequisite, not quest work) and entered and left by its
-- portals; the iceberg/zoo/Lumbridge trips Larry offers are his own teleports.

-- The members' gate south of Taverley, pressed on every crossing (as ikov.lua / biohazard.lua).
local function gate_north(t, name)
    t.exec("goto-" .. name, t.player.goto_tile, 2933, 3318, 0) -- open ground south of the gate
    t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
        near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
        far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
end
local function gate_south(t, name)
    t.exec("goto-" .. name, t.player.goto_tile, 2934, 3322, 0) -- open ground north of the gate
    t.exec(name, t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
        near = { 2934, 3321 }, far_ok = function(tile) return tile.z <= 3319 and math.abs(tile.x - 2934) <= 2 end,
        far_desc = "south of the members' gate, z <= 3319", far = { 2934, 3318 } })
end
-- The Lumbridge sheep pen (Larry and the Thing stand inside it): its east gate.
local PEN_GATE = { closed = "qip_sheep_shearer_fencegate_l", open = "qip_sheep_shearer_openfencegate_l" }
local function pen_in(t, name)
    t.exec("goto-" .. name, t.player.goto_tile, 3215, 3261, 0) -- open ground east of the pen gate
    t.exec(name, t.player.pass_door, { closed = PEN_GATE.closed, open = PEN_GATE.open,
        at = { 3213, 3261, 0 }, near = { 3214, 3261 }, far = { 3211, 3261 } })
end
local function pen_out(t, name)
    t.exec(name .. "-walk", t.player.walk_to, 3212, 3261, 20)
    t.exec(name, t.player.pass_door, { closed = PEN_GATE.closed, open = PEN_GATE.open,
        at = { 3213, 3261, 0 }, near = { 3212, 3261 }, far = { 3215, 3261 } })
end
-- The zoo's penguin enclosure door (the pen is z >= 3267).
local ZOO_DOOR = { closed = "peng_ardougne_enclosure_door", open = "peng_ardougne_enclosure_door_open" }
local function zoo_pen_in(t, name)
    t.exec(name .. "-walk", t.player.walk_to, 2594, 3265, 20)
    t.exec(name, t.player.pass_door, { closed = ZOO_DOOR.closed, open = ZOO_DOOR.open,
        at = { 2594, 3266, 0 }, near = { 2594, 3266 }, far = { 2594, 3268 } })
end
local function zoo_pen_out(t, name)
    t.exec(name .. "-walk", t.player.walk_to, 2594, 3267, 20)
    t.exec(name, t.player.pass_door, { closed = ZOO_DOOR.closed, open = ZOO_DOOR.open,
        at = { 2594, 3266, 0 }, near = { 2594, 3267 }, far = { 2594, 3264 } })
end
-- Zoo -> Lumbridge sheep pen on foot (the suit, if worn, wears off on the way: coldwar_shared.rs2
-- [timer,coldwar_suit] "The spell wears off as you leave the penguins behind").
local function zoo_to_lumbridge(t, name)
    gate_south(t, name .. ".memberGate")
    pen_in(t, name .. ".penGateIn")
end
local function lumbridge_to_zoo(t, name)
    gate_north(t, name .. ".memberGate")
    t.exec("goto-" .. name, t.player.goto_tile, 2599, 3268, 0) -- open ground beside zoo Larry
end

return {
    id = "coldwar",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::coldwar", -- the quest's reset debugproc: state 0 (it also teleports; the placement is reset below)
        -- Quest Helper leg 1 requirements: 10 oak planks, 10 steel nails, hammer, spade
        "::give plank_oak 10",
        "::give nails 10",
        "::give hammer 1",
        "::give spade 1",
        -- Quest Helper skill requirements (coldwar_shared.rs2 coldwar_meets_requirements)
        "::setlevel hunter 10",
        "::setlevel agility 30",
        "::setlevel crafting 30",
        "::setlevel construction 34",
        "::setlevel thieving 15",
        -- Additional access (Quest Helper enterPoh: "a POH with a Crafting table 3 or 4"): building the
        -- table is Construction training, not a Cold War step (coldwar_debug.rs2 [debugproc,coldwarpoh]).
        -- It teleports to the Rimmington portal; the last setup line puts the player back on the fixture tile.
        "::coldwarpoh",
        -- Quest Helper leg 2 requirements (enterPoh items): a steel bar, a normal plank, silk
        "::give steel_bar 1",
        "::give woodplank 1",
        "::give silk 1",
        -- Quest Helper leg 3 requirements: a raw cod for the zoo penguin (returnToZooPenguin),
        -- swamp tar and 5 feathers for Noodle (tellLarryAboutOutpost / noodle2)
        "::give raw_cod 1",
        "::give swamp_tar 1",
        "::give feather 5",
        -- Quest Helper leg 5 requirements (makeBongos): a mahogany plank and leather (Crafting 30 already set)
        "::give plank_mahogany 1",
        "::give leather 1",
        -- killIcelords: the Ice Lords are level 51 and fought for real: armed and fed
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 70",
        "::give rune_scimitar 1",
        "::give lobster 6",
        "::goto 3206 3233 0", -- the fixture's own tile (fresh_lumbridge.ini): the run starts in Lumbridge
    },
    bind = {
        varp = "varb3293_peng_quest",
        constants = {
            not_started = 0, birdhide_setup = 5, emotes_learned = 10, after_emotes = 15,
            relleka = 20, clockwork_penguin = 25, suit_iceberg = 30, zoo_trust = 35,
            zoo_report = 40, lumbridge_visit = 45, zoo_return = 50,
            thing_return = 55, fred = 60, outpost_info = 65, iceberg_kgp = 70, noodle1 = 75, noodle2 = 80,
            kgp_again = 85, debrief = 90, agility_ready = 95, agility_done = 100, army_report = 105,
            pingpong_go = 110, instruments = 115, control_room = 120, icelords = 125, escape = 130,
            complete = 135,
        },
        row = "quest_coldwar",
        display = "Cold War",
        points = 1,
    },
    legs = {
        { name = "larry_and_hide", run = function(t)
            -- LEG 1 BEGIN: talkToLarry
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
            lumbridge_to_zoo(t, "talkToLarry")

            t.exec("talkToLarry", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("talkToLarry-dialog", t.chat.play, {
                "player:Do you need any help",
                "npc:Are you working for them",
                "player:Who are they",
                "npc:EXACTLY",
                "player:Seriously",
                "npc:I think you can be trusted",
                "npc:penguins?",
                "player:they live in the cold",
                "npc:Would you like to learn more",
                "player:What do you need my help for",
                "npc:I need to build a shelter",
                "npc:So, are you up for it",
                "options",
                "choose:Okay, why not!",
                "player:Okay, why not",
                "npc:zoo keeper",
                "player:Ok! Okay then",
                "npc:gather materials",
                "npc:10 oak planks",
                "npc:meet me up by the entrance",
                "player:Right, oak planks",
            })
            t.expect("quest.stage.birdhide_setup", t.quest.expect_stage("birdhide_setup"))

            -- Talk to Larry again with the materials: he teleports us to the iceberg.
            t.exec("talkToLarryAgain", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("talkToLarryAgain-dialog", t.chat.play, {
                "npc:Have you got the equipment",
                "options",
                "choose:Yes, I have all the materials.",
                "player:Yes, I have them",
                "npc:You have all the materials",
                "options",
                "choose:Yes",
                "end",
            })
            t.ticks(8)
            local _, tile = t.world.tile()
            t.check("talkToLarryAgain.iceberg", tile.x >= 2640 and tile.z >= 3980 and tile.z <= 4000,
                "teleported to " .. tile.x .. "," .. tile.z .. " level " .. tostring(t.world.level()))

            -- The hide: oak planks on the firm snow patch, then the spade on the frame.
            local hide = t.player.by_symbol("loc", "peng_observer_cabin_multiloc")
            t.exec("goto-usePlankOnFirmSnow", t.player.goto_tile, 2664, 3988, 1)
            t.exec("usePlankOnFirmSnow", t.player.use_on, "plank_oak", hide)
            t.ticks(4)
            t.check("usePlankOnFirmSnow.planks", select(2, t.inv.count("plank_oak")) == 0,
                "oak planks left " .. tostring(select(2, t.inv.count("plank_oak"))) .. ", nails left " .. tostring(select(2, t.inv.count("nails"))))
            t.exec("useSpadeOnBirdHide", t.player.use_on, "spade", hide)
            t.ticks(4)
            t.expect("quest.stage.emotes_learned", t.quest.expect_stage("emotes_learned"))

            -- Larry on the iceberg: into the hide, the penguins greet each other.
            t.exec("goto-learnPenguinEmotes", t.player.goto_tile, 2662, 3990, 1)
            t.exec("learnPenguinEmotes", t.player.talk_to, "peng_larry_ice", 1)
            t.exec("learnPenguinEmotes-dialog", t.chat.play, {
                "player:Can we start observing",
                "npc:Let's get in",
                "npc:here come two penguins",
                "player:Aww",
                "npc:EVIL",
                "end",
            })
            t.ticks(2)
            t.expect("quest.stage.after_emotes", t.quest.expect_stage("after_emotes"))

            t.exec("talkToLarryAfterEmotes", t.player.talk_to, "peng_larry_ice", 1)
            t.exec("talkToLarryAfterEmotes-dialog", t.chat.play, {
                "npc:I knew it",
                "options",
                "choose:That's crazy!",
                "player:That's crazy",
                "npc:It isn't",
                "player:It looked like one of them",
                "npc:I don't care what you think",
                "player:Okay, calm down",
                "npc:We need to get closer",
                "player:How do you plan",
                "npc:We can't discuss it here",
            })
            t.ticks(2)
            t.expect("quest.stage.relleka", t.quest.expect_stage("relleka"))

            -- The row boat takes us back to Relleka (click_loc walks to it: its click zone is solid).
            t.exec("returnToRelleka", t.player.click_loc, "peng_row_boat_clickzone", 1)
            t.ticks(8)
            local _, rtile = t.world.tile()
            t.check("returnToRelleka.arrived", rtile.x >= 2690 and rtile.z >= 3720 and rtile.z <= 3745,
                "at " .. rtile.x .. "," .. rtile.z)

            t.exec("talkToLarryInRelleka", t.player.talk_to, "peng_larry_rell", 1)
            t.exec("talkToLarryInRelleka-dialog", t.chat.play, {
                "player:How do you plan to infiltrate",
                "npc:I have altered some designs",
                "player:What? We'll just set it off",
                "npc:No! I'm going to use a spell",
                "player:Wait a minute",
                "npc:The penguins know my scent",
                "npc:Don't worry",
                "player:Gee",
                "npc:Here's the book",
                "player:Why do I have to make the suit",
                "npc:You have to make it",
                "player:Oh, sorry",
                "npc:Well...it's permanent leave",
                "npc:their cage and I was fired",
                "npc:I'm sure the penguins",
                "player:So that's why",
                "npc:Err, no",
                "npc:When you're done making the suit",
            })
            t.ticks(2)
            t.expect("quest.stage.clockwork_penguin", t.quest.expect_stage("clockwork_penguin"))
            -- LEG 1 END
            local _, etile = t.world.tile()
            local _, stage = t.quest.stage()
            t.note("leg.1.end: player at " .. etile.x .. "," .. etile.z .. " level " .. tostring(select(2, t.world.level()))
                .. ", varb3293_peng_quest=" .. tostring(stage) .. ", clockwork book in backpack: "
                .. tostring(select(2, t.inv.count("peng_book"))))
        end },
        { name = "suit_and_greetings", run = function(t)
            -- LEG 2 BEGIN: enterPoh
            local emote_names = { "shiver", "spin", "clap", "bow", "cheer", "wave", "preen", "flap" }
            -- Greeting: read the three expected emotes from the server and click them on the panel.
            local function greet(name)
                for i = 1, 3 do
                    local _, v = t.var.server("varb330" .. (i - 1) .. "_peng_emote_" .. i)
                    local en = emote_names[v]
                    local _, w = t.ui.widget("peng_emote:peng_emote_" .. en)
                    t.check(name .. ".emote" .. i .. ".widget", w ~= nil, "expected emote " .. i .. " = " .. tostring(en))
                    local ir = t.ui.invoke(w, 0)
                    t.ticks(3)
                    local _, chk = t.var.server("varb3307_peng_emote_check")
                    t.check(name .. ".emote" .. i, ir == "ok" or ir == nil,
                        "clicked " .. en .. " (invoke " .. tostring(ir) .. "), varb3307_peng_emote_check=" .. tostring(chk))
                    t.chat.drain({ stop_at = "none", max_pages = 4 })
                end
            end

            t.ticks(2)
            t.note("leg.2.start: stage " .. tostring(select(2, t.quest.stage())) .. " pack steel_bar="
                .. tostring(select(2, t.inv.count("steel_bar"))) .. " woodplank=" .. tostring(select(2, t.inv.count("woodplank")))
                .. " silk=" .. tostring(select(2, t.inv.count("silk"))))
            -- enterPoh: the house (staged in setup by ::coldwarpoh: Rimmington, Workshop, Crafting table 3)
            -- is reached on foot from Relleka: overland to the members' gate, through it, on to the portal.
            gate_south(t, "enterPoh.memberGate")
            t.exec("goto-enterPoh", t.player.goto_tile, 2953, 3224, 0) -- open ground beside the Rimmington portal
            t.exec("enterPoh", t.player.click_loc, "poh_rimmington_portal", 2)
            t.ticks(6)
            local _, ptile = t.world.tile()
            t.check("enterPoh.inside", ptile.x > 6000, "in the house instance at " .. ptile.x .. "," .. ptile.z)

            t.exec("makeClockwork", t.player.click_loc, "poh_clockmaking_3", 1)
            t.exec("makeClockwork-dialog", t.chat.play, { "options", "choose:Clockwork" })
            t.inv.await("poh_clockwork_mechanism", 1, 15)
            t.check("makeClockwork.item", select(2, t.inv.count("poh_clockwork_mechanism")) >= 1,
                "clockwork mechanisms " .. tostring(select(2, t.inv.count("poh_clockwork_mechanism"))))

            t.exec("makePenguin", t.player.click_loc, "poh_clockmaking_3", 1)
            t.exec("makePenguin-dialog", t.chat.play, { "options", "choose:Clockwork toys", "options", "choose:Clockwork penguin" })
            t.inv.await("peng_suit_unwound", 1, 15)
            t.check("makePenguin.item", select(2, t.inv.count("peng_suit_unwound")) >= 1,
                "suits " .. tostring(select(2, t.inv.count("peng_suit_unwound"))))

            -- leave the house by its exit portal (poh_enter_leave.rs2 [oploc1,poh_exit_portal] -> ~poh_leave),
            -- then on foot to the zoo through the members' gate
            t.exec("bringSuitToLarry.leavePoh", t.player.click_loc, "poh_exit_portal", 1)
            t.ticks(6)
            local _, xtile = t.world.tile()
            t.check("bringSuitToLarry.outside", xtile.x < 6000 and math.abs(xtile.x - 2953) <= 6 and math.abs(xtile.z - 3224) <= 6,
                "out of the house at " .. xtile.x .. "," .. xtile.z)
            lumbridge_to_zoo(t, "bringSuitToLarry")
            t.exec("bringSuitToLarry", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("bringSuitToLarry-dialog", t.chat.play, {
                "npc:Do you have the suit",
                "options",
                "choose:Yes, I have it.",
                "player:Yes, I have it",
                "npc:perfect",
                "options",
                "choose:Yes",
            })
            t.ticks(10)
            t.expect("quest.stage.suit_iceberg", t.quest.expect_stage("suit_iceberg"))

            t.exec("talkToLarryOnIcebergWithSuit", t.player.talk_to, "peng_larry_ice", 1)
            t.exec("talkToLarryOnIcebergWithSuit-dialog", t.chat.play, {
                "npc:Look what they did",
                "options",
                "choose:/warning message/",
                "player:It looks like a warning message",
                "npc:they act like they aren't",
                "player:there aren't any penguins",
                "npc:But, we were so close",
                "player:There are other penguins",
                "npc:OF COURSE",
                "npc:I can teleport us",
                "player:Then why",
                "npc:Boat rides",
                "options",
                "choose:Yes.",
            })
            t.ticks(10)
            t.expect("quest.stage.zoo_trust", t.quest.expect_stage("zoo_trust"))

            t.exec("goto-tuxedoTime", t.player.goto_tile, 2599, 3268, 0)
            t.exec("tuxedoTime", t.player.talk_to, "peng_larry_zoo", 3)
            t.exec("tuxedoTime-dialog", t.chat.play, { "player:Penguin time" })
            t.ticks(4)
            t.check("tuxedoTime.suit", select(2, t.var.server("varb3306_peng_transmog")) == 1,
                "peng_transmog=" .. tostring(select(2, t.var.server("varb3306_peng_transmog"))))

            zoo_pen_in(t, "enterPenguinPen")

            t.exec("talkToZooPenguin", t.player.talk_to, "peng_zoo", 1)
            t.exec("talkToZooPenguin-dialog", t.chat.drain, { stop_at = "none", max_pages = 12 })
            t.ticks(2)
            greet("emoteAtPenguin")
            t.chat.drain({ stop_at = "none", max_pages = 40 })
            t.ticks(2)
            t.expect("quest.stage.lumbridge_visit", t.quest.expect_stage("lumbridge_visit"))
            t.check("emoteAtPenguin.report", select(2, t.inv.count("peng_report_1")) >= 1,
                "peng_report_1 x" .. tostring(select(2, t.inv.count("peng_report_1"))))

            zoo_pen_out(t, "exitSuit.penDoorOut")
            t.exec("exitSuit", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("exitSuit-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(3)
            t.check("exitSuit.off", select(2, t.var.server("varb3306_peng_transmog")) == 0,
                "peng_transmog=" .. tostring(select(2, t.var.server("varb3306_peng_transmog"))))
            t.exec("talkToLarryMissionReport", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("talkToLarryMissionReport-dialog", t.chat.play, {
                "player:The penguin gave me his mission report",
                "npc:Lumbridge?",
                "player:Maybe the penguins",
                "player:I didn't find out",
                "player:Seems kind of crazy",
                "npc:organised little devils",
                "npc:You go on ahead",
                "player:They don't even have thumbs",
            })
            t.ticks(2)

            zoo_to_lumbridge(t, "tuxedoTimeLumbridge")
            t.exec("goto-tuxedoTimeLumbridge", t.player.goto_tile, 3211, 3263, 0) -- inside the pen, beside Larry
            t.exec("tuxedoTimeLumbridge", t.player.talk_to, "peng_larry_zoo", 3)
            t.exec("tuxedoTimeLumbridge-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(4)
            t.check("tuxedoTimeLumbridge.suit", select(2, t.var.server("varb3306_peng_transmog")) == 1,
                "peng_transmog=" .. tostring(select(2, t.var.server("varb3306_peng_transmog"))))

            t.exec("goto-talkToThing", t.player.goto_tile, 3201, 3268, 0)
            t.exec("talkToThing", t.player.talk_to, "sheep_shearer_the_thing", 3)
            t.exec("talkToThing-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(2)
            greet("emoteAtPenguinInLumbridge")
            t.chat.drain({ stop_at = "none", max_pages = 40 })
            t.ticks(2)
            t.expect("quest.stage.zoo_return", t.quest.expect_stage("zoo_return"))
            -- LEG 2 END
            local _, etile = t.world.tile()
            local _, stage = t.quest.stage()
            t.note("leg.2.end: player at " .. etile.x .. "," .. etile.z .. " level " .. tostring(select(2, t.world.level()))
                .. ", varb3293_peng_quest=" .. tostring(stage) .. ", peng_report_1 x" .. tostring(select(2, t.inv.count("peng_report_1")))
                .. ", suit worn (transmog)=" .. tostring(select(2, t.var.server("varb3306_peng_transmog"))))
        end },
        { name = "phrase_and_noodle", run = function(t)
            -- LEG 3 BEGIN: returnToZooPenguin
            local emote_names = { "shiver", "spin", "clap", "bow", "cheer", "wave", "preen", "flap" }
            local function greet(name)
                for i = 1, 3 do
                    local _, v = t.var.server("varb330" .. (i - 1) .. "_peng_emote_" .. i)
                    local en = emote_names[v]
                    local _, w = t.ui.widget("peng_emote:peng_emote_" .. en)
                    t.check(name .. ".emote" .. i .. ".widget", w ~= nil, "expected emote " .. i .. " = " .. tostring(en))
                    local ir = t.ui.invoke(w, 0)
                    t.ticks(3)
                    local _, chk = t.var.server("varb3307_peng_emote_check")
                    t.check(name .. ".emote" .. i, ir == "ok" or ir == nil,
                        "clicked " .. en .. " (invoke " .. tostring(ir) .. "), varb3307_peng_emote_check=" .. tostring(chk))
                    t.chat.drain({ stop_at = "none", max_pages = 4 })
                end
            end
            local function transmog() return select(2, t.var.server("varb3306_peng_transmog")) end
            local function suit_up(name, larry)
                t.exec(name, t.player.talk_to, larry, 3)
                t.exec(name .. "-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
                t.ticks(4)
                t.check(name .. ".suit", transmog() == 1, "peng_transmog=" .. tostring(transmog()))
            end
            local function suit_off(name, larry)
                t.exec(name, t.player.talk_to, larry, 1)
                t.exec(name .. "-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
                t.ticks(3)
                t.check(name .. ".off", transmog() == 0, "peng_transmog=" .. tostring(transmog()))
            end

            t.ticks(2)
            t.note("leg.3.start: stage " .. tostring(select(2, t.quest.stage())) .. " suit=" .. tostring(transmog())
                .. " raw_cod=" .. tostring(select(2, t.inv.count("raw_cod"))) .. " swamp_tar=" .. tostring(select(2, t.inv.count("swamp_tar")))
                .. " feather=" .. tostring(select(2, t.inv.count("feather"))))
            -- Larry stands 16 tiles east of the farm tile the player ends leg 2 on, off the viewport: walk to him (plain travel)
            t.check("returnToZooPenguin-larryWalk", t.player.walk_to(3209, 3264, 20) == "ok", "walking to Larry at 3212,3263")
            t.ticks(3)
            suit_off("returnToZooPenguin-larrySuitOff", "peng_larry_zoo")
            t.exec("returnToZooPenguin-larry", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("returnToZooPenguin-larry-dialog", t.chat.play, {
                "player:The penguins won't talk to me",
                "npc:No, I keep trying",
                "npc:Well, since the penguin",
                "npc:I can teleport us",
                "options",
                "choose:Yes",
            })
            t.ticks(8)
            local _, ztile = t.world.tile()
            t.check("returnToZooPenguin-larry.zoo", ztile.x >= 2580 and ztile.x <= 2620 and ztile.z >= 3250 and ztile.z <= 3290,
                "teleported to " .. ztile.x .. "," .. ztile.z)

            suit_up("returnToZooPenguin-tuxedo", "peng_larry_zoo")
            zoo_pen_in(t, "returnToZooPenguin-penDoorIn")
            t.exec("returnToZooPenguin", t.player.talk_to, "peng_zoo", 1)
            t.exec("returnToZooPenguin-dialog", t.chat.play, {
                "npc:Yes, comrade?",
                "options",
                "choose:/Lumbridge refuse/",
                "player:The penguins in Lumbridge refuse",
                "npc:So, tell them",
                "player:Pesca",
                "npc:Pescaling Pax",
                "player:Oh, where",
                "npc:Comrade!",
                "player:You sound pretty homesick",
                "npc:If only",
                "player:Gee, I sure",
                "npc:How do you not know",
                "options",
                "choose:/I forgot/",
                "player:Uh, I forgot",
                "npc:That was very careless",
                "player:I need that phrase",
                "npc:I see you have some raw cod",
                "options",
                "choose:Sure!",
                "player:Sure!",
                "npc:Thank you comrade",
            })
            t.ticks(3)
            t.expect("quest.stage.thing_return", t.quest.expect_stage("thing_return"))
            t.check("returnToZooPenguin.cod", select(2, t.inv.count("raw_cod")) == 0,
                "raw_cod x" .. tostring(select(2, t.inv.count("raw_cod"))) .. " traded for the phrase")

            -- out of the pen and on foot to Lumbridge; leaving the zoo takes the suit off
            -- (coldwar_shared.rs2 [timer,coldwar_suit]), so Lumbridge Larry puts it on again
            zoo_pen_out(t, "returnToThing-penDoorOut")
            zoo_to_lumbridge(t, "returnToThing")
            t.check("returnToThing.suitWoreOff", transmog() == 0, "peng_transmog=" .. tostring(transmog())
                .. " after the walk from the zoo (the spell wears off as you leave the penguins behind)")
            t.exec("goto-returnToThing-larry", t.player.goto_tile, 3211, 3263, 0)
            suit_up("returnToThing-tuxedo", "peng_larry_zoo")
            t.exec("goto-returnToThing", t.player.goto_tile, 3201, 3268, 0)
            t.exec("returnToThing", t.player.talk_to, "sheep_shearer_the_thing", 3)
            t.exec("returnToThing-dialog", t.chat.drain, { stop_at = "none", max_pages = 14 })
            t.ticks(3)
            t.expect("quest.stage.fred", t.quest.expect_stage("fred"))

            -- Fred needs the suit OFF (coldwar_lumbridge.rs2:93)
            t.exec("goto-fredTheFarmer-larry", t.player.goto_tile, 3211, 3263, 0)
            suit_off("fredTheFarmer-suitOff", "peng_larry_zoo")
            -- out of the sheep pen, round to Fred's yard gate, in through the yard gate and the house door
            -- (as sheep.lua / onesmallfavour.lua: reach.py 3214,3262 -> 3189,3281 REACH closed-doors len=46)
            pen_out(t, "fredTheFarmer.penGateOut")
            t.exec("goto-fredTheFarmer", t.player.goto_tile, 3188, 3281, 0) -- open ground north of Fred's yard gate
            t.exec("fredTheFarmer.yardGateIn", t.player.pass_door, {
                closed = "qip_sheep_shearer_fencegate_l", open = "qip_sheep_shearer_openfencegate_l",
                at = { 3188, 3279, 0 }, near = { 3188, 3280 }, far = { 3188, 3277 } })
            t.exec("fredTheFarmer.houseDoorIn", t.player.pass_door, {
                closed = "qip_sheep_shearer_poordoor", open = "qip_sheep_shearer_poordooropen",
                at = { 3189, 3275, 0 }, near = { 3189, 3276 }, far = { 3189, 3273 } })
            t.exec("fredTheFarmer", t.player.talk_to, "fred_the_farmer", 1)
            t.exec("fredTheFarmer-dialog", t.chat.play, {
                "npc:What are you doing on my land",
                "options",
                "choose:I need to talk to you about penguins.",
                "player:I need to talk to you about penguins",
                "npc:About what now",
                "options",
                "choose:Bully Fred",
                "player:Hey Fred",
                "npc:What are you shouting about",
                "player:I mean it Fred",
                "player:or you might find",
                "npc:Is this some kind of joke",
            })
            t.chat.drain({ stop_at = "none", max_pages = 6 })
            t.ticks(3)
            t.expect("quest.stage.outpost_info", t.quest.expect_stage("outpost_info"))

            -- out of Fred's house and yard, to the cow field north of the farm, in by its gate
            t.exec("stealCowbell.houseDoorOut", t.player.pass_door, {
                closed = "qip_sheep_shearer_poordoor", open = "qip_sheep_shearer_poordooropen",
                at = { 3189, 3275, 0 }, near = { 3189, 3274 }, far = { 3189, 3277 } })
            t.exec("stealCowbell.yardGateOut", t.player.pass_door, {
                closed = "qip_sheep_shearer_fencegate_l", open = "qip_sheep_shearer_openfencegate_l",
                at = { 3188, 3279, 0 }, near = { 3188, 3278 }, far = { 3188, 3281 } })
            t.exec("goto-stealCowbell", t.player.goto_tile, 3176, 3313, 0) -- open ground south of the cow field gate
            t.exec("stealCowbell.cowFieldIn", t.player.pass_door, {
                closed = "rustic_fencegate_l", open = "rustic_openfencegate_l",
                at = { 3176, 3315, 0 }, near = { 3176, 3314 }, far = { 3175, 3317 } })
            t.exec("stealCowbell.walk", t.player.walk_to, 3172, 3319, 20)
            local tries = 0
            while select(2, t.inv.count("peng_cowbell")) < 1 and tries < 25 do
                tries = tries + 1
                t.player.click_loc("fat_cow", 2)
                t.ticks(5)
            end
            t.check("stealCowbell", select(2, t.inv.count("peng_cowbell")) >= 1,
                "peng_cowbell x" .. tostring(select(2, t.inv.count("peng_cowbell"))) .. " after " .. tries .. " steal attempt(s)")

            -- out of the cow field, back into the sheep pen by its east gate
            t.exec("askThingAboutOutpost.cowFieldOut-walk", t.player.walk_to, 3176, 3316, 20)
            t.exec("askThingAboutOutpost.cowFieldOut", t.player.pass_door, {
                closed = "rustic_fencegate_l", open = "rustic_openfencegate_l",
                at = { 3176, 3315, 0 }, near = { 3176, 3316 }, far = { 3176, 3313 } })
            pen_in(t, "askThingAboutOutpost.penGateIn")
            t.exec("goto-askThingAboutOutpost-larry", t.player.goto_tile, 3211, 3263, 0)
            suit_up("askThingAboutOutpost-tuxedo", "peng_larry_zoo")
            t.exec("goto-askThingAboutOutpost", t.player.goto_tile, 3201, 3268, 0)
            t.exec("askThingAboutOutpost", t.player.talk_to, "sheep_shearer_the_thing", 3)
            t.exec("askThingAboutOutpost-dialog", t.chat.play, {
                "npc:Is the Farmer an agent",
                "options",
                "choose:The Farmer is harmless.",
                "player:The Farmer is harmless",
                "npc:The sheep?",
                "player:Exactly!",
                "npc:You have a good point",
                "npc:Well, off you go",
                "player:Hey wait",
                "npc:You are one clueless",
                "npc:When you try",
                "npc:The password is cabbage",
            })
            t.ticks(3)
            t.expect("quest.stage.iceberg_kgp", t.quest.expect_stage("iceberg_kgp"))

            t.exec("goto-tellLarryAboutOutpost", t.player.goto_tile, 3211, 3263, 0)
            suit_off("tellLarryAboutOutpost-suitOff", "peng_larry_zoo")
            t.exec("tellLarryAboutOutpost", t.player.talk_to, "peng_larry_zoo", 1)
            t.exec("tellLarryAboutOutpost-dialog", t.chat.play, {
                "player:Haa haa",
                "npc:What? How?",
                "player:I just convinced",
                "npc:They are?",
                "player:No! They're just sheep",
                "npc:What a good plan",
                "player:Larry, we need",
                "player:I know where their outpost",
                "npc:Great",
                "options",
                "choose:Yes",
            })
            t.ticks(8)
            local _, itile = t.world.tile()
            t.check("tellLarryAboutOutpost.iceberg", itile.z >= 3950 and select(2, t.world.level()) == 1,
                "teleported to " .. itile.x .. "," .. itile.z .. " level " .. tostring(select(2, t.world.level())))
            t.ticks(2)

            suit_up("kgpAgent-tuxedo", "peng_larry_ice")
            t.exec("goto-kgpAgent", t.player.goto_tile, 2642, 4006, 1)
            t.ticks(4)
            t.exec("kgpAgent", t.player.talk_to, "peng_kgp", 1)
            t.exec("kgpAgent-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(2)
            greet("kgpAgent")
            t.chat.drain({ stop_at = "none", max_pages = 40 })
            t.ticks(2)
            t.expect("quest.stage.noodle1", t.quest.expect_stage("noodle1"))

            t.exec("goto-noodle1", t.player.goto_tile, 2644, 4005, 1)
            t.ticks(3)
            t.exec("noodle1", t.player.talk_to, "peng_noodle", 1)
            t.exec("noodle1-dialog", t.chat.drain, { stop_at = "none", max_pages = 20 })
            t.ticks(3)
            t.expect("quest.stage.noodle2", t.quest.expect_stage("noodle2"))

            t.exec("noodle2", t.player.talk_to, "peng_noodle", 1)
            t.exec("noodle2-dialog", t.chat.play, {
                "npc:Yuv go' the stuff",
                "options",
                "choose:Yeah, I got it.",
                "player:Yeah, I got it",
                "npc:Oi, cheers",
            })
            t.ticks(3)
            t.expect("quest.stage.kgp_again", t.quest.expect_stage("kgp_again"))
            t.check("noodle2.items", select(2, t.inv.count("peng_id")) >= 1 and select(2, t.inv.count("peng_report_3")) >= 1
                and select(2, t.inv.count("swamp_tar")) == 0 and select(2, t.inv.count("feather")) == 0,
                "peng_id x" .. tostring(select(2, t.inv.count("peng_id"))) .. ", peng_report_3 x" .. tostring(select(2, t.inv.count("peng_report_3")))
                .. ", swamp_tar x" .. tostring(select(2, t.inv.count("swamp_tar"))) .. ", feather x" .. tostring(select(2, t.inv.count("feather"))))
            -- LEG 3 END
            local _, etile = t.world.tile()
            local _, stage = t.quest.stage()
            t.note("leg.3.end: player at " .. etile.x .. "," .. etile.z .. " level " .. tostring(select(2, t.world.level()))
                .. ", varb3293_peng_quest=" .. tostring(stage) .. ", suit worn (transmog)=" .. tostring(transmog())
                .. ", peng_id x" .. tostring(select(2, t.inv.count("peng_id"))) .. ", peng_report_1 x" .. tostring(select(2, t.inv.count("peng_report_1")))
                .. ", peng_report_2 x" .. tostring(select(2, t.inv.count("peng_report_2"))) .. ", peng_report_3 x" .. tostring(select(2, t.inv.count("peng_report_3")))
                .. ", peng_cowbell x" .. tostring(select(2, t.inv.count("peng_cowbell"))))
        end },
        { name = "debrief_and_course", run = function(t)
            -- LEG 4 BEGIN: kgpAgent2
            t.ticks(2)
            t.exec("kgpAgent2", t.player.talk_to, "peng_kgp", 1)
            t.exec("kgpAgent2-dialog", t.chat.play, {
                "npc:Let's see your ID",
                "npc:All right, you can go in",
                "npc:Once inside",
            })
            t.ticks(3)
            t.expect("quest.stage.debrief", t.quest.expect_stage("debrief"))

            t.exec("enterAvalanche", t.player.click_loc, "peng_aval_l", 1)
            t.ticks(6)
            local _, atile = t.world.tile()
            t.check("enterAvalanche.inside", atile.z > 10000, "inside the outpost at " .. atile.x .. "," .. atile.z)

            -- the first room west of the entrance corridor: in by its door (peng_base_door 2654,10382, west edge)
            t.exec("kgpAgentInAvalanche.walk", t.player.walk_to, 2655, 10382, 20)
            t.exec("kgpAgentInAvalanche.doorIn", t.player.pass_door, { closed = "peng_base_door", open = "peng_base_door_open",
                at = { 2654, 10382, 0 }, near = { 2655, 10382 }, far = { 2651, 10382 } })
            t.ticks(2)
            t.exec("kgpAgentInAvalanche", t.player.talk_to, "peng_kgp", 1)
            t.exec("kgpAgentInAvalanche-dialog", t.chat.drain, { stop_at = "none", max_pages = 30 })
            t.ticks(3)
            t.expect("quest.stage.agility_ready", t.quest.expect_stage("agility_ready"))
            t.check("kgpAgentInAvalanche.reports", select(2, t.inv.count("peng_report_1")) == 0
                and select(2, t.inv.count("peng_report_2")) == 0 and select(2, t.inv.count("peng_report_3")) == 0,
                "the three mission reports were handed over")

            -- west through the agility door (peng_base_door_agility 2640,10382, west edge) and up the hall
            -- to the course's double door
            t.exec("enterAgilityCourse.walk", t.player.walk_to, 2641, 10382, 20)
            t.exec("enterAgilityCourse.doorIn", t.player.pass_door, { closed = "peng_base_door_agility",
                open = "peng_base_door_agility_open", at = { 2640, 10382, 0 }, near = { 2641, 10382 }, far = { 2637, 10382 } })
            t.exec("enterAgilityCourse.hall", t.player.walk_to, 2633, 10403, 30)
            t.ticks(2)
            t.exec("enterAgilityCourse", t.player.click_loc, "peng_base_double_door_mid_agility", 1)
            t.ticks(6)
            t.expect("quest.stage.agility_done", t.quest.expect_stage("agility_done"))
            local _, ctile = t.world.tile()
            t.check("enterAgilityCourse.outside", ctile.z < 4100 and select(2, t.world.level()) == 1,
                "on the penguin course at " .. ctile.x .. "," .. ctile.z .. " level " .. tostring(select(2, t.world.level())))
            local function transmog() return select(2, t.var.server("varb3306_peng_transmog")) end
            local function agility_state() return select(2, t.var.server("varb3305_peng_agility_state")) end
            local function at() local _, p = t.world.tile(); return p.x .. "," .. p.z .. "," .. tostring(select(2, t.world.level())) end

            -- agilityEnterWater / agilityExitWater: the ledge climbs down into the wading water (seam2)
            t.player.walk_to(2636, 4054, 12)
            t.ticks(3)
            t.check("agilityEnterWater", select(2, t.world.level()) == 0, "walked onto the ledge 2636,4054; now at " .. at() .. " (level 0 = in the water)")
            t.player.walk_to(2630, 4055, 20)
            t.ticks(3)
            t.check("agilityWade", select(2, t.world.tile()).x <= 2631 and select(2, t.world.level()) == 0, "waded past the crushers to " .. at())
            t.exec("agilityExitWater", t.player.click_loc, "peng_agility_crushcourse_stepstone01", 1)
            t.ticks(4)
            if select(2, t.world.level()) ~= 1 then
                t.exec("agilityExitWater-retry", t.player.click_loc, "peng_agility_crushcourse_stepstone01", 1)
                t.ticks(4)
            end
            t.check("agilityExitWater.out", select(2, t.world.level()) == 1, "out of the water, now at " .. at())

            -- agilityJumpStones: seven stones (stone 7 is an aploc)
            for i = 1, 7 do
                t.exec("agilityJumpStones-" .. i, t.player.click_loc, "peng_jump_stone_clickzone_0" .. i, 1)
                t.ticks(3)
            end
            t.check("agilityJumpStones.done", agility_state() == 1, "varb3305_peng_agility_state=" .. tostring(agility_state()) .. " at " .. at())

            -- agilityTreadSoftly: four icicle pillars, west to east (the first press can flake: retry)
            -- pillar tiles from m41_63.jl2 (21134 at level 1): x 2644, 2652, 2658, 2662
            local pillars = { { x = 2644, z = 4083 }, { x = 2652, z = 4079 }, { x = 2658, z = 4082 }, { x = 2662, z = 4082 } }
            for i = 1, 4 do
                local nm = "agilityTreadSoftly-" .. i
                -- the first press can flake (pickset held=false, gaps-world): press again, record the press that landed
                local res, det = t.player.click_loc("peng_iciclepillar_clickzone", 1, { at = pillars[i] })
                if res ~= "ok" then
                    t.ticks(2)
                    res, det = t.player.click_loc("peng_iciclepillar_clickzone", 1, { at = pillars[i] })
                end
                t.ticks(4)
                t.check(nm, res == "ok", "pillar " .. pillars[i].x .. "," .. pillars[i].z .. ": " .. tostring(res) .. " " .. tostring(det)
                    .. "; now at " .. at())
            end
            t.check("agilityTreadSoftly.done", agility_state() == 2, "varb3305_peng_agility_state=" .. tostring(agility_state()) .. " at " .. at())

            -- agilityCrossIce
            t.exec("agilityCrossIce", t.player.click_loc, "peng_agility_slippery_glitters01", 1)
            t.ticks(8)
            t.check("agilityCrossIce.done", agility_state() == 3, "varb3305_peng_agility_state=" .. tostring(agility_state()) .. " at " .. at())

            -- agilityDone
            t.exec("agilityDone", t.player.talk_to, "peng_agility_instructor", 1)
            t.exec("agilityDone-dialog", t.chat.drain, { stop_at = "none", max_pages = 10 })
            t.ticks(3)
            t.expect("quest.stage.army_report", t.quest.expect_stage("army_report"))

            -- back through the fence gate and the door into the corridor, where taking the suit off
            -- throws the player out beside Larry (coldwar_shared.rs2 coldwar_suit_unequip_click)
            t.exec("agilityGate", t.player.click_loc, "peng_agility_fencing_door", 1)
            t.ticks(4)
            t.exec("agilityBackDoor", t.player.click_loc, "peng_cliffwall_door_mid", 1)
            t.ticks(5)
            local _, btile = t.world.tile()
            t.check("agilityBackDoor.inside", btile.z > 10000, "back in the outpost corridor at " .. at())

            t.exec("removeSuit", t.player.unequip, "peng_suit_unwound")
            t.ticks(5)
            t.check("removeSuit.thrown", transmog() == 0 and select(2, t.world.tile()).z < 4100,
                "suit off (transmog=" .. tostring(transmog()) .. "), standing at " .. at())

            -- tellLarryAboutArmy
            t.exec("tellLarryAboutArmy", t.player.talk_to, "peng_larry_ice", 1)
            t.exec("tellLarryAboutArmy-dialog", t.chat.drain, { stop_at = "none", max_pages = 20 })
            t.ticks(3)
            t.expect("quest.stage.pingpong_go", t.quest.expect_stage("pingpong_go"))

            -- enterAvalanche2: back in the suit (Larry op 3), through the avalanche as a penguin
            t.exec("enterAvalanche2-tuxedo", t.player.talk_to, "peng_larry_ice", 3)
            t.exec("enterAvalanche2-tuxedo-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(4)
            t.check("enterAvalanche2.suit", transmog() == 1, "peng_transmog=" .. tostring(transmog()))
            t.exec("enterAvalanche2", t.player.click_loc, "peng_aval_l", 1)
            t.ticks(6)
            local _, itile = t.world.tile()
            t.check("enterAvalanche2.inside", itile.z > 10000, "inside the outpost at " .. at())

            -- pingPong1: the room to the east
            t.exec("pingPong1.walk", t.player.walk_to, 2661, 10396, 30)
            t.exec("pingPong1.doorIn", t.player.pass_door, { closed = "peng_base_door_bard", open = "peng_base_door_bard_open",
                at = { 2662, 10396, 0 }, near = { 2661, 10396 }, far = { 2665, 10396 } })
            t.ticks(2)
            t.exec("pingPong1", t.player.talk_to, "peng_ping", 1)
            t.exec("pingPong1-dialog", t.chat.drain, { stop_at = "none", max_pages = 40 })
            t.ticks(4)
            local _, stage = t.quest.stage()
            t.note("leg.4.end: player at " .. at() .. ", varb3293_peng_quest=" .. tostring(stage)
                .. ", suit worn (transmog)=" .. tostring(transmog()) .. ", peng_id x" .. tostring(select(2, t.inv.count("peng_id")))
                .. ", peng_cowbell x" .. tostring(select(2, t.inv.count("peng_cowbell"))) .. "; next: instruments (bongos, cowbell) for Ping and Pong")
            -- LEG 4 END
        end },
        { name = "bongos_and_war_room", run = function(t)
            -- LEG 5 BEGIN: makeBongos
            local function transmog() return select(2, t.var.server("varb3306_peng_transmog")) end
            local function at() local _, p = t.world.tile(); return p.x .. "," .. p.z .. "," .. tostring(select(2, t.world.level())) end
            local function stage() return select(2, t.quest.stage()) end
            local function count(name) return select(2, t.inv.count(name)) end
            t.ticks(2)

            -- removePenguinSuitForBongos: the suit comes off inside the outpost, which throws the player to Larry
            t.exec("removePenguinSuitForBongos", t.player.unequip, "peng_suit_unwound")
            t.ticks(5)
            t.check("removePenguinSuitForBongos.off", transmog() == 0 and select(2, t.world.tile()).z < 4100,
                "suit off (transmog=" .. tostring(transmog()) .. "), standing at " .. at())

            -- makeBongos: mahogany plank on leather (Crafting 30, 25 XP)
            t.exec("makeBongos", t.player.use_item_on_item, "plank_mahogany", "leather")
            t.check("makeBongos.bongos", t.inv.await("peng_bongos", 1, 10) == "ok",
                "peng_bongos x" .. count("peng_bongos") .. ", plank x" .. count("plank_mahogany") .. ", leather x" .. count("leather"))

            -- enterAvalanche3: back in the suit (Larry op 3), through the avalanche
            t.exec("enterAvalanche3-tuxedo", t.player.talk_to, "peng_larry_ice", 3)
            t.exec("enterAvalanche3-tuxedo-dialog", t.chat.drain, { stop_at = "none", max_pages = 6 })
            t.ticks(4)
            t.check("enterAvalanche3.suit", transmog() == 1, "peng_transmog=" .. tostring(transmog()))
            t.exec("enterAvalanche3", t.player.click_loc, "peng_aval_l", 1)
            t.ticks(6)
            local _, itile = t.world.tile()
            t.check("enterAvalanche3.inside", itile.z > 10000, "inside the outpost at " .. at())

            -- pingPong2 / pingPong3: hand over the bongos and the cowbell
            t.exec("pingPong2.walk", t.player.walk_to, 2661, 10396, 30)
            t.exec("pingPong2.doorIn", t.player.pass_door, { closed = "peng_base_door_bard", open = "peng_base_door_bard_open",
                at = { 2662, 10396, 0 }, near = { 2661, 10396 }, far = { 2665, 10396 } })
            t.ticks(2)
            t.exec("pingPong2", t.player.talk_to, "peng_ping", 1)
            t.exec("pingPong3", t.chat.play, {
                "npc:Man, did you bring the instruments",
                "options",
                "choose:Yes.",
                "player:Yes.",
                "npc:Dude, good job on the instruments",
            })
            t.exec("pingPong3-dialog", t.chat.drain, { stop_at = "none", max_pages = 40 })
            t.ticks(4)
            t.expect("quest.stage.control_room", t.quest.expect_stage("control_room"))
            t.check("pingPong3.instruments", count("peng_bongos") == 0 and count("peng_cowbell") == 0,
                "bongos x" .. count("peng_bongos") .. ", cowbell x" .. count("peng_cowbell") .. " handed over")

            -- openControlDoor: the control panel in the booth, the guard is gone
            t.exec("openControlDoor.doorOut-walk", t.player.walk_to, 2663, 10396, 20)
            t.exec("openControlDoor.doorOut", t.player.pass_door, { closed = "peng_base_door_bard", open = "peng_base_door_bard_open",
                at = { 2662, 10396, 0 }, near = { 2663, 10396 }, far = { 2660, 10396 } })
            t.exec("openControlDoor.walk", t.player.walk_to, 2655, 10406, 30)
            t.ticks(2)
            t.exec("openControlDoor", t.player.click_loc, "peng_base_booth_front", 1)
            t.ticks(3)
            t.check("openControlDoor.msg", t.msg.expect("massive blast doors grind open") == "ok",
                "blast doors opened at " .. at())

            -- enterWarRoom: through the blast door to the war room door; the capture follows
            t.exec("enterWarRoom", t.player.click_loc, "peng_base_door", 1, { at = { 2671, 10418 } })
            t.exec("enterWarRoom-dialog", t.chat.drain, { stop_at = "none", max_pages = 30 })
            t.ticks(4)
            t.expect("quest.stage.icelords", t.quest.expect_stage("icelords"))
            t.check("enterWarRoom.captured", transmog() == 0, "captured, suit confiscated (transmog=" .. tostring(transmog()) .. ") at " .. at())

            -- killIcelords: real combat in the pen, the scimitar wielded, lobsters eaten
            t.exec("killIcelords-wield", t.player.equip, "rune_scimitar")
            t.ticks(2)
            local atk_before = select(2, t.skill.read("attack")).experience
            local kills = 0
            local lowest_hp = nil
            local _, food_start = t.inv.count("lobster")
            -- Single-way combat: an Ice Lord that has claimed the player makes the server refuse a swing at
            -- any other ("I'm already under attack.", verbs-combat: `refused`, meaning two), and which one
            -- claims first is the pen's own aggression, so the one fought is the first that accepts.
            local ICELORDS = { "peng_icelord_warrior01", "peng_icelord_warrior02", "peng_icelord_warrior03", "peng_icelord_warrior04" }
            while stage() == 125 and kills < 6 do
                kills = kills + 1
                local r, d, refused = nil, nil, {}
                for _, sym in ipairs(ICELORDS) do
                    r, d = t.player.attack(sym, 2, 20)
                    if r == "ok" or r == "timeout" then break end
                    refused[#refused + 1] = sym .. " -> " .. tostring(r)
                end
                t.check("killIcelords.attack" .. kills, r == "ok" or r == "timeout", tostring(r) .. " " .. tostring(d)
                    .. (#refused > 0 and ("; not engaged first: " .. table.concat(refused, ", ")) or ""))
                local _, dd = t.exec("killIcelords.dead" .. kills, t.npc.await_dead_engaged, 200, 20, { eat = { item = "lobster", below = 40 } })
                local low = tonumber(string.match(tostring(dd), "lowest hp (%d+)/") or "")
                if low ~= nil and (lowest_hp == nil or low < lowest_hp) then lowest_hp = low end
                t.ticks(8)
            end
            local food_r, food_left = t.inv.count("lobster")
            t.check("killIcelords.margin", lowest_hp ~= nil and lowest_hp >= 18 and food_r == "ok" and food_left >= 1,
                kills .. " fight(s), lowest hp " .. tostring(lowest_hp) .. "/70, lobsters left " .. tostring(food_left)
                    .. " of " .. tostring(food_start) .. " carried in (margin: lowest hp >= 18, a quarter of 70, AND food left)")
            t.check("killIcelords.opened", stage() == 130, "after " .. kills .. " kill(s) varb3293_peng_quest=" .. tostring(stage()))
            local atk_gain = select(2, t.skill.read("attack")).experience - atk_before
            t.check("killIcelords.xp", atk_gain >= 40 * kills,
                "Attack XP +" .. atk_gain .. " over " .. kills .. " kill(s) (the pen awards 40 per kill, plus combat xp)")

            -- exitIcelordPen: the door west of the pen
            t.exec("exitIcelordPen", t.player.click_loc, "peng_icelord_pen_door", 1)
            t.ticks(4)
            t.check("exitIcelordPen.out", select(2, t.world.tile()).x <= 2638, "through the pen door (peng_icelord_pen_door 2639,10424, west edge), standing at " .. at())

            -- useChasm: spat out beside Larry
            t.exec("useChasm", t.player.click_loc, "peng_base_chasm", 1)
            t.ticks(5)
            local _, ctile = t.world.tile()
            t.check("useChasm.surface", ctile.z < 4100, "on the iceberg at " .. at())

            -- tellLarryPlans: the hand-in, reward XP asserted at the literal documented amounts
            t.exec("tellLarryPlans", t.player.talk_to, "peng_larry_ice", 1)
            local _, before = t.skill.snapshot()
            t.exec("tellLarryPlans-dialog", t.chat.drain, { stop_at = "none", max_pages = 30 })
            t.ticks(4)
            local rr, rx = t.scroll.reward_xp("agility")
            t.check("quest.reward.agility", rr == "ok" and rx == 5000, "scroll Agility XP " .. tostring(rx) .. " (documented 5000)")
            t.expect("quest.xp.agility", t.skill.expect_gain("agility", 5000, before))
            t.expect("quest.xp.crafting", t.skill.expect_gain("crafting", 2000, before))
            t.expect("quest.xp.construction", t.skill.expect_gain("construction", 1500, before))
            t.quest.expect_complete()
            t.expect("leg.5.quiet", "ok", "player at " .. at() .. ", quest complete, no dialogue open")
            -- LEG 5 END
        end },
    },
}
