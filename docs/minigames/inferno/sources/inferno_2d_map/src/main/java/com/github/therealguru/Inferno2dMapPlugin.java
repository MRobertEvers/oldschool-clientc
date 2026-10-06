package com.github.therealguru;

import com.google.inject.Binder;
import com.google.inject.Provides;

import javax.inject.Inject;

import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.api.gameval.NpcID;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.util.ImageUtil;

import java.awt.*;
import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

@Slf4j
@PluginDescriptor(
        name = "Inferno 2d Map"
)
public class Inferno2dMapPlugin extends Plugin {

	private static final int INFERNO_REGION = 9043;
	private static final List<Integer> INFERNO_NPC_LIST = List.of(
			NpcID.INFERNO_CREATURE_HARPIE,
			NpcID.INFERNO_CREATURE_SPLITTER,
			NpcID.INFERNO_CREATURE_MELEE,
			NpcID.INFERNO_CREATURE_RANGER,
			NpcID.INFERNO_CREATURE_MAGER
    );
    public static final Map<Integer, Color> NPC_ID_TO_COLOUR = Map.of(
      NpcID.INFERNO_CREATURE_HARPIE, Color.PINK,
      NpcID.INFERNO_CREATURE_SPLITTER, Color.YELLOW,
      NpcID.INFERNO_CREATURE_MELEE, Color.RED,
      NpcID.INFERNO_CREATURE_RANGER, Color.GREEN,
      NpcID.INFERNO_CREATURE_MAGER, Color.BLUE
    );
    private static final List<Integer> PILLAR_IDS = List.of(30353, 30354, 30355);
    public static final int GRID_WIDTH = 29;
    public static final int GRID_HEIGHT = 30;

    private Integer currentWaveNumber;
	private final List<InfernoNpc> infernoNpcs = new ArrayList<>();
    private final List<GameObject> infernoPillars = new ArrayList<>();
    private Color[][] infernoMap;

    @Inject
    private Client client;
    @Inject
    private ClientToolbar clientToolbar;
    private NavigationButton navigationButton;
    private InfernoMapPluginPanel panel;

    @Inject
    private InfernoMapConfig config;

    @Override
    protected void startUp() {
        panel = new InfernoMapPluginPanel(this, config);
        BufferedImage icon = ImageUtil.loadImageResource(getClass(), "/devil.png");
        navigationButton = NavigationButton.builder()
                .tooltip("Inferno 2d Map")
                .priority(10000)
                .panel(panel)
                .icon(icon)
                .build();

        clientToolbar.addNavigation(navigationButton);
        infernoMap = generateBlankMap();
    }

    @Override
    protected void shutDown() throws Exception {
        clientToolbar.removeNavigation(navigationButton);
        infernoMap = generateBlankMap();
        infernoNpcs.clear();
        infernoPillars.clear();
        currentWaveNumber = null;
    }

    @Subscribe
    private void onChatMessage(ChatMessage event) {
        if (!isInInferno() || event.getType() != ChatMessageType.GAMEMESSAGE) {
            return;
        }

        String message = event.getMessage();

        if (event.getMessage().contains("Wave:")) {
            message = message.substring(message.indexOf(": ") + 2);
            currentWaveNumber = Integer.parseInt(message.substring(0, message.indexOf('<')));
            infernoNpcs.clear();
            panel.updateWave(currentWaveNumber);
        }
    }

    @Subscribe
    public void onGameStateChanged(GameStateChanged gameStateChanged) {
        if (gameStateChanged.getGameState() != GameState.LOGGED_IN) {
            return;
        }

        infernoNpcs.clear();
        infernoPillars.clear();
        recalculate();
    }


	@Subscribe
	private void onNpcSpawned(NpcSpawned event) {
		if(!isInInferno()) return;

        if(!INFERNO_NPC_LIST.contains(event.getNpc().getId())) return;

		infernoNpcs.add(new InfernoNpc(event.getNpc()));
        Coordinate clean = new Coordinate(event.getNpc());
        Coordinate scout = clean.toScoutToolPoint();
        log.debug("Npc has spawned at {} scout coordinate: {}", clean, scout);
        recalculate();
	}

    void recalculate() {
        List<ColourRegion> npcLocations = infernoNpcs.stream()
                .map(InfernoNpc::getColourRegion)
                .collect(Collectors.toList());

		List<ColourRegion> pillars = infernoPillars.stream()
                .map(this::createGameObjectRegion)
                .collect(Collectors.toList());

        infernoMap = generateBlankMap();

        for (ColourRegion npcColourRegion : npcLocations) {
            fillRegion(infernoMap, npcColourRegion);
        }

        for (ColourRegion pillarColourRegion : pillars) {
            fillRegion(infernoMap, pillarColourRegion);
        }

        panel.updateGrid(infernoMap);
    }

