/*
 * Copyright (c) 2024 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.events;

import lombok.Getter;

public enum EventType {
    // These events are sent via server messages, so they do not have IDs.
    CHALLENGE_START(-1),
    CHALLENGE_END(-1),
    CHALLENGE_UPDATE(-1),
    STAGE_UPDATE(-1),

    PLAYER_UPDATE(4),
    PLAYER_ATTACK(5),
    PLAYER_DEATH(6),
    NPC_SPAWN(7),
    NPC_UPDATE(8),
    NPC_DEATH(9),
    NPC_ATTACK(10),
    PLAYER_SPELL(11),

    MAIDEN_CRAB_LEAK(100),
    MAIDEN_BLOOD_SPLATS(101),

    BLOAT_DOWN(110),
    BLOAT_UP(111),
    BLOAT_HANDS_DROP(112),
    BLOAT_HANDS_SPLAT(113),

    NYLO_WAVE_SPAWN(120),
    NYLO_WAVE_STALL(121),
    NYLO_CLEANUP_END(122),
    NYLO_BOSS_SPAWN(123),

    SOTE_MAZE_PROC(130),
    SOTE_MAZE_PATH(131),
    SOTE_MAZE_END(132),

    XARPUS_PHASE(140),
    XARPUS_EXHUMED(141),
    XARPUS_SPLAT(142),

    VERZIK_PHASE(150),
    VERZIK_BOUNCE(154),
    VERZIK_REDS_SPAWN(151),
    VERZIK_YELLOWS(153),
    VERZIK_HEAL(155),
    VERZIK_DAWN(156),
    VERZIK_DAWN_DROP(157),

    COLOSSEUM_HANDICAP_CHOICE(200),
    COLOSSEUM_DOOM_APPLIED(201),
    COLOSSEUM_TOTEM_HEAL(202),
    COLOSSEUM_REENTRY_POOLS(203),
    COLOSSEUM_SOL_DUST(204),
    COLOSSEUM_SOL_GRAPPLE(205),
    COLOSSEUM_SOL_POOLS(206),
    COLOSSEUM_SOL_LASERS(207),

    MOKHAIOTL_ATTACK_STYLE(250),
    MOKHAIOTL_ORB(251),
    MOKHAIOTL_OBJECTS(252),
    MOKHAIOTL_LARVA_LEAK(253),
    MOKHAIOTL_SHOCKWAVE(254),

    INFERNO_WAVE_START(300),
    ;

    @Getter
    private final int id;

    EventType(int id) {
        this.id = id;
    }
}
