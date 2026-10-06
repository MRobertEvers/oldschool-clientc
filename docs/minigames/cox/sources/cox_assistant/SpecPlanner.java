package com.coxmegascale.calc;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Pure planning layer that converts scaled NPC stats into landed-hit defence
 * routes. It never performs actions; the UI and live tracker consume its
 * immutable recommendations after game events confirm results.
 */
public class SpecPlanner
{
    private static final int MAGE_HAND_MAGIC_DEFENCE = 50;
    private static final int MYSTICS_REFERENCE_DEFENCE = 333;
    private static final int MYSTICS_FAST_BGS_DAMAGE = 986;
    private static final int MYSTICS_EMBERLIGHT_BGS_DAMAGE = 368;

    private final RaidMath raidMath;
    private int cachedStatsScale = -1;
    private Map<String, Map<String, Integer>> cachedStatsByRoom = Collections.emptyMap();

    public SpecPlanner(RaidMath raidMath)
    {
        this.raidMath = raidMath;
    }

    public List<SpecSummary> buildCoxSpecPlan(int scale, List<String> selectedPuzzles)
    {
        // Only include conditional puzzle targets when that room is present in
        // the selected scout, keeping plans aligned with the actual layout.
        Map<String, Map<String, Integer>> statsByRoom = getStatsByRoom(scale);
        List<String> roomKeys = new ArrayList<>();
        roomKeys.add("mystics");
        roomKeys.add("shamans");
        if (selectedPuzzles != null && selectedPuzzles.contains(RaidMath.PUZZLE_TIGHTROPE))
        {
            roomKeys.add("ropeMager");
            roomKeys.add("ropeRanger");
        }
        roomKeys.add("olmHead");
        roomKeys.add("meleeHand");
        roomKeys.add("mageHand");

        List<SpecSummary> plans = new ArrayList<>();
        for (String roomKey : roomKeys)
        {
            if (statsByRoom.containsKey(roomKey))
            {
                plans.add(planRoom(roomKey, statsByRoom.get(roomKey)));
            }
        }
        return plans;
    }

    public SpecSummary planRoom(String roomKey, Map<String, Integer> stats)
    {
        if ("mageHand".equals(roomKey))
        {
            List<String> lines = new ArrayList<>();
            lines.add("Magic Defence: " + MAGE_HAND_MAGIC_DEFENCE);
            lines.add("Eye of Ayak Damage: " + MAGE_HAND_MAGIC_DEFENCE);
            Map<String, Integer> specs = new LinkedHashMap<>();
            specs.put("Eye of Ayak", MAGE_HAND_MAGIC_DEFENCE);
            return new SpecSummary("mageHand", "Mage Hand", lines, MAGE_HAND_MAGIC_DEFENCE, specs, null, null, null);
        }

        List<SpecStep> sequence = planDefenceReduction(roomKey, stats);
        Map<String, Integer> counts = new LinkedHashMap<>();
        for (SpecStep step : sequence)
        {
            counts.put(step.weapon, counts.getOrDefault(step.weapon, 0) + 1);
        }

        List<String> lines = new ArrayList<>();
        lines.add("Defence: " + RaidMath.formatInteger(stats.get("defence")));
        if (counts.isEmpty())
        {
            lines.add("No configured drain");
        }
        for (Map.Entry<String, Integer> entry : counts.entrySet())
        {
            lines.add(entry.getValue() == 1 ? entry.getKey() : RaidMath.formatInteger(entry.getValue()) + "x " + entry.getKey());
        }

        if ("mystics".equals(roomKey))
        {
            Map<String, Integer> allStatSpecs = new LinkedHashMap<>();
            allStatSpecs.put("Elder Maul", 2);
            allStatSpecs.put("Ralos", 2);
            return new SpecSummary(
                roomKey,
                "Mystics",
                lines,
                stats.get("defence"),
                counts,
                allStatSpecs,
                scaleMysticDamage(stats.get("defence"), MYSTICS_EMBERLIGHT_BGS_DAMAGE),
                scaleMysticDamage(stats.get("defence"), MYSTICS_FAST_BGS_DAMAGE)
            );
        }

        return new SpecSummary(roomKey, displayName(roomKey), lines, stats.get("defence"), counts, null, null, null);
    }

    public Map<String, Integer> getStatsForRoom(String roomKey, int scale)
    {
        Map<String, Integer> stats = getStatsByRoom(scale).get(roomKey);
        if (stats != null)
        {
            return stats;
        }
        return CoxDefenceProfiles.statsFor(roomKey, scale);
    }

