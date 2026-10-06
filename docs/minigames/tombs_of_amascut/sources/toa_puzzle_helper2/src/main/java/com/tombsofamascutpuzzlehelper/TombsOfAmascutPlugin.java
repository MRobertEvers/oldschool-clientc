package com.tombsofamascutpuzzlehelper;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.NPC;
import net.runelite.api.Tile;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GroundObjectSpawned;
import net.runelite.api.events.NpcChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.overlay.OverlayManager;

@Slf4j
@PluginDescriptor(
	name = "Tombs of Amascut Puzzle Helper",
	description = "Plugin for various features within the Tombs of Amascut"
)
public class TombsOfAmascutPlugin extends Plugin
{
	private static final int SIMON_SAYS_LIT_UP_TILE_ID = 45341;
	private static final int UNLOCKED_ROOM_ID = 29733;
	private static final int LIT_UP_OBELISK_ID = 11699;

	private List<Tile> numberPuzzleTiles;
	private Map<Integer, List<Tile>> puzzleTileMap;
	private Map<String, List<Integer>> puzzleSolutionMap;
	private int puzzleXMin;
	private int puzzleXMax;
	private int puzzleYMin;
	private int puzzleYMax;
	private int puzzleSpotCount;

	private List<SimonSaysTile> simonSaysTiles;

	private List<NPC> obelisks;

	private Map<Integer, List<MatchingTile>> matchingTileMap;
	private List<MatchingTile> matchingTiles;

	@Inject
	private OverlayManager overlayManager;

	@Inject
	private TombsOfAmascutOverlay tombsOfAmascutOverlay;

	@Override
	protected void startUp() throws Exception
	{
		populatePuzzleTileMap();
		populatePuzzleSolutionMap();
		init();
		overlayManager.add(tombsOfAmascutOverlay);
	}

	@Override
	protected void shutDown() throws Exception
	{
		overlayManager.remove(tombsOfAmascutOverlay);
	}

	public List<Tile> getNumberPuzzleTiles() {
		return this.numberPuzzleTiles;
	}

	public List<SimonSaysTile> getSimonSaysTiles() {
		return this.simonSaysTiles;
	}

	public List<NPC> getObelisks() {
		return this.obelisks;
	}

	public List<MatchingTile> getMatchingTiles() {
		return this.matchingTiles;
	}

	@Subscribe
	public void onChatMessage(ChatMessage event) {
		String message = event.getMessage();

		if(event.getType() == ChatMessageType.GAMEMESSAGE && event.getMessage().contains("has been hastily chipped into the stone.")) {
			String targetNumber = message.substring(message.indexOf(">") + 1, message.indexOf("</"));
			populateNumberPuzzleTiles(targetNumber);
		}
	}

	@Subscribe
	public void onNpcChanged(NpcChanged event) {
		NPC npc = event.getNpc();
		if(npc.getId() == LIT_UP_OBELISK_ID) {
			boolean shouldAdd = true;

			for(NPC obelisk : obelisks) {
				if(obelisk.getWorldLocation().getX() == npc.getWorldLocation().getX() &&
					obelisk.getWorldLocation().getY() == npc.getWorldLocation().getY()) {
					shouldAdd = false;
					break;
				}
			}

			if(shouldAdd) {
				obelisks.add(npc);
			}
		}
	}

	@Subscribe
	public void onGameObjectSpawned(GameObjectSpawned event) {
		int gameObjectId = event.getGameObject().getId();

		if(gameObjectId == SIMON_SAYS_LIT_UP_TILE_ID) {
			long timeSinceLastAdd = !simonSaysTiles.isEmpty() ? System.currentTimeMillis() - simonSaysTiles.get(simonSaysTiles.size() - 1).getTimestamp() : 10_000;

			if(timeSinceLastAdd > 750) {
				simonSaysTiles = new ArrayList<>();
			}

			simonSaysTiles.add(new SimonSaysTile(System.currentTimeMillis(), event.getTile()));

			if(simonSaysTiles.size() == 9) {
				simonSaysTiles.clear();
			}
		}

		if(gameObjectId == UNLOCKED_ROOM_ID) {
			init();
		}
	}

