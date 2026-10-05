/*
 * Copyright (c) 2025, Will Ediger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package com.colosseumwaves;

import java.awt.image.BufferedImage;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Inject;
import javax.annotation.Nullable;
import javax.inject.Provider;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Provides;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.Point;
import net.runelite.api.WorldView;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.NpcID;
import static net.runelite.api.gameval.VarbitID.COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.util.ImageUtil;

@PluginDescriptor(
	name = "Colosseum Waves",
	description = "Captures player & NPC locations for wave spawns & reinforcements in Fortis Colosseum, and generates Colosseum LoS links for planning and analysis. You can also generate a \"Current LoS\" link to capture the current pillar stack.",
	tags = {"fortis", "colosseum", "colo", "waves", "wave", "spawns", "los"},
	configName = "colosseumwaves"
)
public class ColosseumWavesPlugin extends Plugin
{
	private static final int COLOSSEUM_REGION_ID = 7216;
	private static final int LOS_COORD_OFFSET_X = 32;
	private static final int LOS_COORD_OFFSET_Y = 83;

	private static final Pattern WAVE_START_PATTERN = Pattern.compile("Wave: (\\d+)");
	private static final Pattern WAVE_COMPLETE_PATTERN = Pattern.compile("Wave (\\d+) completed");

	private static final Map<Integer, Integer> COLOSSEUM_WAVE_NPCS = ImmutableMap.<Integer, Integer>builder()
		.put(NpcID.COLOSSEUM_STANDARD_MAGER, 1) // Serpent shaman
		.put(NpcID.COLOSSEUM_JAVELIN_COLOSSUS, 2) // Javelin Colossus
		.put(NpcID.COLOSSEUM_JAGUAR_WARRIOR, 3) // Jaguar warrior
		.put(NpcID.COLOSSEUM_MANTICORE, 4) // Manticore
		.put(NpcID.COLOSSEUM_MINOTAUR, 5) // Minotaur
		.put(NpcID.COLOSSEUM_MINOTAUR_ROUTEFIND, 5) // Minotaur (Red Flag)
		.put(NpcID.COLOSSEUM_SHOCKWAVE_COLOSSUS, 6) // Shockwave Colossus
		.build();

	@Inject
	private Client client;

	@Inject
	private ClientToolbar clientToolbar;

	@Inject
	private ColosseumWavesConfig config;

	@Inject
	private ManticoreHandler manticoreHandler;

	@Inject
	private Provider<ColosseumWavesPanel> panelProvider;

	private ColosseumWavesPanel panel;
	private NavigationButton navButton;

	private boolean inColosseum;
	private int currentWave;
	private int waveStartTick;
	private boolean reinforcementsPhase;
	private boolean npcsCaptured;

	private final List<NpcSpawn> waveSpawns = new ArrayList<>();
	private final List<NpcSpawn> reinforcementSpawns = new ArrayList<>();
	private Point playerLocationAtWaveSpawn;
	private Point playerLocationAtReinforcements;

	// Mantimayhem III tracking
	private boolean mantimayhem3Active = false;

	@Provides
	ColosseumWavesConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(ColosseumWavesConfig.class);
	}

	@Override
	protected void startUp() throws Exception
	{
		resetState();

		panel = panelProvider.get();

		final BufferedImage icon = ImageUtil.loadImageResource(getClass(), "colosseum_icon.png");

		navButton = NavigationButton.builder()
			.tooltip("Colosseum Waves")
			.icon(icon)
			.priority(10)
			.panel(panel)
			.build();

		clientToolbar.addNavigation(navButton);

		// Set up callback for when manticore patterns are completed
		manticoreHandler.setOnPatternCompleteCallback(this::onManticorePatternComplete);
	}

	@Override
	protected void shutDown() throws Exception
	{
		manticoreHandler.clear();

		clientToolbar.removeNavigation(navButton);
		panel = null;
		navButton = null;

		resetState();
	}

	@Subscribe
	public void onChatMessage(ChatMessage event)
	{
		if (event.getType() != ChatMessageType.GAMEMESSAGE)
		{
			return;
		}

		String message = event.getMessage();
		Matcher waveStartMatcher = WAVE_START_PATTERN.matcher(message);
		Matcher waveCompleteMatcher = WAVE_COMPLETE_PATTERN.matcher(message);

		if (waveStartMatcher.find())
		{
			int newWave = Integer.parseInt(waveStartMatcher.group(1));

			if (newWave == 1)
			{
				panel.reset();
			}

			currentWave = newWave;
			waveStartTick = client.getTickCount();

			// Check MM3 status when a new wave starts
			checkMantimayhem3Status();
		}
		else if (waveCompleteMatcher.find())
		{
			clearCurrentWaveState();
		}
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged event)
	{
		if (inColosseum && event.getGameState() == GameState.LOGGED_IN && !isInColosseum())
		{
			resetState();
		}
	}

	@Subscribe
	public void onGameTick(GameTick event)
	{
		if (!inColosseum && isInColosseum())
		{
			resetState();
			inColosseum = true;
		}
		else if (inColosseum && !isInColosseum())
		{
			resetState();
		}

		if (waveStartTick > 0)
		{
			int currentTick = client.getTickCount();
			int ticksSinceWaveStart = currentTick - waveStartTick;
			if (ticksSinceWaveStart > 10 && !reinforcementsPhase)
			{
				reinforcementsPhase = true;
				npcsCaptured = false;
			}
		}

		if (inColosseum && npcsCaptured)
		{
			handleWaveSpawnsAndReinforcements();
			npcsCaptured = false;
		}

		// Check all manticores for graphic changes every tick
		// This is more reliable than GraphicChanged events which may not fire when NPCs are behind pillars
		if (inColosseum && currentWave > 0)
		{
			manticoreHandler.checkAllManticores();
		}
	}

	@Nullable
	private Point getPlayerLocation()
	{
		Player localPlayer = client.getLocalPlayer();
		if (localPlayer == null)
		{
			return null;
		}

		WorldView wv = client.getTopLevelWorldView();
		if (wv == null)
		{
			return null;
		}

		LocalPoint lp = LocalPoint.fromWorld(wv, localPlayer.getWorldLocation());
		if (lp == null)
		{
			return null;
		}

		return convertToLoSCoordinates(new Point(lp.getSceneX(), lp.getSceneY()));
	}

	private boolean isInColosseum()
	{
		WorldView wv = client.getTopLevelWorldView();
		if (wv == null)
		{
			return false;
		}
		for (int region : wv.getMapRegions())
		{
			if (region == COLOSSEUM_REGION_ID)
			{
				return true;
			}
		}
		return false;
	}

	private boolean isManticore(NPC npc)
	{
		return npc.getId() == NpcID.COLOSSEUM_MANTICORE;
	}

	@Subscribe
	public void onNpcSpawned(NpcSpawned event)
	{
		if (!inColosseum)
		{
			return;
		}
		NPC npc = event.getNpc();

		if (isManticore(npc) && !reinforcementsPhase)
		{
			// Only track new manticores during initial spawn, not reinforcements
			manticoreHandler.onNpcSpawned(npc);
		}

		if (COLOSSEUM_WAVE_NPCS.containsKey(npc.getId()))
		{
			// Don't track NPCs if we're not in an active wave
			if (currentWave <= 0)
			{
				return;
			}

			if (!npcsCaptured)
			{
				List<NpcSpawn> spawns = collectActiveColosseumNPCs();
				if (!reinforcementsPhase)
				{
					waveSpawns.clear();
					waveSpawns.addAll(spawns);
					manticoreHandler.captureSpawnStates(false);
				}
				else
				{
					reinforcementSpawns.clear();
					reinforcementSpawns.addAll(spawns);
					manticoreHandler.captureSpawnStates(true);
				}

				npcsCaptured = true;
			}
		}
	}

	// We no longer need the GraphicChanged event handler since we're polling every tick
	// This approach is more reliable for detecting manticore charges behind pillars

	private void onManticorePatternComplete()
	{
		// Update URLs when a manticore pattern becomes complete
		if (!waveSpawns.isEmpty())
		{
			boolean hasManticore = waveSpawns.stream()
				.anyMatch(s -> s.getNpcId() == NpcID.COLOSSEUM_MANTICORE);
			if (hasManticore)
			{
				updateCurrentWaveUrl(false);
			}
		}

		if (!reinforcementSpawns.isEmpty())
		{
			boolean hasManticore = reinforcementSpawns.stream()
				.anyMatch(s -> s.getNpcId() == NpcID.COLOSSEUM_MANTICORE);
			if (hasManticore)
			{
				updateCurrentWaveUrl(true);
			}
		}
	}

	private List<NpcSpawn> collectActiveColosseumNPCs()
	{
		List<NpcSpawn> activeNPCs = new ArrayList<>();

		WorldView wv = client.getTopLevelWorldView();
		if (wv == null)
		{
			return activeNPCs;
		}

		for (NPC npc : wv.npcs())
		{
			if (COLOSSEUM_WAVE_NPCS.containsKey(npc.getId()))
			{
				Point currentPos = getNPCSceneLocation(npc);
				if (currentPos == null)
				{
					continue;
				}
				if (isManticore(npc))
				{
					manticoreHandler.ensureManticoreTracked(npc);
				}
				activeNPCs.add(new NpcSpawn(npc.getId(), currentPos, npc.getIndex()));
			}
		}
		return activeNPCs;
	}

	@Nullable
	private Point getNPCSceneLocation(NPC npc)
	{
		WorldView wv = client.getTopLevelWorldView();
		if (wv == null)
		{
			return null;
		}
		LocalPoint lp = LocalPoint.fromWorld(wv, npc.getWorldLocation());
		if (lp == null)
		{
			return null;
		}
		return new Point(lp.getSceneX(), lp.getSceneY());
	}

	private Point convertToLoSCoordinates(Point sceneLocation)
	{
		int losX = sceneLocation.getX() - LOS_COORD_OFFSET_X;
		int losY = LOS_COORD_OFFSET_Y - sceneLocation.getY();

		return new Point(losX, losY);
	}

	private void handleWaveSpawnsAndReinforcements()
	{
		if (!reinforcementsPhase)
		{
			if (!waveSpawns.isEmpty())
			{
				if (config.includePlayerLocationSpawns())
				{
					playerLocationAtWaveSpawn = getPlayerLocation();
				}
				panel.addWave(currentWave);
				updateCurrentWaveUrl(false);
			}
		}
		else
		{
			if (!reinforcementSpawns.isEmpty())
			{
				if (config.includePlayerLocationReinforcements())
				{
					playerLocationAtReinforcements = getPlayerLocation();
				}
				updateCurrentWaveUrl(true);
			}
		}
	}

	@Nullable
	public String generateCurrentLoSLink()
	{
		if (!inColosseum)
		{
			return null;
		}

		List<NpcSpawn> currentSpawns = collectActiveColosseumNPCs();

		if (currentSpawns.isEmpty())
		{
			return null;
		}

		Point currentPlayerLocation = config.includePlayerLocationCurrent() ? getPlayerLocation() : null;
		// Pass false for isSpawnUrl since this is current LoS, not initial spawn
		return buildLoSUrl(currentSpawns, currentPlayerLocation, false, false);
	}

	private void appendManticoreSuffixIfNeeded(StringBuilder urlBuilder, NpcSpawn spawn, boolean isSpawnUrl, boolean isReinforcement)
	{
		if (spawn.getNpcId() != NpcID.COLOSSEUM_MANTICORE)
		{
			return;
		}

		String suffix;
		if (isSpawnUrl)
		{
			suffix = manticoreHandler.getManticoreSpawnLosSuffix(spawn.getNpcIndex(), isReinforcement);
		}
		else
		{
			suffix = manticoreHandler.getManticoreLosSuffix(spawn.getNpcIndex());
		}
		urlBuilder.append(suffix);
	}

	private String buildLoSUrl(List<NpcSpawn> spawns, @Nullable Point playerLocation, boolean isSpawnUrl, boolean isReinforcement)
	{
		String baseUrl = "https://los.colosim.com/?";
		StringBuilder urlBuilder = new StringBuilder(baseUrl);

		for (NpcSpawn spawn : spawns)
		{
			Point losPos = convertToLoSCoordinates(spawn.getLocation());
			Integer losNpcId = COLOSSEUM_WAVE_NPCS.get(spawn.getNpcId());

			if (losNpcId != null)
			{
				String spawnCode = String.format("%02d%02d%d", losPos.getX(), losPos.getY(), losNpcId);
				urlBuilder.append(spawnCode);

				appendManticoreSuffixIfNeeded(urlBuilder, spawn, isSpawnUrl, isReinforcement);
				urlBuilder.append(".");
			}
		}

		if (playerLocation != null)
		{
			int playerEncoded = playerLocation.getX() + (256 * playerLocation.getY());
			urlBuilder.append("#").append(playerEncoded);
		}

		// Add suffixes
		if (isSpawnUrl && !isReinforcement)
		{
			urlBuilder.append("_ws");
		}
		if (mantimayhem3Active)
		{
			urlBuilder.append("_mm3");
		}

		return urlBuilder.toString();
	}

	private void updateCurrentWaveUrl(boolean isReinforcements)
	{
		if (currentWave <= 0)
		{
			return;
		}

		List<NpcSpawn> spawns;
		Point playerLocation;

		if (!isReinforcements)
		{
			spawns = waveSpawns;
			if (spawns.isEmpty())
			{
				return;
			}
			playerLocation = config.includePlayerLocationSpawns() ? playerLocationAtWaveSpawn : null;

			String url = buildLoSUrl(spawns, playerLocation, true, false);
			panel.setWaveSpawnUrl(currentWave, url);
		}
		else
		{
			spawns = reinforcementSpawns;
			if (spawns.isEmpty())
			{
				return;
			}
			playerLocation = config.includePlayerLocationReinforcements() ? playerLocationAtReinforcements : null;

			String url = buildLoSUrl(spawns, playerLocation, true, true);
			panel.setWaveReinforcementUrl(currentWave, url);
		}
	}

	private void resetState()
	{
		inColosseum = false;
		mantimayhem3Active = false;
		clearCurrentWaveState();
	}

	private void clearCurrentWaveState()
	{
		currentWave = 0;
		waveStartTick = 0;
		reinforcementsPhase = false;
		npcsCaptured = false;

		waveSpawns.clear();
		reinforcementSpawns.clear();
		playerLocationAtWaveSpawn = null;
		playerLocationAtReinforcements = null;

		manticoreHandler.clear();
	}

	private void checkMantimayhem3Status()
	{
		// Simply check if Mantimayhem is level 3 or higher
		int mantimayhemLevel = client.getVarbitValue(COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT);
		boolean mm3Active = mantimayhemLevel >= 3;

		// Only log when status changes
		if (mantimayhem3Active != mm3Active)
		{
			mantimayhem3Active = mm3Active;
			// Also update the handler so it knows the current MM3 status
			manticoreHandler.setMantimayhem3Active(mm3Active);
		}
	}
}