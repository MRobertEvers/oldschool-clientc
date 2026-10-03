package com.tombsofamascutpuzzlehelper;

import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics2D;
import java.awt.Polygon;
import java.util.List;
import javax.inject.Inject;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.Perspective;
import net.runelite.api.Point;
import net.runelite.api.Tile;
import net.runelite.api.coords.LocalPoint;
import net.runelite.client.ui.overlay.Overlay;
import net.runelite.client.ui.overlay.OverlayLayer;
import net.runelite.client.ui.overlay.OverlayPosition;
import net.runelite.client.ui.overlay.OverlayUtil;

public class TombsOfAmascutOverlay extends Overlay
{
	private final Client client;
	private final TombsOfAmascutPlugin plugin;

	@Inject
	private TombsOfAmascutOverlay(Client client, TombsOfAmascutPlugin plugin)
	{
		super(plugin);
		setPosition(OverlayPosition.DYNAMIC);
		setLayer(OverlayLayer.ABOVE_SCENE);
		this.client = client;
		this.plugin = plugin;
	}

	@Override
	public Dimension render(Graphics2D graphics)
	{
		plugin.getNumberPuzzleTiles().forEach(tile -> highlightTile(graphics, tile, Color.BLACK));

		List<SimonSaysTile> simonSaysTiles = plugin.getSimonSaysTiles();
		for(int i = 0; i < simonSaysTiles.size(); i++) {
			highlightAndLabelTile(graphics, simonSaysTiles.get(i).getTile(), Color.BLACK, Color.WHITE, String.valueOf(i + 1));
		}

		List<NPC> obelisks = plugin.getObelisks();
		for(int i = 0; i < obelisks.size(); i++) {
			NPC obelisk = obelisks.get(i);
			highlightAndLabelTile(graphics, obelisk.getCanvasTilePoly(), obelisk.getLocalLocation(), Color.BLACK, Color.WHITE, String.valueOf(i + 1));
		}

		plugin.getMatchingTiles().forEach(matchingTile -> highlightAndLabelTile(graphics, matchingTile.getTile(), Color.BLACK, Color.WHITE, matchingTile.getTileName()));
		return null;
	}

	private void highlightTile(Graphics2D graphics, Tile tile, Color color)
	{
		final Polygon poly = tile.getGroundObject().getCanvasTilePoly();

		if (poly != null)
		{
			OverlayUtil.renderPolygon(graphics, poly, color);
		}
	}

	private void highlightAndLabelTile(Graphics2D graphics, Tile tile, Color polyColor, Color textColor, String label)
	{
		final Polygon poly = tile.getGroundObject().getCanvasTilePoly();

		if (poly != null)
		{
			Point canvasTextLocation = Perspective.getCanvasTextLocation(client, graphics, tile.getLocalLocation(), label, 0);
			OverlayUtil.renderTextLocation(graphics, canvasTextLocation, label, textColor);
			OverlayUtil.renderPolygon(graphics, poly, polyColor);
		}
	}

	private void highlightAndLabelTile(Graphics2D graphics, Polygon poly, LocalPoint localPoint, Color polyColor, Color textColor, String label)
	{
		if (poly != null)
		{
			Point canvasTextLocation = Perspective.getCanvasTextLocation(client, graphics, localPoint, label, 0);
			OverlayUtil.renderTextLocation(graphics, canvasTextLocation, label, textColor);
			OverlayUtil.renderPolygon(graphics, poly, polyColor);
		}
	}
}
