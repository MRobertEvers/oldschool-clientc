package com.coxmegascale.calc;

import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Pure calculation layer for scaled NPC stats, supported-room point estimates,
 * Olm/consumable totals, and capped purple-roll probabilities. Returned view
 * models are immutable so Swing can render them without touching client state.
 */
public class RaidMath
{
    public static final String PUZZLE_ICE_DEMON = "iceDemon";
    public static final String PUZZLE_TIGHTROPE = "tightrope";
    public static final String PUZZLE_THIEVING = "thieving";

    public static final String ROOM_MUTTADILES = "muttadiles";
    public static final String ROOM_TEKTON = "tekton";
    public static final String ROOM_VASA = "vasa";
    public static final String ROOM_VESPULA = "vespula";
    public static final String ROOM_VANGUARDS = "vanguards";

    private static final int COMBAT_POINT_NUMERATOR = 415;
    private static final int COMBAT_POINT_DENOMINATOR = 100;
    private static final int CRABS_POINTS = 1_600;
    private static final int POINTS_PER_POTENTIAL_ROLL = 570_000;
    private static final int POINTS_PER_EXPECTED_PURPLE = 867_500;
    private static final int MAX_POTENTIAL_ROLLS = 6;
    private static final int MAX_DISPLAYED_PURPLES = 6;
    private static final NumberFormat INTEGER_FORMAT = NumberFormat.getIntegerInstance(Locale.US);

    private static final List<StatSectionData> STAT_DATA = buildStatData();
    private int cachedNpcStatsScale = -1;
    private List<StatSection> cachedNpcStats = Collections.emptyList();

    public synchronized List<StatSection> calculateNpcStats(int scale)
    {
        assertPositiveInteger(scale, "Scale");
        // NPC tables are expensive but depend only on scale; reuse the immutable
        // result across selective sidebar refreshes.
        if (cachedNpcStatsScale == scale)
        {
            return cachedNpcStats;
        }
        List<StatSection> sections = new ArrayList<>();

        for (StatSectionData section : STAT_DATA)
        {
            List<StatGroup> groups = new ArrayList<>();
            for (StatGroupData group : section.groups)
            {
                Map<String, Integer> stats = new LinkedHashMap<>();
                for (Map.Entry<String, List<Anchor>> entry : group.stats.entrySet())
                {
                    stats.put(entry.getKey(), Math.round((float) valueFromAnchors(entry.getValue(), scale)));
                }
                groups.add(new StatGroup(group.name, stats));
            }

            Map<String, Integer> maxHits = new LinkedHashMap<>();
            for (Map.Entry<String, List<Anchor>> entry : section.maxHits.entrySet())
            {
                maxHits.put(entry.getKey(), Math.round((float) valueFromAnchors(entry.getValue(), scale)));
            }
            sections.add(new StatSection(section.key, section.title, groups, maxHits));
        }

        List<StatSection> result = Collections.unmodifiableList(sections);
        cachedNpcStats = result;
        cachedNpcStatsScale = scale;
        return result;
    }

