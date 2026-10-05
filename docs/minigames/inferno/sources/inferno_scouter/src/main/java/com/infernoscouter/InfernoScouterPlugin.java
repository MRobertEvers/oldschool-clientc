package com.infernoscouter;

import com.google.gson.JsonSyntaxException;
import com.google.inject.Provides;
import java.awt.Color;
import java.awt.Toolkit;
import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.UnsupportedFlavorException;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.EnumMap;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Inject;
import net.runelite.api.Actor;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.GameObject;
import net.runelite.api.NPC;
import net.runelite.api.Scene;
import net.runelite.api.Tile;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.ClientTick;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.events.NpcSpawned;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.ui.overlay.OverlayManager;
import net.runelite.client.util.ImageUtil;
import net.runelite.client.util.Text;

@PluginDescriptor(
        name = "Inferno Scouter",
        description = "Scouts Inferno wave spawns and outputs a 9-tile code.",
        tags = {"inferno", "scout", "waves"}
)
public class InfernoScouterPlugin extends Plugin
{
    private static final int INFERNO_REGION_ID = 9043;
    private static final int REGION_X_OFFSET = 17;
    private static final int REGION_Y_OFFSET = 46;
    private static final int GRID_WIDTH = 29;
    private static final int GRID_HEIGHT = 30;
    private static final Pattern WAVE_MESSAGE = Pattern.compile("Wave: (\\d+)");
    private static final Color PLACEHOLDER_COLOR = new Color(210, 210, 210);
    private static final Color START_TILE_COLOR = new Color(0xFF51B4BA, true);
    private static final Color PILLAR_ALIVE_COLOR = new Color(18, 18, 18, 220);
    private static final Color PILLAR_DESTROYED_COLOR = new Color(100, 70, 76, 190);
    private static final String START_TILE_LABEL = "Start";
    private static final String DEFAULT_CODE = "[ooooooooo000000]";
    private static final int PILLAR_SIZE = 3;

    // Mirrors inferno-2d-map pillar object IDs and coordinate matching.
    private static final Set<Integer> PILLAR_OBJECT_IDS = Set.of(30353, 30354, 30355);
    private static final Set<Integer> PILLAR_NPC_IDS = Set.of(7709, 7710);
    private static final List<PillarSlot> PILLAR_CODE_ORDER = List.of(
            PillarSlot.NORTH,
            PillarSlot.SOUTH,
            PillarSlot.WEST
    );

    private static final Set<Integer> ALLOWED_NPC_IDS = Set.of(
            7692, // Jal-MejRah (bat)
            7693, // Jal-Ak (blob)
            7697, // Jal-ImKot (melee)
            7698, // Jal-Xil (ranger)
            7702, // Jal-Xil (alt)
            7699, // Jal-Zek (mager)
            7703  // Jal-Zek (alt)
    );

    private static final List<P> REGION_SPAWNS = buildRegionSpawns();
    private static final Map<Long, Integer> REGION_INDEX = buildRegionIndex(REGION_SPAWNS);

    @Inject private Client client;
    @Inject private ClientToolbar clientToolbar;
    @Inject private OverlayManager overlayManager;
    @Inject private InfernoScouterConfig config;
    @Inject private InfernoStartTileOverlay startTileOverlay;
    @Inject private com.google.gson.Gson gson;

    private InfernoScouterPanel panel;
    private NavigationButton navButton;

    private String lastCode = DEFAULT_CODE;
    private int currentWaveNumber = -1;
    private int pendingWaveNumber = -1;
    private int pendingWaveStartTick = -1;
    private StartTile startTile = null;
    private String startTileDisplayText = "";
    private final EnumSet<PillarSlot> alivePillars = EnumSet.allOf(PillarSlot.class);
    private final EnumMap<PillarSlot, Integer> pillarHpBySlot = buildInitialPillarHp();
    private final EnumMap<PillarSlot, Integer> displayedPillarHpBySlot = buildInitialPillarHp();

