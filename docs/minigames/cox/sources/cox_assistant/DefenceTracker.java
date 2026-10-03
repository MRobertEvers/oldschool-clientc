package com.coxmegascale.calc;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Passive model of one CoX target's confirmed Defence-reduction progress.
 * Initial scaled stats stay immutable while each accepted hit updates current
 * Defence, menu color, and the cached remaining-spec recommendation.
 */
public final class DefenceTracker
{
    public static final String MENU_COLOR_FULL = "8fd3ff";
    public static final String MENU_COLOR_DRAINED = "ff981f";
    public static final String MENU_COLOR_TARGET = "00ff00";

    private final String targetKey;
    private final String profileKey;
    private final String displayName;
    private final String defenceLabel;
    private final int initialDefence;
    private final int baseMagic;
    private final SpecPlanner specPlanner;
    private int currentDefence;
    private Map<String, Integer> remainingSpecs;

    DefenceTracker(String targetKey, String profileKey, String displayName, String defenceLabel, int initialDefence, int baseMagic, SpecPlanner specPlanner)
    {
        this.targetKey = targetKey;
        this.profileKey = profileKey;
        this.displayName = displayName;
        this.defenceLabel = defenceLabel;
        this.initialDefence = Math.max(0, initialDefence);
        this.currentDefence = this.initialDefence;
        this.baseMagic = Math.max(0, baseMagic);
        this.specPlanner = specPlanner;
        this.remainingSpecs = planRemainingSpecs();
    }

    public String getTargetKey()
    {
        return targetKey;
    }

    public String getDisplayName()
    {
        return displayName;
    }

    public String getDefenceLabel()
    {
        return defenceLabel;
    }

    public int getCurrentDefence()
    {
        return currentDefence;
    }

    public String getMenuColor()
    {
        if (initialDefence > 0 && currentDefence * 10 <= initialDefence)
        {
            return MENU_COLOR_TARGET;
        }
        if (currentDefence < initialDefence)
        {
            return MENU_COLOR_DRAINED;
        }
        return MENU_COLOR_FULL;
    }

    public Map<String, Integer> getRemainingSpecs()
    {
        return remainingSpecs;
    }

    public boolean recordSpecHit(String weapon, int amount)
    {
        // Amount represents projectile count for Ralos, damage for BGS/Ayak,
        // and a positive-hit confirmation for percentage-based weapons.
        if (weapon == null || amount <= 0 || currentDefence == 0)
        {
            return false;
        }

        int drain;
        switch (weapon)
        {
            case "Elder Maul":
                drain = Math.floorDiv(currentDefence * 35, 100);
                break;
            case "DWH":
                drain = Math.floorDiv(currentDefence * 30, 100);
                break;
            case "Ralos":
                drain = Math.floorDiv(baseMagic, 10) * amount;
                break;
            case "Emberlight":
                drain = Math.floorDiv(initialDefence * 5, 100);
                break;
            case "BGS":
            case "Eye of Ayak":
                drain = amount;
                break;
            default:
                return false;
        }

        if (drain <= 0)
        {
            return false;
        }
        int updated = Math.max(0, currentDefence - drain);
        if (updated == currentDefence)
        {
            return false;
        }
        currentDefence = updated;
        remainingSpecs = planRemainingSpecs();
        return true;
    }

    private Map<String, Integer> planRemainingSpecs()
    {
        // Cache the immutable plan because menu rendering reads it repeatedly;
        // it is invalidated only when a confirmed hit changes Defence.
        Map<String, Integer> plan = specPlanner.planRemainingDefence(profileKey, currentDefence, baseMagic);
        return plan.isEmpty() ? Collections.emptyMap() : Collections.unmodifiableMap(new LinkedHashMap<>(plan));
    }

    public static DefenceTracker forScale(String targetKey, String profileKey, String displayName, int scale, SpecPlanner specPlanner)
    {
        Map<String, Integer> stats = specPlanner.getStatsForRoom(profileKey, scale);
        if (stats.isEmpty())
        {
            return null;
        }
        if ("mageHand".equals(profileKey))
        {
            return new DefenceTracker(targetKey, profileKey, displayName, "Magic Defence", 50, stats.get("magic"), specPlanner);
        }
        return new DefenceTracker(targetKey, profileKey, displayName, "Defence", stats.get("defence"), stats.get("magic"), specPlanner);
    }

    public static String baseDisplayName(String profileKey)
    {
        switch (profileKey)
        {
            case "mystics": return "Mystics";
            case "shamans": return "Shamans";
            case "iceDemon": return "Ice Demon";
            case "ropeMager": return "Rope Mager";
            case "ropeRanger": return "Rope Ranger";
            case "olmHead": return "Olm Head";
            case "meleeHand": return "Melee Hand";
            case "mageHand": return "Mage Hand";
            case "abyssalPortal": return "Abyssal Portal";
            case "tekton": return "Tekton";
            case "vasa": return "Vasa";
            default: return profileKey;
        }
    }
}
