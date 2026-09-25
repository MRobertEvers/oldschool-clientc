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

package io.blert.challenges.tob.rooms.nylocas;

import io.blert.challenges.tob.TobNpc;
import io.blert.core.Hitpoints;
import io.blert.core.TrackedNpc;
import javax.annotation.Nullable;
import lombok.*;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;

@Slf4j
public class Nylo extends TrackedNpc {
    @AllArgsConstructor
    @Getter
    public static class Properties extends TrackedNpc.Properties {
        private long roomId;
        private long parentRoomId;
        private int wave;
        private boolean big;
        private SpawnType spawnType;
        private Style style;
    }

    public enum Style {
        MELEE,
        RANGE,
        MAGE;

        static Style fromNpcId(int id) {
            if (TobNpc.isNylocasIschyros(id)) {
                return MELEE;
            }
            if (TobNpc.isNylocasToxobolos(id)) {
                return RANGE;
            }
            return MAGE;
        }
    }

    @Setter
    private @Nullable Nylo parent;

    @Getter
    @Setter(AccessLevel.PACKAGE)
    private SpawnType spawnType;

    @Getter
    private final WorldPoint spawnPoint;

    @Getter
    private final int spawnTick;

    @Getter
    private Style style;

    @Getter
    @Setter(AccessLevel.PACKAGE)
    private int wave;

    @Getter
    private final boolean big;

    @Getter
    private WorldPoint deathPoint;

    private int deathTick;

    public Nylo(
            @NonNull NPC npc,
            TobNpc tobNpc,
            long roomId,
            WorldPoint spawnPoint,
            int spawnTick,
            int wave,
            int baseHitpoints) {
        super(npc, tobNpc, roomId, new Hitpoints(baseHitpoints));
        this.parent = null;
        this.spawnType = SpawnType.fromWorldPoint(spawnPoint);
        this.spawnPoint = spawnPoint;
        this.spawnTick = spawnTick;
        this.style = Style.fromNpcId(npc.getId());
        this.wave = wave;
        this.big = npc.getComposition().getSize() > 1;
        this.deathTick = -1;
    }

    @Override
    public @NonNull TrackedNpc.Properties getProperties() {
        long parentRoomId = parent != null ? parent.getRoomId() : 0;
        return new Properties(getRoomId(), parentRoomId, wave, big, spawnType, style);
    }

    public void setStyle(Style style) {
        setUpdatedProperties(true);
        this.style = style;
    }

    public boolean isLaneSpawn() {
        return spawnType.isLaneSpawn();
    }

    /**
     * Marks this nylo as having died.
     *
     * @param tick     Room tick on which the nylo died.
     * @param location Point at which the nylo died.
     */
    public void recordDeath(int tick, WorldPoint location) {
        deathTick = tick;
        deathPoint = location;
    }

    /**
     * Checks if this nylo may be a parent of {@code other}. Only possible after calling {@link #recordDeath} and if
     * this nylo is a big and {@code other} is a split.
     *
     * @param other Potential child to check.
     * @return {@code true} if this could be a parent of {@code other}.
     */
    public boolean isPossibleParentOf(Nylo other) {
        if (!big || other.isLaneSpawn() || deathTick != other.spawnTick) {
            return false;
        }

        // Splits are observed to spawn within the 2x2 footprint of their dead parent
        // plus two extensions: (-1, 0) and (2, 1) relative to the southwest tile of the parent.
        // This is shown visually below, where B represents the parent big and S labels the
        // two additional extension tiles.
        //
        // 1 |  .  B  B  S
        // 0 |  S  B  B  .
        // --+-------------
        //   | -1  0  1  2
        //
        int dx = other.spawnPoint.getX() - deathPoint.getX();
        int dy = other.spawnPoint.getY() - deathPoint.getY();
        return (dy == 0 && dx >= -1 && dx <= 1) || (dy == 1 && dx >= 0 && dx <= 2);
    }
}
