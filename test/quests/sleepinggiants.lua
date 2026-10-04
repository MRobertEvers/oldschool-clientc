-- Sleeping Giants. Written in legs (docs/quest_authoring/relay.md). Leg 1: the strike, the
-- cave, the three repairs, Kovac after them. Guide: SleepingGiants.java via ladder.py.
-- The foundry is a private instance during the quest (sleepinggiants.rs2:132): a checkpoint
-- cannot carry it, so every leg begins and ends at the cave mouth.

return {
    id = "sleepinggiants",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::sleepinggiants",              -- stages the quest at 0
        "::setlevel smithing 15",        -- guide: Smithing 15 to start (sleepinggiants.rs2:19)
        "::give oak_logs 3",             -- guide: trip hammer 1 + polishing wheel 2 (sg_repairs.rs2:10,100)
        "::give nails_bronze 10",        -- guide: 5 nails each for hammer and wheel
        "::give wool 1",                 -- guide: polishing wheel (sg_repairs.rs2:100)
        "::give chisel 1",               -- guide: grindstone (sg_repairs.rs2:55)
    },
    bind = {
        varp = "varb13902_sleeping_giants",
        constants = { not_started = 0, kovac = 5, repairs = 10, repairs_done = 15, after_repairs = 20, commission = 25, complete = 30 },
        row = "quest_sleepinggiants",
        display = "Sleeping Giants",
        points = 1,
    },
    legs = {
        { name = "foundry_repairs", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- LEG 1 BEGIN: takeHammer
            t.exec("goto-takeHammer", t.player.goto_tile, 3350, 3160, 0)
            t.exec("takeHammer", t.player.click_obj, "hammer", 3)
            t.inv.expect_has("hammer", 1)
            t.check("takeHammer.have", select(2, t.inv.count("hammer")) == 1, "hammer in pack: " .. tostring(select(2, t.inv.count("hammer"))))

            -- goToDesertPlateau: the Giants' Foundry minigame teleport lands here; plain travel.
            t.exec("goToDesertPlateau", t.player.goto_tile, 3362, 3148, 0)

            t.exec("strikeHillGiant", t.player.talk_to, "giants_foundry_kovac_multi_outside", 1)
            t.exec("strikeHillGiant-dialog", t.chat.play, {
                "npc:Wait", "npc:Do not attack", "player:What the", "npc:Do", "npc:do not", "npc:flig",
                "player:You don't want", "npc:Want", "npc:no, need", "npc:your help", "player:I must be dreaming",
                "npc:Please", "choose:Yes.",
                "player:Okay giant", "npc:Kovac", "player:Kovac... alright", "npc:Please", "npc:follow in",
                "player:Hang on", "npc:No", "npc:follow",
                "player:Woah", "player:Did you build", "npc:No", "npc:Kovac", "npc:accep", "player:Giants built",
                "npc:Yes", "npc:long ago", "player:How?", "npc:Before", "npc:giants differing", "npc:Now", "npc:not",
                "player:Huh?", "npc:Look", "mesbox:Kovac looks", "npc:Help fix", "player:You want me",
                "npc:Yes", "player:Okay, I'll help", "npc:Make", "npc:wheelpo", "player:Giants made",
                "player:That's incredible", "npc:Make", "npc:giants", "npc:remember", "player:Remember what",
                "npc:Giants", "npc:not just", "npc:killers", "npc:Kovac want", "npc:help giants remember",
                "player:I see", "player:Alright, Kovac", "npc:We fix tools",
            })
            t.ticks(2)
            t.expect("quest.stage.repairs.1", t.quest.expect_stage("repairs"))
            t.check("strikeHillGiant.where", true, "tile after dialogue " .. tostring(select(2, t.world.tile())))

            -- Stage 5 (guide speakToKovac) is the same conversation: Kovac's own pages ("Kovac.", "follow in.")
            -- were played above and stage 10 is read here (sleepinggiants.rs2:201-243).
            t.check("speakToKovac", true, "Kovac pages played in strikeHillGiant-dialog; stage now " .. tostring(select(2, t.quest.stage())))
            -- The conversation walks the player into the copy (spec-pending cutscene). The guide's
            -- enterFoundry is the cave click: leave by the exit loc, then enter by the guide's loc.
            t.exec("foundry-exit-loc", t.player.click_loc, "giants_foundry_exit", 1)
            t.ticks(3)
            t.exec("enterFoundry", t.player.click_loc, "giants_foundry_entrance", 1)
            t.ticks(3)
            t.check("enterFoundry.where", true, "tile inside " .. tostring(select(2, t.world.tile())))

            t.exec("fixPolishingStone", t.player.click_loc, "giants_foundry_polishing_wheel_quest_multi", 1)
            t.exec("fixPolishingStone-dialog", t.chat.play, {
                "player:Okay, what is this", "npc:Hmm", "mesbox:Kovac ponders", "mesbox:...", "npc:Kovac think he know",
                "player:And?", "npc:This polishing wheel", "npc:make thing shiny", "player:Looks like it's lost",
                "choose:Yes.",
                "player:Not my finest", "npc:Hmm", "npc:Kovac agree", "player:So when", "npc:Polishing sword",
                "npc:Create perfect", "npc:very good", "player:That makes sense",
            })
            t.ticks(2)

            t.exec("fixGrindstone", t.player.click_loc, "giants_foundry_grindstone_quest_multi", 1)
            t.exec("fixGrindstone-dialog", t.chat.play, {
                "player:What was this", "npc:Hmm", "npc:Kovac think it was", "player:I see. It looks", "npc:Kovac conquer",
                "player:Conquer", "npc:Hmm", "npc:Kovac agree", "player:Right... now", "choose:Yes.",
                "player:Well that required", "npc:Thank you small", "player:Didn't want", "npc:Kovac did not know",
                "npc:Kovac beggin", "player:Can you tell", "mesbox:Kovac takes", "mesbox:...", "npc:Grindstone important",
                "npc:We grind", "npc:make sword sharp", "player:So we use", "npc:Yes",
            })
            t.ticks(2)

            t.exec("fixHammer", t.player.click_loc, "giants_foundry_trip_hammer_quest_multi", 1)
            t.exec("fixHammer-dialog", t.chat.play, {
                "player:I think this has seen", "npc:Hmm", "npc:big hammer", "player:Right... and", "npc:Hit things",
                "player:Very perceptive", "player:Let me take", "choose:Yes.",
                "player:That looks like", "npc:Hmm", "npc:that look right", "player:What's it supposed",
                "mesbox:Kovac takes", "mesbox:...", "npc:Kovac think Kovac figure", "player:Go on",
                "npc:Trip hammer power", "npc:Water spin", "npc:Hammer fall", "player:That makes sense", "npc:Mhm",
            })
            t.ticks(2)
            t.expect("quest.stage.repairs_done", t.quest.expect_stage("repairs_done"))

            t.exec("speakToKovacAfterRepairs", t.player.talk_to, "giants_foundry_kovac_multi", 1)
            t.exec("speakToKovacAfterRepairs-dialog", t.chat.play, {
                "player:That's all the tools", "npc:Yes", "npc:Kovac begin", "player:I've noticed", "npc:Kovac know what",
                "npc:but sometime", "player:That's alright", "npc:Kovac agree", "player:Yes! I'm itching",
                "npc:Kovac get few", "npc:then we can start",
            })
            t.ticks(2)
            t.expect("quest.stage.after_repairs", t.quest.expect_stage("after_repairs"))

            -- Quiet boundary: leave the copy (a checkpoint cannot carry an instance).
            t.exec("leg.1.leave", t.player.click_loc, "giants_foundry_exit", 1)
            t.ticks(4)
            local _, tile = t.world.tile()
            local _, stage = t.var.server("varb13902_sleeping_giants")
            local held = {}
            for slot = 0, 27 do
                local r, cell = t.inv.slot(slot)
                if r == "ok" and cell.name ~= "" and cell.count ~= 0 then held[#held + 1] = cell.name .. "x" .. cell.count end
            end
            t.check("leg.1.end", true, "tile=" .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level)) .. " stage=" .. tostring(stage) .. " inv=" .. table.concat(held, ","))
            -- LEG 1 END
        end },
        { name = "foundry_commission", run = function(t)
            t.ticks(3)
            -- LEG 2 BEGIN: enterFoundryToMakeWeapon
            t.exec("enterFoundryToMakeWeapon", t.player.click_loc, "giants_foundry_entrance", 1)
            t.ticks(3)
            t.check("enterFoundryToMakeWeapon.where", true, "tile inside " .. tostring(t.world.tile and (select(2, t.world.tile()) or {}).x) .. "," .. tostring((select(2, t.world.tile()) or {}).z) .. " stage " .. tostring(select(2, t.quest.stage())))

            t.exec("speakToKovacContinue", t.player.talk_to, "giants_foundry_kovac_multi", 1)
            t.exec("speakToKovacContinue-dialog", t.chat.play, {
                "player:Okay Kovac, I think", "npc:Hmm", "npc:Kovac feel", "npc:Kovac think you", "player:So what do I need",
                "npc:First", "npc:you will need", "player:Where can I", "npc:Kovac will supply", "npc:Kovac already",
                "player:Okay, so what", "npc:Kovac need a", "player:Great", "player:How do I make",
                "npc:Kovac think we need to fill the...", "npc:how's it called", "npc:the big stone", "player:Oh, you mean",
                "npc:Yes, thank", "npc:Kovac think we need to fill the crucible", "npc:Kovac has been", "npc:They're in",
                "npc:Use bars", "npc:Crucible can", "npc:Load metals", "npc:While you", "npc:Kovac think about next",
            })
            t.ticks(2)
            t.expect("quest.stage.commission", t.quest.expect_stage("commission"))

            t.exec("searchCrate", t.player.click_loc, "giants_foundry_supply_box_multi", 1)
            t.exec("searchCrate-dialog", t.chat.play, { "mesbox:You rummage", "mesbox:You find", "choose:Yes." })
            t.ticks(2)
            t.check("searchCrate.kind", true, "dialogue now " .. tostring(t.chat.kind()))
            t.chat.continue_()
            t.ticks(2)
            t.check("searchCrate.have", select(2, t.inv.count("bronze_bar")) == 8 and select(2, t.inv.count("iron_bar")) == 4,
                "bronze bars " .. tostring(select(2, t.inv.count("bronze_bar"))) .. ", iron bars " .. tostring(select(2, t.inv.count("iron_bar"))))

            t.exec("fillCrucible", t.player.click_loc, "giants_foundry_crucible_multi", 1)
            t.ticks(2)
            t.check("fillCrucible.full", select(2, t.var.varbit("varb13937_giants_foundry_crucible_state")) == 2,
                "crucible state " .. tostring(select(2, t.var.varbit("varb13937_giants_foundry_crucible_state"))) .. " (2 = full); bronze_bar left " .. tostring(select(2, t.inv.count("bronze_bar"))))

            t.exec("speakToKovacAboutMould", t.player.talk_to, "giants_foundry_kovac_multi", 1)
            t.exec("speakToKovacAboutMould-dialog", t.chat.play, {
                "player:Right, that's the crucible full", "npc:Kovac think we need to set up a mould", "player:What's a mould used for",
                "npc:Hmm", "npc:Kovac think mould will determine", "npc:Remember", "player:Okay, so how do I go about",
                "npc:Kovac remember being told", "npc:a forte", "npc:Use mould jig", "npc:You can select", "npc:Each mould part",
                "npc:They will suit", "npc:Look for parts", "player:Right... I think", "npc:If you are still", "npc:come back to me",
            })
            t.ticks(2)

            t.exec("interactWithMould", t.player.click_loc, "giants_foundry_mould_jig", 1)
            t.exec("interactWithMould.open", t.ui.await_open, "giants_foundry_mould", 10)
            t.ticks(2)
            local function varb(name) return select(2, t.var.varbit(name)) end
            -- The mould screen's picks are client-local; the server hears the op (sg_commission.rs2:300-345).
            -- A row is 17 components, a tab 9; parts 6..11 are the six everyone has.
            local function row_slot(part) return (part - 1) * 17 end
            local function pick(name, sub, op_target)
                local wr, widget = t.ui.widget(op_target, sub)
                t.check(name .. ".widget", wr == "ok", op_target .. " child " .. tostring(sub) .. " -> " .. tostring(wr) .. " " .. tostring(widget))
                local ir = t.ui.invoke(widget, 1)
                t.ticks(2)
                t.check(name, ir == "ok", "if_button op1 on " .. op_target .. " child " .. tostring(sub) .. " -> " .. tostring(ir))
            end
            pick("selectForte", row_slot(11), "giants_foundry_mould:content")
            t.check("selectForte.read", varb("varb13910_giants_foundry_mould_selected_ricasso") == 11, "forte part " .. tostring(varb("varb13910_giants_foundry_mould_selected_ricasso")) .. " server " .. tostring(select(2, t.var.server("varb13910_giants_foundry_mould_selected_ricasso"))) .. " smithing " .. tostring(select(2, t.skill.read("smithing"))))
            pick("selectBladesTab", 9, "giants_foundry_mould:side_menu")
            pick("selectBlade", row_slot(11), "giants_foundry_mould:content")
            t.check("selectBlade.read", varb("varb13911_giants_foundry_mould_selected_blade") == 11, "blade part " .. tostring(varb("varb13911_giants_foundry_mould_selected_blade")))
            pick("selectTipsTab", 18, "giants_foundry_mould:side_menu")
            pick("selectTip", row_slot(11), "giants_foundry_mould:content")
            t.check("selectTip.read", varb("varb13912_giants_foundry_mould_selected_tip") == 11, "tip part " .. tostring(varb("varb13912_giants_foundry_mould_selected_tip")))
            local sr, setw = t.ui.widget("giants_foundry_mould:set_button", -1)
            local setr = t.ui.invoke(setw, 1)
            t.ticks(2)
            t.check("setMould", setr == "ok", "if_button op1 on giants_foundry_mould:set_button -> " .. tostring(setr))
            t.check("setMould.read", varb("varb13914_giants_foundry_mould_state") == 1, "mould state " .. tostring(varb("varb13914_giants_foundry_mould_state")) .. " tutorial " .. tostring(varb("varb13903_sleeping_giants_tutorial")))

            t.exec("talkToKovakAfterMould", t.player.talk_to, "giants_foundry_kovac_multi", 1)
            t.exec("talkToKovakAfterMould-dialog", t.chat.play, {
                "player:Right, the mould is setup", "npc:Hmm", "npc:let me look", "mesbox:Kovac takes a look", "mesbox:...",
                "npc:Yes, all things", "npc:Well done little one", "npc:Pour the metal", "npc:then take out", "npc:Once you have done", "npc:Kovac will tell",
            })
            t.ticks(2)
            t.check("talkToKovakAfterMould.read", varb("varb13903_sleeping_giants_tutorial") >= 40, "tutorial " .. tostring(varb("varb13903_sleeping_giants_tutorial")))

            t.exec("pourMetal", t.player.click_loc, "giants_foundry_crucible_multi", 1)
            t.ticks(2)
            t.check("pourMetal.read", varb("varb13914_giants_foundry_mould_state") == 2 and varb("varb13937_giants_foundry_crucible_state") == 0,
                "mould state " .. tostring(varb("varb13914_giants_foundry_mould_state")) .. " (2 poured), crucible state " .. tostring(varb("varb13937_giants_foundry_crucible_state")) .. ", quality " .. tostring(varb("varb13939_giants_foundry_preform_quality")))

            t.exec("takeBucket", t.player.click_loc, "my2arm_throne_room_buckets", 1)
            t.inv.await("bucket_empty", 1, 10)
            t.check("takeBucket.have", select(2, t.inv.count("bucket_empty")) == 1, "empty buckets " .. tostring(select(2, t.inv.count("bucket_empty"))))

            t.exec("fillBucketWaterfall", t.player.use_on, "bucket_empty", t.player.by_symbol("loc", "giants_foundry_waterfall"))
            t.inv.await("bucket_water", 1, 10)
            t.check("fillBucketWaterfall.have", select(2, t.inv.count("bucket_water")) == 1, "buckets of water " .. tostring(select(2, t.inv.count("bucket_water"))))

            t.exec("coolDownSword", t.player.click_loc, "giants_foundry_mould_jig", 1)
            t.ticks(3)
            t.check("coolDownSword.read", varb("varb13914_giants_foundry_mould_state") == 0 and varb("varb13903_sleeping_giants_tutorial") == 50,
                "mould state " .. tostring(varb("varb13914_giants_foundry_mould_state")) .. " (0 empty), tutorial " .. tostring(varb("varb13903_sleeping_giants_tutorial")) .. " (50 preform), empty buckets " .. tostring(select(2, t.inv.count("bucket_empty"))))

            -- Quiet boundary: leave the copy (a checkpoint cannot carry an instance).
            t.exec("leg.2.leave", t.player.click_loc, "giants_foundry_exit", 1)
            t.ticks(4)
            local _, tile = t.world.tile()
            local _, stage = t.var.server("varb13902_sleeping_giants")
            local held = {}
            for slot = 0, 27 do
                local r, cell = t.inv.slot(slot)
                if r == "ok" and cell.name ~= "" and cell.count ~= 0 then held[#held + 1] = cell.name .. "x" .. cell.count end
            end
            t.check("leg.2.end", true, "tile=" .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level)) .. " stage=" .. tostring(stage) .. " inv=" .. table.concat(held, ","))
            -- LEG 2 END
        end },
        { name = "foundry_refine", run = function(t)
            -- LEG 3 BEGIN: getPreform
            local function sv(name) local _, v = t.var.server(name); return tonumber(v) or -1 end
            local function temp() return sv("varb13948_giants_foundry_preform_temperature") end
            local function done() return sv("varb13949_giants_foundry_preform_completion") end
            local function quality() return sv("varb13939_giants_foundry_preform_quality") end
            -- Heat bands (sg_refine.rs2:106-125): width = 333 - difficulty*167/130; band 0 hammer, 1 grindstone, 2 polishing wheel.
            local function width() return 333 - math.floor(sv("varb13938_giants_foundry_preform_dificulty") * 167 / 130) end
            local function band(b)
                local w = width()
                local start = ({ [0] = 166 + 666, [1] = 166 + 333, [2] = 166 })[b] - math.floor(w / 2)
                return start, start + w
            end
            local function state(label)
                return label .. ": temp " .. temp() .. " done " .. done() .. " quality " .. quality() .. " band0 " .. band(0) .. " band1 " .. band(1) .. " band2 " .. band(2)
            end
            -- A machine here repeats its op every tick, so a settled click_loc waits out its 20-tick timeout while the heat
            -- runs on; a menu press returns at once and the loop below keeps reading the heat.
            local function press(loc, op)
                local target, sym_result, sym_name = t.player.by_symbol("loc", loc)
                if not target then t.check("press." .. loc, false, tostring(sym_result) .. " " .. tostring(sym_name)); return end
                -- The hud overlay covers the wheel from the default camera, and a covered press burns ~20 ticks of heat before it
                -- answers: turn the camera first (yaw 128 measured clear) for the wheel.
                if loc == "giants_foundry_polishing_wheel" then t.drive.camera(128, 383, 600) end
                local r, d = t.drive.click_minimenu(target, op)
                if r ~= "ok" then
                    -- The foundry hud overlay can sit on top of the machine from this camera: turn the camera until a menu press lands.
                    local tried = {}
                    for yaw = 128, 1920, 256 do
                        t.drive.camera(yaw, 383, 600)
                        t.ticks(1)
                        local r4, d4 = t.drive.click_minimenu(target, op)
                        tried[#tried + 1] = yaw .. ":" .. tostring(r4)
                        if r4 == "ok" then r = "ok"; break end
                    end
                    t.note("press " .. loc .. ": covered at the old camera, turned: " .. table.concat(tried, ",") .. " at temp " .. temp())
                    if r ~= "ok" then t.player.click_loc(loc, op) end
                end
            end
            -- Press a machine and watch the heat every tick until pred() holds.
            local function drive_until(name, loc, op, pred)
                press(loc, op)
                if name == "heatPreformToPolish" then t.drive.camera(128, 383, 600) end
                t.check(name, true, "pressed " .. loc .. " op " .. op .. "; " .. state("at press"))
                for _ = 1, 120 do
                    if pred() then return true end
                    t.ticks(1)
                end
                return pred()
            end
            -- Work a machine until the completion reaches target, re-heating or cooling when the heat nears the band edge.
            -- The polishing wheel cannot be pressed from the lava or the waterfall (covered), so its press is a settled click_loc
            -- that holds ~20 ticks while the wheel cools the sword: heat it high first (slow dunk, no overshoot), switch early.
            local function work(name, tool, bandn, target, wheel)
                press(tool, 1)
                t.check(name, true, "pressed " .. tool .. " op 1; " .. state("at press"))
                local mode = "tool"
                local last_quality = quality()
                for iteration = 1, 300 do
                    if quality() ~= last_quality then
                        t.note("quality " .. last_quality .. " -> " .. quality() .. " at iteration " .. iteration .. " mode " .. mode .. " temp " .. temp() .. " done " .. done())
                        last_quality = quality()
                    end
                    if done() >= target then return true end
                    local lo, hi = band(bandn)
                    local w = hi - lo
                    local tp = temp()
                    if mode == "tool" then
                        if tp > hi - 12 then press("giants_foundry_waterfall", 2); mode = "cool"
                        elseif tp < lo + (wheel and 70 or 12) then press("giants_foundry_lava_pool", wheel and 1 or 2); mode = "heat"; if wheel then t.drive.camera(128, 383, 600) end end
                    elseif mode == "cool" then
                        if tp <= lo + math.floor(w * 0.45) then press(tool, 1); mode = "tool" end
                    elseif mode == "heat" then
                        if tp >= hi - (wheel and 35 or math.floor(w * 0.45)) then press(tool, 1); mode = "tool" end
                    end
                    t.ticks(1)
                end
                return done() >= target
            end

            t.exec("enterFoundryToRefine", t.player.click_loc, "giants_foundry_entrance", 1)
            t.ticks(3)
            -- The preform is gripped fast in the hands; the storage is where it leaves and re-enters them (sg_refine.rs2:399).
            t.exec("storePreform", t.player.click_loc, "giants_foundry_preform_storage", 1)
            t.ticks(2)
            t.check("storePreform.read", sv("varb13947_giants_foundry_preform_stored") == 1, "stored flag " .. sv("varb13947_giants_foundry_preform_stored"))
            t.exec("getPreform", t.player.click_loc, "giants_foundry_preform_storage", 1)
            t.ticks(2)
            t.check("getPreform.read", sv("varb13947_giants_foundry_preform_stored") == 0 and select(2, t.world.tile()) ~= nil, state("held again, stored flag " .. sv("varb13947_giants_foundry_preform_stored")))

            local lo0, hi0 = band(0)
            t.check("dunkPreform.target", drive_until("dunkPreform", "giants_foundry_lava_pool", 2, function() return temp() >= math.floor((lo0 + hi0) / 2) - 40 end),
                state("after dunk"))
            t.check("hitPreformWhileRed.done", work("hitPreformWhileRed", "giants_foundry_trip_hammer", 0, 333), state("after hammer"))

            local lo1, hi1 = band(1)
            t.check("coolPreformToGrindstone.target", drive_until("coolPreformToGrindstone", "giants_foundry_waterfall", 2, function() return temp() <= lo1 + 25 end),
                state("after cool"))
            t.check("dunkPreformToGrindstone.target", drive_until("dunkPreformToGrindstone", "giants_foundry_lava_pool", 1, function() return temp() >= lo1 + 60 end),
                state("after slow heat"))
            t.check("grindstonePreform.done", work("grindstonePreform", "giants_foundry_grindstone", 1, 666), state("after grindstone"))

            local lo2, hi2 = band(2)
            t.check("coolPreformToPolish.target", drive_until("coolPreformToPolish", "giants_foundry_waterfall", 2, function() return temp() <= 200 end),
                state("after cool"))
            t.check("heatPreformToPolish.target", drive_until("heatPreformToPolish", "giants_foundry_lava_pool", 1, function() return temp() >= hi2 - 40 end),
                state("after heat"))
            t.check("polishPreform.done", work("polishPreform", "giants_foundry_polishing_wheel", 2, 1000, true), state("after polish"))
            t.check("polishPreform.quality", quality() > 0, state("sword kept some quality"))

            t.check("handInPreform.quality", quality() > 0, "the sword kept quality " .. quality() .. " (the dialogue below is the 'Great!' branch, sg_refine.rs2:577)")
            local _, xp_before = t.skill.snapshot()
            t.exec("handInPreform", t.player.talk_to, "giants_foundry_kovac_multi", 1)
            t.exec("handInPreform-dialog", t.chat.play, {
                "player:Okay Kovac, I think I've finally finished", "npc:Hmm", "npc:let Kovac look", "mesbox:You pass the sword", "mesbox:He analyses",
                "npc:Hmm", "npc:Yes", "npc:Hmm", "npc:This is", "npc:Well done little one",
                "player:Great! So what do we do", "npc:Kovac take care", "player:Out of curiosity", "npc:Kovac friend", "npc:Obor",
                "player:Obor?", "npc:Kovac think Obor", "player:That's a pretty", "npc:Kovac know what",
            })
            t.ticks(3)
            t.quest.expect_complete()
            t.check("reward.smithing_xp", t.skill.expect_gain("smithing", 6000, xp_before))
            -- LEG 3 END
            t.finish(0)
        end },
    },
}