    public RoomPoints calculateRoomPoints(int scale, RaidOptions options)
    {
        // Presence flags come from the scout. Unsupported or absent rooms are
        // never silently added to the displayed supported estimate.
        assertPositiveInteger(scale, "Scale");
        RaidOptions safeOptions = options == null ? RaidOptions.defaults(scale) : options;
        List<String> selectedPuzzles = safeOptions.selectedPuzzles();
        List<PointRow> rows = new ArrayList<>();

        if (safeOptions.includeGuardians)
        {
            rows.add(pointRow("guardians", "Guardians", calculateGuardiansPoints(scale), null));
        }
        if (safeOptions.includeMystics)
        {
            rows.add(pointRow("mystics", "Mystics", calculateMysticsPoints(scale), null));
        }
        if (safeOptions.includeShamans)
        {
            rows.add(pointRow("shamans", "Shamans", calculateShamansPoints(scale), null));
        }
        if (safeOptions.includeMuttadiles)
        {
            rows.add(pointRow(ROOM_MUTTADILES, "Muttadiles", calculateMuttadilesPoints(scale), "Combat contribution estimate"));
        }
        if (safeOptions.includeTekton)
        {
            rows.add(pointRow(ROOM_TEKTON, "Tekton", calculateTektonPoints(scale), "Combat contribution estimate"));
        }
        if (safeOptions.includeVasa)
        {
            rows.add(pointRow(ROOM_VASA, "Vasa", calculateVasaPoints(scale), "Combat contribution estimate"));
        }
        if (safeOptions.includeVespula)
        {
            rows.add(pointRow(ROOM_VESPULA, "Vespula", calculateVespulaPoints(scale), "Combat contribution estimate"));
        }
        if (safeOptions.includeVanguards)
        {
            rows.add(pointRow(ROOM_VANGUARDS, "Vanguards", calculateVanguardsPoints(scale), "Combat contribution estimate"));
        }

        if (safeOptions.includeCrabs)
        {
            rows.add(pointRow("crabs", "Crabs", CRABS_POINTS, null));
        }
        for (String puzzle : selectedPuzzles)
        {
            if (PUZZLE_TIGHTROPE.equals(puzzle))
            {
                rows.add(pointRow("tightrope", "Tightrope", calculateTightropePoints(scale), null));
            }
            else if (PUZZLE_THIEVING.equals(puzzle))
            {
                ThievingPoints thieving = calculateThievingPoints(safeOptions.totalThievingLevel);
                rows.add(pointRow("thieving", "Thieving", thieving.points, formatInteger(thieving.grubsToCap) + " Grubs"));
            }
            else if (PUZZLE_ICE_DEMON.equals(puzzle))
            {
                IceDemonPoints ice = calculateIceDemonPoints(scale);
                rows.add(pointRow("iceDemon", "Ice Demon", ice.points, formatInteger(ice.kindlingPoints) + " from kindling"));
            }
        }

        OlmPoints olm = calculateOlmPoints(scale);
        rows.add(pointRow("olmHead", "Olm head", olm.head, null));
        rows.add(pointRow("olmHands", "Olm hands", olm.hands, olmPointNote(scale, olm, safeOptions.suppressOlmPointNotes)));

        int overloadSips = safeOptions.overloadSips > 0 ? safeOptions.overloadSips : 5 * (scale + 1);
        OverloadPoints overloads = calculateOverloadPointsForSips(overloadSips);
        rows.add(pointRow("overloads", "Overloads", overloads.points, formatInteger(overloads.overloadsToCap) + " Overloads"));

        FishingPoints fishing = calculateFishingPoints(scale, safeOptions.fishingLevel);
        rows.add(pointRow("fishing", "Fishing", fishing.points, formatInteger(fishing.fishToCap) + " Fish"));

        PointRow olmTotal = pointRow("olmTotal", "Olm total", olm.total, null);
        PointRow olmHands = pointRow("olmHands", "Olm hands", olm.hands, null);
        PointRow olmPerPhase = pointRow("olmPerPhase", "Per Phase", olm.hands / olm.phases, null);
        int grandTotal = olmTotal.points;
        for (PointRow row : rows)
        {
            if (!row.key.equals("olmHead") && !row.key.equals("olmHands"))
            {
                grandTotal += row.points;
            }
        }

        return new RoomPoints(rows, Arrays.asList(olmHands, olmPerPhase), grandTotal);
    }

    public ThievingPoints calculateThievingPoints(int totalThievingLevel)
    {
        if (totalThievingLevel < 0)
        {
            throw new IllegalArgumentException("Total thieving level must be non-negative.");
        }

        int levelPoints = (totalThievingLevel / 6) * 150;
        int points = Math.max(4_500, levelPoints);
        return new ThievingPoints(points, (int) Math.ceil(points / 115.0), totalThievingLevel, points == 4_500 && levelPoints < 4_500);
    }

    public IceDemonPoints calculateIceDemonPoints(int scale)
    {
        assertPositiveInteger(scale, "Scale");
        int killPoints = Math.round((float) valueFromAnchors(anchors(1, 580, 8, 2_825), scale));
        return new IceDemonPoints(6_760 + killPoints, 6_760, killPoints, 182, 6.5);
    }

    public FishingPoints calculateFishingPoints(int scale, int fishingLevel)
    {
        assertPositiveInteger(scale, "Scale");
        if (fishingLevel < 60)
        {
            throw new IllegalArgumentException("Fishing level must be at least 60.");
        }

        int pointsPerFish = fishingLevel >= 90 ? 90 : fishingLevel >= 75 ? 80 : 70;
        int fishToCap = 20 * scale;
        return new FishingPoints(fishToCap * pointsPerFish, fishToCap, fishingLevel, pointsPerFish);
    }

    public OverloadPoints calculateOverloadPoints(int scale)
    {
        assertPositiveInteger(scale, "Scale");
        return calculateOverloadPointsForSips(5 * (scale + 1));
    }

