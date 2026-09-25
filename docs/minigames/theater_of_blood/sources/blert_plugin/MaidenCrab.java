/*
 * Copyright (c) 2023-2024 Alexei Frolov
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

package io.blert.challenges.tob.rooms.maiden;

import io.blert.challenges.tob.TobNpc;
import io.blert.core.Hitpoints;
import io.blert.core.TrackedNpc;
import java.util.Optional;
import lombok.*;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;

@Getter
public class MaidenCrab extends TrackedNpc {
    @AllArgsConstructor
    @Getter
    public static class Properties extends TrackedNpc.Properties {
        private final CrabSpawn spawn;
        private final Position position;
        private final boolean scuffed;
    }

    @Setter(AccessLevel.PACKAGE)
    private CrabSpawn spawn;

    private final Position position;
    private final boolean scuffed;

    public enum Position {
        S1,
        N1,
        S2,
        N2,
        S3,
        N3,
        S4_INNER,
        S4_OUTER,
        N4_INNER,
        N4_OUTER,
    }

    /**
     * Returns the crab that spawns at a specified location.
     *
     * @param location Spawn location corresponding to the southwest tile of the crab.
     * @return The type of crab which spawns at that location, if one exists.
     */
    public static Optional<MaidenCrab> fromSpawnLocation(
            int scale, NPC npc, long roomId, CrabSpawn spawn, WorldPoint location) {
        Position position;
        boolean scuffed;

        if (location.equals(N1_SPAWN)) {
            position = Position.N1;
            scuffed = false;
        } else if (location.equals(N1_SCUFFED_SPAWN)) {
            position = Position.N1;
            scuffed = true;
        } else if (location.equals(N2_SPAWN)) {
            position = Position.N2;
            scuffed = false;
        } else if (location.equals(N2_SCUFFED_SPAWN)) {
            position = Position.N2;
            scuffed = true;
        } else if (location.equals(N3_SPAWN)) {
            position = Position.N3;
            scuffed = false;
        } else if (location.equals(N3_SCUFFED_SPAWN)) {
            position = Position.N3;
            scuffed = true;
        } else if (location.equals(N4_INNER_SPAWN)) {
            position = Position.N4_INNER;
            scuffed = false;
        } else if (location.equals(N4_INNER_SCUFFED_SPAWN)) {
            position = Position.N4_INNER;
            scuffed = true;
        } else if (location.equals(N4_OUTER_SPAWN)) {
            position = Position.N4_OUTER;
            scuffed = false;
        } else if (location.equals(N4_OUTER_SCUFFED_SPAWN)) {
            position = Position.N4_OUTER;
            scuffed = true;
        } else if (location.equals(S1_SPAWN)) {
            position = Position.S1;
            scuffed = false;
        } else if (location.equals(S1_SCUFFED_SPAWN)) {
            position = Position.S1;
            scuffed = true;
        } else if (location.equals(S2_SPAWN)) {
            position = Position.S2;
            scuffed = false;
        } else if (location.equals(S2_SCUFFED_SPAWN)) {
            position = Position.S2;
            scuffed = true;
        } else if (location.equals(S3_SPAWN)) {
            position = Position.S3;
            scuffed = false;
        } else if (location.equals(S3_SCUFFED_SPAWN)) {
            position = Position.S3;
            scuffed = true;
        } else if (location.equals(S4_INNER_SPAWN)) {
            position = Position.S4_INNER;
            scuffed = false;
        } else if (location.equals(S4_INNER_SCUFFED_SPAWN)) {
            position = Position.S4_INNER;
            scuffed = true;
        } else if (location.equals(S4_OUTER_SPAWN)) {
            position = Position.S4_OUTER;
            scuffed = false;
        } else if (location.equals(S4_OUTER_SCUFFED_SPAWN)) {
            position = Position.S4_OUTER;
            scuffed = true;
        } else {
            return Optional.empty();
        }

        return TobNpc.withId(npc.getId())
                .map(tobNpc -> new MaidenCrab(
                        npc, tobNpc, roomId, spawn, position, scuffed, new Hitpoints(tobNpc.getBaseHitpoints(scale))));
    }

    private MaidenCrab(
            @NonNull NPC npc,
            TobNpc tobNpc,
            long roomId,
            CrabSpawn spawn,
            Position position,
            boolean scuffed,
            Hitpoints hitpoints) {
        super(npc, tobNpc, roomId, hitpoints);
        this.spawn = spawn;
        this.position = position;
        this.scuffed = scuffed;
    }

    @Override
    public @NonNull TrackedNpc.Properties getProperties() {
        return new Properties(spawn, position, scuffed);
    }

    private static final WorldPoint N1_SPAWN = new WorldPoint(3173, 4456, 0);
    private static final WorldPoint N1_SCUFFED_SPAWN = new WorldPoint(3174, 4457, 0);
    private static final WorldPoint N2_SPAWN = new WorldPoint(3177, 4456, 0);
    private static final WorldPoint N2_SCUFFED_SPAWN = new WorldPoint(3178, 4457, 0);
    private static final WorldPoint N3_SPAWN = new WorldPoint(3181, 4456, 0);
    private static final WorldPoint N3_SCUFFED_SPAWN = new WorldPoint(3182, 4457, 0);
    private static final WorldPoint N4_INNER_SPAWN = new WorldPoint(3185, 4454, 0);
    private static final WorldPoint N4_INNER_SCUFFED_SPAWN = new WorldPoint(3186, 4455, 0);
    private static final WorldPoint N4_OUTER_SPAWN = new WorldPoint(3185, 4456, 0);
    private static final WorldPoint N4_OUTER_SCUFFED_SPAWN = new WorldPoint(3186, 4457, 0);

    private static final WorldPoint S1_SPAWN = new WorldPoint(3173, 4436, 0);
    private static final WorldPoint S1_SCUFFED_SPAWN = new WorldPoint(3174, 4435, 0);
    private static final WorldPoint S2_SPAWN = new WorldPoint(3177, 4436, 0);
    private static final WorldPoint S2_SCUFFED_SPAWN = new WorldPoint(3178, 4435, 0);
    private static final WorldPoint S3_SPAWN = new WorldPoint(3181, 4436, 0);
    private static final WorldPoint S3_SCUFFED_SPAWN = new WorldPoint(3182, 4435, 0);
    private static final WorldPoint S4_INNER_SPAWN = new WorldPoint(3185, 4438, 0);
    private static final WorldPoint S4_INNER_SCUFFED_SPAWN = new WorldPoint(3186, 4437, 0);
    private static final WorldPoint S4_OUTER_SPAWN = new WorldPoint(3185, 4436, 0);
    private static final WorldPoint S4_OUTER_SCUFFED_SPAWN = new WorldPoint(3186, 4435, 0);
}