    public Map<String, Integer> planRemainingDefence(String roomKey, int defence, int baseMagic)
    {
        if (defence <= 0)
        {
            return Collections.emptyMap();
        }
        if ("mageHand".equals(roomKey))
        {
            return Collections.singletonMap("Eye of Ayak", defence);
        }

        boolean elderMaul = "mystics".equals(roomKey) || "meleeHand".equals(roomKey)
            || CoxDefenceProfiles.contains(roomKey);
        boolean ralos = "mystics".equals(roomKey) || "shamans".equals(roomKey) || "ropeMager".equals(roomKey)
            || "ropeRanger".equals(roomKey) || "olmHead".equals(roomKey) || "meleeHand".equals(roomKey)
            || CoxDefenceProfiles.contains(roomKey);
        int ralosDrain = Math.floorDiv(baseMagic, 10);
        if (!elderMaul && !ralos)
        {
            return Collections.emptyMap();
        }
        if (!elderMaul)
        {
            return ralosDrain <= 0 ? Collections.emptyMap() : single("Ralos", divideRoundUp(defence, ralosDrain));
        }

        // Try each useful Elder Maul prefix and choose the lowest total number
        // of landed Elder/Ralos hits from the target's current Defence.
        int maximumElders = "mystics".equals(roomKey) ? 2 : 10;
        int bestElders = 0;
        int bestRalos = Integer.MAX_VALUE;
        int bestTotal = Integer.MAX_VALUE;
        int afterElders = defence;
        for (int elders = 0; elders <= maximumElders; elders++)
        {
            int ralosCount = ralosDrain <= 0 ? Integer.MAX_VALUE : divideRoundUp(afterElders, ralosDrain);
            int total = elders + ralosCount;
            if (total < bestTotal)
            {
                bestTotal = total;
                bestElders = elders;
                bestRalos = ralosCount;
            }
            int drain = Math.floorDiv(afterElders * 35, 100);
            if (drain <= 0)
            {
                break;
            }
            afterElders -= drain;
        }
        Map<String, Integer> result = new LinkedHashMap<>();
        if (bestElders > 0)
        {
            result.put("Elder Maul", bestElders);
        }
        if (bestRalos != Integer.MAX_VALUE && bestRalos > 0)
        {
            result.put("Ralos", bestRalos);
        }
        return result;
    }

    private static Map<String, Integer> single(String weapon, int count)
    {
        Map<String, Integer> result = new LinkedHashMap<>();
        result.put(weapon, count);
        return result;
    }

    private static int divideRoundUp(int value, int divisor)
    {
        return Math.floorDiv(value + divisor - 1, divisor);
    }

    private List<SpecStep> planDefenceReduction(String roomKey, Map<String, Integer> stats)
    {
        if ("mystics".equals(roomKey))
        {
            return planMysticsDefenceReduction(stats);
        }

        boolean elderMaul = "mystics".equals(roomKey) || "meleeHand".equals(roomKey);
        boolean ralos = "shamans".equals(roomKey) || "ropeMager".equals(roomKey) || "ropeRanger".equals(roomKey) || "olmHead".equals(roomKey) || "meleeHand".equals(roomKey);
        if (!elderMaul && !ralos)
        {
            return Collections.emptyList();
        }

        int defence = stats.get("defence");
        int baseMagic = stats.get("magic");
        int maxDepth = 100;
        Plan best = new Plan(defence, Collections.emptyList());
        List<Plan> queue = new ArrayList<>();
        queue.add(best);

        // Defence strictly decreases and maxDepth bounds future profiles with
        // unexpected drains, so this breadth-first search has predictable cost.
        while (!queue.isEmpty())
        {
            Plan current = queue.remove(0);
            if (current.defence < best.defence || (current.defence == best.defence && current.sequence.size() < best.sequence.size()))
            {
                best = current;
            }
            if (current.defence == 0 || current.sequence.size() >= maxDepth)
            {
                continue;
            }

            if (elderMaul)
            {
                addNextPlan(queue, current, "Elder Maul", Math.floorDiv(current.defence * 35, 100));
            }
            if (ralos)
            {
                // Count each independently rolled projectile, not a throw that assumes both land.
                addNextPlan(queue, current, "Ralos", Math.floorDiv(baseMagic, 10));
            }
        }

        return best.sequence;
    }

