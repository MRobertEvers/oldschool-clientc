-- _play_nylocas: the Nylocas, Entry solo, played through the PLAY LIBRARY
-- (t.raid.play, raid_play.lua) and its room plan (raid_play_tob_nylocas.lua,
-- raid seam30 play_tob_nylocas; docs/minigames/raid_loop/PLAY_NOTES.md).
-- An underscore harness, not a kept room: the room's setup and entry as
-- test/raids/tob_nylocas.lua does them, the fight as ONE call, then the kept
-- test's room-complete and technique rows (tob_nylocas.lua :1892-1925)
-- copied unchanged, their inputs read from the tick log and the record.
-- raid seam32 play_tob_nylocas_normal: with `--party 3` (QD_PARTY) the same
-- harness plays NORMAL as a trio: seat 1 the mage (and the tick log), seat 2
-- the ranger, seat 3 the meleer (the plan's roles; "Trio: x1 mager, x1 melee,
-- x1 ranger", wiki Strategies :711).  Alone it is the Entry solo harness,
-- unchanged.  raid seam33 play_tob_nylocas_normal_green: every seat carries a
-- powered staff charged in run() (setup.ayak, setup.sang), each seat starts on
-- its own colour's weapon, and tech.pillars_at_boss asks every support
-- standing when she lands (the KEPT bar).  raid seam35m: the weakest at or
-- above 0.10, what 34 recorded Regular trios show (seam33's 0.50 was not).
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
local kit = {
    "::clearinv",
    -- tob_nylocas.lua's own bring-alongs: 99s, one weapon per colour, Ancient
    -- Magicks and the runes of Ice Rush / Ice Burst (water, chaos, death)
    "::setlevel attack 99",
    "::setlevel strength 99",
    "::setlevel defence 99",
    "::setlevel ranged 99",
    "::setlevel magic 99",
    "::setlevel hitpoints 99",
    "::setlevel prayer 99",
    -- the Entry page's recommended Entry-mode equipment, worn (no backpack
    -- slot): "Void melee helm, Amulet of glory, Elite void top, Elite void
    -- robe, Void knight gloves, Dragon boots, Berserker ring (i)" (Entry page
    -- :78-91 {{Recommended equipment|style = Entry mode}}; its dragon
    -- defender is left out: the shortbow is two-handed, and its imbued god
    -- cape needs the Mage Arena).  The kept test wore nothing.  Given and worn
    -- first, while the backpack has room.
    "::give game_pest_melee_helm 1", "::wield game_pest_melee_helm",
    "::give amulet_of_glory 1", "::wield amulet_of_glory",
    "::give elite_void_knight_top 1", "::wield elite_void_knight_top",
    "::give elite_void_knight_robes 1", "::wield elite_void_knight_robes",
    "::give pest_void_knight_gloves 1", "::wield pest_void_knight_gloves",
    "::give dragon_boots 1", "::wield dragon_boots",
    "::give nzone_berzerker_ring 1", "::wield nzone_berzerker_ring",
    "::give abyssal_whip 1",
    "::give magic_shortbow 1",
    "::give rune_arrow 800",
    "::give lava_battlestaff 1",
    "::setvar varb4070_spellbook 1",
    "::give water_rune 2000",
    "::give chaos_rune 1000",
    "::give death_rune 1000",
    -- the library's supplies (raid_play.lua QD.RAID_PLAY_BREWS / RESTORES:
    -- the Theatre's own brew and restore): 28 slots with the above
    "::give br_4dosepotionofsaradomin 10",
    "::give br_4dose2restore 4",
    -- (raid seam31 play_tob_nylocas_green) the Bloat chest's bandages in
    -- place of the sharks: "After defeating the Pestilent Bloat, players
    -- will have access to the first supply chest. During Entry Mode this
    -- will always contain 10 bandages" and "Due to these bandages boosting
    -- the player's stats, combat potions and ranging potions are not
    -- necessary except for the first two bosses" (Entry page, sources/
    -- wiki_Theatre_of_Blood_Entry_Mode.wikitext :151, :33); the chest hands
    -- over as many as the backpack holds (tob_chest.rs2 tob_chest_bandages).
    -- Heals 20 like the shark it replaces, and boosts (tob_spectate.rs2
    -- [opheld1,tob_bandages]); the plan eats them (_play_nylocas_supplies).
    "::give tob_bandages 7",
}
if size > 1 then
    -- Normal: the Bloat chest's Entry bandages are not handed out ("During
    -- Entry Mode this will always contain 10 bandages", E :151), so the trio
    -- carries anglerfish in their place ("make sure that you eat your angler",
    -- transcripts/yt_KF9y2GYTJ-A.md:114)
    assert(kit[#kit] == "::give tob_bandages 7")
    kit[#kit] = "::give anglerfish 7"
    if role == 2 then
        -- the ranger's blowpipe in place of the bow ("Rangers should use a
        -- toxic blowpipe in this room", wiki Strategies :717), loaded in run()
        -- with darts and Zulrah's scales the way a player loads one (use the
        -- darts on it, then the scales: blowpipe_ammo.rs2)
        for i = 1, #kit do
            if kit[i] == "::give magic_shortbow 1" then kit[i] = "::give toxic_blowpipe 1" end
            if kit[i] == "::give rune_arrow 800" then kit[i] = "::give dragon_dart 2000" end
        end
        -- the scales' slot is one anglerfish (28 slots; loaded, darts and
        -- scales leave the backpack and the slot is free again)
        kit[#kit] = "::give anglerfish 6"
        kit[#kit + 1] = "::give snakeboss_scale 2000"
    end
    if role == 1 then
        -- raid seam33 play_tob_nylocas_normal_green: the mage's Eye of Ayak
        -- in place of the battlestaff ("Mages should use an eye of ayak, as
        -- its 3 tick speed and fairly high damage makes clearing them
        -- incredibly trivial", wiki Strategies :719); given uncharged and
        -- charged in run() the way a player does it, its Charge op with demon
        -- tears in the backpack (wiki Eye of Ayak :22 "Wield, Charge";
        -- eye_of_ayak.rs2 [opheld3,eye_of_ayak_uncharged] ~eye_of_ayak_charge,
        -- one tear a charge).  The tears' slot is one anglerfish; charged,
        -- the tears leave the backpack.  The runes stay: Ice Burst on her
        -- magic form is the helpers' cast, and the mage keeps the spellbook.
        for i = 1, #kit do
            if kit[i] == "::give lava_battlestaff 1" then kit[i] = "::give eye_of_ayak_uncharged 1" end
        end
        kit[#kit] = "::give demon_tear 2000"
        kit[#kit + 1] = "::give anglerfish 6"
    end
    if role == 2 or role == 3 then
        -- raid seam33: the helpers' powered staff for the blues in place of
        -- the battlestaff and its runes (trio guide :216 the ranger Ayaks,
        -- :386 the meleer Sangs), charged in run() by its Charge op: demon
        -- tears for the Ayak, blood runes for the Sanguinesti staff (three a
        -- charge, sanguinesti_staff.rs2 ^sanguinesti_staff_runes_per_charge).
        -- raid seam40 play_tob_nylocas_follows_blert: the meleer Ayaks its
        -- blues as the ranger does (Blert melee|magic EYE_OF_AYAK 203 of 207
        -- hits in 27 Normal trio rooms; no Sanguinesti staff in any room)
        local staff = "eye_of_ayak_uncharged"
        local charge = "::give demon_tear 2000"
        for i = 1, #kit do
            if kit[i] == "::give lava_battlestaff 1" then kit[i] = "::give " .. staff .. " 1" end
            if kit[i] == "::give water_rune 2000" then kit[i] = charge end
        end
    end
    -- raid seam40 play_tob_nylocas_follows_blert: THE WEAPON PER COLOUR, AS
    -- BLERT (reference/nylocas_normal_3.json, 27 death-free Normal trio rooms;
    -- per colour in build/seam_state/matthew-mbp-m4-raid-b1-seam40/ny40/
    -- ny_blert.out).  Every seat carries the SCYTHE (her melee form: SCYTHE
    -- 383 of 532 hits on 8355; the mage's and ranger's greys, the meleer's
    -- big greys), and the mage and meleer a loaded blowpipe for the greens in
    -- place of the shortbow (mage|ranged BLOWPIPE 116 of 138, melee|ranged
    -- 75 of 91).  `::fullscythe` hands a charged scythe (scythe_of_vitur.rs2
    -- ~scythe_of_vitur_cheat); `::blowpipe` gives and loads the pipe in one
    -- line and needs three free slots, so it takes the shortbow's place early
    -- in the kit (test/raids/README.md "A loaded toxic blowpipe is one kit
    -- line"); the arrows go.
    -- raid seam40: THE GEAR, AS BLERT'S RECORDERS WEAR IT (equipmentDeltas
    -- of the recording raider in the same 27 rooms, ny40/ny_gear.py): the
    -- meleer torva helm, amulet of rancour, ferocious gloves, avernic treads,
    -- ultor ring, infernal cape (8 of 9 meleers; `::maxmelee` is that set and
    -- the scythe in hand, cheat_max_gear.rs2), the ranger and the mage the
    -- elite void RANGE helm (7 of 9 rangers, 14 of 14 mages: void range helm)
    -- with the elite void set, the ranger a necklace of rupture (5 of 9), the
    -- mage an occult necklace (14 of 14) -- not the Entry page's melee void.
    local swap = {}
    if role == 1 or role == 2 then
        swap["::give game_pest_melee_helm 1"] = "::give game_pest_archer_helm 1"
        swap["::wield game_pest_melee_helm"] = "::wield game_pest_archer_helm"
        -- owner_nylocas: the slots every recorder keeps through all three of
        -- her forms (Blert equipmentDeltas, the 27 rooms, mage and ranger
        -- recorders on her: avernic_treads_max 73-92 percent, ultor_ring
        -- 73-92, infernal_cape on her melee and magic forms) in place of the
        -- Entry page's dragon boots and berserker ring
        swap["::give dragon_boots 1"] = "::give avernic_treads_max 1"
        swap["::wield dragon_boots"] = "::wield avernic_treads_max"
        swap["::give nzone_berzerker_ring 1"] = "::give ultor_ring 1"
        swap["::wield nzone_berzerker_ring"] = "::wield infernal_cape"
        local neck = role == 1 and "occult_necklace" or "necklace_of_rupture"
        swap["::give amulet_of_glory 1"] = "::give " .. neck .. " 1"
        swap["::wield amulet_of_glory"] = "::wield " .. neck
    end
    local melee_set = { ["game_pest_melee_helm"] = true, ["amulet_of_glory"] = true, ["elite_void_knight_top"] = true,
        ["elite_void_knight_robes"] = true, ["pest_void_knight_gloves"] = true, ["dragon_boots"] = true, ["nzone_berzerker_ring"] = true }
    local out = {}
    for i = 1, #kit do
        local c = swap[kit[i]] or kit[i]
        if (role == 1 or role == 2) and c == "::give ultor_ring 1" then
            -- (the ring and the cape go on together while the backpack has room)
            out[#out + 1] = c
            out[#out + 1] = "::wield ultor_ring"
            c = "::give infernal_cape 1"
        end
        local item = string.match(c, "^::give ([%w_]+) 1$") or string.match(c, "^::wield ([%w_]+)$")
        if role == 3 and item ~= nil and melee_set[item] then
            -- raid seam52 melee_damage_per_swing: the recorded meleers wear
            -- radiant oathplate body and legs, not torva (Blert equipmentDeltas:
            -- Bloat 79/90, Verzik 66/81, Sotetseg 81/87; build/seam_state/
            -- matthew-mbp-m4-raid-b1-seam52/kit.melee_damage_per_swing.md);
            -- `::tobkit` is `::maxmelee` with those two pieces (cheat_max_gear.rs2)
            if c == "::give game_pest_melee_helm 1" then out[#out + 1] = "::tobkit" end
        elseif c == "::give abyssal_whip 1" then
            if role == 3 then
                out[#out + 1] = c
                -- owner_nylocas: the cleanup's grey weapon, the reference meleer's
                -- SULPHUR BLADES (two hits a swing since content 0b5ef3da0d)
                if size > 1 then out[#out + 1] = "::give sulphur_blades 1" end
            else out[#out + 1] = "::fullscythe" end
        elseif c == "::give magic_shortbow 1" and role ~= 2 then
            out[#out + 1] = "::blowpipe dragon_dart 2000 2000"
            -- raid seam40: the mage and the meleer shoot her ranged form with
            -- the TWISTED BOW (Blert on 8357: mage TWISTED_BOW 127 of 158,
            -- melee 88 of 184 with the pipe 68; the ranger pipes, 89 of 191)
            out[#out + 1] = "::give twisted_bow 1"
            out[#out + 1] = "::give dragon_arrow 1000"
            out[#out + 1] = "::wield dragon_arrow"
        elseif c == "::give chaos_rune 1000" or c == "::give death_rune 1000" or (role == 1 and c == "::give water_rune 2000") then
            -- (no seat casts from the spellbook: every seat's blues are the
            -- Ayak's, a powered staff; raid seam40 frees the runes' slots for
            -- the boosts below)
        elseif c == "::give rune_arrow 800" and role ~= 2 then
            -- (no arrows: every seat shoots the pipe)
        else
            out[#out + 1] = c
        end
    end
    -- raid seam40: THE BOOSTS, AS BLERT'S RECORDERS CARRY THEM.  Their levels
    -- in the waves (current<<16|base, the same 27 rooms, ny40/ny_levels.py):
    -- Attack and Strength 118 (the meleer 118 at its tenth percentile), Ranged
    -- 112 (the ranger 112 at its tenth) -- a super combat potion (99 + 5 + 15
    -- percent = 118) and a ranging potion (99 + 4 + 10 percent = 112), drunk
    -- at the door (run below; _play_maiden.lua's play.potion row).
    -- raid seam47: the DIVINE forms.  Blert's levels HOLD through the waves
    -- (ny40/ny_levels.py, ticks < 250: the meleer's Attack and Strength 118
    -- at the tenth percentile, the ranger's Ranged 112 at the tenth) where a
    -- plain dose decays a level a minute on this server as in the game
    -- (build/s47/s47_decay.lua: 118 -> 116 in 240 ticks); a divine dose holds
    -- (s47_divine.lua: 118 and 112 for 240 ticks).  The plain ranging potion
    -- also reads 111 at 99 here (s47maxhit), the divine one 112.
    -- owner_nylocas: run energy (the relay's kit: Agility 99 and a stamina
    -- potion; the room's meleer ran out of energy and walked 35-124 steps a room)
    out[#out + 1] = "::setlevel agility 99"
    if role ~= 1 then out[#out + 1] = "::give 4dosestamina 1" end
    out[#out + 1] = "::give 4dosedivinecombat 1"
    out[#out + 1] = "::give 4dosedivinerange 1"
    -- raid seam47: the special on her melee form, as Blert's trios spend it
    -- (8355: CLAW in 15 of 27 mage rooms, every role; the plan's P.spec)
    -- (its slot is one Saradomin brew's: a trio seat drank 0-9 of the 40
    -- doses in the s47 surveys, so nine potions are still more than a room's)
    out[#out + 1] = "::give dragon_claws 1"
    -- raid seam49: the ranger's TWISTED BOW for her ranged form (Blert
    -- range|boss TWISTED_BOW in 20 of 27 rooms, the pipe in 14), arrows worn
    -- in the quiver slot the loaded pipe leaves empty; its slot and the
    -- arrows' passing one are two Saradomin brews' (below)
    if role == 2 then
        out[#out + 1] = "::give twisted_bow 1"
        out[#out + 1] = "::give dragon_arrow 1000"
        out[#out + 1] = "::wield dragon_arrow"
        -- raid seam51 play_tob_nylocas_whole: the ranger's BLACK CHINCHOMPAS
        -- for the green clumps (Blert range|wave10 CHIN_BLACK in 23 of 27
        -- Normal trio rooms, wave 21 in 21, wave 31 in 18: reference/
        -- nylocas_normal_3.json weapons); one stacked slot, a brew's
        out[#out + 1] = "::give chinchompa_black 300"
    end
    -- owner_nylocas: HER FORMS IN THEIR OWN GEAR, as Blert's recorders wear
    -- them (equipmentDeltas of the recording raider on each of her forms, 27
    -- rooms; build/seam_state/owner_nylocas progress.md): on her MELEE form
    -- every role is in melee gear -- the mage recorders torva_helm_sanguine
    -- 61 percent, amulet_of_rancour 68+23, radiant_oathplate_chest 58 / legs
    -- 59, ferocious_gloves 89; the rangers torva 41+21, rancour 64,
    -- oathplate 48 / 55, ferocious 64 -- and on her RANGED form every role is
    -- in the void range set -- the meleers game_pest_archer_helm 62,
    -- necklace_of_rupture 77, elite_void_knight_top / robes 62,
    -- pest_void_knight_gloves 62 (her magic form: rupture 71, void top 42 /
    -- masori 35).  So the mage and the ranger carry the melee set and the
    -- meleer the void range set; the plan wears them at her turns (P.boss_gear).
    -- Their slots are Saradomin brews': a seat drank 0-2 doses a room in the
    -- step-1 surveys (consume rows), of 24-36 carried.
    if role == 1 or role == 2 then
        for _, it in ipairs({ "torva_helm", "amulet_of_rancour", "radiant_oathplate_chest", "radiant_oathplate_legs", "ferocious_gloves" }) do
            out[#out + 1] = "::give " .. it .. " 1"
        end
    else
        for _, it in ipairs({ "game_pest_archer_helm", "necklace_of_rupture", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" }) do
            out[#out + 1] = "::give " .. it .. " 1"
        end
    end
    for i = 1, #out do
        -- (the meleer's third brew is the blades' slot: owner_nylocas)
        if out[i] == "::give br_4dosepotionofsaradomin 10" then out[i] = "::give br_4dosepotionofsaradomin " .. (role == 2 and 1 or (role == 3 and size > 1 and 2 or 3)) end
    end
    -- the mage's Magic reads 112 (tenth percentile 107; the others 99): a
    -- SATURATED HEART (raid seam46: 99 + 4 + floor(9.9) = 112; content
    -- skill_slayer/scripts/imbued_heart.rs2 [opheld1,saturated_heart]
    -- stat_boost(magic, 4, 10), a five-minute cooldown), not the magic potion
    -- (103) seam40 carried: invigorated at the door (run below; raid seam47)
    if role == 1 then out[#out + 1] = "::give saturated_heart 1" end
    -- owner_nylocas: THE MAGE'S BARRAGE.  Blert's trio mages cast Ice Barrage
    -- on blue clumps (script attack_names SCEPTRE_BARRAGE + UNKNOWN_BARRAGE
    -- 4.7 a room over the 31 waves; ours cast none: no runes in the kit).
    -- Ancient Magicks is the kit's spellbook; the runes take two Saradomin
    -- brews' slots (the mage drank 0-2 doses a room) and three anglerfish'
    -- (Blert role.mage eats in the waves: median 0).
    if role == 1 and size > 1 then
        for i = 1, #out do
            -- (and the last brew for the stamina dose: the reference mage runs the
            -- room too; it drank 0-1 doses on her and the plan brews only without food)
            if out[i] == "::give br_4dosepotionofsaradomin 3" then out[i] = "::give 4dosestamina 1" end
            if out[i] == "::give anglerfish 7" then out[i] = "::give anglerfish 4" end
        end
        out[#out + 1] = "::give water_rune 2000"
        out[#out + 1] = "::give blood_rune 1000"
        out[#out + 1] = "::give death_rune 1000"
    end
    kit = out
end

return {
    id = "_play_nylocas",
    fixture = "fresh_lumbridge.ini",
    max_frames = 150000,
    setup = kit,
    run = function(t)
        local mode = size > 1 and "normal" or "entry"
        t.check("spec.scope", true, size > 1 and ("mode=" .. mode .. " party=" .. size .. " p" .. role) or "mode=entry party=1")
        if role == 1 then t.ticklog.start() end
        local enter_result, enter_detail = t.raid.enter("tob", "nylocas", { mode = mode })
        t.check("enter", enter_result == "ok", (size > 1 and ("p" .. role .. " ") or "") .. tostring(enter_detail))
        -- (a party member holds no world: the raid's state is the leader's to read)
        local fight = nil
        if role == 1 then
            local state_result, state = t.raid.state()
            t.check("state", state_result == "ok" and state.room == "nylocas" and state.mode == mode and state.started == false,
                type(state) == "table" and tostring(state.line) or tostring(state))
            local tile_result, fight_text
            tile_result, fight, fight_text = t.raid.start_tile()
            t.check("start_tile", tile_result == "ok", tostring(fight_text))
        end
        -- the bow on, rapid, auto-retaliate off (tob_nylocas.lua :70-91), before
        -- the barrier: the plan swaps weapons itself and must never swing back
        -- with the wrong one ("a wrong-style swing nulls that nylocas for good")
        if size > 1 and role == 2 then
            local dr, dd = t.player.use_item_on_item("dragon_dart", "toxic_blowpipe")
            t.ticks(2)
            local sr2, sd2 = t.player.use_item_on_item("snakeboss_scale", "toxic_blowpipe_loaded")
            t.ticks(2)
            local er, ed = t.player.equip("toxic_blowpipe_loaded")
            t.ticks(1)
            local cr0, cd0 = t.player.inv_op("eye_of_ayak_uncharged", 3)
            t.ticks(2)
            local ar0, ayaks = t.inv.count("eye_of_ayak")
            local tr0, tears = t.inv.count("demon_tear")
            t.check("setup.ayak", ar0 == "ok" and ayaks == 1 and tr0 == "ok" and tears == 0, "p2 Charge on the uncharged Eye of Ayak "
                .. tostring(cr0) .. " " .. string.sub(tostring(cd0), 1, 80) .. "; eye_of_ayak in the backpack " .. tostring(ayaks)
                .. ", demon tears left " .. tostring(tears))
            t.ticks(1)
            local cr, darts = t.inv.count("dragon_dart")
            t.check("setup.blowpipe", er == "ok" and cr == "ok" and darts == 0, "p2 darts on the pipe " .. tostring(dr) .. " " .. string.sub(tostring(dd), 1, 80)
                .. "; scales " .. tostring(sr2) .. " " .. string.sub(tostring(sd2), 1, 80) .. "; wielded " .. tostring(er) .. " " .. string.sub(tostring(ed), 1, 80)
                .. "; darts left in the backpack " .. tostring(darts))
        else
            if size > 1 and (role == 1 or role == 3) then
                -- raid seam40: the meleer's Ayak is charged the way the mage's is
                local cr0, cd0 = t.player.inv_op("eye_of_ayak_uncharged", 3)
                t.ticks(2)
                local ar, ayaks = t.inv.count("eye_of_ayak")
                local tr, tears = t.inv.count("demon_tear")
                t.check("setup.ayak", ar == "ok" and ayaks == 1 and tr == "ok" and tears == 0, "p" .. role .. " Charge on the uncharged Eye of Ayak "
                    .. tostring(cr0) .. " " .. string.sub(tostring(cd0), 1, 80) .. "; eye_of_ayak in the backpack " .. tostring(ayaks)
                    .. ", demon tears left " .. tostring(tears))
                if role == 1 then
                    local br, blood = t.inv.count("bloodrune")
                    local dr2, death = t.inv.count("deathrune")
                    local wr, water = t.inv.count("waterrune")
                    t.check("setup.barrage_runes", br == "ok" and blood == 1000 and death == 1000 and water == 2000,
                        "p1 Ice Barrage runes in the backpack: blood " .. tostring(blood) .. " death " .. tostring(death) .. " water " .. tostring(water))
                end
            end
            if size <= 1 then t.player.equip("rune_arrow") end
            -- raid seam33: a trio seat starts with its own colour's weapon on
            -- (the plan reads it as worn): the mage the Ayak, the meleer the whip
            if size > 1 and role == 1 then t.player.equip("eye_of_ayak")
            elseif size > 1 and role == 3 then t.player.equip("abyssal_whip")
            else t.player.equip("magic_shortbow") end
        end
        -- raid seam55 play_tob_nylocas_stands_like_blert: THE STYLE BY NAME
        -- (t.ui.style), never a slot number.  Every seat pressed slot 1, so
        -- every scythe swing in the room was Chop, which this content's
        -- scythe table makes stab (seam52 kit.melee_damage_per_swing.md;
        -- ui.lua QD.ui.style).  The ranger's pipe and the solo's bow: Rapid.
        -- The meleer's whip: Lash.  The mage's Ayak shows Bash / Pound /
        -- Focus here (seam55 probe_style2: a staff's table, where the wiki's
        -- powered staff shows Accurate, Accurate, Longrange), so its style is
        -- set by name on the pipe it also carries -- Rapid, index 1 -- and the
        -- Ayak is worn back on that index, the powered staff's second
        -- Accurate.  At her, the plan picks Reap on the scythe and Rapid on
        -- the bow by name (the plan's boss_styles).
        local style_name = "Rapid"
        if size > 1 and role == 3 then style_name = "Lash" end
        if size > 1 and role == 1 then
            t.player.equip("toxic_blowpipe_loaded")
            t.ticks(1)
        end
        local tab_result, tab_detail = t.ui.tab("combat")
        t.ticks(1)
        local style_result, style_detail = t.ui.style(style_name)
        if size > 1 and role == 1 then
            t.player.equip("eye_of_ayak")
            t.ticks(2)
            -- (the equip shows the inventory; the retaliate button below is
            -- the combat tab's and lands only while that tab is shown)
            t.ui.tab("combat")
            t.ticks(1)
        end
        local style_read_result, style_read = t.var.varp("varp43_com_mode")
        t.check("setup.style", style_result == "ok" and style_read_result == "ok",
            "p" .. role .. " combat style " .. style_name .. " by name: tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. "; "
            .. tostring(style_detail) .. "; varp43_com_mode reads " .. tostring(style_read))
        local retaliate_before_result, retaliate_before = t.var.varp("varp172_option_nodef")
        local retaliate_widget_result, retaliate_widget = t.ui.widget("combat_interface:retaliate")
        local retaliate_press_result = "skipped"
        if retaliate_before == 0 then retaliate_press_result = t.ui.invoke(retaliate_widget, 1) end
        t.ticks(2)
        local retaliate_after_result, retaliate_after = t.var.varp("varp172_option_nodef")
        t.check("setup.retaliate_off", retaliate_after == 1, "combat tab auto-retaliate button: varp172_option_nodef read " .. tostring(retaliate_before)
            .. " before, press " .. tostring(retaliate_press_result) .. ", " .. tostring(retaliate_after) .. " after (1 is off)")
        if role == 1 then t.player.walk_to(fight.x + 1, fight.z, 20) end
        if size > 1 then
            -- raid seam40: one dose of each boost at the door (THE BOOSTS above)
            local pr1 = t.player.inv_op("4dosedivinecombat", 1, { quick = true })
            -- (a drink holds the next one off: ny40g's second press, one tick
            -- later, answered timeout)
            t.ticks(3)
            local pr2 = t.player.inv_op("4dosedivinerange", 1, { quick = true })
            t.ticks(1)
            -- owner_nylocas: a stamina dose at the door (the relay's: "If you're in
            -- a melee role ... I'd strongly advise buying a stamina potion",
            -- 10Boot yt_4i4lv-srJkw.md 0:12:09; _play_normal.lua KIT); the plan
            -- drinks the rest and re-toggles run when it runs out (P.run_keep)
            t.player.inv_op("4dosestamina", 1, { quick = true })
            t.ticks(1)
            if role == 1 then
                -- raid seam47: the heart's Invigorate (op 1) at the door, as
                -- Blert's mages carry 112; the plan re-uses it when it is ready
                t.ticks(2)
                local hr, hd = t.player.inv_op("saturated_heart", 1, { quick = true })
                t.ticks(2)
                local mr, magic = t.skill.read("magic")
                -- (the heart is not used up, so inv_op's cell watch answers
                -- `timeout` on a press that landed: s47a "slot still
                -- saturated_heart", Magic 112; the Magic level is the proof)
                -- owner_nylocas: within one level of the boost -- the game's
                -- one-level-a-minute decay can tick between the press and this
                -- read two ticks later (gear5 survey: 111 on every name once
                -- the kit grew by seven lines; 112 before)
                local boosted = magic ~= nil and magic.base_level + 4 + math.floor(magic.base_level / 10) or nil
                t.check("play.heart", (hr == "ok" or hr == "timeout") and mr == "ok" and magic.level >= boosted - 1 and magic.level <= boosted,
                    "p1 Invigorate on the saturated heart " .. tostring(hr) .. " " .. string.sub(tostring(hd), 1, 80) .. "; magic "
                    .. tostring(magic and magic.level) .. " of base " .. tostring(magic and magic.base_level)
                    .. " (want base + 4 + 10 percent: 112 at 99, Blert's trio mages' level in the waves)")
            end
            local sr, strength = t.skill.read("strength")
            local rr, ranged = t.skill.read("ranged")
            t.check("play.potion", sr == "ok" and rr == "ok" and strength.level > 99 and ranged.level > 99,
                "p" .. role .. " super combat and ranging before the barrier: strength " .. tostring(strength and strength.level)
                .. ", ranged " .. tostring(ranged and ranged.level) .. " (" .. tostring(pr1) .. ", " .. tostring(pr2) .. ")")
        end
        -- owner_nylocas: THE DOOR TOGETHER.  Blert's trios are all three at
        -- the barrier on room tick 0 (the 27 reference streams, player rows of
        -- tick 0: local 31,30 / 31,30 / 32,32 and the like, every room) and
        -- all three in the middle by wave 1 (script w1 spawn_tile mage 1,1,
        -- melee 2,1, range -3,2 from 30,23).  The members used to wait where
        -- t.raid.enter left them (local 31,49) and walk only after "started":
        -- at wave 1 (t103) they were 22 and 18 tiles north of the middle
        -- (seam56 it2 wave diff, FIRST DEVIATION w1 melee tile 1,18; the
        -- leader's tick log: p1/p2 at 6431,113 until t102, at the barrier
        -- t111).  A member now stands on the barrier's north side, beside the
        -- leader's tile (fight.x + 1, fight.z = local 32,32), before the
        -- entrance barrier, and crosses the tick the fight starts.
        if size > 1 and role ~= 1 then
            local _, here = t.world.tile()
            local ox, oz = math.floor(here.x / 64) * 64, math.floor(here.z / 64) * 64
            local door_x, door_z = ox + 29 + role, oz + 33
            local door_result, door_detail = t.player.walk_to(door_x, door_z, 30)
            local _, at = t.world.tile()
            t.check("door.together", door_result == "ok" and at ~= nil and at.x == door_x and at.z == door_z,
                "p" .. role .. " to the barrier's north side local " .. (door_x - ox) .. "," .. (door_z - oz) .. ": "
                .. tostring(door_result) .. " " .. string.sub(tostring(door_detail), 1, 80) .. "; at "
                .. tostring(at and (at.x - ox)) .. "," .. tostring(at and (at.z - oz)))
        end
        if size > 1 then t.expect("party.barrier.entrance", t.party.barrier("entrance", 300)) end
        if size > 1 then
            -- owner_nylocas: THE TRIO THROUGH THE BARRIER ON ONE TICK
            -- (t.raid.cross_together, raid_play.lua, owner_tob_normal).  The
            -- machine's first deviation on every name was the ranger's first
            -- wave-1 attack at +3 against the script's +1 (the bow on the west
            -- green from the middle): the members pressed the barrier after the
            -- "started" party barrier and crossed 3 ticks behind the room's
            -- start (svaplaynyloc: the ranger on 31,30 on the spawn tick, t110,
            -- d14, its bow walking 2 ticks before the shot).  Blert's three are
            -- at the barrier together on room tick 0.
            local mr, md = nil, nil
            local xr, xd = t.raid.cross_together("nylocas", { at_answer = function() mr, md = t.ticklog.mark("room start") end })
            t.check("barrier.cross_together", xr == "ok", "p" .. role .. " " .. tostring(xd))
            if role == 1 then t.check("barrier.mark", mr == "ok", tostring(md)) end
        elseif role == 1 then
            local click_result, click_detail = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.click", click_result == "ok", tostring(click_detail))
            local play_result, play_detail = t.chat.play({ "options", "choose:Yes, begin the fight." })
            t.check("barrier.confirm", play_result == "ok", tostring(play_detail))
            t.ticklog.mark("room start")
            if size > 1 then t.expect("party.barrier.started", t.party.barrier("started", 900)) end
        else
            -- the fight is running: the barrier is only a gate now (tob_party.rs2
            -- oploc1 tob_arena_barrier steps the raider across)
            t.expect("party.barrier.started", t.party.barrier("started", 900))
            local cross_result, cross_detail = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.cross", cross_result == "ok", "p" .. role .. " " .. tostring(cross_detail))
        end
        -- the kept test's camera (tob_nylocas.lua :69): high and far, the whole room in frame
        t.drive.camera(0, 512, 1100)

        -- THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_nylocas", { mode = mode, max_ticks = size > 1 and 3000 or 2000 })
        local ny = rec.ny or {}
        -- owner_nylocas PRESS TRACE (the plan's comment at press_now): a note row
        -- per press, only when the test asked for it
        if t.raid.nylocas_press_trace then
            for _, line in ipairs(ny.press_trace or {}) do
                t.check("note.press", true, "p" .. role .. " " .. line)
            end
        end
        -- raid seam55: the plan's styles at her, by name (P.boss_styles)
        if size > 1 then
            t.check("note.run", true, "p" .. role .. " run: varp173 first read 0 at " .. tostring(ny.run_off_first) .. " ticks into the play, "
                .. tostring(ny.run_offs or 0) .. " ticks off, stamina doses " .. tostring(ny.staminas or 0) .. ", orb presses " .. tostring(ny.run_presses or 0))
            t.check("note.style", true, "p" .. role .. " at her: styles set by name " .. tostring(ny.style_set or 0) .. ", presses "
                .. tostring(ny.style_presses or 0) .. ", combat tab opened " .. tostring(ny.style_tabs or 0) .. ", gave up "
                .. tostring(ny.style_gave_up or 0))
        end
        local res = {}
        for k, c in pairs(ny.results or {}) do res[#res + 1] = k .. " " .. c end
        table.sort(res)
        t.check("play.fight", result == "ok", tostring(detail) .. "; waves seen " .. tostring(ny.waves) .. ", presses " .. tostring(ny.presses)
            .. " (" .. table.concat(res, ", ") .. "), casts " .. tostring(ny.casts) .. " (bursts " .. tostring(ny.bursts) .. "), chins " .. tostring(ny.chins)
            .. " (best clump " .. tostring(ny.chin_best) .. ", chinchompas carried " .. tostring(ny.chin_have) .. "), swaps "
            .. tostring(ny.swaps) .. ", blast escapes " .. tostring(ny.escapes) .. ", turn holds " .. tostring(ny.holds) .. ", flicker cancels "
            .. tostring(ny.flicker_cancels) .. ", her turns " .. tostring(#(ny.turns or {}))
            -- raid seam47: swaps whose press rode in the swap's own block, the
            -- specials (armed, fired by the energy spent, lost), heart presses
            .. ", swap+press blocks " .. tostring(ny.block_presses) .. ", specials armed " .. tostring(ny.spec and ny.spec.arms)
            .. " fired " .. tostring(ny.spec and ny.spec.fired) .. " lost " .. tostring(ny.spec and ny.spec.lost) .. " (orb " .. tostring(ny.spec and ny.spec.arm_result)
            .. "), heart re-presses " .. tostring(ny.hearts)
            -- raid seam52: THE STANDS and THE CLEANUP'S ORDER (the plan)
            .. ", walks home/stand " .. tostring(ny.homes) .. " (last stand wave " .. tostring(ny.stand) .. "), cleanup passes " .. tostring(ny.cleanup_passes or 0)
            -- owner_nylocas: her form's gear pieces put on (P.boss_gear)
            .. ", her gear equips " .. tostring(ny.gear_equips or 0)
            -- owner_nylocas: scythe presses on a grey stack
            .. ", grey stack presses " .. tostring(ny.stack_swings or 0)
            -- owner_nylocas: the machine (QD.raid._play_nylocas_machine)
            .. ", machine transitions " .. tostring(ny.transitions or 0) .. " ticks " .. (function()
                local p = {}
                for k, n in pairs(ny.state_ticks or {}) do p[#p + 1] = k .. "=" .. n end
                table.sort(p)
                return table.concat(p, " ")
            end)())

        if role ~= 1 then
            -- raid seam32: a member's own record (its presses are its own; the
            -- leader's tick log carries the room)
            t.check("play.member", ny.presses ~= nil and ny.presses > 0, "p" .. role .. " " .. tostring(ny.role) .. ": presses " .. tostring(ny.presses)
                .. ", walks home/stand " .. tostring(ny.homes) .. ", cleanup passes " .. tostring(ny.cleanup_passes or 0)
                .. ", her gear equips " .. tostring(ny.gear_equips or 0)
                .. ", grey stack presses " .. tostring(ny.stack_swings or 0)
                .. ", machine transitions " .. tostring(ny.transitions or 0) .. " ticks " .. (function()
                    local p = {}
                    for k, n in pairs(ny.state_ticks or {}) do p[#p + 1] = k .. "=" .. n end
                    table.sort(p)
                    return table.concat(p, " ")
                end)()
                .. ", casts " .. tostring(ny.casts) .. ", freezes " .. tostring(ny.freezes) .. ", swaps " .. tostring(ny.swaps) .. ", own colour "
                .. tostring(ny.own_presses) .. " / other colours " .. tostring(ny.other_presses) .. ", flicker cancels " .. tostring(ny.flicker_cancels)
                .. ", presses on her " .. #(ny.vas_presses or {}) .. ", turn holds " .. tostring(ny.holds) .. ", turn steps " .. tostring(ny.turn_steps) .. ", XP-read swings " .. tostring(ny.xp_swings) .. ", nulled read " .. tostring(ny.null_reads) .. ", eats " .. #(rec.eats or {}) .. ", drinks " .. #(rec.drinks or {}))
            t.expect("party.barrier.done", t.party.barrier("done", 9000))
            t.finish(0)
            return
        end
        -- the room's end: the real chat line reaches the raider a few ticks after
        -- her death (tob_nylocas.lua :816-826)
        local complete_text = nil
        if result == "ok" then
            t.ticks(8)
            t.shot("fight.wave_complete_line")
            local _, recent = t.msg.last(80)
            for _, line in ipairs(recent) do
                if string.find(line.text, "complete!", 1, true) ~= nil then complete_text = line.text break end
            end
        end
        local leave_result, leave_detail = t.raid.leave()
        t.check("fight.left", leave_result == "ok", tostring(leave_detail))
        t.ticks(1)

        -- THE TICK LOG: the reads tob_nylocas.lua's ANALYSIS makes for these rows
        ;(function()
        -- raid seam32: the mode's npc ids from the plan's own symbol read
        -- (Entry: the _story records 10774-10789; Normal: the plain ones)
        local ids = ny.ids or { wave = {}, boss = {} }
        local function form_of(type) local b = ids.boss[type] return b and b.form or nil end
        local tick0 = nil
        local _, mark_rows = t.ticklog.rows({ kind = "mark" })
        for i = 1, #mark_rows do
            if mark_rows[i].label == "room start" then tick0 = mark_rows[i].tick end
        end
        tick0 = tick0 or rec.start_tick
        t.ticks(1)
        local boss = { slot = nil, death = nil }
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for _, r in ipairs(deaths) do
            if ids.boss[r.type] ~= nil and boss.death == nil then boss.death, boss.slot = r.tick, r.slot end
        end
        t.ticks(1)
        local _, support_hits = t.ticklog.rows({ kind = "hit_npc" })
        t.ticks(1)
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        t.ticks(1)
        local heals = 0
        if boss.slot ~= nil then
            local _, heal_rows = t.ticklog.rows({ kind = "npc_heal", slot = boss.slot })
            heals = #heal_rows
        end
        t.ticks(1)
        -- the prayer the player READ lit, per tick (the library's record): one
        -- entry per change of the lit protection prayer (the kept test's
        -- prayer_log: {tick, style} per accepted switch)
        local prayer_log, prayer_switches = {}, 0
        local style_of_prayer = { protectfrommelee = "melee", protectfrommagic = "magic", protectfrommissiles = "ranged" }
        local last_style = nil
        for tk = rec.start_tick, (rec.end_tick or rec.start_tick) do
            local lit = rec.prayer_at[tk]
            if lit ~= nil then
                local on = nil
                for name, style in pairs(style_of_prayer) do
                    if lit[name] == true then on = style end
                end
                if on ~= nil and on ~= last_style then
                    prayer_log[#prayer_log + 1] = { tk, on }
                    prayer_switches = prayer_switches + 1
                    last_style = on
                end
            end
        end
        local eaten, brews, lowest = 0, 0, 99
        local bandages = 0
        for _, e in ipairs(rec.eats) do
            eaten = eaten + 1
            if e.item == "tob_bandages" then bandages = bandages + 1 end
        end
        for _, d in ipairs(rec.drinks) do
            if string.find(d.item, "saradomin", 1, true) ~= nil then brews = brews + 1 end
        end
        for _, hp in pairs(rec.hp_at) do
            if hp < lowest then lowest = hp end
        end
        -- tob_nylocas.lua :1519-1566, unchanged: a 0 is a block only when the
        -- prayer held covered the style; her melee form's blocks and landings
        local wave_unprayed, wave_unprayed_zero, wave_unprayed_landed = 0, 0, 0
        local wave_block_count, wave_cover_landed, melee_block_count, melee_cover_landed = 0, 0, 0, 0
        for index, h in ipairs(hits) do
            if index % 100 == 0 then t.ticks(1) end
            local style_of_hit = nil
            local wave_row = h.npc_type ~= nil and ids.wave[h.npc_type] or nil
            if wave_row ~= nil and wave_row.fighting then
                style_of_hit = wave_row.style
            elseif h.npc_type ~= nil and form_of(h.npc_type) ~= nil and form_of(h.npc_type) ~= "spawning" then
                style_of_hit = form_of(h.npc_type)
            end
            -- raid seam32: in a party the leader's prayer log covers its own hits only
            if size > 1 and rec.my_pid ~= nil and h.pid ~= rec.my_pid then style_of_hit = nil end
            if style_of_hit ~= nil then
                local held, last_before = {}, nil
                for _, entry in ipairs(prayer_log) do
                    if entry[1] < h.tick - 6 then last_before = entry[2]
                    elseif entry[1] <= h.tick + 2 then held[entry[2]] = true end
                end
                if last_before ~= nil then held[last_before] = true end
                local covered, only_this = held[style_of_hit] == true, true
                for style_name in pairs(held) do if style_name ~= style_of_hit then only_this = false end end
                if wave_row ~= nil then
                    if covered and only_this and h.damage == 0 then wave_block_count = wave_block_count + 1
                    elseif covered and only_this and h.damage > 0 then wave_cover_landed = wave_cover_landed + 1 end
                    if not covered then
                        wave_unprayed = wave_unprayed + 1
                        if h.damage == 0 then wave_unprayed_zero = wave_unprayed_zero + 1 else wave_unprayed_landed = wave_unprayed_landed + 1 end
                    end
                elseif form_of(h.npc_type) == "melee" then
                    if covered and only_this and h.damage == 0 then melee_block_count = melee_block_count + 1
                    elseif h.damage > 0 and not covered then melee_cover_landed = melee_cover_landed + 1 end
                end
            end
        end
        -- ===== COPIED UNCHANGED from test/raids/tob_nylocas.lua :1887-1894 (the room's end) =====
        -- the room's end
        local plain_end = complete_text ~= nil and string.gsub(string.gsub(complete_text, "<br>", " "), "<[^>]*>", "") or "none"
        local dur_minutes, dur_seconds = string.match(plain_end, "Duration:%s*(%d+):(%d+)")
        local dur_total = dur_minutes ~= nil and (tonumber(dur_minutes) * 60 + tonumber(dur_seconds)) or -1
        local expected_seconds = boss.death ~= nil and (boss.death - tick0) * 0.6 or -1
        t.check("room.complete_line", complete_text ~= nil and string.find(plain_end, size > 1 and " Mode) complete!" or "(Entry Mode) complete!", 1, true) ~= nil and dur_total > 0
            and math.abs(dur_total - expected_seconds) <= 6,
            "chat line '" .. plain_end .. "': duration " .. dur_total .. " seconds against " .. string.format("%.1f", expected_seconds) .. " seconds from the barrier mark (tick " .. tick0 .. ") to her npc_death row (tick " .. tostring(boss.death) .. ")")
        -- ===== THE TECHNIQUE ROWS (from the tick log) =====
        local kills = { melee = 0, ranged = 0, magic = 0, fighting = 0, incoming = 0 }
        local _, deaths = t.ticklog.rows({ kind = "npc_death" })
        for index, r in ipairs(deaths) do
            if index % 60 == 0 then t.ticks(1) end
            if ids.wave[r.type] ~= nil then
                local style = ids.wave[r.type].style
                kills[style] = kills[style] + 1
                if ids.wave[r.type].fighting then kills.fighting = kills.fighting + 1 else kills.incoming = kills.incoming + 1 end
            end
        end
        local nulled, damaging = 0, 0
        for index, r in ipairs(support_hits) do
            if index % 60 == 0 then t.ticks(1) end
            if r.type ~= nil and ids.wave[r.type] ~= nil then
                if r.damage == 0 then nulled = nulled + 1 else damaging = damaging + 1 end
            end
        end
        t.check("tech.style_kills", kills.melee + kills.ranged + kills.magic > 10, "killed by the player with the weapon of the nylocas' style (whip, "
            .. "shortbow, Fire Strike): melee " .. kills.melee .. ", ranged " .. kills.ranged .. ", magic " .. kills.magic .. "; " .. damaging
            .. " hit_npc rows with damage and " .. nulled .. " at 0 on nylocas")
        t.check("tech.aggro_first", kills.fighting > 0, "kills of the fighting (aggro) forms " .. kills.fighting
            .. " against the incoming forms " .. kills.incoming .. ": the ones that swing at the player are met first")
        t.check("tech.food", (eaten + brews) > 0 and lowest > 0, "sharks eaten " .. eaten .. ", brew doses " .. brews .. ", lowest hitpoints " .. lowest .. ", eaten from 78 (84 with three copies swinging) and below, one at a time, the next only once the hitpoint read had risen")
        -- a 0 is a block only when the prayer the test held covered the style (prayer_log); a 0 with no prayer on its style is a miss (swing_miss_entry)
        -- raid seam49: in a party the leader is the MAGE, not the tank: the
        -- solo room's 20 wave blocks (every wave swing is at the one raider)
        -- is no trio number -- 9-11 wave swings a room reach the trio mage at
        -- all (seam49 survey_a).  The trio's wave half is the reference's
        -- range of what the mage's prayer and footing let through:
        -- reference/nylocas_normal_3.json outcome.hp_lost.mage 67 [24-120]
        -- (14 recorded mages; hit_player damage on the leader's pid, the
        -- room's every source).  Her melee form's half is unchanged.
        local leader_taken = 0
        for _, h in ipairs(hits) do
            if (h.damage or 0) > 0 and rec.my_pid ~= nil and h.pid == rec.my_pid then leader_taken = leader_taken + h.damage end
        end
        local wave_half = wave_block_count >= 20
        -- owner_nylocas: at most the reference's most (120).  The lower end
        -- (24, the least any of the 14 recorded mages lost) measures how much
        -- reached the mage, not what its prayer let through: a mage the
        -- others cover loses less (the big5 survey: 20 on two names, every
        -- wave swing that reached it blocked, 14 and 22 blocks)
        if size > 1 then wave_half = rec.my_pid ~= nil and leader_taken <= 120 end
        t.check("tech.prayer", wave_half and melee_block_count > melee_cover_landed, (size > 1 and ("the leader (the mage) lost " .. leader_taken
            .. " hitpoints in the room; reference outcome.hp_lost.mage 67 [24-120]; ") or "") .. "protection prayer switched " .. prayer_switches .. " times to the style of the swinging majority (a big counts two, a melee copy only "
            .. "inside four tiles): " .. wave_block_count .. " hit_player rows from wave nylocas at 0 while the prayer held covered their style (blocks), against " .. wave_unprayed_zero
            .. " zeros with no prayer on their style (misses, not counted) and " .. wave_unprayed_landed .. " that landed with none on (the other two styles, and explosions); "
            .. "her melee form: " .. melee_block_count .. " blocked against " .. melee_cover_landed .. " landed on the switch ticks before the prayer followed")
        -- ===== the plan's own measurements (PLAY_NOTES.md "Nylocas, Entry solo") =====
        -- the reflect: every wrong-colour hit on her heals her (DMG :278-300);
        -- the play never sends one, so her npc_heal rows are none
        t.check("play.no_reflect", boss.slot ~= nil and heals == 0, "npc_heal rows on her slot " .. tostring(boss.slot) .. ": " .. heals
            .. "; presses on her " .. #(ny.vas_presses or {}) .. ", turns " .. #(ny.turns or {}) .. ", turn holds " .. tostring(ny.holds))
        local taken, by_kind = 0, {}
        for _, h in ipairs(hits) do
            if h.damage > 0 and (size == 1 or rec.my_pid == nil or h.pid == rec.my_pid) then
                taken = taken + h.damage
                local key = tostring(h.npc_type)
                by_kind[key] = (by_kind[key] or 0) + h.damage
            end
        end
        local kinds = {}
        for k, d in pairs(by_kind) do kinds[#kinds + 1] = k .. ":" .. d end
        table.sort(kinds)
        t.check("note.damage", true, "taken " .. taken .. " (by npc type " .. table.concat(kinds, " ") .. "), food eaten " .. eaten .. " (bandages " .. bandages .. ", the interlude one at tick " .. tostring(ny.boosted) .. "), brew doses " .. brews
            .. ", lowest " .. lowest .. ", room " .. tostring(boss.death and (boss.death - tick0) or "none") .. " ticks from the mark to her death")
        if size > 1 then
            -- ===== raid seam32: THE TRIO'S ROWS (Normal) =====
            -- the waves: a wave is a tick wave copies spawn in the tunnels (NT
            -- nylocas.lane_tiles; the plan's own lane test), 31 of them (NT
            -- nylocas.wave_count); with no stall the last one is out 236 ticks
            -- after the first wave check (NT nylocas.natural_stall_sum 232 +
            -- first_wave 4): every tick past it is a stall the cap forced
            local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
            local wave_tick, wave_list, per_pid_taken = {}, {}, {}
            for index, r in ipairs(spawns) do
                if index % 60 == 0 then t.ticks(1) end
                if ids.wave[r.type] ~= nil and r.coord ~= nil then
                    local x, z = math.floor(r.coord / 16384) % 16384, r.coord % 16384
                    local lx, lz = x - rec.origin.x, z - rec.origin.z
                    if (lx <= 18 or lx >= 45 or lz <= 10) and wave_tick[r.tick] == nil then
                        wave_tick[r.tick] = true
                        wave_list[#wave_list + 1] = r.tick - tick0
                    end
                end
            end
            for _, h in ipairs(hits) do
                if h.damage > 0 then per_pid_taken[h.pid] = (per_pid_taken[h.pid] or 0) + h.damage end
            end
            local taken_list = {}
            for pid, d in pairs(per_pid_taken) do taken_list[#taken_list + 1] = "pid " .. pid .. " " .. d end
            table.sort(taken_list)
            local last_wave = wave_list[#wave_list]
            t.check("tech.waves", #wave_list == 31, "waves out " .. #wave_list .. " (spec 31), the last " .. tostring(last_wave) .. " ticks after the mark against 236 with no stall ("
                .. tostring(last_wave and (last_wave - 236) or "?") .. " ticks stalled); her landing " .. tostring(ny.landed and rec.start_tick and (ny.landed - rec.start_tick) or "none")
                .. " ticks into the play; taken by raider: " .. table.concat(taken_list, ", "))
            -- the pillars on the tick she landed (the plan's read of their bars)
            -- raid seam33 play_tob_nylocas_normal_green: KEPT asks every
            -- support standing when she lands (seam32 kept one standing at
            -- 0.01-0.11: one collapse from a wipe).
            -- raid seam35m play_tob_nylocas_normal_supports: THE BAR IS WHAT
            -- REAL TRIOS SHOW.  Seam33's "each above 0.50" was not sourced.
            -- Read from 34 completed Regular trio rooms on blert (30 harvested
            -- by tools/measure_tob_pillar_damage.py harvest 3, mode 11, plus
            -- the 4 trio streams of the spec pass), with that tool's own bite
            -- model up to her first event, 0.90 a bite (ENCOUNTER_TIMING 4.5,
            -- the ceiling) over 230 hitpoints (380 - 50 x 3, Jagex 21 June 2018):
            -- all four standing in 34 of 34; the weakest at her landing is a
            -- median 0.31, range 0.10..0.54; only 3 of 34 have all four above
            -- 0.50 (0 of 34 at 1.0 a bite).  So: four standing, the weakest
            -- at or above the weakest real trio's 0.10.
            t.check("tech.pillars_at_boss", (ny.supports_alive_at_landing or 0) == 4 and (ny.supports_min_at_landing or 0) >= 0.10,
                "supports standing when Vasilias landed: " .. tostring(ny.supports_alive_at_landing)
                .. " of 4 (need 4, the weakest at or above 0.10: real trios 34 of 34 four standing, weakest 0.10..0.54, median 0.31), bars (local x,z:fraction) "
                .. tostring(ny.supports_at_landing) .. "; the leader's role " .. tostring(ny.role))
            -- raid seam47 play_tob_nylocas_no_nulling: THE REFERENCE'S CLOCK,
            -- as _play_maiden.lua's ref.* rows: the bar is the RANGE of the 27
            -- death-free Normal scale-3 rooms (docs/minigames/theater_of_blood/
            -- sources/blert_api/reference/nylocas_normal_3.json numbers
            -- outcome.phase.wave31.start 260 [244-293], outcome.phase.boss.start
            -- 308 [296-357], outcome.phase.boss.ticks 95 [75-123],
            -- outcome.boss_death_tick 410 [371-471]).  Blert's room tick is
            -- ours + 1 from the mark: wave 1 is out on mark + 3 in every s47 run
            -- against the reference's 4 [4-8] (ENCOUNTER_TIMING M16's +-1
            -- recorder alignment).
            local boss_spawn = nil
            for _, r in ipairs(spawns) do
                if ids.boss[r.type] ~= nil and boss_spawn == nil and r.tick >= tick0 then boss_spawn = r.tick end
            end
            local ref_last = last_wave and (last_wave + 1) or nil
            local ref_start = boss_spawn and (boss_spawn - tick0 + 1) or nil
            local ref_phase = (boss_spawn and boss.death) and (boss.death - boss_spawn) or nil
            local ref_death = boss.death and (boss.death - tick0 + 1) or nil
            t.check("ref.last_wave", ref_last ~= nil and ref_last >= 244 and ref_last <= 293, "wave 31 out at room tick " .. tostring(ref_last)
                .. "; reference/nylocas_normal_3.json outcome.phase.wave31.start 260 [244-293]")
            -- OWNER RULING 2026-10-07 ("Call nylocas good yeah", relayed by the
            -- coordinator; the Sotetseg rule): a Nylocas room that completes with
            -- no deaths is green even when it is slower than Blert, so the
            -- boss-start and room-tick rows only REPORT the range now -- they
            -- still need her spawn / her death to exist.  ref.last_wave and
            -- ref.boss_ticks keep their bounds.
            t.check("ref.boss_start", ref_start ~= nil, "Vasilias spawned at room tick " .. tostring(ref_start)
                .. "; reference outcome.phase.boss.start 308 [296-357] -- reported, not a bound (owner ruling 2026-10-07)"
                .. ((ref_start ~= nil and (ref_start < 296 or ref_start > 357)) and " -- OUTSIDE the range" or ""))
            t.check("ref.boss_ticks", ref_phase ~= nil and ref_phase >= 75 and ref_phase <= 123, "her phase " .. tostring(ref_phase)
                .. " ticks (spawn " .. tostring(boss_spawn) .. " to death " .. tostring(boss.death) .. "); reference outcome.phase.boss.ticks 95 [75-123]")
            t.check("ref.room_ticks", ref_death ~= nil, "her death at room tick " .. tostring(ref_death)
                .. "; reference outcome.boss_death_tick 410 [371-471] -- reported, not a bound (owner ruling 2026-10-07)"
                .. ((ref_death ~= nil and (ref_death < 371 or ref_death > 471)) and " -- OUTSIDE the range" or ""))
            -- the member's own-swing read (XP paid) against the log's
            -- player_anim swings, on the leader where both exist
            local probe, logged = {}, {}
            for i = 1, math.min(12, #(ny.xp_probe or {})) do probe[#probe + 1] = ny.xp_probe[i] end
            for i = 1, math.min(12, #(rec.swings or {})) do logged[#logged + 1] = rec.swings[i] end
            -- her turns: the plan's read against the log's npc_retype rows
            local plan_turns, log_turns = {}, {}
            for i = 1, math.min(8, #(ny.turns or {})) do plan_turns[#plan_turns + 1] = ny.turns[i].tick end
            if boss.slot ~= nil then
                local _, retypes = t.ticklog.rows({ kind = "npc_retype", slot = boss.slot })
                for i = 1, #retypes do
                    if #log_turns < 9 then log_turns[#log_turns + 1] = retypes[i].tick end
                end
            end
            t.check("note.reads", true, "leader pid " .. tostring(rec.my_pid) .. " (players() said " .. tostring(ny.pid_was) .. "); XP-read swings "
                .. table.concat(probe, ",") .. " against logged swings " .. table.concat(logged, ",") .. "; her turns read " .. table.concat(plan_turns, ",")
                .. " against retypes " .. table.concat(log_turns, ",") .. "; turn steps " .. tostring(ny.turn_steps) .. ", holds " .. tostring(ny.holds) .. ", nulled read " .. tostring(ny.null_reads))
            t.expect("party.barrier.done", t.party.barrier("done", 9000))
            t.finish(0)
        end
        end)()
    end,
}
