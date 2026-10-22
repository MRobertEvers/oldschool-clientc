-- tfa_ocellus_p3: Ocellus's change from phase 2 to 3 and phase 3 itself,
-- for the tick log, against RSProx index 4301 (three raiders,
-- docs/rsprox-analysis/guthixian_archive/ocellus_plan.md §4): phase 2 from
-- `::tfaocphase 2`, the party scaled to three, the bot on the safe spot; then
-- phase 3 with the bot in melee beside him and his health replayed from 4301's
-- (its hud, by the tick from his change P), so his rotation and each
-- quadrant's fall come on the recording's ticks.
--
--   ./src/build_oc3_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocellus_p3.lua --bots 1 \
--       --name tfa0 --ticks 3000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

-- 4301's hud health from P = [1235], {P+k, health} at each change.
local HP4301 = { {0,7018}, {10,7003}, {12,6960}, {14,6956}, {16,6929}, {18,6875}, {21,6835}, {22,6831}, {23,6804}, {24,6743}, {26,6740}, {28,6703}, {30,6698}, {31,6624}, {33,6554}, {34,6519}, {36,6503}, {38,6499}, {41,6428}, {42,6426}, {44,6355}, {46,6317}, {48,6259}, {49,6217}, {53,6152}, {54,6149}, {58,6147}, {59,6137}, {62,6077}, {64,6076}, {65,6015}, {66,6014}, {67,5979}, {68,5978}, {71,5934}, {72,5931}, {73,5852}, {76,5851}, {77,5826}, {78,5796}, {80,5793}, {82,5774}, {83,5742}, {84,5741}, {86,5740}, {87,5701}, {91,5674}, {92,5673}, {93,5652}, {99,5648}, {111,5629}, {113,5614}, {119,5612}, {122,5580}, {123,5579}, {124,5576}, {125,5534}, {127,5497}, {131,5495}, {132,5414}, {135,5411}, {136,5408}, {140,5402}, {143,5400}, {145,5324}, {146,5257}, {147,5256}, {149,5202}, {150,5145}, {151,5108}, {153,5107}, {155,5104}, {159,5103}, {160,5072}, {163,5071}, {167,5068}, {169,5067}, {170,4994}, {174,4991}, {175,4957}, {178,4955}, {179,4954}, {182,4952}, {187,4951}, {188,4939}, {191,4935}, {192,4900}, {195,4881}, {198,4852}, {199,4805}, {202,4756}, {203,4753}, {205,4680}, {206,4624}, {207,4620}, {209,4532}, {210,4517}, {211,4515}, {215,4510}, {218,4487}, {219,4486}, {220,4459}, {222,4441}, {223,4436}, {225,4429}, {228,4420}, {230,4414}, {231,4412}, {232,4397}, {233,4378}, {235,4377}, {236,4331}, {238,4293}, {239,4235}, {241,4179}, {243,4098}, {244,4055}, {246,4021}, {247,4018}, {248,4010}, {249,3980}, {251,3939}, {252,3936}, {254,3901}, {255,3891}, {256,3871}, {259,3848}, {260,3813}, {262,3812}, {263,3811}, {264,3778}, {265,3738}, {267,3737}, {268,3704}, {270,3688}, {272,3687}, {273,3665}, {274,3663}, {275,3625}, {278,3592}, {279,3591}, {303,3529}, {305,3484}, {313,3480}, {317,3476}, {320,3332}, {321,3330}, {325,3327}, {328,3315}, {329,3314}, {333,3309}, {334,3245}, {336,3075}, {337,3070}, {341,3067}, {345,3064}, {353,3062}, {355,2990}, {357,2986}, {361,2983}, {365,2982}, {368,2961}, {369,2959}, {370,2904}, {373,2852}, {377,2851}, {380,2729}, {381,2724}, {385,2723}, {388,2681}, {392,2604}, {394,2602}, {396,2568}, {398,2567}, {400,2564}, {402,2561}, {404,2560}, {405,2483}, {406,2480}, {408,2479}, {409,2403}, {410,2400}, {412,2397}, {416,2394}, {418,2392}, {419,2322}, {420,2319}, {424,2184}, {425,2181}, {428,2107}, {429,2105}, {433,2104}, {434,2073}, {435,2001}, {440,1973}, {441,1972}, {448,1971}, {450,1966}, {451,1962}, {456,1928}, {460,1927}, {462,1888} }