    public OverloadPoints calculateOverloadPointsForSips(int sipsToCap)
    {
        if (sipsToCap < 0)
        {
            throw new IllegalArgumentException("Overload sips must be non-negative.");
        }
        return new OverloadPoints(sipsToCap * 600, sipsToCap, (int) Math.ceil(sipsToCap / 4.0), 600, 4);
    }

    public int calculateRemainingOverloadPoints(int currentSips, int sipCap)
    {
        if (currentSips < 0 || sipCap < 0)
        {
            throw new IllegalArgumentException("Overload sips must be non-negative.");
        }
        return Math.max(0, sipCap - currentSips) * 600;
    }

    public int calculateOlmPhaseCount(int scale)
    {
        assertPositiveInteger(scale, "Scale");
        return Math.min(9, 3 + scale / 8);
    }

    /**
     * These rows intentionally model only the normal combat contribution of a
     * completed room. They do not include tree healing, resets, crystal/tree
     * interactions, or other strategy-dependent point events.
     */
    public int calculateMuttadilesPoints(int scale)
    {
        return calibratedCombatRoomPoints(scale, 2_271, 4_205,
            anchors(1, 1_550, 3, 3_100, 5, 4_650, 40, 32_550, 51, 41_463, 80, 64_800, 100, 83_700));
    }

    public int calculateTektonPoints(int scale)
    {
        return calibratedCombatRoomPoints(scale, 1_362, 2_467,
            anchors(1, 1_100, 3, 2_200, 5, 3_300, 40, 23_100, 51, 29_447, 80, 46_200, 100, 59_670));
    }

    public int calculateVasaPoints(int scale)
    {
        return calibratedCombatRoomPoints(scale, 2_282, 3_498,
            anchors(1, 1_200, 3, 2_400, 5, 3_600, 40, 25_200, 51, 32_130, 80, 50_400, 100, 65_100));
    }

    public int calculateVespulaPoints(int scale)
    {
        return calibratedCombatRoomPoints(scale, 1_948, 3_383,
            anchors(1, 1_000, 3, 2_000, 5, 3_000, 40, 21_000, 51, 26_775, 80, 42_000, 100, 54_250));
    }

    public int calculateVanguardsPoints(int scale)
    {
        return calibratedCombatRoomPoints(scale, 2_497, 5_410,
            anchors(1, 1_350, 3, 2_700, 5, 4_050, 40, 28_350, 51, 36_146, 80, 56_700, 100, 73_200));
    }

    private int estimatedCombatRoomPoints(int scale, List<Anchor> roomDamage)
    {
        assertPositiveInteger(scale, "Scale");
        return Math.round((float) valueFromAnchors(roomDamage, scale) * COMBAT_POINT_NUMERATOR / COMBAT_POINT_DENOMINATOR);
    }

    private int calibratedCombatRoomPoints(int scale, int observedScaleOnePoints, int observedScaleTwoPoints,
        List<Anchor> roomDamage)
    {
        int uncalibrated = estimatedCombatRoomPoints(scale, roomDamage);
        int uncalibratedScaleTwo = estimatedCombatRoomPoints(2, roomDamage);
        return calibratedObservedScaleCurve(scale, uncalibrated, uncalibratedScaleTwo,
            observedScaleOnePoints, observedScaleTwoPoints);
    }

    public OlmPoints calculateOlmPoints(int scale)
    {
        assertPositiveInteger(scale, "Scale");
        int phases = calculateOlmPhaseCount(scale);
        int cappedScale = Math.min(scale, 51);
        int hpUnit = cappedScale + 1 - ((phases - 3) * 3);
        int head = 1_660 * hpUnit;
        int hands = 2_490 * hpUnit * phases;
        return new OlmPoints(head, hands, head + hands, phases, cappedScale, hpUnit);
    }

    public PurpleChance calculatePurpleChance(int points)
    {
        if (points < 0)
        {
            throw new IllegalArgumentException("Points cannot be negative.");
        }
        List<Double> rollProbabilities = new ArrayList<>();
        // CoX caps eligibility at six point chunks; excess points remain visible
        // in the result but cannot create additional potential rolls.
        int eligiblePoints = Math.min(points, POINTS_PER_POTENTIAL_ROLL * MAX_POTENTIAL_ROLLS);
        int remaining = eligiblePoints;
        while (remaining > 0 && rollProbabilities.size() < MAX_POTENTIAL_ROLLS)
        {
            int chunk = Math.min(remaining, POINTS_PER_POTENTIAL_ROLL);
            rollProbabilities.add(Math.min(1.0, chunk / (double) POINTS_PER_EXPECTED_PURPLE));
            remaining -= chunk;
        }

        List<Double> distribution = poissonBinomial(rollProbabilities);
        int displayed = Math.min(distribution.size(), MAX_DISPLAYED_PURPLES + 1);
        return new PurpleChance(points, rollProbabilities.size(), eligiblePoints / (double) POINTS_PER_EXPECTED_PURPLE,
            rollProbabilities, distribution.subList(0, displayed));
    }

