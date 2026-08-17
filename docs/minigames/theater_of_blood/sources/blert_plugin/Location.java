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

package io.blert.challenges.tob;

import io.blert.challenges.tob.rooms.Room;
import net.runelite.api.coords.WorldArea;
import net.runelite.api.coords.WorldPoint;

public enum Location {
    ELSEWHERE, // Anywhere but TOB (why?)
    LOBBY,
    MAIDEN_INSTANCE,
    MAIDEN_ROOM,
    BLOAT_INSTANCE,
    BLOAT_ROOM,
    NYLOCAS_INSTANCE,
    NYLOCAS_ROOM,
    SOTETSEG_INSTANCE,
    SOTETSEG_ROOM,
    SOTETSEG_MAZE,
    XARPUS_INSTANCE,
    XARPUS_ROOM,
    VERZIK_INSTANCE,
    VERZIK_ROOM,
    LOOT_ROOM;

    private static final int LOBBY_REGION_ID = 14642;

    // The corridor leading up to Maiden.
    private static final int CORRIDOR_REGION_ID = 12869;
    private static final int MAIDEN_REGION_ID = 12613;

    private static final int BLOAT_REGION_ID = 13125;
    private static final int NYLOCAS_REGION_ID = 13122;
    private static final int SOTETSEG_REGION_ID = 13123;
    private static final int SOTETSEG_MAZE_REGION_ID = 13379;
    private static final int XARPUS_REGION_ID = 12612;
    private static final int VERZIK_REGION_ID = 12611;

    private static final int LOOT_ROOM_REGION_ID = 12867;

    private static final WorldArea MAIDEN_ROOM_AREA = new WorldArea(3159, 4434, 29, 25, 0);
    private static final WorldArea MAIDEN_STAIRCASE_AREA = new WorldArea(3185, 4444, 3, 6, 0);
    private static final WorldArea BLOAT_ROOM_AREA = new WorldArea(3287, 4439, 18, 17, 0);
    private static final WorldArea NYLOCAS_ROOM_AREA = new WorldArea(3276, 4228, 38, 27, 0);
    private static final WorldArea SOTETSEG_ROOM_AREA = new WorldArea(3271, 4304, 17, 30, 0);
    private static final WorldArea SOTETSEG_STAIRCASE_AREA = new WorldArea(3277, 4304, 6, 3, 0);
    private static final WorldArea XARPUS_ROOM_AREA = new WorldArea(3162, 4379, 17, 17, 0);
    private static final WorldArea VERZIK_ROOM_AREA = new WorldArea(3154, 4302, 28, 21, 0);

    public static Location fromWorldPoint(WorldPoint worldPoint) {
        switch (worldPoint.getRegionID()) {
            case LOBBY_REGION_ID:
                return LOBBY;
            case CORRIDOR_REGION_ID:
            case MAIDEN_REGION_ID:
                if (MAIDEN_ROOM_AREA.contains2D(worldPoint) && !MAIDEN_STAIRCASE_AREA.contains2D(worldPoint)) {
                    return MAIDEN_ROOM;
                }
                return MAIDEN_INSTANCE;
            case BLOAT_REGION_ID:
                return BLOAT_ROOM_AREA.contains2D(worldPoint) ? BLOAT_ROOM : BLOAT_INSTANCE;
            case NYLOCAS_REGION_ID:
                return NYLOCAS_ROOM_AREA.contains2D(worldPoint) ? NYLOCAS_ROOM : NYLOCAS_INSTANCE;
            case SOTETSEG_REGION_ID:
                if (SOTETSEG_ROOM_AREA.contains2D(worldPoint) && !SOTETSEG_STAIRCASE_AREA.contains2D(worldPoint)) {
                    return SOTETSEG_ROOM;
                }
                return SOTETSEG_INSTANCE;
            case SOTETSEG_MAZE_REGION_ID:
                return SOTETSEG_MAZE;
            case XARPUS_REGION_ID:
                return XARPUS_ROOM_AREA.contains2D(worldPoint) ? XARPUS_ROOM : XARPUS_INSTANCE;
            case VERZIK_REGION_ID:
                return VERZIK_ROOM_AREA.contains2D(worldPoint) ? VERZIK_ROOM : VERZIK_INSTANCE;
            case LOOT_ROOM_REGION_ID:
                return LOOT_ROOM;
            default:
                return ELSEWHERE;
        }
    }

    public boolean inRoom(Room room) {
        switch (room) {
            case MAIDEN:
                return inMaiden();
            case BLOAT:
                return inBloat();
            case NYLOCAS:
                return inNylocas();
            case SOTETSEG:
                return inSotetseg();
            case XARPUS:
                return inXarpus();
            case VERZIK:
                return inVerzik();
            default:
                return false;
        }
    }

    public boolean inRoomInstance(Room room) {
        switch (room) {
            case MAIDEN:
                return inMaidenInstance();
            case BLOAT:
                return inBloatInstance();
            case NYLOCAS:
                return inNylocasInstance();
            case SOTETSEG:
                return inSotetsegInstance();
            case XARPUS:
                return inXarpusInstance();
            case VERZIK:
                return inVerzikInstance();
            default:
                return false;
        }
    }

    public boolean inRaid() {
        return this != ELSEWHERE && this != LOBBY;
    }

    public boolean inMaidenInstance() {
        return this == MAIDEN_INSTANCE || inMaiden();
    }

    public boolean inMaiden() {
        return this == MAIDEN_ROOM;
    }

    public boolean inBloatInstance() {
        return this == BLOAT_INSTANCE || inBloat();
    }

    public boolean inBloat() {
        return this == BLOAT_ROOM;
    }

    public boolean inNylocasInstance() {
        return this == NYLOCAS_INSTANCE || inNylocas();
    }

    public boolean inNylocas() {
        return this == NYLOCAS_ROOM;
    }

    public boolean inSotetsegInstance() {
        return this == SOTETSEG_INSTANCE || inSotetseg();
    }

    public boolean inSotetseg() {
        return this == SOTETSEG_ROOM || this == SOTETSEG_MAZE;
    }

    public boolean inSotetsegOverworld() {
        return this == SOTETSEG_INSTANCE || this == SOTETSEG_ROOM;
    }

    public boolean inSotetsegUnderworld() {
        return this == SOTETSEG_MAZE;
    }

    public boolean inXarpusInstance() {
        return this == XARPUS_INSTANCE || inXarpus();
    }

    public boolean inXarpus() {
        return this == XARPUS_ROOM;
    }

    public boolean inVerzikInstance() {
        return this == VERZIK_INSTANCE || inVerzik();
    }

    public boolean inVerzik() {
        return this == VERZIK_ROOM;
    }
}
