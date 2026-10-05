/*
 * Copyright (c) 2026 Alexei Frolov
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

package io.blert.events.colosseum;

import io.blert.core.Stage;
import io.blert.events.Event;
import io.blert.events.EventType;
import javax.annotation.Nullable;
import lombok.Getter;
import net.runelite.api.coords.WorldPoint;

@Getter
public class SolDustEvent extends Event {
    public enum Pattern {
        TRIDENT_1,
        TRIDENT_2,
        SHIELD_1,
        SHIELD_2,
    }

    public enum Direction {
        NORTH,
        EAST,
        SOUTH,
        WEST,
    }

    private final Pattern pattern;

    @Nullable
    private final Direction direction;

    public SolDustEvent(int tick, WorldPoint point, Pattern pattern, @Nullable Direction direction) {
        super(EventType.COLOSSEUM_SOL_DUST, Stage.COLOSSEUM_WAVE_12, tick, point);
        this.pattern = pattern;
        this.direction = direction;
    }

    @Override
    protected String eventDataString() {
        return "pattern=" + pattern + (direction != null ? ", direction=" + direction : "");
    }
}
