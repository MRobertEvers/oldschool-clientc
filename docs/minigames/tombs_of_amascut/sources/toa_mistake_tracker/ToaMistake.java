package com.toamistaketracker;

import lombok.Getter;
import lombok.NonNull;
import net.runelite.client.util.ImageUtil;

import java.awt.image.BufferedImage;
import java.util.function.Function;
import java.util.Arrays;
import java.util.EnumSet;
import java.util.Set;

public enum ToaMistake {
    // Deaths
    DEATH("Death", (config) -> "", "", "death.png"), // Chat message handled in deaths below
    DEATH_HET("Path of Het Death", ToaMistakeTrackerConfig::deathMessage, defaultDeathMessage(), "death-akkha.png"),
    DEATH_CRONDIS("Path of Crondis Death", ToaMistakeTrackerConfig::deathMessage, defaultDeathMessage(), "death-zebak.png"),
    DEATH_SCABARAS("Path of Scabaras Death", ToaMistakeTrackerConfig::deathMessage, defaultDeathMessage(), "death-kephri.png"),
    DEATH_APMEKEN("Path of Apmeken Death", ToaMistakeTrackerConfig::deathMessage, defaultDeathMessage(), "death-baba.png"),
    DEATH_WARDENS("Wardens Death", ToaMistakeTrackerConfig::deathMessage, defaultDeathMessage(), "death-wardens.png"),

    // Het
    HET_PUZZLE_LIGHT("Het Puzzle Energy Beam", ToaMistakeTrackerConfig::hetPuzzleBeamMessage, "Ah! It burns!",
            "het-light.png"),
    HET_PUZZLE_DARK_ORB("Het Puzzle Dark Orb", ToaMistakeTrackerConfig::hetPuzzleOrbMessage, "Embrace Darkness!",
            "het-dark-orb2.png"),
    AKKHA_SPECIAL_QUADRANT_BOMB("Akkha Quadrant Bombs", ToaMistakeTrackerConfig::akkhaQuadrantMessage, "I'm too slow!",
            "akkha-quadrant3.png"),
    AKKHA_SPECIAL_ELEMENTAL_ORBS("Akkha Elemental Orbs", ToaMistakeTrackerConfig::akkhaElementalOrbMessage, "I'm griefing!",
            "akkha-elemental2.png"),
    AKKHA_UNSTABLE_ORB("Akkha Unstable Orb", ToaMistakeTrackerConfig::akkhaUnstableOrbMessage, "?",
            "akkha-unstable-orb.png"),

    // Crondis
    CRONDIS_PUZZLE_LOW_WATER("Path of Crondis Watering", ToaMistakeTrackerConfig::crondisWaterMessage,
            "This jug feels a little light...", "crondis-water.png"),
    ZEBAK_ACID_TILE("Zebak Acid Tile", ToaMistakeTrackerConfig::zebakAcidMessage, "I'm drowning in acid!",
            "zebak-acid.png"),
    ZEBAK_BLOOD_CLOUD("Zebak Blood Cloud", ToaMistakeTrackerConfig::zebakBloodMessage, "I'm on a blood cloud!",
            "zebak-blood-cloud.png"),
    ZEBAK_EARTHQUAKE("Zebak Scream", ToaMistakeTrackerConfig::zebakScreamMessage, "Nihil!", "zebak-scream.png"),
    ZEBAK_WAVE("Zebak Wave", ToaMistakeTrackerConfig::zebakWaveMessage, "I'm surfing!", "zebak-wave.png"),

    // Scabaras
    KEPHRI_BOMB("Kephri Bomb", ToaMistakeTrackerConfig::kephriBombMessage, "I'm exploding!", "kephri-bomb.png"),
    KEPHRI_SWARM_HEAL("Kephri Swarm Heal", (config) -> "The swarms are going in!", "The swarms are going in!",
            "kephri-swarm.png"),
    KEPHRI_EGG_EXPLODE("Kephri Egg Explode", (config) -> "I've been hatched!", "I've been hatched!", ""),