    private final List<SpawnSnapshot> currentWaveSpawns = new ArrayList<>();

    private int batchTick = -1;
    private int quietClientTicks = 0;
    private boolean batchOpen = false;
    private int batchWaveNumber = -1;
    private final List<SpawnSnapshot> batch = new ArrayList<>();

    @Provides
    InfernoScouterConfig provideConfig(ConfigManager configManager)
    {
        return configManager.getConfig(InfernoScouterConfig.class);
    }

    @Override
    protected void startUp()
    {
        panel = new InfernoScouterPanel();
        panel.setCode(lastCode);
        panel.setWaveNumber(currentWaveNumber);
        panel.setSpawns(buildInitialSpawns());
        panel.setPillars(buildPillarRender());
        panel.setStartTileActions(this::handlePasteStartTile, this::handleResetStartTile);
        panel.setStartTileDisplay(startTileDisplayText);
        applyStartTileToPanel();
        panel.setLegendColors(
                config.batColor(),
                config.blobColor(),
                config.meleeColor(),
                config.rangerColor(),
                config.magerColor()
        );

        BufferedImage icon;
        try
        {
            icon = ImageUtil.loadImageResource(getClass(), "icon.png");
            if (icon == null)
            {
                icon = new BufferedImage(16, 16, BufferedImage.TYPE_INT_ARGB);
            }
        }
        catch (Exception e)
        {
            icon = new BufferedImage(16, 16, BufferedImage.TYPE_INT_ARGB);
        }

        navButton = NavigationButton.builder()
                .tooltip("Inferno Scouter")
                .icon(icon)
                .panel(panel)
                .priority(6)
                .build();

        clientToolbar.addNavigation(navButton);
        overlayManager.add(startTileOverlay);
    }

    @Override
    protected void shutDown()
    {
        if (navButton != null)
        {
            clientToolbar.removeNavigation(navButton);
            navButton = null;
        }
        if (startTileOverlay != null)
        {
            overlayManager.remove(startTileOverlay);
        }
        panel = null;
    }

    @Subscribe
    public void onGameStateChanged(GameStateChanged e)
    {
        if (batchOpen && e.getGameState() != GameState.LOGGED_IN)
        {
            finalizeBatch();
            clearBatch();
        }

        if (e.getGameState() != GameState.LOGGED_IN)
        {
            pendingWaveNumber = -1;
            pendingWaveStartTick = -1;
        }
    }

    @Subscribe
    public void onChatMessage(ChatMessage event)
    {
        if (client.getGameState() != GameState.LOGGED_IN)
        {
            return;
        }

        if (event.getType() != ChatMessageType.SPAM && event.getType() != ChatMessageType.GAMEMESSAGE)
        {
            return;
        }

        String message = Text.removeTags(event.getMessage());
        Matcher matcher = WAVE_MESSAGE.matcher(message);
        if (matcher.find())
        {
            pendingWaveNumber = Integer.parseInt(matcher.group(1));
            pendingWaveStartTick = client.getTickCount();
            if (pendingWaveNumber == 1)
            {
                resetPillarHp();
            }
            if (batchOpen && batchTick == pendingWaveStartTick && batchWaveNumber <= 0)
            {
                batchWaveNumber = pendingWaveNumber;
            }
        }
    }

