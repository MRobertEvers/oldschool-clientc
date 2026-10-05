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

import io.blert.core.BasicTrackedNpc;
import io.blert.core.Hitpoints;
import lombok.Getter;
import lombok.NonNull;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;

@Getter
public class Pillar extends BasicTrackedNpc {
    enum Location {
        WEST,
        EAST,
        SOUTH,
    }

    private final Location location;

    private final WorldPoint WEST_PILLAR_LOCATION = new WorldPoint(2257, 5349, 0);
    private final WorldPoint EAST_PILLAR_LOCATION = new WorldPoint(2274, 5351, 0);
    private final WorldPoint SOUTH_PILLAR_LOCATION = new WorldPoint(2267, 5335, 0);

    public Pillar(@NonNull NPC npc, long roomId, WorldPoint point) {
        super(npc, roomId, new Hitpoints(InfernoNpc.ROCKY_SUPPORT.getHitpoints()));
        if (point.equals(WEST_PILLAR_LOCATION)) {
            this.location = Location.WEST;
        } else if (point.equals(EAST_PILLAR_LOCATION)) {
            this.location = Location.EAST;
        } else {
            this.location = Location.SOUTH;
        }
    }
}