    // Apmeken
    APMEKEN_PUZZLE_SIGHT("Apmeken Sight", (config) -> "", "", "apmeken-sight.png"),
    APMEKEN_PUZZLE_VENT("Apmeken Vent", ToaMistakeTrackerConfig::apmekenVentMessage, "I'm fuming!", ""),
    APMEKEN_PUZZLE_PILLAR("Apmeken Pillar", ToaMistakeTrackerConfig::apmekenPillarMessage, "The sky is falling!", ""),
    APMEKEN_PUZZLE_CORRUPTION("Apmeken Corruption", ToaMistakeTrackerConfig::apmekenCorruptionMessage,
            "I've been corrupted!", ""),
    APMEKEN_PUZZLE_VENOM("Apmeken Venom Tile", ToaMistakeTrackerConfig::apmekenVenomMessage, "It's venomous!",
            "apmeken-venom.png"),
    APMEKEN_PUZZLE_VOLATILE("Apmeken Volatile", ToaMistakeTrackerConfig::apmekenVolatileMessage, "I'm exploding!",
            "apmeken-volatile.png"),
    BABA_SLAM("Ba-Ba Slam", ToaMistakeTrackerConfig::babaSlamMessage, "Come on and slam!|And welcome to the jam!",
            "baba-slam.png"),
    BABA_PROJECTILE_BOULDER("Ba-Ba Projectile Boulder", ToaMistakeTrackerConfig::babaProjectileBoulderMessage,
            "I got rocked!", "baba-projectile-boulder.png"),
    BABA_ROLLING_BOULDER("Ba-Ba Rolling Boulder", ToaMistakeTrackerConfig::babaRollingBoulderMessage,
            "They see me rollin'...", "baba-rolling-boulder.png"),
    BABA_FALLING_BOULDER("Ba-Ba Falling Boulder", ToaMistakeTrackerConfig::babaFallingBoulderMessage, "It's raining!",
            "baba-falling-boulder.png"),
    BABA_BANANA("Ba-Ba Banana", ToaMistakeTrackerConfig::babaBananaMessage, "Who put that there?", "baba-banana.png"),
    BABA_GAP("Ba-Ba Gap", ToaMistakeTrackerConfig::babaGapMessage, "I'm going down!", "baba-gap.png"),

    // Wardens
    WARDENS_P1_PYRAMID("Wardens P1 Pyramid", ToaMistakeTrackerConfig::wardensPyramidMessage, "I'm disco-ing!",
            "wardens-pyramid.png"),
    WARDENS_P2_OBELISK("Wardens P2 Obelisk", (config) -> "", "", "wardens-obelisk.png"),
    WARDENS_P2_DDR("Wardens P2 DDR", ToaMistakeTrackerConfig::wardensDDRMessage, "I'm dancing!", ""),
    WARDENS_P2_WINDMILL("Wardens P2 Windmill", ToaMistakeTrackerConfig::wardensWindmillMessage, "I'm winded!", ""),
    WARDENS_P2_BOMBS("Wardens P2 Bombs", ToaMistakeTrackerConfig::wardensBombsMessage, "I'm getting bombed!", ""),
    WARDENS_P2_BIND("Wardens P2 Bind", ToaMistakeTrackerConfig::wardensBindMessage, "I'm in jail!", "wardens-bind.png"),
    WARDENS_P2_SPECIAL_PRAYER("Wardens P2 Special Prayer", ToaMistakeTrackerConfig::wardensPrayerMessage,
            "What even was that attack?", "wardens-special-prayer.png"),
    WARDENS_P3_EARTHQUAKE("Wardens P3 Slam", ToaMistakeTrackerConfig::wardensSlamMessage, "I'm tripping!",
            "wardens-earthquake.png"),
    WARDENS_P3_AKKHA("Wardens P3 Akkha", (config) -> "", "", "wardens-akkha.png"),
    WARDENS_P3_ZEBAK("Wardens P3 Zebak", (config) -> "", "", "wardens-zebak.png"),
    WARDENS_P3_KEPHRI("Wardens P3 Kephri", ToaMistakeTrackerConfig::kephriBombMessage, KEPHRI_BOMB.getDefaultMessage(),
            "wardens-kephri.png"),
    WARDENS_P3_BABA("Wardens P3 Ba-Ba", ToaMistakeTrackerConfig::babaFallingBoulderMessage,
            BABA_FALLING_BOULDER.getDefaultMessage(), "wardens-baba.png"),
    WARDENS_P3_LIGHTNING("Wardens P3 Lightning", (config) -> "", "", "wardens-lightning.png"), // Too noisy
    ;

    private static final Set<ToaMistake> ROOM_DEATHS = EnumSet.of(DEATH_HET, DEATH_CRONDIS, DEATH_SCABARAS,
            DEATH_APMEKEN, DEATH_WARDENS);