-- 4309's (four raiders, its segment 2 falling straight after a spin with no
-- Untether), in the same percent of a party of three's health.
local HP4309 = { {0,6828}, {9,6795}, {10,6785}, {14,6742}, {15,6711}, {16,6682}, {19,6680}, {20,6574}, {21,6572}, {24,6537}, {25,6498}, {26,6494}, {29,6468}, {30,6439}, {31,6437}, {32,6435}, {34,6389}, {35,6387}, {36,6384}, {39,6295}, {40,6294}, {41,6285}, {44,6278}, {45,6247}, {46,6240}, {47,6239}, {48,6238}, {49,6170}, {51,6140}, {52,6139}, {53,6138}, {55,6136}, {56,6128}, {57,6126}, {59,6091}, {60,6090}, {61,6042}, {64,6039}, {65,6039}, {66,5953}, {69,5948}, {70,5919}, {72,5918}, {73,5902}, {74,5901}, {75,5872}, {76,5870}, {78,5832}, {80,5803}, {81,5802}, {84,5800}, {85,5798}, {88,5796}, {89,5775}, {90,5774}, {91,5760}, {92,5759}, {93,5734}, {94,5733}, {95,5725}, {96,5724}, {98,5723}, {99,5704}, {114,5641}, {115,5509}, {116,5447}, {118,5423}, {120,5380}, {125,5332}, {126,5311}, {129,5282}, {131,5257}, {133,5219}, {135,5166}, {136,5152}, {141,5089}, {143,5061}, {145,5018}, {146,4957}, {151,4951}, {156,4869}, {157,4826}, {161,4796}, {162,4747}, {167,4736}, {172,4696}, {173,4651}, {177,4625}, {178,4569}, {181,4567}, {182,4538}, {183,4501}, {184,4473}, {185,4470}, {187,4454}, {188,4407}, {189,4404}, {191,4390}, {192,4366}, {193,4328}, {194,4314}, {197,4280}, {198,4261}, {199,4220}, {201,4218}, {202,4159}, {203,4139}, {205,4133}, {207,4094}, {208,4060}, {209,4058}, {210,4041}, {211,4039}, {212,4014}, {213,3995}, {215,3994}, {216,3961}, {217,3960}, {219,3894}, {221,3878}, {222,3860}, {225,3859}, {227,3858}, {241,3792}, {242,3705}, {243,3658}, {245,3669}, {246,3638}, {249,3649}, {251,3623}, {253,3632}, {255,3630}, {256,3548}, {257,3557}, {259,3530}, {261,3464}, {264,3424}, {266,3353}, {267,3352}, {268,3302}, {269,3260}, {271,3216}, {273,3192}, {275,3191}, {276,3086}, {278,3084}, {279,3081}, {281,2987}, {283,2968}, {286,2909}, {287,2907}, {288,2869}, {291,2814}, {293,2834}, {296,2776}, {297,2787}, {299,2785}, {303,2782}, {306,2721}, {307,2719}, {311,2637}, {313,2603}, {314,2566}, {315,2564}, {316,2525}, {321,2474}, {324,2431}, {326,2406}, {327,2405}, {329,2358}, {331,2272}, {334,2254}, {336,2245}, {338,2240}, {339,2238}, {344,2231}, {346,2176}, {347,2118}, {349,2100}, {351,2096}, {352,2073}, {354,2023}, {355,2021}, {356,2018}, {359,2006}, {361,1990}, {362,1969}, {364,1959}, {365,1956}, {367,1955}, {368,1938}, {369,1924}, {371,1924}, {375,1918}, {377,1917} }

-- The curve replayed (HP4309 for the fall with no Untether).
local CURVE = HP4301

local function recorded_hp(k)
    local hp = CURVE[1][2]
    for _, row in ipairs(CURVE) do
        if row[1] > k then break end
        hp = row[2]
    end
    return hp
end

local EAT = { eat = { item = "shark", below = 50 } }

return {
    id = "tfa_ocellus_p3",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::give abyssal_whip 1",
        "::wield abyssal_whip",
        "::give shark 20",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.cheat("::tfaocphase 2", false)
        local deadline = now(t) + 300
        while now(t) < deadline and not present(t, "tfa_ocellus_p2") do
            t.cheat("::tfaheal", false)
            t.ticks(1)
        end
        t.check("p2.reached", present(t, "tfa_ocellus_p2"), "no phase 2")
        t.cheat("::tfaparty 3", false)
        t.ticklog.mark("phase2")
        -- Phase 2 on the safe spot to its change (T+131).
        deadline = now(t) + 200
        while now(t) < deadline and not present(t, "tfa_ocellus_p3") do
            t.cheat("::tfaheal", false)
            t.cheat("::tfaocsafe", false)
            t.ticks(1)
        end
        t.check("p3.reached", present(t, "tfa_ocellus_p3"), "no phase 3")
        local P = now(t)
        t.ticklog.mark("phase3")
        t.note("P = " .. P)
        t.prayer.set("protectfrommelee", true)
        -- Phase 3: beside him in melee (the vine first), off each quadrant as
        -- it falls; his health 4301's at the same P+k.
        -- A cheat lands on the next tick: send the next tick's health, again
        -- after anything that may have spent a tick.
        local function replay()
            local k = now(t) - P
            if k >= 6 and present(t, "tfa_ocellus_p3") then
                t.cheat("::tfaboss " .. tostring(recorded_hp(k + 1)), false)
            end
        end
        while now(t) < P + 520 do
            replay()
            t.cheat("::tfaheal", false)
            t.cheat("::tfaocsafe", false)
            if (now(t) - P) % 3 == 0 then
                if present(t, "tfa_ocellus_p3_void_vine") then
                    t.player.attack("tfa_ocellus_p3_void_vine", 2, 1, EAT)
                elseif present(t, "tfa_ocellus_p3") then
                    t.player.attack("tfa_ocellus_p3", 2, 1, EAT)
                end
                replay()
            end
            t.ticks(1)
        end
        t.ticklog.mark("end")
        local hr, hv = t.skill.read("hitpoints")
        t.check("alive", hr == "ok" and type(hv) == "table" and (hv.level or 0) > 0, "dead")
        t.expect("tfa_ocellus_p3.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
