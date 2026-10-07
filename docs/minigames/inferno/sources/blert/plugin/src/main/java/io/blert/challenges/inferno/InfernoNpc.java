/*
 * Copyright (c) 2025 Alexei Frolov
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

package io.blert.challenges.inferno;

import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import java.util.Optional;
import lombok.Getter;
import org.apache.commons.lang3.tuple.Pair;

public enum InfernoNpc {
    NIBBLER(7691, 10),
    BAT(7692, 25, Pair.of(7578, NpcAttack.INFERNO_BAT_AUTO)),
    BLOB(
            7693,
            40,
            Pair.of(7581, NpcAttack.INFERNO_BLOB_MAGE),
            Pair.of(7582, NpcAttack.INFERNO_BLOB_MELEE),
            Pair.of(7583, NpcAttack.INFERNO_BLOB_RANGED)),
    BLOB_MAGER(7694, 15, Pair.of(7581, NpcAttack.INFERNO_BLOBLET_MAGE_AUTO)),
    BLOB_RANGER(7695, 15, Pair.of(7583, NpcAttack.INFERNO_BLOBLET_RANGED_AUTO)),
    BLOB_MELEER(7696, 15, Pair.of(7582, NpcAttack.INFERNO_BLOBLET_MELEE_AUTO)),
    MELEER(7697, 75, Pair.of(7597, NpcAttack.INFERNO_MELEER_AUTO), Pair.of(7600, NpcAttack.INFERNO_MELEER_DIG)),
    RANGER(7698, 125, Pair.of(7604, NpcAttack.INFERNO_RANGER_MELEE), Pair.of(7605, NpcAttack.INFERNO_RANGER_AUTO)),
    MAGER(
            7699,
            220,
            Pair.of(7610, NpcAttack.INFERNO_MAGER_AUTO),
            Pair.of(7611, NpcAttack.INFERNO_MAGER_RESURRECT),
            Pair.of(7612, NpcAttack.INFERNO_MAGER_MELEE)),
    JAD(
            7700,
            350,
            Pair.of(7590, NpcAttack.INFERNO_JAD_MELEE),
            Pair.of(7592, NpcAttack.INFERNO_JAD_MAGE),
            Pair.of(7593, NpcAttack.INFERNO_JAD_RANGED)),
    JAD_HEALER(7701, 90, Pair.of(2637, NpcAttack.INFERNO_JAD_HEALER_AUTO)),
    ZUK_RANGER(7702, 125, Pair.of(7604, NpcAttack.INFERNO_RANGER_MELEE), Pair.of(7605, NpcAttack.INFERNO_RANGER_AUTO)),
    ZUK_MAGER(
            7703,
            220,
            Pair.of(7610, NpcAttack.INFERNO_MAGER_AUTO),
            Pair.of(7611, NpcAttack.INFERNO_MAGER_RESURRECT),
            Pair.of(7612, NpcAttack.INFERNO_MAGER_MELEE)),
    ZUK_JAD(
            7704,
            350,
            Pair.of(7590, NpcAttack.INFERNO_JAD_MELEE),
            Pair.of(7592, NpcAttack.INFERNO_JAD_MAGE),
            Pair.of(7593, NpcAttack.INFERNO_JAD_RANGED)),
    ZUK_JAD_HEALER(7705, 90, Pair.of(2637, NpcAttack.INFERNO_JAD_HEALER_AUTO)),
    ZUK(7706, 1200, Pair.of(7566, NpcAttack.INFERNO_ZUK_AUTO)),
    ZUK_MOVING_SAFESPOT(7707, 600),
    ZUK_HEALER(7708, 75),
    ROCKY_SUPPORT(7709, 255),
    ;

    @Getter
    private final int id;

    @Getter
    private final int hitpoints;

    private final Pair<Integer, NpcAttack>[] attacks;

    public static Optional<InfernoNpc> withId(int id) {
        for (InfernoNpc npc : values()) {
            if (npc.id == id) {
                return Optional.of(npc);
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

    boolean isPillar() {
        return this == ROCKY_SUPPORT;
    }

    boolean isResurrectable() {
        switch (this) {
            case BAT:
            case BLOB:
            case MELEER:
            case RANGER:
            case MAGER:
                return true;
            default:
                return false;
        }
    }

    Hitpoints resurrectedHitpoints() {
        return new Hitpoints((hitpoints + 1) / 2, hitpoints);
    }

    @SafeVarargs
    InfernoNpc(int id, int hitpoints, Pair<Integer, NpcAttack>... attacks) {
        this.id = id;
        this.hitpoints = hitpoints;
        this.attacks = attacks;
    }
}