    private Color[][] generateBlankMap() {
        int width = GRID_WIDTH;
        int height = GRID_HEIGHT;
        Color[][] map = new Color[width][height];

        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                map[x][y] = config.emptyColour();
            }
        }
        return map;
    }

    private void fillRegion(Color[][] colorArray, ColourRegion colourRegion) {
        for (int x = colourRegion.getFrom().getX(); x <= colourRegion.getTo().getX(); x++) {
            for (int y = colourRegion.getFrom().getY(); y <= colourRegion.getTo().getY(); y++) {
                colorArray[x][y] = colourRegion.getColor();
            }
        }
    }

    @Subscribe
    private void onGameObjectSpawned(GameObjectSpawned event) {
        if(!isInInferno()) return;

        if(!PILLAR_IDS.contains(event.getGameObject().getId())) return;

        Coordinate clean = new Coordinate(event.getGameObject());
        Coordinate scout = clean.toScoutToolPoint();
        log.debug("Pillar has spawned at {} scout coordinate: {}", clean, scout);
        infernoPillars.add(event.getGameObject());
        recalculate();
    }

    @Subscribe
    private void onGameObjectDespawned(GameObjectDespawned event) {
        if(!isInInferno()) return;

        infernoPillars.remove(event.getGameObject());
        recalculate();
    }

    private boolean isInInferno() {
        return WorldPoint.fromLocalInstance(client, client.getLocalPlayer().getLocalLocation()).getRegionID() == INFERNO_REGION;
    }

    @Provides
    InfernoMapConfig provideConfig(ConfigManager configManager) {
        return configManager.getConfig(InfernoMapConfig.class);
    }

    private ColourRegion createGameObjectRegion(GameObject gameObject) {
        Coordinate southWest = new Coordinate(gameObject.getWorldLocation().getRegionX() - 1, gameObject.getWorldLocation().getRegionY() - 1).toCleanPoint();
        Coordinate northEast = southWest.transform(gameObject.sizeX() - 1, gameObject.sizeY() - 1);
        return new ColourRegion(southWest, northEast, Color.BLACK);
    }

    public String generateScoutUrl() {
        StringBuilder url = new StringBuilder("https://ifreedive-osrs.github.io/?");

        appendNpcData(url);
        appendPillarFlags(url);
        appendDegenFlag(url);

        return url.toString();
    }

    private void appendNpcData(StringBuilder url) {
        Map<Integer, Integer> npcIdToMobType = createNpcIdToMobTypeMap();
        int blobCount = 0;

        for (InfernoNpc npc : infernoNpcs) {
            if (!npcIdToMobType.containsKey(npc.getNpc().getId())) {
                continue;
            }

            // Convert to scout tool coordinates (north-west origin)
            Coordinate scoutCoord = getScoutCoordinate(npc.getColourRegion());

            // Get mob type, handling blob differentiation
            int baseMobType = npcIdToMobType.get(npc.getNpc().getId());
            int mobType = baseMobType;

            if (baseMobType == 2) { // This is a blob/splitter
                blobCount++;
                if (blobCount == 2) {
                    mobType = 3; // Second blob gets type 3
                }
            }

            // Ensure coordinates are in valid range
            if (!isValidScoutCoordinate(scoutCoord)) {
                log.warn("Invalid coordinates for NPC {}: x={}, y={}", npc.getNpc().getId(), scoutCoord.getX(), scoutCoord.getY());
                continue;
            }

            // Format as XXYYT. (XX = 2-digit x, YY = 2-digit y, T = mob type)
            String npcData = String.format("%02d%02d%d.", scoutCoord.getX(), scoutCoord.getY(), mobType);
            url.append(npcData);

            log.debug("Added NPC {} at ({},{}) as type {} -> {}", npc.getNpc().getId(), scoutCoord.getX(), scoutCoord.getY(), mobType, npcData);
        }
    }

    private Coordinate getScoutCoordinate(ColourRegion region) {
        return region.getFrom().toScoutToolPoint();
    }

    private Coordinate getScoutCoordinate(GameObject gameObject) {
        return new Coordinate(gameObject).toScoutToolPoint();
    }

    private boolean isValidScoutCoordinate(Coordinate coord) {
        return coord.getX() >= 0 && coord.getX() < GRID_WIDTH && coord.getY() >= 0 && coord.getY() < GRID_HEIGHT;
    }

    private Map<Integer, Integer> createNpcIdToMobTypeMap() {
        return Map.of(
                NpcID.INFERNO_CREATURE_HARPIE, 1,      // Bat
                NpcID.INFERNO_CREATURE_SPLITTER, 2,    // Blob
                NpcID.INFERNO_CREATURE_MELEE, 5,       // Melee
                NpcID.INFERNO_CREATURE_RANGER, 6,      // Range
                NpcID.INFERNO_CREATURE_MAGER, 7        // Mager
        );
    }

    private void appendPillarFlags(StringBuilder url) {
        Map<String, int[]> expectedPillars = getExpectedPillarPositions();

        for (Map.Entry<String, int[]> entry : expectedPillars.entrySet()) {
            String pillarName = entry.getKey();
            int[] expectedPos = entry.getValue();

            if (!pillarExistsAt(expectedPos[0], expectedPos[1])) {
                url.append(getPillarFlag(pillarName));
            }
        }
    }

    private Map<String, int[]> getExpectedPillarPositions() {
        // Positions in north-west coordinate system
        return Map.of(
                "west", new int[]{0, 9},    // West pillar
                "north", new int[]{17, 7},  // North pillar
                "south", new int[]{10, 23}  // South pillar
        );
    }

    private boolean pillarExistsAt(int expectedX, int expectedY) {
        return infernoPillars.stream().anyMatch(pillar -> {
            Coordinate scoutCoord = getScoutCoordinate(pillar);
            return scoutCoord.getX() == expectedX && scoutCoord.getY() == expectedY;
        });
    }

    private String getPillarFlag(String pillarName) {
        switch (pillarName) {
            case "west":
                return ".noWe";
            case "north":
                return ".noN";
            case "south":
                return ".noS";
            default:
                return "";
        }
    }

    private void appendDegenFlag(StringBuilder url) {
        url.append(".degeN");
    }
}