	@Subscribe
	public void onGroundObjectSpawned(GroundObjectSpawned event) {
		int groundObjectId = event.getGroundObject().getId();

		if(puzzleTileMap.containsKey(groundObjectId)) {
			puzzleTileMap.get(groundObjectId).add(event.getTile());
			puzzleSpotCount++;
		}

		if(puzzleSpotCount == 25) {
			populatePuzzleBounds();
		}

		if(matchingTileMap.containsKey(groundObjectId)) {
			List<MatchingTile> matchingTiles = matchingTileMap.get(groundObjectId);
			if(matchingTiles.size() < 2) {
				MatchingTile matchingTile = new MatchingTile(getMatchingTileName(groundObjectId), event.getTile());
				matchingTiles.add(matchingTile);
				populateMatchingTiles();
			}
		}
	}

	private void init() {
		puzzleSpotCount = 0;
		numberPuzzleTiles = new ArrayList<>();
		simonSaysTiles = new ArrayList<>();
		obelisks = new ArrayList<>();
		matchingTileMap = new HashMap<>();
		matchingTiles = new ArrayList<>();
		puzzleXMin = Integer.MAX_VALUE;
		puzzleXMax = Integer.MIN_VALUE;
		puzzleYMin = Integer.MAX_VALUE;
		puzzleYMax = Integer.MIN_VALUE;
		populateMatchingTileMap();
		populateMatchingTiles();
		populatePuzzleBounds();
	}

 	private void populatePuzzleSolutionMap()
	{
		puzzleSolutionMap = new HashMap<>();

		addSolutionList("20", "2,8,14");
		addSolutionList("21", "4,9,14,19,24");
		addSolutionList("22", "5,10,15,20,25");
		addSolutionList("23", "1,7,13,19,25");
		addSolutionList("24", "2,8,14,20");
		addSolutionList("25", "3,8,13,18");
		addSolutionList("26", "5,10,15,20,24,25");
		addSolutionList("27", "3,8,13,18,19");
		addSolutionList("28", "2,7,12,17,21,22");
		addSolutionList("29", "3,8,13,18,24");
		addSolutionList("30", "3,8,13,18,23");
		addSolutionList("31", "1,2,3,4,5");
		addSolutionList("32", "5,10,15,20,22,23,24,25");
		addSolutionList("33", "1,2,3,4,5,9");
		addSolutionList("34", "3,8,13,18,23,24");
		addSolutionList("35", "1,6,7,8,9,10");
		addSolutionList("36", "1,6,11,16,21,22,23");
		addSolutionList("37", "3,8,13,18,23,24,25");
		addSolutionList("38", "3,8,11,13,17,18,23");
		addSolutionList("39", "1,6,11,16,18,21,22");
		addSolutionList("40", "1,2,3,4,5,10");
		addSolutionList("41", "1,2,3,4,5,6,11");
		addSolutionList("42", "1,5,7,10,13,15,19,20,25");
		addSolutionList("43", "1,2,3,4,5,6,11,16");
		addSolutionList("44", "6,7,8,9,10,11,16,21");
		addSolutionList("45", "1,2,3,4,9,14,19,24");
	}

	private void addSolutionList(String targetNumber, String solutionSequence)
	{
		String[] solutionSequenceArr = solutionSequence.split(",");

		List<Integer> solutionList = new ArrayList<>();

		for(String solutionSequenceNumber : solutionSequenceArr) {
			solutionList.add(Integer.parseInt(solutionSequenceNumber));
		}

		puzzleSolutionMap.put(targetNumber, solutionList);
	}