    public Map<String, Integer> statsByGroupName(int scale)
    {
        Map<String, Integer> flattened = new LinkedHashMap<>();
        for (StatSection section : calculateNpcStats(scale))
        {
            for (StatGroup group : section.groups)
            {
                flattened.put(group.name + ":hitpoints", group.stats.get("hitpoints"));
                flattened.put(group.name + ":attack", group.stats.get("attack"));
                flattened.put(group.name + ":strength", group.stats.get("strength"));
                flattened.put(group.name + ":defence", group.stats.get("defence"));
                flattened.put(group.name + ":magic", group.stats.get("magic"));
                flattened.put(group.name + ":ranged", group.stats.get("ranged"));
            }
        }
        return flattened;
    }

    public double valueFromAnchors(List<Anchor> anchors, int scale)
    {
        // Exact anchors win; values between or outside anchors use the nearest
        // linear segment so every supported scale produces a deterministic value.
        if (anchors == null || anchors.isEmpty())
        {
            return 0;
        }

        List<Anchor> sorted = new ArrayList<>(anchors);
        sorted.sort((left, right) -> Integer.compare(left.scale, right.scale));
        for (Anchor anchor : sorted)
        {
            if (anchor.scale == scale)
            {
                return anchor.value;
            }
        }

        if (sorted.size() == 1)
        {
            Anchor only = sorted.get(0);
            return only.value * (scale / (double) only.scale);
        }

        int upperIndex = -1;
        for (int i = 0; i < sorted.size(); i++)
        {
            if (sorted.get(i).scale > scale)
            {
                upperIndex = i;
                break;
            }
        }

        if (upperIndex == -1)
        {
            return interpolate(sorted.get(sorted.size() - 2), sorted.get(sorted.size() - 1), scale);
        }
        if (upperIndex == 0)
        {
            return interpolate(sorted.get(0), sorted.get(1), scale);
        }
        return interpolate(sorted.get(upperIndex - 1), sorted.get(upperIndex), scale);
    }

    public static String formatInteger(int value)
    {
        return INTEGER_FORMAT.format(value);
    }

    public static String formatPercent(double value)
    {
        return String.format(Locale.US, "%.2f%%", value * 100);
    }

    private static double interpolate(Anchor lower, Anchor upper, int scale)
    {
        int scaleSpan = upper.scale - lower.scale;
        if (scaleSpan == 0)
        {
            return lower.value;
        }
        return lower.value + (upper.value - lower.value) * ((scale - lower.scale) / (double) scaleSpan);
    }

    private static List<Double> poissonBinomial(List<Double> probabilities)
    {
        // distribution[i] stores the probability of exactly i successes after
        // processing the rolls seen so far.
        List<Double> distribution = new ArrayList<>();
        distribution.add(1.0);
        for (double probability : probabilities)
        {
            List<Double> next = new ArrayList<>(Collections.nCopies(distribution.size() + 1, 0.0));
            for (int i = 0; i < distribution.size(); i++)
            {
                double chance = distribution.get(i);
                next.set(i, next.get(i) + chance * (1 - probability));
                next.set(i + 1, next.get(i + 1) + chance * probability);
            }
            distribution = next;
        }
        return distribution;
    }

    private static PointRow pointRow(String key, String name, int points, String note)
    {
        return new PointRow(key, name, points, note);
    }

    private static String olmPointNote(int scale, OlmPoints olm, boolean suppress)
    {
        if (suppress)
        {
            return null;
        }
        String note = formatInteger(olm.phases) + " Olm phases";
        if (olm.cappedScale != scale)
        {
            note += "\nHP scale capped at " + formatInteger(olm.cappedScale);
        }
        return note;
    }

    private static int calculateGuardiansPoints(int scale)
    {
        return calibratedObservedScaleCurve(scale, combatKillPoints(201 * (scale / 2 + 1), 2),
            combatKillPoints(201 * 2, 2), 2_083, 4_055);
    }

