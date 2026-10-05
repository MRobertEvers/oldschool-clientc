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

package io.blert.events;

import io.blert.core.Hitpoints;
import io.blert.core.Stage;
import io.blert.core.TrackedNpc;
import lombok.Getter;
import net.runelite.api.coords.WorldPoint;

public class NpcEvent extends Event {
    @Getter
    private final long roomId;

    @Getter
    private final int npcId;

    @Getter
    private final Hitpoints hitpoints;

    @Getter
    private final long prayers;

    @Getter
    private final TrackedNpc.Properties properties;

    private final boolean propertiesChanged;

    public static NpcEvent spawn(Stage stage, int tick, WorldPoint point, TrackedNpc trackedNpc) {
        return new NpcEvent(EventType.NPC_SPAWN, stage, tick, point, trackedNpc);
    }

    public static NpcEvent update(Stage stage, int tick, WorldPoint point, TrackedNpc trackedNpc) {
        return new NpcEvent(EventType.NPC_UPDATE, stage, tick, point, trackedNpc);
    }

    public static NpcEvent death(Stage stage, int tick, WorldPoint point, TrackedNpc trackedNpc) {
        return new NpcEvent(EventType.NPC_DEATH, stage, tick, point, trackedNpc);
    }

    protected NpcEvent(EventType type, Stage stage, int tick, WorldPoint point, TrackedNpc trackedNpc) {
        super(type, stage, tick, point);
        this.roomId = trackedNpc.getRoomId();
        // Omit an unknown NPC ID. The server should carry forward the last known one for this room ID.
        this.npcId = Math.max(trackedNpc.getNpcId(), 0);
        this.hitpoints = trackedNpc.getHitpoints();
        this.prayers = trackedNpc.getActivePrayers().getValue();
        this.properties = trackedNpc.getProperties();
        this.propertiesChanged = trackedNpc.hasUpdatedProperties();
    }

    public boolean propertiesChanged() {
        return propertiesChanged;
    }

    @Override
    protected String eventDataString() {
        return "npc=(npcId=" + npcId + ", roomId=" + roomId + ", hitpoints=" + hitpoints + ")";
    }
}
