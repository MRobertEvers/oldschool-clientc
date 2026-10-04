/*
 * Copyright (c) 2024 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the “Software”), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.challenges.colosseum;

import io.blert.core.NpcAttack;
import java.util.Optional;
import lombok.Getter;
import org.apache.commons.lang3.tuple.Pair;

public enum ColosseumNpc {
    JAGUAR_WARRIOR(12810, 125, Pair.of(10847, NpcAttack.COLOSSEUM_JAGUAR_AUTO)),
    SERPENT_SHAMAN(12811, 125, Pair.of(10859, NpcAttack.COLOSSEUM_SHAMAN_AUTO)),
    MINOTAUR(new int[] {12812, 12813}, 225, Pair.of(10843, NpcAttack.COLOSSEUM_MINOTAUR_AUTO)),
    FREMENNIK_ARCHER(12814, 50, Pair.of(10850, NpcAttack.COLOSSEUM_ARCHER_AUTO)),
    FREMENNIK_SEER(12815, 50, Pair.of(10853, NpcAttack.COLOSSEUM_SEER_AUTO)),
    FREMENNIK_BERSERKER(12816, 50, Pair.of(10856, NpcAttack.COLOSSEUM_BERSERKER_AUTO)),
    JAVELIN_COLOSSUS(
            12817,
            220,
            Pair.of(10892, NpcAttack.COLOSSEUM_JAVELIN_AUTO),
            Pair.of(10893, NpcAttack.COLOSSEUM_JAVELIN_TOSS)),
    MANTICORE(12818, 250),
    SHOCKWAVE_COLOSSUS(12819, 125, Pair.of(10903, NpcAttack.COLOSSEUM_SHOCKWAVE_AUTO)),
    SOL_HEREDIT(
            12821,
            1500,
            Pair.of(10883, NpcAttack.COLOSSEUM_HEREDIT_THRUST),
            Pair.of(10884, NpcAttack.COLOSSEUM_HEREDIT_BREAK),
            Pair.of(10885, NpcAttack.COLOSSEUM_HEREDIT_SLAM),
            Pair.of(10887, NpcAttack.COLOSSEUM_HEREDIT_COMBO)),
    BEE_SWARM(12823, 0), // Deliberately ignoring hitpoints as they're not meaningful.
    LASER_PRISM(12824, 0),
    HEALING_TOTEM(12825, 1),
    SOLARFLARE(12826, 0),
    ;

    private final int[] ids;

    @Getter
    private final int hitpoints;

    private final Pair<Integer, NpcAttack>[] attacks;

    public static Optional<ColosseumNpc> withId(int id) {
        for (ColosseumNpc npc : values()) {
            for (int npcId : npc.ids) {
                if (npcId == id) {
                    return Optional.of(npc);
                }
            }
        }
        return Optional.empty();
    }

    Optional<NpcAttack> getAttack(int animationId) {
        for (Pair<Integer, NpcAttack> attack : attacks) {
            if (attack.getLeft() == animationId) {
                return Optional.of(attack.getRight());
            }
        }
        return Optional.empty();
    }

    public boolean isManticore() {
        return this == MANTICORE;
    }

    public boolean isHealingTotem() {
        return this == HEALING_TOTEM;
    }

    public static boolean isSolHeredit(int id) {
        return id == SOL_HEREDIT.ids[0];
    }

    @SafeVarargs
    ColosseumNpc(int id, int hitpoints, Pair<Integer, NpcAttack>... attacks) {
        this.ids = new int[] {id};
        this.hitpoints = hitpoints;
        this.attacks = attacks;
    }

    @SafeVarargs
    ColosseumNpc(int[] ids, int hitpoints, Pair<Integer, NpcAttack>... attacks) {
        this.ids = ids;
        this.hitpoints = hitpoints;
        this.attacks = attacks;
    }
}