    private static int calculateMysticsPoints(int scale)
    {
        return calibratedObservedScaleCurve(scale,
            combatKillPoints(160 * (scale / 2 + 1), Math.min(12, 3 + scale / 3)),
            combatKillPoints(160 * 2, 3), 2_049, 4_041);
    }

    private static int calculateShamansPoints(int scale)
    {
        return calibratedObservedScaleCurve(scale,
            combatKillPoints(190 * (scale / 2 + 1), scale >= 9 ? 5 : 3),
            combatKillPoints(190 * 2, 3), 1_590, 3_051);
    }

    private static int calculateTightropePoints(int scale)
    {
        int count = scale >= 15 ? 8 : scale >= 8 ? 7 : 6;
        return calibratedObservedScaleCurve(scale, combatKillPoints(60 * (scale + 2), count),
            combatKillPoints(60 * 4, 6), 4_043, 5_115);
    }

    private static int combatKillPoints(int hitpoints, int count)
    {
        return (hitpoints * count * COMBAT_POINT_NUMERATOR) / COMBAT_POINT_DENOMINATOR;
    }

    private static int calibratedObservedScaleCurve(int scale, int uncalibratedPoints, int uncalibratedScaleTwoPoints,
        int observedScaleOnePoints, int observedScaleTwoPoints)
    {
        if (scale == 1)
        {
            return observedScaleOnePoints;
        }
        return Math.round(uncalibratedPoints * observedScaleTwoPoints / (float) uncalibratedScaleTwoPoints);
    }

    private static void assertPositiveInteger(int value, String label)
    {
        if (value <= 0)
        {
            throw new IllegalArgumentException(label + " must be a positive whole number.");
        }
    }

    private static List<Anchor> anchors(int... pairs)
    {
        List<Anchor> anchors = new ArrayList<>();
        for (int i = 0; i < pairs.length; i += 2)
        {
            anchors.add(new Anchor(pairs[i], pairs[i + 1]));
        }
        return anchors;
    }

    private static Map<String, List<Anchor>> stats(List<Anchor> hp, List<Anchor> attack, List<Anchor> strength, List<Anchor> defence, List<Anchor> magic, List<Anchor> ranged)
    {
        Map<String, List<Anchor>> stats = new LinkedHashMap<>();
        stats.put("hitpoints", hp);
        stats.put("attack", attack);
        stats.put("strength", strength);
        stats.put("defence", defence);
        stats.put("magic", magic);
        stats.put("ranged", ranged);
        return stats;
    }

    private static StatGroupData group(String name, List<Anchor> hp, List<Anchor> attack, List<Anchor> strength, List<Anchor> defence, List<Anchor> magic, List<Anchor> ranged)
    {
        return new StatGroupData(name, stats(hp, attack, strength, defence, magic, ranged));
    }