    @Subscribe
    public void onNpcSpawned(NpcSpawned event)
    {
        if (!isInInferno())
        {
            return;
        }

        NPC npc = event.getNpc();
        if (npc == null)
        {
            return;
        }

        if (PILLAR_NPC_IDS.contains(npc.getId()))
        {
            syncPillarsFromScene();
            return;
        }

        if (!ALLOWED_NPC_IDS.contains(npc.getId()))
        {
            return;
        }

        int tick = client.getTickCount();
        if (pendingWaveStartTick >= 0 && (tick - pendingWaveStartTick) > 2)
        {
            return;
        }

        WorldPoint wp = npc.getWorldLocation();
        if (wp == null)
        {
            return;
        }

        if (!batchOpen || tick != batchTick)
        {
            batchOpen = true;
            batchTick = tick;
            batchWaveNumber = pendingWaveNumber;
            batch.clear();
            quietClientTicks = 0;

            // Keep pillar state in sync with the same initial scout tick as NPC detection.
            syncPillarsFromScene();
            snapshotDisplayedPillarHp();
        }

        quietClientTicks = 0;

        MobType type = typeFor(npc);
        if (type == null)
        {
            return;
        }

        batch.add(new SpawnSnapshot(type, wp.getRegionX(), wp.getRegionY(), npc.getIndex()));
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event)
    {
        if (!isInInferno())
        {
            return;
        }

        GameObject gameObject = event.getGameObject();
        if (gameObject == null || !PILLAR_OBJECT_IDS.contains(gameObject.getId()))
        {
            return;
        }

        PillarSlot slot = pillarSlotFor(gameObject);
        if (slot != null)
        {
            int oldHp = pillarHpBySlot.getOrDefault(slot, 0);
            int newHp = oldHp > 0 ? oldHp : 99;
            alivePillars.add(slot);
            pillarHpBySlot.put(slot, newHp);
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event)
    {
        if (!isInInferno())
        {
            return;
        }

        GameObject gameObject = event.getGameObject();
        if (gameObject == null)
        {
            return;
        }

        PillarSlot slot = pillarSlotFor(gameObject);
        if (slot != null)
        {
            alivePillars.remove(slot);
            pillarHpBySlot.put(slot, 0);
        }
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event)
    {
        if (!isInInferno())
        {
            return;
        }

        Actor actor = event.getActor();
        if (actor instanceof NPC)
        {
            NPC npc = (NPC) actor;
            if (PILLAR_NPC_IDS.contains(npc.getId()))
            {
                updatePillarHpFromNpcs();
            }
        }
    }

    @Subscribe
    public void onGameTick(GameTick tick)
    {
        if (isInInferno())
        {
            updatePillarHpFromNpcs();
        }
    }

    @Subscribe
    public void onClientTick(ClientTick tick)
    {
        if (!batchOpen)
        {
            return;
        }

        int now = client.getTickCount();
        if (now > batchTick)
        {
            finalizeBatch();
            clearBatch();
            return;
        }

        quietClientTicks++;
        if (quietClientTicks >= 2)
        {
            finalizeBatch();
            clearBatch();
        }
    }

    @Subscribe
    public void onConfigChanged(ConfigChanged event)
    {
        if (!"infernoscout".equals(event.getGroup()))
        {
            return;
        }

        if (panel != null)
        {
            panel.setLegendColors(
                    config.batColor(),
                    config.blobColor(),
                    config.meleeColor(),
                    config.rangerColor(),
                    config.magerColor()
            );
            panel.setSpawns(buildInitialSpawns());
            panel.setPillars(buildPillarRender());
        }
    }

    private void finalizeBatch()
    {
        if (batch.isEmpty())
        {
            return;
        }

        if (batchWaveNumber <= 0)
        {
            return;
        }

        syncPillarsFromScene();

        lastCode = buildCode(batch);
        currentWaveSpawns.clear();
        currentWaveSpawns.addAll(batch);

        if (batchWaveNumber > 0)
        {
            currentWaveNumber = batchWaveNumber;
        }

        pendingWaveNumber = -1;
        pendingWaveStartTick = -1;

        updatePanel();
    }

    private void updatePanel()
    {
        if (panel == null)
        {
            return;
        }

        panel.setCode(lastCode);
        panel.setWaveNumber(currentWaveNumber);
        panel.setSpawns(buildInitialSpawns());
        panel.setPillars(buildPillarRender());
        applyStartTileToPanel();
    }

    private void handlePasteStartTile()
    {
        String text = readClipboardText();
        StartTile parsed = parseStartTile(text);
        if (parsed == null)
        {
            startTile = null;
            startTileDisplayText = "X";
        }
        else
        {
            startTile = parsed;
            startTileDisplayText = "(" + parsed.gridX + ", " + parsed.gridY + ")";
        }

        applyStartTileToPanel();
    }

    private void handleResetStartTile()
    {
        startTile = null;
        startTileDisplayText = "";
        applyStartTileToPanel();
    }

    private void applyStartTileToPanel()
    {
        if (panel == null)
        {
            return;
        }

        if (startTile != null)
        {
            panel.setStartTile(startTile.gridX, startTile.gridY, startTile.color);
        }
        else
        {
            panel.clearStartTile();
        }
        panel.setStartTileDisplay(startTileDisplayText);
    }

    private static String readClipboardText()
    {
        try
        {
            Object data = Toolkit.getDefaultToolkit().getSystemClipboard().getData(DataFlavor.stringFlavor);
            if (data instanceof String)
            {
                return (String) data;
            }
        }
        catch (UnsupportedFlavorException | IOException | IllegalStateException ignored)
        {
            // Clipboard unavailable
        }
        return null;
    }

    private StartTile parseStartTile(String text)
    {
        if (text == null)
        {
            return null;
        }

        String trimmed = text.trim();
        if (trimmed.isEmpty())
        {
            return null;
        }

        TileMarkerData[] markers;
        try
        {
            markers = gson.fromJson(trimmed, TileMarkerData[].class);
        }
        catch (JsonSyntaxException ex)
        {
            return null;
        }

        if (markers == null || markers.length != 1 || markers[0] == null)
        {
            return null;
        }

        TileMarkerData marker = markers[0];
        if (marker.regionId != INFERNO_REGION_ID)
        {
            return null;
        }

        if (marker.z != 0)
        {
            return null;
        }

        int gridX = marker.regionX - REGION_X_OFFSET;
        int gridY = REGION_Y_OFFSET - marker.regionY;
        if (gridX < 0 || gridX >= GRID_WIDTH || gridY < 0 || gridY >= GRID_HEIGHT)
        {
            return null;
        }

        WorldPoint wp = WorldPoint.fromRegion(marker.regionId, marker.regionX, marker.regionY, marker.z);
        return new StartTile(marker.regionX, marker.regionY, gridX, gridY, wp, START_TILE_COLOR, START_TILE_LABEL);
    }

    private String buildCode(List<SpawnSnapshot> wave)
    {
        char[] out = new char[9];
        Arrays.fill(out, 'o');
        Map<Integer, SpawnSnapshot> bySpawnSlot = new HashMap<>();

        for (SpawnSnapshot s : wave)
        {
            int idx = indexForRegion(s.regionX, s.regionY);
            if (idx >= 0 && idx < out.length)
            {
                out[idx] = letterFor(s.type);
                bySpawnSlot.put(idx, s);
            }
        }

        Map<Integer, Integer> rankBySpawnSlot = buildRankBySpawnSlot(bySpawnSlot);
        int lowestRank = rankBySpawnSlot.size();

        StringBuilder code = new StringBuilder(28);
        code.append('[');
        for (int i = 0; i < out.length; i++)
        {
            code.append(out[i]);
            Integer rank = rankBySpawnSlot.get(i);
            if (rank != null && rank < lowestRank)
            {
                code.append(rank);
            }
        }
        appendPillarHp(code);
        code.append(']');
        return code.toString();
    }

    private void appendPillarHp(StringBuilder code)
    {
        for (PillarSlot slot : PILLAR_CODE_ORDER)
        {
            appendTwoDigits(code, displayedPillarHpBySlot.getOrDefault(slot, 0));
        }
    }

    private static void appendTwoDigits(StringBuilder out, int value)
    {
        int clamped = Math.max(0, Math.min(99, value));
        out.append((char) ('0' + (clamped / 10)));
        out.append((char) ('0' + (clamped % 10)));
    }

    private static Map<Integer, Integer> buildRankBySpawnSlot(Map<Integer, SpawnSnapshot> bySpawnSlot)
    {
        List<Map.Entry<Integer, SpawnSnapshot>> entries = new ArrayList<>(bySpawnSlot.entrySet());
        entries.sort((a, b) ->
        {
            int byNpcIndex = Integer.compare(b.getValue().npcIndex, a.getValue().npcIndex);
            if (byNpcIndex != 0)
            {
                return byNpcIndex;
            }
            return Integer.compare(a.getKey(), b.getKey());
        });

        Map<Integer, Integer> rankBySpawnSlot = new HashMap<>();
        for (int i = 0; i < entries.size(); i++)
        {
            rankBySpawnSlot.put(entries.get(i).getKey(), i + 1);
        }
        return rankBySpawnSlot;
    }

    private List<InfernoSpawnImage.Spawn> buildRenderSpawns(List<SpawnSnapshot> wave)
    {
        List<InfernoSpawnImage.Spawn> spawns = new ArrayList<>();
        for (SpawnSnapshot s : wave)
        {
            int x = s.regionX - REGION_X_OFFSET;
            int y = REGION_Y_OFFSET - s.regionY;
            if (x < 0 || y < 0)
            {
                continue;
            }

            spawns.add(new InfernoSpawnImage.Spawn(x, y, sizeFor(s.type), colorFor(s.type), letterFor(s.type)));
        }
        return spawns;
    }

    private List<InfernoSpawnImage.Spawn> buildInitialSpawns()
    {
        if (!currentWaveSpawns.isEmpty())
        {
            return buildRenderSpawns(currentWaveSpawns);
        }

        List<InfernoSpawnImage.Spawn> spawns = new ArrayList<>();
        for (int i = 0; i < REGION_SPAWNS.size(); i++)
        {
            P p = REGION_SPAWNS.get(i);
            int x = p.x - REGION_X_OFFSET;
            int y = REGION_Y_OFFSET - p.y;
            if (x < 0 || y < 0)
            {
                continue;
            }

            char number = (char) ('1' + i);
            spawns.add(new InfernoSpawnImage.Spawn(x, y, 1, PLACEHOLDER_COLOR, number));
        }
        return spawns;
    }

    private List<InfernoSpawnImage.Pillar> buildPillarRender()
    {
        List<InfernoSpawnImage.Pillar> pillars = new ArrayList<>();
        for (PillarSlot slot : PillarSlot.values())
        {
            int hp = displayedPillarHpBySlot.getOrDefault(slot, 0);
            boolean alive = hp > 0;
            Color color = alive ? PILLAR_ALIVE_COLOR : PILLAR_DESTROYED_COLOR;
            String label = alive ? pillarLabelFor(slot, hp) : null;
            pillars.add(new InfernoSpawnImage.Pillar(slot.x, slot.y, PILLAR_SIZE, color, label));
        }
        return pillars;
    }

    private static String pillarLabelFor(PillarSlot slot, int hp)
    {
        if (hp >= 99)
        {
            return String.valueOf(slot.label);
        }
        return String.valueOf(Math.max(1, Math.min(98, hp)));
    }

    private void updatePillarsOnPanel()
    {
        if (panel == null)
        {
            return;
        }
        panel.setPillars(buildPillarRender());
    }

    private void syncPillarsFromScene()
    {
        if (!isInInferno())
        {
            return;
        }

        Scene scene = client.getScene();
        if (scene == null)
        {
            return;
        }

        Tile[][][] tiles = scene.getTiles();
        int plane = client.getPlane();
        if (tiles == null || plane < 0 || plane >= tiles.length || tiles[plane] == null)
        {
            return;
        }

        EnumSet<PillarSlot> detected = EnumSet.noneOf(PillarSlot.class);
        EnumMap<PillarSlot, Integer> detectedHp = new EnumMap<>(PillarSlot.class);
        Tile[][] planeTiles = tiles[plane];
        for (int x = 0; x < planeTiles.length; x++)
        {
            Tile[] column = planeTiles[x];
            if (column == null)
            {
                continue;
            }

            for (int y = 0; y < column.length; y++)
            {
                Tile tile = column[y];
                if (tile == null)
                {
                    continue;
                }

                GameObject[] gameObjects = tile.getGameObjects();
                if (gameObjects == null)
                {
                    continue;
                }

                for (GameObject gameObject : gameObjects)
                {
                    if (gameObject == null || !PILLAR_OBJECT_IDS.contains(gameObject.getId()))
                    {
                        continue;
                    }

                    PillarSlot slot = pillarSlotFor(gameObject);
                    if (slot != null)
                    {
                        detected.add(slot);
                        detectedHp.put(slot, pillarHpBySlot.getOrDefault(slot, 99));
                    }
                }
            }
        }

        for (NPC npc : client.getNpcs())
        {
            if (npc == null || !PILLAR_NPC_IDS.contains(npc.getId()))
            {
                continue;
            }

            PillarSlot slot = pillarSlotFor(npc);
            if (slot == null)
            {
                continue;
            }

            int hp = pillarHpFor(npc);
            if (hp < 0)
            {
                detected.add(slot);
                detectedHp.putIfAbsent(slot, pillarHpBySlot.getOrDefault(slot, 99));
            }
            else if (hp > 0)
            {
                detected.add(slot);
                detectedHp.put(slot, hp);
            }
            else
            {
                detected.remove(slot);
                detectedHp.put(slot, 0);
            }
        }

        for (PillarSlot slot : PillarSlot.values())
        {
            detectedHp.putIfAbsent(slot, detected.contains(slot) ? pillarHpBySlot.getOrDefault(slot, 99) : 0);
        }

        if (!alivePillars.equals(detected) || !pillarHpBySlot.equals(detectedHp))
        {
            alivePillars.clear();
            alivePillars.addAll(detected);
            pillarHpBySlot.clear();
            pillarHpBySlot.putAll(detectedHp);
        }
    }

    private static PillarSlot pillarSlotFor(GameObject gameObject)
    {
        WorldPoint worldPoint = gameObject.getWorldLocation();
        if (worldPoint == null)
        {
            return null;
        }

        // Same coordinate conversion used by inferno-2d-map for pillar matching.
        int scoutX = worldPoint.getRegionX() - 18;
        int scoutY = 47 - worldPoint.getRegionY();

        for (PillarSlot slot : PillarSlot.values())
        {
            if (slot.x == scoutX && slot.y == scoutY)
            {
                return slot;
            }
        }
        return null;
    }

    private static PillarSlot pillarSlotFor(NPC npc)
    {
        WorldPoint worldPoint = npc.getWorldLocation();
        if (worldPoint == null)
        {
            return null;
        }

        int gridX = worldPoint.getRegionX() - REGION_X_OFFSET;
        int gridY = REGION_Y_OFFSET - worldPoint.getRegionY();
        PillarSlot containing = pillarSlotContaining(gridX, gridY);
        if (containing != null)
        {
            return containing;
        }

        return nearestPillarSlot(gridX, gridY);
    }

    private void updatePillarHpFromNpcs()
    {
        for (NPC npc : client.getNpcs())
        {
            if (npc == null || !PILLAR_NPC_IDS.contains(npc.getId()))
            {
                continue;
            }

            PillarSlot slot = pillarSlotFor(npc);
            if (slot == null)
            {
                continue;
            }

            int hp = pillarHpFor(npc);
            if (hp < 0)
            {
                alivePillars.add(slot);
                continue;
            }

            boolean alive = hp > 0;
            if (alive)
            {
                alivePillars.add(slot);
            }
            else
            {
                alivePillars.remove(slot);
            }

            pillarHpBySlot.put(slot, hp);
        }
    }

    private static PillarSlot pillarSlotContaining(int gridX, int gridY)
    {
        for (PillarSlot slot : PillarSlot.values())
        {
            if (gridX >= slot.x && gridX < slot.x + PILLAR_SIZE
                    && gridY <= slot.y && gridY > slot.y - PILLAR_SIZE)
            {
                return slot;
            }
        }
        return null;
    }

    private static PillarSlot nearestPillarSlot(int gridX, int gridY)
    {
        PillarSlot nearest = null;
        int nearestDistance = Integer.MAX_VALUE;
        for (PillarSlot slot : PillarSlot.values())
        {
            int centerX = slot.x + 1;
            int centerY = slot.y - 1;
            int dx = gridX - centerX;
            int dy = gridY - centerY;
            int distance = dx * dx + dy * dy;
            if (distance < nearestDistance)
            {
                nearest = slot;
                nearestDistance = distance;
            }
        }
        return nearestDistance <= 8 ? nearest : null;
    }

    private static int pillarHpFor(NPC npc)
    {
        if (npc.getId() == 7710 || npc.isDead())
        {
            return 0;
        }

        int ratio = npc.getHealthRatio();
        int scale = npc.getHealthScale();
        if (ratio < 0 || scale <= 0)
        {
            return -1;
        }

        if (ratio <= 0)
        {
            return 0;
        }

        if (ratio >= scale)
        {
            return 99;
        }

        return Math.max(1, Math.min(98, (int) Math.round((ratio * 100.0) / scale)));
    }

    private static EnumMap<PillarSlot, Integer> buildInitialPillarHp()
    {
        EnumMap<PillarSlot, Integer> hp = new EnumMap<>(PillarSlot.class);
        for (PillarSlot slot : PillarSlot.values())
        {
            hp.put(slot, 99);
        }
        return hp;
    }

    private void resetPillarHp()
    {
        alivePillars.clear();
        alivePillars.addAll(EnumSet.allOf(PillarSlot.class));
        pillarHpBySlot.clear();
        pillarHpBySlot.putAll(buildInitialPillarHp());
        snapshotDisplayedPillarHp();
        lastCode = DEFAULT_CODE;
        if (panel != null)
        {
            panel.setCode(lastCode);
        }
        updatePillarsOnPanel();
    }

    private void snapshotDisplayedPillarHp()
    {
        displayedPillarHpBySlot.clear();
        displayedPillarHpBySlot.putAll(pillarHpBySlot);
    }

    private void clearBatch()
    {
        batchOpen = false;
        batchTick = -1;
        batchWaveNumber = -1;
        quietClientTicks = 0;
        batch.clear();
    }

    boolean isInInferno()
    {
        if (client.getGameState() != GameState.LOGGED_IN)
        {
            return false;
        }
        int[] regions = client.getMapRegions();
        if (regions == null)
        {
            return false;
        }
        for (int r : regions)
        {
            if (r == INFERNO_REGION_ID)
            {
                return true;
            }
        }
        return false;
    }

    StartTile getStartTile()
    {
        return startTile;
    }

    private static int indexForRegion(int x, int y)
    {
        Integer idx = REGION_INDEX.get(key(x, y));
        return idx == null ? -1 : idx;
    }

    private static char letterFor(MobType type)
    {
        switch (type)
        {
            case BAT: return 'Y';
            case BLOB: return 'B';
            case MELEE: return 'X';
            case RANGER: return 'R';
            case MAGER: return 'M';
            default: return '?';
        }
    }

    private static int sizeFor(MobType type)
    {
        switch (type)
        {
            case BAT: return 2;
            case BLOB: return 3;
            case MELEE: return 4;
            case RANGER: return 3;
            case MAGER: return 4;
            default: return 2;
        }
    }

    private Color colorFor(MobType type)
    {
        switch (type)
        {
            case BAT: return config.batColor();
            case BLOB: return config.blobColor();
            case MELEE: return config.meleeColor();
            case RANGER: return config.rangerColor();
            case MAGER: return config.magerColor();
            default: return Color.WHITE;
        }
    }

    private static MobType typeFor(NPC npc)
    {
        String name = npc.getName();
        if ("Jal-MejRah".equals(name)) return MobType.BAT;
        if ("Jal-Ak".equals(name)) return MobType.BLOB;
        if ("Jal-ImKot".equals(name)) return MobType.MELEE;
        if ("Jal-Xil".equals(name)) return MobType.RANGER;
        if ("Jal-Zek".equals(name)) return MobType.MAGER;

        switch (npc.getId())
        {
            case 7692: return MobType.BAT;
            case 7693: return MobType.BLOB;
            case 7697: return MobType.MELEE;
            case 7698:
            case 7702: return MobType.RANGER;
            case 7699:
            case 7703: return MobType.MAGER;
            default: return null;
        }
    }

    private static List<P> buildRegionSpawns()
    {
        List<P> pts = new ArrayList<>();
        pts.add(new P(18, 41));
        pts.add(new P(39, 41));
        pts.add(new P(20, 35));
        pts.add(new P(40, 34));
        pts.add(new P(33, 29));
        pts.add(new P(22, 23));
        pts.add(new P(40, 21));
        pts.add(new P(18, 18));
        pts.add(new P(32, 18));

        pts.sort((a, b) ->
        {
            if (a.y != b.y) return Integer.compare(b.y, a.y);
            return Integer.compare(a.x, b.x);
        });

        return Collections.unmodifiableList(pts);
    }

    private static Map<Long, Integer> buildRegionIndex(List<P> pts)
    {
        Map<Long, Integer> map = new HashMap<>();
        for (int i = 0; i < pts.size(); i++)
        {
            P p = pts.get(i);
            map.put(key(p.x, p.y), i);
        }
        return Collections.unmodifiableMap(map);
    }

    private static final class P
    {
        final int x;
        final int y;
        P(int x, int y)
        {
            this.x = x;
            this.y = y;
        }
    }

    private enum PillarSlot
    {
        WEST(0, 9, 'W'),
        NORTH(17, 7, 'N'),
        SOUTH(10, 23, 'S');

        final int x;
        final int y;
        final char label;

        PillarSlot(int x, int y, char label)
        {
            this.x = x;
            this.y = y;
            this.label = label;
        }
    }

    private enum MobType
    {
        BAT,
        BLOB,
        MELEE,
        RANGER,
        MAGER
    }

    private static final class SpawnSnapshot
    {
        final MobType type;
        final int regionX;
        final int regionY;
        final int npcIndex;

        SpawnSnapshot(MobType type, int regionX, int regionY, int npcIndex)
        {
            this.type = type;
            this.regionX = regionX;
            this.regionY = regionY;
            this.npcIndex = npcIndex;
        }
    }

    static final class StartTile
    {
        final int regionX;
        final int regionY;
        final int gridX;
        final int gridY;
        final WorldPoint worldPoint;
        final Color color;
        final String label;

        StartTile(int regionX, int regionY, int gridX, int gridY, WorldPoint worldPoint, Color color, String label)
        {
            this.regionX = regionX;
            this.regionY = regionY;
            this.gridX = gridX;
            this.gridY = gridY;
            this.worldPoint = worldPoint;
            this.color = color;
            this.label = label;
        }
    }

    private static final class TileMarkerData
    {
        int regionId;
        int regionX;
        int regionY;
        int z;
        String color;
        String label;
    }

    private static long key(int x, int y)
    {
        return (((long) x) << 32) ^ (y & 0xffffffffL);
    }
}
