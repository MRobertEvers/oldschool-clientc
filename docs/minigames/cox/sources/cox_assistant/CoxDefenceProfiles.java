package com.coxmegascale.calc;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Defence profiles for CoX targets not covered by the detailed NPC stat table.
 *
 * The profile selection and scaling approach was informed by the public Party
 * Defence Tracker plugin by Hyftar:
 * https://github.com/Hyftar/party-defence-tracker
 * This class is an independent implementation for this plugin.
 */
final class CoxDefenceProfiles
{
    private static final Map<String, Profile> PROFILES = profiles();

    private CoxDefenceProfiles()
    {
    }

    static Map<String, Integer> statsFor(String profileKey, int scale)
    {
        Profile profile = PROFILES.get(profileKey);
        if (profile == null || scale <= 0)
        {
            return Collections.emptyMap();
        }

        // Defence and Magic use different CoX party-scaling factors; keep both
        // derived values together so callers cannot mix scales or profiles.
        int partyFactor = (int) Math.sqrt(Math.max(0, scale - 1)) + ((scale - 1) * 7 / 10 + 100);
        int magicFactor = ((int) Math.sqrt(Math.max(0, scale - 1))) * 7 + scale + 99;
        Map<String, Integer> stats = new LinkedHashMap<>();
        stats.put("defence", (int) (profile.baseDefence * partyFactor / 100.0));
        stats.put("magic", (int) (profile.baseMagic * magicFactor / 100.0));
        return Collections.unmodifiableMap(stats);
    }

    static boolean contains(String profileKey)
    {
        return PROFILES.containsKey(profileKey);
    }

    private static Map<String, Profile> profiles()
    {
        Map<String, Profile> profiles = new LinkedHashMap<>();
        profiles.put("abyssalPortal", new Profile(176, 176));
        profiles.put("tekton", new Profile(205, 205));
        profiles.put("vasa", new Profile(175, 230));
        return Collections.unmodifiableMap(profiles);
    }

    private static final class Profile
    {
        private final int baseDefence;
        private final int baseMagic;

        private Profile(int baseDefence, int baseMagic)
        {
            this.baseDefence = baseDefence;
            this.baseMagic = baseMagic;
        }
    }
}