    private static List<StatSectionData> buildStatData()
    {
        List<Anchor> one = anchors(1, 1, 3, 1, 5, 1, 40, 1, 51, 1, 80, 1, 100, 1);
        List<Anchor> iceHp = anchors(1, 140, 3, 140, 5, 140, 40, 140, 51, 140, 80, 140, 100, 140);
        List<Anchor> iceDefence = anchors(1, 160, 3, 160, 5, 160, 40, 160, 51, 160, 80, 160, 100, 160);
        List<Anchor> iceMagic = anchors(1, 390, 3, 390, 5, 390, 40, 390, 51, 390, 80, 390, 100, 390);
        List<Anchor> combat = anchors(1, 140, 3, 152, 5, 165, 40, 253, 51, 278, 80, 329, 100, 366);
        List<Anchor> shamanCombat = anchors(1, 130, 3, 141, 5, 153, 40, 235, 51, 258, 80, 305, 100, 340);
        List<Anchor> olmCombat = anchors(1, 250, 3, 272, 5, 295, 40, 452, 51, 497, 80, 587, 100, 655);

        Map<String, List<Anchor>> maxHits = new LinkedHashMap<>();
        maxHits.put("normal", anchors(1, 26, 3, 28, 5, 30, 40, 48, 51, 54, 80, 63, 100, 69));
        maxHits.put("crippled", anchors(1, 27, 3, 29, 5, 31, 40, 50, 51, 56, 80, 66, 100, 73));
        maxHits.put("enraged", anchors(1, 28, 3, 30, 5, 32, 40, 51, 51, 58, 80, 68, 100, 75));
        maxHits.put("head", anchors(1, 29, 3, 31, 5, 33, 40, 53, 51, 60, 80, 70, 100, 77));

        return Arrays.asList(
            new StatSectionData("combatRooms", "Combat rooms", Arrays.asList(
                group("Guardians", anchors(1, 201, 3, 402, 5, 603, 40, 4221, 51, 5226, 80, 8241, 100, 10251), combat, combat, anchors(1, 100, 3, 109, 5, 118, 40, 181, 51, 199, 80, 235, 100, 262), one, one),
                group("Shamans", anchors(1, 190, 3, 380, 5, 570, 40, 3990, 51, 4940, 80, 7790, 100, 9690), shamanCombat, shamanCombat, anchors(1, 210, 3, 215, 5, 220, 40, 279, 51, 298, 80, 342, 100, 374), shamanCombat, shamanCombat),
                group("Mystics", anchors(1, 160, 3, 320, 5, 480, 40, 3360, 51, 4160, 80, 6560, 100, 8160), combat, combat, anchors(1, 187, 3, 191, 5, 195, 40, 249, 51, 265, 80, 305, 100, 333), combat, one)
            ), Collections.emptyMap()),
            new StatSectionData("puzzleRooms", "Puzzle rooms", Arrays.asList(
                group("Ice Demon", iceHp, one, one, iceDefence, iceMagic, iceMagic),
                group("Rope Mager", anchors(1, 120, 3, 240, 5, 360, 40, 2520, 51, 3120, 80, 4920, 100, 6120), one, one, anchors(1, 155, 3, 158, 5, 162, 40, 206, 51, 220, 80, 253, 100, 276), anchors(1, 210, 3, 228, 5, 247, 40, 380, 51, 417, 80, 493, 100, 550), one),
                group("Rope Ranger", anchors(1, 120, 3, 240, 5, 360, 40, 2520, 51, 3120, 80, 4920, 100, 6120), one, one, anchors(1, 155, 3, 158, 5, 162, 40, 206, 51, 220, 80, 253, 100, 276), anchors(1, 155, 3, 168, 5, 182, 40, 280, 51, 308, 80, 364, 100, 406), anchors(1, 210, 3, 228, 5, 247, 40, 380, 51, 417, 80, 493, 100, 550))
            ), Collections.emptyMap()),
            new StatSectionData("olm", "Olm", Arrays.asList(
                group("Olm Head", anchors(1, 800, 3, 1600, 5, 2400, 40, 10400, 51, 13600, 80, 13600, 100, 13600), olmCombat, olmCombat, anchors(1, 150, 3, 153, 5, 157, 40, 199, 51, 213, 80, 244, 100, 267), olmCombat, olmCombat),
                group("Melee Hand", anchors(1, 600, 3, 1200, 5, 1800, 40, 7800, 51, 10200, 80, 10200, 100, 10200), olmCombat, olmCombat, anchors(1, 175, 3, 178, 5, 183, 40, 232, 51, 248, 80, 285, 100, 311), anchors(1, 175, 3, 190, 5, 206, 40, 316, 51, 348, 80, 411, 100, 458), olmCombat),
                group("Mage Hand", anchors(1, 600, 3, 1200, 5, 1800, 40, 7800, 51, 10200, 80, 10200, 100, 10200), olmCombat, olmCombat, anchors(1, 175, 3, 178, 5, 183, 40, 232, 51, 248, 80, 285, 100, 311), anchors(1, 87, 3, 94, 5, 102, 40, 157, 51, 173, 80, 204, 100, 227), olmCombat)
            ), maxHits)
        );
    }

    public static class RaidOptions
    {
        public final int scale;
        public final int totalThievingLevel;
        public final int fishingLevel;
        public final String puzzle1;
        public final String puzzle2;
        public final String puzzle3;
        public final boolean suppressOlmPointNotes;
        public final boolean includeCrabs;
        public final int overloadSips;
        public final boolean includeGuardians;
        public final boolean includeMystics;
        public final boolean includeShamans;
        public final boolean includeMuttadiles;
        public final boolean includeTekton;
        public final boolean includeVasa;
        public final boolean includeVespula;
        public final boolean includeVanguards;
        public final boolean useDefaultPuzzles;

        public RaidOptions(int scale, int totalThievingLevel, int fishingLevel, String puzzle1, String puzzle2, boolean suppressOlmPointNotes, boolean includeCrabs)
        {
            this(scale, totalThievingLevel, fishingLevel, puzzle1, puzzle2, suppressOlmPointNotes, includeCrabs, 0);
        }

