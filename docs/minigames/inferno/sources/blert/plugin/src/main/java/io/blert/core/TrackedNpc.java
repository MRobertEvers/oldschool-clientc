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

package io.blert.core;

import io.blert.challenges.tob.TobNpc;
import io.blert.events.NpcEvent;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import net.runelite.api.HeadIcon;
import net.runelite.api.NPC;

/**
 * A {@code TrackedNpc} is an NPC within a PVM challenge room whose data is trackable and reported via
 * {@link NpcEvent}s. The {@code TrackedNpc} wraps an existing Runelite {@link NPC} with additional Blert-specific
 * data fields. Each {@code TrackedNpc} also has an ID which uniquely identifies it among all NPCs within its
 * environment.
 */
public abstract class TrackedNpc {
    /**
     * Properties are an immutable copy of NPC-specific data fields for a {@link TrackedNpc}.
     * <p>
     * Properties are assumed to be immutable by default. If a {@link TrackedNpc}'s state can change over time,
     * the implementation should call {@link TrackedNpc#setUpdatedProperties(boolean)} to indicate when the properties
     * have been changed.
     */
    public abstract static class Properties {}

    @Getter
    private final long roomId;

    @Getter
    @Setter
    private @NonNull NPC npc;

    @Getter
    private final @NonNull ChallengeMode mode;

    @Getter
    @Setter
    private Hitpoints hitpoints;

    @Getter
    @Setter
    private int spawnTick;

    @Setter
    private boolean updatedProperties;

    protected TrackedNpc(@NonNull NPC npc, long roomId, Hitpoints hitpoints) {
        this.npc = npc;
        this.mode = ChallengeMode.NO_MODE;
        this.roomId = roomId;
        this.hitpoints = hitpoints;
        this.spawnTick = -1;
        this.updatedProperties = true;
    }

    protected TrackedNpc(@NonNull NPC npc, @NonNull TobNpc tobNpc, long roomId, Hitpoints hitpoints) {
        this.npc = npc;
        this.mode = tobNpc.getMode();
        this.roomId = roomId;
        this.hitpoints = hitpoints;
        this.spawnTick = -1;
        this.updatedProperties = true;
    }

    public boolean hasUpdatedProperties() {
        return updatedProperties;
    }

    /**
     * Returns a snapshot of NPC-specific properties for the NPC at a point in time.
     *
     * @return Copy of the current state of the NPC.
     */
    public abstract @NonNull Properties getProperties();

    public int getNpcId() {
        return npc.getId();
    }

    public String getName() {
        return npc.getName();
    }

    public PrayerSet getActivePrayers() {
        short[] ids = npc.getOverheadSpriteIds();
        PrayerSet prayerSet = new PrayerSet(Prayer.PRAYER_BOOK_NORMAL);
        if (ids != null && ids.length > 0) {
            // Only the first ID is used to store overhead prayers.
            HeadIcon icon = HeadIcon.values()[ids[0]];
            switch (icon) {
                case RANGED:
                    prayerSet.add(Prayer.PROTECT_FROM_MISSILES);
                    break;
                case MAGIC:
                    prayerSet.add(Prayer.PROTECT_FROM_MAGIC);
                    break;
                case MELEE:
                    prayerSet.add(Prayer.PROTECT_FROM_MELEE);
                    break;
                case RANGE_MAGE:
                    prayerSet.add(Prayer.PROTECT_FROM_MISSILES);
                    prayerSet.add(Prayer.PROTECT_FROM_MAGIC);
                    break;
                case RANGE_MELEE:
                    prayerSet.add(Prayer.PROTECT_FROM_MISSILES);
                    prayerSet.add(Prayer.PROTECT_FROM_MELEE);
                    break;
                case MAGE_MELEE:
                    prayerSet.add(Prayer.PROTECT_FROM_MAGIC);
                    prayerSet.add(Prayer.PROTECT_FROM_MELEE);
                    break;
                case RANGE_MAGE_MELEE:
                    prayerSet.add(Prayer.PROTECT_FROM_MISSILES);
                    prayerSet.add(Prayer.PROTECT_FROM_MAGIC);
                    prayerSet.add(Prayer.PROTECT_FROM_MELEE);
                    break;
            }
        }
        return prayerSet;
    }
}
