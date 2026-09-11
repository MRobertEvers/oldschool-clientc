package com.tobutilities.bloat;

import static com.tobutilities.bloat.BloatConstants.BLOAT_FLOOR_IDS;
import static com.tobutilities.bloat.BloatConstants.PESTILENT_BLOAT;
import com.tobutilities.common.RoomHandler;
import com.tobutilities.TobUtilitiesPlugin;
import com.tobutilities.TobUtilitiesConfig;
import javax.inject.Inject;

import lombok.extern.slf4j.Slf4j;

import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameTick;

import net.runelite.api.events.PreMapLoad;
import net.runelite.client.callback.RenderCallback;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.events.ConfigChanged;

import java.awt.*;

@Slf4j
public class BloatHandler extends RoomHandler implements RenderCallback
{
	private static final String configGroup = "tobutilities";
	private boolean isBloatAlive = false;

    // Bloat floor coordinates
    private static final int MIN_X = 3288;
    private static final int MAX_X = 3303;
    private static final int MIN_Y = 4440;
    private static final int MAX_Y = 4455;
    private static final int PLANE = 0;

    // Inner square (Chamber object) to skip
    private static final int INNER_MIN_X = 3293;
    private static final int INNER_MAX_X = 3298;
    private static final int INNER_MIN_Y = 4445;
    private static final int INNER_MAX_Y = 4450;

    @Inject
    private ConfigManager configManager;

	@Inject
	protected BloatHandler(TobUtilitiesPlugin plugin, TobUtilitiesConfig config, Client client)
	{
		super(plugin, config, client);
	}

    @Override
    public boolean addEntity(Renderable renderable, boolean drawingUi)
	{
		if (!(renderable instanceof Player) || !isBloatAlive)
		{
			return true;
		}

		Player player = (Player) renderable;

		if (player.equals(client.getLocalPlayer()) && !drawingUi)
		{
			return !config.hideLocalPlayerDuringBloat();
		}

		return drawingUi
			? !config.hideOtherPlayersOverheadsDuringBloat()
			: !config.hideOtherPlayersDuringBloat();
	}

    private void hideBloatGroundObject(Tile tile)
    {
        if (tile == null)
        {
            return;
        }

        GroundObject obj = tile.getGroundObject();
        if (obj != null && BLOAT_FLOOR_IDS.contains(obj.getId()))
        {
            tile.setGroundObject(null);
        }
    }

    @Subscribe public void onPreMapLoad(PreMapLoad event)
    {
        if (!config.hideBloatFloor())
        {
            return;
        }

        int hsl = getSafeHsl(config.bloatFloorColor());

        Scene scene = event.getScene();
        Tile[][][] tiles = scene.getTiles();
        Tile[][] planeTiles = tiles[0];
        for (Tile[] row : planeTiles) {
            for (Tile tile : row) {
                if (!isBloatFloor(scene, tile))
                {
                    continue;
                }
                recolorTile(tile, hsl);
                hideBloatGroundObject(tile);
            }
        }
    }

    private boolean isBloatFloor(Scene scene, Tile tile)
    {
        if (tile == null || tile.getPlane() != PLANE)
        {
            return false;
        }

        WorldPoint wp = WorldPoint.fromLocalInstance(scene, tile.getLocalLocation(), tile.getPlane());

        // Check outer bounds
        boolean inOuter = wp.getX() >= MIN_X && wp.getX() <= MAX_X
                && wp.getY() >= MIN_Y && wp.getY() <= MAX_Y;

        // Check inner bounds (tiles below the Chamber object in the middle of the room)
        boolean inInner = wp.getX() >= INNER_MIN_X && wp.getX() <= INNER_MAX_X
                && wp.getY() >= INNER_MIN_Y && wp.getY() <= INNER_MAX_Y;

        return inOuter && !inInner;
    }

    private void recolorTile(Tile tile, int color)
    {
        SceneTilePaint paint = tile.getSceneTilePaint();
        if (paint != null)
        {
            paint.setNwColor(color);
            paint.setNeColor(color);
            paint.setSwColor(color);
            paint.setSeColor(color);
            tile.setSceneTilePaint(paint);
        }
    }

    // Some RGB colors (blue/magenta) will not render properly when converting to HSL which is why I add a small offset to light.
    // The color will be slightly lighter but this ensures it will render.
    private int getSafeHsl(Color color)
    {
        int r = color.getRed();
        int g = color.getGreen();
        int b = color.getBlue();
        int rgb = (r << 16) | (g << 8) | b;

        int hsl = JagexColor.rgbToHSL(rgb, 1.0);
        if (hsl > 0)
        {
            return hsl;
        }

        int hue = (hsl >> 10) & 0x3F;
        int sat = (hsl >> 7) & 0x07;
        int light = hsl & 0x7F;

        // Add small offset to light
        light = Math.max(1, light);

        hsl = (hue << 10) | (sat << 7) | light;

        // Fallback to a gray 0x707070
        if (hsl <= 0)
        {
            hsl = JagexColor.rgbToHSL(0x707070, 1.0d);
        }

        return hsl;
    }

	@Subscribe
	public void onGameTick(GameTick tick)
	{
		for (NPC npc : client.getWorldView(WorldView.TOPLEVEL).npcs())
		{
			if (PESTILENT_BLOAT.equals(npc.getName()))
			{
				isBloatAlive = !npc.isDead();
                break;
			}
		}
	}

    @Subscribe
    public void onConfigChanged(ConfigChanged event)
    {
        if (configGroup.equals(event.getGroup()))
        {
            switch (event.getKey())
            {
                case "hideBloatFloor":
                case "bloatFloorColor":
                    reloadScene();
                    break;
            }
        }
    }

    private void reloadScene()
    {
        clientThread.invokeLater(() ->
        {
            if (client.getGameState() == GameState.LOGGED_IN)
            {
                client.setGameState(GameState.LOADING);
            }
        });
    }

    public void onRoomExit() {}

    public void onRoomEntry() {}

	public void startUp()
	{
		// Migrate old skybox color config to new floor color config
		String oldSkyboxColor = configManager.getConfiguration(configGroup, "bloatSkyboxColor");
		if (oldSkyboxColor != null)
		{
			log.debug("Migrating Bloat floor color config");
			configManager.setConfiguration(configGroup, "bloatFloorColor", oldSkyboxColor);
			configManager.unsetConfiguration(configGroup, "bloatSkyboxColor");
		}
	}

    public void shutDown() {}
}
