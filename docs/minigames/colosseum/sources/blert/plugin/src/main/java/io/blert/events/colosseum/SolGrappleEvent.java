/*
 * Copyright (c) 2026 Alexei Frolov
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

package io.blert.events.colosseum;

import io.blert.core.EquipmentSlot;
import io.blert.core.Stage;
import io.blert.events.Event;
import io.blert.events.EventType;
import lombok.Getter;
import net.runelite.api.coords.WorldPoint;

@Getter
public class SolGrappleEvent extends Event {
    public enum Outcome {
        HIT,
        DEFEND,
        PARRY,
    }

    private final int attackTick;
    private final EquipmentSlot target;
    private final Outcome outcome;

    public SolGrappleEvent(int tick, WorldPoint coords, int attackTick, EquipmentSlot target, Outcome outcome) {
        super(EventType.COLOSSEUM_SOL_GRAPPLE, Stage.COLOSSEUM_WAVE_12, tick, coords);
        this.attackTick = attackTick;
        this.target = target;
        this.outcome = outcome;
    }

    @Override
    protected String eventDataString() {
        return "attackTick=" + attackTick + ", target=" + target + ", outcome=" + outcome;
    }
}
