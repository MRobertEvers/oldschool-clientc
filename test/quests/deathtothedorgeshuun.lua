-- Death to the Dorgeshuun -- the flow of RSProx index 4523 (rev 242, 2026-11-11), the only
-- recorded playthrough: docs/rsprox-analysis/quests/death_to_the_dorgeshuun.md.
--
-- Driven by hand: Mistag's offer and the start ([2811..2838]), Zanik in the cellar
-- ([2906..2957]), the robes ([3433..3440]), the sun ([3595..3615]), Hans with Zanik
-- ([3639..3654]). The later sections are entered by ::dttdstage (the store rooms' copy,
-- Juna with the body, the mill's crate) and checked to their stage write.
return {
    id = "deathtothedorgeshuun",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::complete quest_losttribe", "::setlevel agility 23", "::setlevel thieving 23",
        "::give lit_black_candle 1", "::give poh_tablet_lumbridgeteleport 5" },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb2258_dttd_main",
            constants = { not_started = 0, find_zanik = 1, get_robes = 2, tour = 3, to_ham = 4, trapdoor_found = 5,
                captured = 6, body_at_juna = 7, revived = 8, to_mill = 9, mill_fight = 10, sigmund_fled = 11,
                drill_smashed = 12, complete = 13 },
            row = "quest_deathtothedorgeshuun", display = "Death to the Dorgeshuun", points = 1,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.ticks(3)
        local r, d, tr, tl

        local function tile_text()
            local rr, tt = t.world.tile()
            if rr ~= "ok" or type(tt) ~= "table" then
                return tostring(rr)
            end
            return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
        end
        local function choose(tag, choice)
            r, d = t.chat.drain({ stop_at = "options" }); t.expect(tag .. ".opts", r, d)
            t.exec(tag, t.chat.choose, choice)
        end
        local function drain_all(tag)
            r, d = t.chat.drain({ stop_at = "none" }); t.expect(tag, r, d)
        end

        t.ticklog.start()

        -- [2811..2866] Mistag's favour and the start, then his way out of the mines
        t.cheat("::deathtothedorgeshuun"); t.ticks(1)
        t.ticks(3)
        t.exec("mistag.talk", t.player.talk_to, "lost_tribe_mistag", 1)
        choose("mistag.favour", "What is this favour?")
        choose("mistag.start", "Yes.")
        choose("mistag.wayout", "Can you show me the way out of the mines?")
        t.exec("mistag.started", t.var.await, "varb2258_dttd_main", 1, 2)
        t.exec("mistag.cellar", t.var.await, "varb2264_dttd_zanik_in_cellar", 1, 2)
        drain_all("mistag.end")
        t.ticks(6)
        tr, tl = t.world.tile()
        t.check("mistag.guided", tr == "ok" and tl.x < 3260, "after the guide: " .. tile_text())

        -- [2906..2957] Zanik in the cellar: the name, the stage on her second page, "OK"
        t.exec("cellar.tp", t.player.goto_tile, 3221, 9618, 0)
        t.exec("cellar.hole", t.player.cross_trap, { loc = "lost_tribe_cellar_wall_back", at = { 3221, 9618, 0 },
            src = { 3221, 9618 }, dest = { 3219, 9618 }, attempts = 1 })
        t.exec("zanik.walk", t.player.walk_route, { { 3216, 9616 }, { 3214, 9620 }, { 3211, 9622 } }, { level = 0 })
        t.exec("zanik.talk", t.player.talk_to, "dttd_zanik_cellar", 1)
        choose("zanik.name", "Yes, I'm deathtothedorgeshuun!")
        choose("zanik.robes", "Where do I get the robes?")
        t.exec("zanik.met", t.var.await, "varb2258_dttd_main", 2, 2)
        choose("zanik.ok", "OK")
        drain_all("zanik.end")

        -- [3433..3440] both sets: she takes hers and follows
        t.cheat("::give ham_shirt 2"); t.ticks(1)
        t.cheat("::give ham_robe 2"); t.ticks(1)
        t.cheat("::give ham_hood 2"); t.ticks(1)
        t.cheat("::give ham_cloak 2"); t.ticks(1)
        t.cheat("::give ham_gloves 2"); t.ticks(1)
        t.cheat("::give ham_boots 2"); t.ticks(1)
        t.cheat("::give ham_badge 2"); t.ticks(1)
        t.exec("robes.talk", t.player.talk_to, "dttd_zanik_cellar", 1)
        drain_all("robes.end")
        t.expect("quest.stage.tour", t.quest.expect_stage("tour"))
        t.exec("robes.left", t.inv.await, "ham_shirt", 1, 2)
        t.exec("follower.obj", t.var.await, "varp448_follower_obj", 8887, 2)

        -- [3595..3615] out of the castle on the west: the sun
        t.exec("kitchen.up", t.player.climb, { loc = "ladder_from_cellar", op = 1, op_name = "Climb-up",
            at = { 3209, 9616, 0 }, src = { 3210, 9616 }, dest = { 3210, 3216, 0 } })
        t.exec("sun.walk", t.player.walk_route, { { 3208, 3214 }, { 3205, 3214 }, { 3203, 3214 } }, { level = 0 })
        t.ticks(3)
        drain_all("sun.pages")
        t.exec("sun.seen", t.var.await, "varb2263_dttd_tour_sun", 1, 4)

        -- [3639..3654] Hans with Zanik: her line before his, his rows after
        t.exec("hans.goto", t.player.goto_tile, 3219, 3214, 0)
        t.exec("hans.talk", t.player.talk_to, "hans", 1)
        r, d = t.chat.drain({ stop_at = "options" }); t.expect("hans.opts", r, d)
        t.exec("hans.bye", t.chat.choose, "No, I'm good.")
        drain_all("hans.end")

        -- The store rooms' copy (stage 5): the climb into a fresh copy, five guards
        t.cheat("::dttdstage 5"); t.ticks(1)
        t.exec("ham.tp", t.player.goto_tile, 3166, 9623, 0)
        t.exec("trapdoor.pick", t.player.click_loc, "dttd_ham_trapdoor", 2)
        t.exec("trapdoor.unlocked", t.var.await, "varb2270_dttd_ham_trapdoor_state", 2, 6)
        t.exec("trapdoor.down", t.player.click_loc, "dttd_ham_trapdoor", 1)
        t.ticks(4)
        tr, tl = t.world.tile()
        t.check("storerooms.copy", tr == "ok" and tl.x > 6400, "in the copy: " .. tile_text())

        -- Juna with the body (stage 6): laid down, the stage
        t.cheat("::dttdstage 6"); t.ticks(1)
        t.cheat("::give dttd_dead_zanik 1"); t.ticks(1)
        t.exec("juna.tp", t.player.goto_tile, 3251, 9516, 2)
        t.exec("juna.talk", t.player.click_loc, "tog_juna", 1)
        drain_all("juna.pages")
        t.exec("juna.laid", t.var.await, "varb2258_dttd_main", 7, 8)
        t.exec("body.taken", t.inv.await, "dttd_dead_zanik", 0, 2)
    end,
}
