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
import io.blert.core.TrackedNpc;
import io.blert.events.Event;
import io.blert.events.EventType;
import lombok.Getter;
import net.runelite.api.coords.WorldPoint;

@Getter
public class TotemHealEvent extends Event {
    private final int startTick;
    private final int healAmount;
    private final int sourceNpcId;
    private final long sourceRoomId;
    private final int targetNpcId;
    private final long targetRoomId;

    public TotemHealEvent(
            Stage stage,
            int tick,
            WorldPoint coords,
            TrackedNpc source,
            TrackedNpc target,
            int startTick,
            int healAmount) {
        super(EventType.COLOSSEUM_TOTEM_HEAL, stage, tick, coords);
        this.startTick = startTick;
        this.healAmount = healAmount;
        this.sourceNpcId = source.getNpc().getId();
        this.sourceRoomId = source.getRoomId();
        this.targetNpcId = target.getNpc().getId();
        this.targetRoomId = target.getRoomId();
    }

    @Override
    protected String eventDataString() {
        return "source=" + sourceRoomId + ", target=" + targetRoomId + ", startTick=" + startTick + ", healAmount="
                + healAmount;
    }
}