	private void populatePuzzleTileMap()
	{
		puzzleTileMap = new HashMap<>();
		puzzleTileMap.put(45345, new ArrayList<>()); //I
		puzzleTileMap.put(45346, new ArrayList<>()); //Wings
		puzzleTileMap.put(45347, new ArrayList<>()); //Triangle
		puzzleTileMap.put(45348, new ArrayList<>()); //Diamond
		puzzleTileMap.put(45349, new ArrayList<>()); //Hand
		puzzleTileMap.put(45350, new ArrayList<>()); //Bird
		puzzleTileMap.put(45351, new ArrayList<>()); //Cane
		puzzleTileMap.put(45352, new ArrayList<>()); //W
		puzzleTileMap.put(45353, new ArrayList<>()); //Boot
	}

	private void populateMatchingTileMap() {
		matchingTileMap = new HashMap<>();
		matchingTileMap.put(45366, new ArrayList<>()); //Wings
		matchingTileMap.put(45368, new ArrayList<>()); //Diamond
		matchingTileMap.put(45370, new ArrayList<>()); //Pray
		matchingTileMap.put(45372, new ArrayList<>()); //W
		matchingTileMap.put(45373, new ArrayList<>()); //Boot
	}

	private void populateNumberPuzzleTiles(String targetNumber)
	{
		numberPuzzleTiles = new ArrayList<>();
		List<Integer> puzzleSolutionList = puzzleSolutionMap.get(targetNumber);

		for(int tileLocation : puzzleSolutionList) {
			int worldX = getWorldX(tileLocation);
			int worldY = getWorldY(tileLocation);

			for(Map.Entry<Integer, List<Tile>> entry : puzzleTileMap.entrySet()) {
				for(Tile tile : entry.getValue()) {
					if(worldX == tile.getWorldLocation().getX() && worldY == tile.getWorldLocation().getY()) {
						numberPuzzleTiles.add(tile);
					}
				}
			}
		}

	}

	private int getWorldX(int tileLocation)
	{
		switch(tileLocation) {
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
				return puzzleXMin + 1;
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
				return puzzleXMin + 2;
			case 16:
			case 17:
			case 18:
			case 19:
			case 20:
				return puzzleXMin + 3;
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
				return puzzleXMin + 4;
			default:
				return puzzleXMin;
		}
	}

	private int getWorldY(int tileLocation)
	{
		switch(tileLocation) {
			case 2:
			case 7:
			case 12:
			case 17:
			case 22:
				return puzzleYMax - 1;
			case 3:
			case 8:
			case 13:
			case 18:
			case 23:
				return puzzleYMax - 2;
			case 4:
			case 9:
			case 14:
			case 19:
			case 24:
				return puzzleYMax - 3;
			case 5:
			case 10:
			case 15:
			case 20:
			case 25:
				return puzzleYMax - 4;
			default:
				return puzzleYMax;
		}
	}

	private void populateMatchingTiles() {
		matchingTiles = new ArrayList<>();
		for(Map.Entry<Integer, List<MatchingTile>> entry : matchingTileMap.entrySet()) {
			matchingTiles.addAll(entry.getValue());
		}
	}

	private String getMatchingTileName(int groundObjectId)
	{
		switch(groundObjectId) {
			case 45366:
				return "Wings";
			case 45368:
				return "Diamond";
			case 45370:
				return "Pray";
			case 45372:
				return "W";
			case 45373:
				return "Boot";
			default:
				return "Unknown";
		}
	}

	private void populatePuzzleBounds() {
		for(Map.Entry<Integer, List<Tile>> entry : puzzleTileMap.entrySet()) {
			for(Tile tile : entry.getValue()) {
				int tileWorldX = tile.getWorldLocation().getX();
				int tileWorldY = tile.getWorldLocation().getY();
				if(tileWorldX < puzzleXMin) {
					puzzleXMin = tileWorldX;
				}
				if(tileWorldY < puzzleYMin) {
					puzzleYMin = tileWorldY;
				}
				if(tileWorldX > puzzleXMax) {
					puzzleXMax = tileWorldX;
				}
				if(tileWorldY > puzzleYMax) {
					puzzleYMax = tileWorldY;
				}
			}
		}
	}
}
