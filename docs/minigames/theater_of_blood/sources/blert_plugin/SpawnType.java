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

import net.runelite.api.coords.WorldPoint;

public enum SpawnType {
    SPLIT,
    WEST,
    SOUTH,
    EAST,
    UNKNOWN;

    static final WorldPoint EAST_LANE_NORTH = new WorldPoint(3310, 4249, 0);
    static final WorldPoint EAST_LANE_SOUTH = new WorldPoint(3310, 4248, 0);
    static final WorldPoint EAST_LANE_SOUTHWEST = new WorldPoint(3309, 4248, 0); // east bigs
    static final WorldPoint WEST_LANE_NORTH = new WorldPoint(3281, 4249, 0);
    static final WorldPoint WEST_LANE_SOUTH = new WorldPoint(3281, 4248, 0);
    static final WorldPoint SOUTH_LANE_WEST = new WorldPoint(3295, 4233, 0);
    static final WorldPoint SOUTH_LANE_EAST = new WorldPoint(3296, 4233, 0);

    public static SpawnType fromWorldPoint(WorldPoint point) {
        if (point.equals(EAST_LANE_NORTH) || point.equals(EAST_LANE_SOUTH) || point.equals(EAST_LANE_SOUTHWEST)) {
            return EAST;
        }
        if (point.equals(WEST_LANE_NORTH) || point.equals(WEST_LANE_SOUTH)) {
            return WEST;
        }
        if (point.equals(SOUTH_LANE_WEST) || point.equals(SOUTH_LANE_EAST)) {
            return SOUTH;
        }

        return UNKNOWN;
    }

    public boolean isLaneSpawn() {
        return this == WEST || this == SOUTH || this == EAST;
    }
}
