package com.coxmegascale.detect;

import java.util.Collections;
import java.util.EnumSet;
import java.util.Locale;
import java.util.Collection;
import java.util.Set;

/**
 * Allocation-light fallback detector that accumulates unique rooms from public
 * NPC and object names when a complete RuneLite scout is not yet available.
 */
public class RaidRoomDetector
{
    private final EnumSet<RaidRoom> detectedRooms = EnumSet.noneOf(RaidRoom.class);

    public boolean observeName(String rawName)
    {
        if (rawName == null || rawName.equals("null"))
        {
            return false;
        }

        String name = rawName.toLowerCase(Locale.ROOT);
        RaidRoom room = roomFromName(name);
        // EnumSet.add reports whether the snapshot actually changed, allowing
        // callers to suppress redundant sidebar refreshes.
        return room != null && detectedRooms.add(room);
    }

    public static RaidRoom identifyRoom(String rawName)
    {
        if (rawName == null || rawName.equals("null"))
        {
            return null;
        }

        return roomFromName(rawName.toLowerCase(Locale.ROOT));
    }

    public Set<RaidRoom> snapshot()
    {
        // Consumers receive an immutable defensive copy, never the live set.
        return Collections.unmodifiableSet(EnumSet.copyOf(detectedRooms));
    }

    public void setDetectedRooms(Collection<RaidRoom> rooms)
    {
        detectedRooms.clear();
        detectedRooms.addAll(rooms);
    }

    public void clear()
    {
        detectedRooms.clear();
    }

    private static RaidRoom roomFromName(String name)
    {
        if (name.contains("guardian"))
        {
            return RaidRoom.GUARDIANS;
        }
        if (name.contains("skeletal mystic"))
        {
            return RaidRoom.MYSTICS;
        }
        if (name.contains("lizardman shaman"))
        {
            return RaidRoom.SHAMANS;
        }
        if (name.contains("deathly mage") || name.contains("deathly ranger") || name.contains("tightrope"))
        {
            return RaidRoom.TIGHTROPE;
        }
        if (name.contains("thieving") || name.contains("cavern grub"))
        {
            return RaidRoom.THIEVING;
        }
        if (name.contains("ice demon") || name.contains("kindling") || name.contains("brazier"))
        {
            return RaidRoom.ICE_DEMON;
        }
        if (name.contains("great olm") || name.contains("olm") || name.contains("left claw") || name.contains("right claw"))
        {
            return RaidRoom.OLM;
        }
        return null;
    }
}
