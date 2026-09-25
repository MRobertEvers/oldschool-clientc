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

package io.blert.challenges.tob.rooms.verzik;

import io.blert.challenges.tob.TobNpc;
import io.blert.core.Hitpoints;
import io.blert.core.TrackedNpc;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NonNull;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;

public class VerzikCrab extends TrackedNpc {
    public enum Spawn {
        NORTH,
        NORTHEAST,
        NORTHWEST,
        EAST,
        SOUTH,
        SOUTHEAST,
        SOUTHWEST,
        WEST,
        UNKNOWN,
        SOUTH_FAR,
        CENTER, // If Verzik is dragged.
    }

    @AllArgsConstructor
    @Getter
    public static class Properties extends TrackedNpc.Properties {
        private final VerzikPhase phase;
        private final Spawn spawn;
    }

    private final Properties properties;

    @Override
    public @NonNull TrackedNpc.Properties getProperties() {
        return properties;
    }

    static VerzikCrab fromSpawnedNpc(
            @NonNull NPC npc,
            @NonNull TobNpc tobNpc,
            long roomId,
            WorldPoint spawnPoint,
            int raidScale,
            VerzikPhase phase) {
        Spawn spawn;

        if (spawnPoint.equals(NORTH_SPAWN)) {
            spawn = Spawn.NORTH;
        } else if (spawnPoint.equals(NORTHEAST_SPAWN)) {
            spawn = Spawn.NORTHEAST;
        } else if (spawnPoint.equals(NORTHWEST_SPAWN)) {
            spawn = Spawn.NORTHWEST;
        } else if (spawnPoint.equals(EAST_SPAWN)) {
            spawn = Spawn.EAST;
        } else if (spawnPoint.equals(SOUTH_SPAWN)) {
            spawn = Spawn.SOUTH;
        } else if (spawnPoint.equals(SOUTH_FAR_SPAWN)) {
            spawn = Spawn.SOUTH_FAR;
        } else if (spawnPoint.equals(SOUTHEAST_SPAWN)) {
            spawn = Spawn.SOUTHEAST;
        } else if (spawnPoint.equals(SOUTHWEST_SPAWN)) {
            spawn = Spawn.SOUTHWEST;
        } else if (spawnPoint.equals(WEST_SPAWN)) {
            spawn = Spawn.WEST;
        } else if (spawnPoint.equals(CENTER_SPAWN)) {
            spawn = Spawn.CENTER;
        } else {
            spawn = Spawn.UNKNOWN;
        }

        return new VerzikCrab(npc, tobNpc, roomId, new Hitpoints(tobNpc, raidScale), phase, spawn);
    }

    private VerzikCrab(
            @NonNull NPC npc,
            @NonNull TobNpc tobNpc,
            long roomId,
            Hitpoints hitpoints,
            VerzikPhase phase,
            Spawn spawn) {
        super(npc, tobNpc, roomId, hitpoints);
        this.properties = new Properties(phase, spawn);
    }

    private static final WorldPoint NORTH_SPAWN = new WorldPoint(3167, 4320, 0);
    private static final WorldPoint NORTHEAST_SPAWN = new WorldPoint(3177, 4319, 0);
    private static final WorldPoint NORTHWEST_SPAWN = new WorldPoint(3157, 4320, 0);
    private static final WorldPoint EAST_SPAWN = new WorldPoint(3176, 4315, 0);
    private static final WorldPoint SOUTH_SPAWN = new WorldPoint(3166, 4308, 0);
    private static final WorldPoint SOUTH_FAR_SPAWN = new WorldPoint(3171, 4303, 0);
    private static final WorldPoint SOUTHEAST_SPAWN = new WorldPoint(3179, 4310, 0);
    private static final WorldPoint SOUTHWEST_SPAWN = new WorldPoint(3157, 4311, 0);
    private static final WorldPoint WEST_SPAWN = new WorldPoint(3157, 4315, 0);
    private static final WorldPoint CENTER_SPAWN = new WorldPoint(3168, 4315, 0);
}