    private List<SpecStep> planMysticsDefenceReduction(Map<String, Integer> stats)
    {
        Plan plan = new Plan(stats.get("defence"), Collections.emptyList());
        for (int i = 0; i < 2 && plan.defence > 0; i++)
        {
            plan = nextPlan(plan, "Elder Maul", Math.floorDiv(plan.defence * 35, 100));
        }

        int ralosDrain = Math.floorDiv(stats.get("magic"), 10);
        while (plan.defence > 0 && ralosDrain > 0)
        {
            plan = nextPlan(plan, "Ralos", ralosDrain);
        }
        return plan.sequence;
    }

    private static void addNextPlan(List<Plan> queue, Plan current, String weapon, int drain)
    {
        if (drain > 0)
        {
            queue.add(nextPlan(current, weapon, drain));
        }
    }

    private static Plan nextPlan(Plan current, String weapon, int drain)
    {
        int actualDrain = Math.max(0, Math.min(current.defence, drain));
        if (actualDrain == 0)
        {
            return current;
        }

        List<SpecStep> sequence = new ArrayList<>(current.sequence);
        sequence.add(new SpecStep(weapon, actualDrain, current.defence - actualDrain));
        return new Plan(current.defence - actualDrain, sequence);
    }

    private synchronized Map<String, Map<String, Integer>> getStatsByRoom(int scale)
    {
        // Stat interpolation is shared by multiple tabs and menu lookups; cache
        // one immutable room map until the detected/manual scale changes.
        if (cachedStatsScale == scale)
        {
            return cachedStatsByRoom;
        }
        Map<String, Map<String, Integer>> statsByRoom = new LinkedHashMap<>();
        for (RaidMath.StatSection section : raidMath.calculateNpcStats(scale))
        {
            for (RaidMath.StatGroup group : section.groups)
            {
                statsByRoom.put(roomKeyFromGroup(group.name), group.stats);
            }
        }
        Map<String, Map<String, Integer>> result = Collections.unmodifiableMap(statsByRoom);
        cachedStatsByRoom = result;
        cachedStatsScale = scale;
        return result;
    }

    private static int scaleMysticDamage(int defence, int referenceDamage)
    {
        return Math.round(defence * (referenceDamage / (float) MYSTICS_REFERENCE_DEFENCE));
    }

    private static String roomKeyFromGroup(String name)
    {
        switch (name)
        {
            case "Guardians":
                return "guardians";
            case "Shamans":
                return "shamans";
            case "Mystics":
                return "mystics";
            case "Ice Demon":
                return "iceDemon";
            case "Rope Mager":
                return "ropeMager";
            case "Rope Ranger":
                return "ropeRanger";
            case "Olm Head":
                return "olmHead";
            case "Melee Hand":
                return "meleeHand";
            case "Mage Hand":
                return "mageHand";
            default:
                return name;
        }
    }

    private static String displayName(String roomKey)
    {
        switch (roomKey)
        {
            case "shamans":
                return "Shamans";
            case "ropeMager":
                return "Rope Mager";
            case "ropeRanger":
                return "Rope Ranger";
            case "olmHead":
                return "Olm Head";
            case "meleeHand":
                return "Melee Hand";
            case "mageHand":
                return "Mage Hand";
            default:
                return roomKey;
        }
    }

    public static class SpecSummary
    {
        public final String roomKey;
        public final String name;
        public final List<String> lines;
        public final int defence;
        public final Map<String, Integer> specs;
        public final Map<String, Integer> allStatSpecs;
        public final Integer bgsDamage;
        public final Integer bgsOnlyDamage;

        SpecSummary(
            String roomKey,
            String name,
            List<String> lines,
            int defence,
            Map<String, Integer> specs,
            Map<String, Integer> allStatSpecs,
            Integer bgsDamage,
            Integer bgsOnlyDamage
        )
        {
            this.roomKey = roomKey;
            this.name = name;
            this.lines = Collections.unmodifiableList(lines);
            this.defence = defence;
            this.specs = Collections.unmodifiableMap(new LinkedHashMap<>(specs));
            this.allStatSpecs = allStatSpecs == null
                ? Collections.emptyMap()
                : Collections.unmodifiableMap(new LinkedHashMap<>(allStatSpecs));
            this.bgsDamage = bgsDamage;
            this.bgsOnlyDamage = bgsOnlyDamage;
        }
    }

    private static class SpecStep
    {
        final String weapon;
        final int drain;
        final int resultingDefence;

        SpecStep(String weapon, int drain, int resultingDefence)
        {
            this.weapon = weapon;
            this.drain = drain;
            this.resultingDefence = resultingDefence;
        }
    }

    private static class Plan
    {
        final int defence;
        final List<SpecStep> sequence;

        Plan(int defence, List<SpecStep> sequence)
        {
            this.defence = defence;
            this.sequence = sequence;
        }
    }
}