        public RaidOptions(int scale, int totalThievingLevel, int fishingLevel, String puzzle1, String puzzle2, boolean suppressOlmPointNotes,
            boolean includeCrabs, int overloadSips)
        {
            this(scale, totalThievingLevel, fishingLevel, puzzle1, puzzle2, suppressOlmPointNotes, includeCrabs, overloadSips,
                true, true, true, true);
        }

        public RaidOptions(int scale, int totalThievingLevel, int fishingLevel, String puzzle1, String puzzle2, boolean suppressOlmPointNotes,
            boolean includeCrabs, int overloadSips, boolean includeGuardians, boolean includeMystics, boolean includeShamans,
            boolean useDefaultPuzzles)
        {
            this(scale, totalThievingLevel, fishingLevel, puzzle1, puzzle2, "", suppressOlmPointNotes, includeCrabs, overloadSips,
                includeGuardians, includeMystics, includeShamans, false, false, false, false, false, useDefaultPuzzles);
        }

        public RaidOptions(int scale, int totalThievingLevel, int fishingLevel, String puzzle1, String puzzle2, String puzzle3,
            boolean suppressOlmPointNotes, boolean includeCrabs, int overloadSips, boolean includeGuardians, boolean includeMystics,
            boolean includeShamans, boolean useDefaultPuzzles)
        {
            this(scale, totalThievingLevel, fishingLevel, puzzle1, puzzle2, puzzle3, suppressOlmPointNotes, includeCrabs, overloadSips,
                includeGuardians, includeMystics, includeShamans, false, false, false, false, false, useDefaultPuzzles);
        }

        public RaidOptions(int scale, int totalThievingLevel, int fishingLevel, String puzzle1, String puzzle2, String puzzle3,
            boolean suppressOlmPointNotes, boolean includeCrabs, int overloadSips, boolean includeGuardians, boolean includeMystics,
            boolean includeShamans, boolean includeMuttadiles, boolean includeTekton, boolean includeVasa, boolean includeVespula,
            boolean includeVanguards, boolean useDefaultPuzzles)
        {
            this.scale = scale;
            this.totalThievingLevel = totalThievingLevel;
            this.fishingLevel = fishingLevel;
            this.puzzle1 = puzzle1;
            this.puzzle2 = puzzle2;
            this.puzzle3 = puzzle3;
            this.suppressOlmPointNotes = suppressOlmPointNotes;
            this.includeCrabs = includeCrabs;
            this.overloadSips = Math.max(0, overloadSips);
            this.includeGuardians = includeGuardians;
            this.includeMystics = includeMystics;
            this.includeShamans = includeShamans;
            this.includeMuttadiles = includeMuttadiles;
            this.includeTekton = includeTekton;
            this.includeVasa = includeVasa;
            this.includeVespula = includeVespula;
            this.includeVanguards = includeVanguards;
            this.useDefaultPuzzles = useDefaultPuzzles;
        }

        public static RaidOptions defaults(int scale)
        {
            return new RaidOptions(scale, 0, 75, PUZZLE_ICE_DEMON, PUZZLE_TIGHTROPE, false, false);
        }

        public List<String> selectedPuzzles()
        {
            List<String> puzzles = new ArrayList<>();
            if (isPuzzle(puzzle1))
            {
                puzzles.add(puzzle1);
            }
            if (isPuzzle(puzzle2))
            {
                puzzles.add(puzzle2);
            }
            if (isPuzzle(puzzle3))
            {
                puzzles.add(puzzle3);
            }
            if (puzzles.isEmpty() && useDefaultPuzzles)
            {
                puzzles.add(PUZZLE_ICE_DEMON);
                puzzles.add(PUZZLE_TIGHTROPE);
            }
            return puzzles;
        }

        private static boolean isPuzzle(String value)
        {
            return PUZZLE_ICE_DEMON.equals(value) || PUZZLE_TIGHTROPE.equals(value) || PUZZLE_THIEVING.equals(value);
        }
    }

    public static class Anchor
    {
        final int scale;
        final int value;

        Anchor(int scale, int value)
        {
            this.scale = scale;
            this.value = value;
        }
    }

    public static class StatSection
    {
        public final String key;
        public final String title;
        public final List<StatGroup> groups;
        public final Map<String, Integer> maxHits;

        StatSection(String key, String title, List<StatGroup> groups, Map<String, Integer> maxHits)
        {
            this.key = key;
            this.title = title;
            this.groups = Collections.unmodifiableList(groups);
            this.maxHits = Collections.unmodifiableMap(maxHits);
        }
    }

    public static class StatGroup
    {
        public final String name;
        public final Map<String, Integer> stats;