    private static final Set<ToaMistake> APMEKEN_SIGHT_MISTAKES = EnumSet.of(APMEKEN_PUZZLE_VENT, APMEKEN_PUZZLE_PILLAR,
            APMEKEN_PUZZLE_CORRUPTION);
    private static final Set<ToaMistake> WARDENS_P2_OBELISK_MISTAKES = EnumSet.of(WARDENS_P2_DDR, WARDENS_P2_WINDMILL,
            WARDENS_P2_BOMBS);

    private static final String FALLBACK_IMAGE_PATH = "death.png";

    private static final int MAX_STACKING_CHAT_MESSAGE_LENGTH = 10;
    public static final int MAX_MESSAGE_LENGTH = 40;

    @Getter
    @NonNull
    private final String mistakeName;

    @Getter
    @NonNull
    private final String defaultMessage;

    @NonNull
    private final Function<ToaMistakeTrackerConfig, String> chatMessageFunc;

    @Getter
    @NonNull
    private final BufferedImage mistakeImage;

    ToaMistake(@NonNull String mistakeName, @NonNull Function<ToaMistakeTrackerConfig, String> chatMessageFunc, @NonNull String defaultMessage,
            @NonNull String mistakeImagePath) {
        this.mistakeName = mistakeName;
        this.chatMessageFunc = chatMessageFunc;
        this.defaultMessage = defaultMessage;

        final String imagePath;
        if (mistakeImagePath.isEmpty()) {
            imagePath = FALLBACK_IMAGE_PATH;
        } else {
            imagePath = mistakeImagePath;
        }
        this.mistakeImage = ImageUtil.loadImageResource(getClass(), imagePath);
    }

    public String getChatMessage(ToaMistakeTrackerConfig config) {
        return chatMessageFunc.apply(config);
    }

    public static String defaultDeathMessage() {
        return "I'm planking!";
    }

    public static boolean isRoomDeath(ToaMistake mistake) {
        return ROOM_DEATHS.contains(mistake);
    }

    /**
     * Get the grouped mistake for the specified detected mistake.
     *
     * @param mistake The detected mistake
     * @return The grouped mistake
     */
    public static ToaMistake toGroupedMistake(ToaMistake mistake) {
        if (APMEKEN_SIGHT_MISTAKES.contains(mistake)) {
            return APMEKEN_PUZZLE_SIGHT;
        } else if (WARDENS_P2_OBELISK_MISTAKES.contains(mistake)) {
            return WARDENS_P2_OBELISK;
        } else {
            return mistake;
        }
    }

    /**
     * Retrieve the chat message for the given mistake, considering special cases given the config
     * settings about whether to stack question marks and the current mistake count of either
     * this specific mistake in the raid for the current raider, *or* the total current raid mistake
     * count for the current raider.
     *
     * @param config       The configuration object to retrieve chat message from
     * @param mistake      The mistake
     * @param mistakeCount The current mistake count of this mistake *or* the current raid mistake count for the
     *                     raider in this raid
     *
     * @return The mistake chat message to use for the raider
     */
    public static String getChatMessageForMistakeCount(ToaMistakeTrackerConfig config, ToaMistake mistake,
            int mistakeCount) {
        String mistakeMessage = mistake.getChatMessage(config);
        // Special case a few mistake chat messages based on config
        if (config.mistakeMessageStacking() != StackingBehavior.NONE && mistakeMessage.equals("?")) {
            return getStackingChatMessage(mistakeMessage, mistakeCount);
        }
        if (mistakeMessage.contains("|")) {
            return getAlternatingChatMessage(mistakeMessage, mistake.getDefaultMessage(), mistakeCount);
        }

        return mistakeMessage;
    }

    private static String getStackingChatMessage(String message, int mistakeCount) {
        // Keep adding ?s to the text for every subsequent mistake in this raid. A bit hacky but it's funny.
        StringBuilder sb = new StringBuilder(message);
        for (int i = 0; i < Math.min(mistakeCount, MAX_STACKING_CHAT_MESSAGE_LENGTH); i++) {
            sb.append(message);
        }
        return sb.toString();
    }

    private static String getAlternatingChatMessage(String message, String defaultMessage, int mistakeCount) {
        String[] messageChoices = Arrays.stream(message.split("\\|")).filter(msg -> !msg.isEmpty())
                .toArray(String[]::new);
        if (messageChoices.length == 0) {
            return defaultMessage;
        }
        return messageChoices[mistakeCount % messageChoices.length];
    }
}