        StatGroup(String name, Map<String, Integer> stats)
        {
            this.name = name;
            this.stats = Collections.unmodifiableMap(stats);
        }
    }

    public static class PointRow
    {
        public final String key;
        public final String name;
        public final int points;
        public final String note;

        PointRow(String key, String name, int points, String note)
        {
            this.key = key;
            this.name = name;
            this.points = points;
            this.note = note;
        }
    }

    public static class RoomPoints
    {
        public final List<PointRow> rooms;
        public final List<PointRow> totals;
        public final int grandTotal;

        RoomPoints(List<PointRow> rooms, List<PointRow> totals, int grandTotal)
        {
            this.rooms = Collections.unmodifiableList(rooms);
            this.totals = Collections.unmodifiableList(totals);
            this.grandTotal = grandTotal;
        }
    }

    public static class ThievingPoints
    {
        public final int points;
        public final int grubsToCap;
        public final int totalThievingLevel;
        public final boolean usedMinimum;

        ThievingPoints(int points, int grubsToCap, int totalThievingLevel, boolean usedMinimum)
        {
            this.points = points;
            this.grubsToCap = grubsToCap;
            this.totalThievingLevel = totalThievingLevel;
            this.usedMinimum = usedMinimum;
        }
    }

    public static class IceDemonPoints
    {
        public final int points;
        public final int kindlingPoints;
        public final int killPoints;
        public final int kindlingToCap;
        public final double stacksToCap;

        IceDemonPoints(int points, int kindlingPoints, int killPoints, int kindlingToCap, double stacksToCap)
        {
            this.points = points;
            this.kindlingPoints = kindlingPoints;
            this.killPoints = killPoints;
            this.kindlingToCap = kindlingToCap;
            this.stacksToCap = stacksToCap;
        }
    }

    public static class FishingPoints
    {
        public final int points;
        public final int fishToCap;
        public final int fishingLevel;
        public final int pointsPerFish;

        FishingPoints(int points, int fishToCap, int fishingLevel, int pointsPerFish)
        {
            this.points = points;
            this.fishToCap = fishToCap;
            this.fishingLevel = fishingLevel;
            this.pointsPerFish = pointsPerFish;
        }
    }

    public static class OverloadPoints
    {
        public final int points;
        public final int sipsToCap;
        public final int overloadsToCap;
        public final int pointsPerSip;
        public final int sipsPerOverload;

        OverloadPoints(int points, int sipsToCap, int overloadsToCap, int pointsPerSip, int sipsPerOverload)
        {
            this.points = points;
            this.sipsToCap = sipsToCap;
            this.overloadsToCap = overloadsToCap;
            this.pointsPerSip = pointsPerSip;
            this.sipsPerOverload = sipsPerOverload;
        }
    }

    public static class OlmPoints
    {
        public final int head;
        public final int hands;
        public final int total;
        public final int phases;
        public final int cappedScale;
        public final int hpUnit;

        OlmPoints(int head, int hands, int total, int phases, int cappedScale, int hpUnit)
        {
            this.head = head;
            this.hands = hands;
            this.total = total;
            this.phases = phases;
            this.cappedScale = cappedScale;
            this.hpUnit = hpUnit;
        }
    }

    public static class PurpleChance
    {
        public final int points;
        public final int potentialRolls;
        public final double expectedPurples;
        public final List<Double> rollProbabilities;
        public final List<Double> distribution;

        PurpleChance(int points, int potentialRolls, double expectedPurples, List<Double> rollProbabilities, List<Double> distribution)
        {
            this.points = points;
            this.potentialRolls = potentialRolls;
            this.expectedPurples = expectedPurples;
            this.rollProbabilities = Collections.unmodifiableList(new ArrayList<>(rollProbabilities));
            this.distribution = Collections.unmodifiableList(new ArrayList<>(distribution));
        }
    }

    private static class StatSectionData
    {
        final String key;
        final String title;
        final List<StatGroupData> groups;
        final Map<String, List<Anchor>> maxHits;

        StatSectionData(String key, String title, List<StatGroupData> groups, Map<String, List<Anchor>> maxHits)
        {
            this.key = key;
            this.title = title;
            this.groups = groups;
            this.maxHits = maxHits;
        }
    }

    private static class StatGroupData
    {
        final String name;
        final Map<String, List<Anchor>> stats;

        StatGroupData(String name, Map<String, List<Anchor>> stats)
        {
            this.name = name;
            this.stats = stats;
        }
    }
}
