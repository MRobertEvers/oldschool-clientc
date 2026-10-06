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
package com.duckblade.osrs.fortis.features.loslinks;

import com.duckblade.osrs.fortis.FortisColosseumConfig;
import com.duckblade.osrs.fortis.features.loslinks.model.ManticoreOrbOrder;
import com.duckblade.osrs.fortis.features.loslinks.model.ManticoreOrbType;
import com.duckblade.osrs.fortis.features.loslinks.model.NpcSpawn;
import com.duckblade.osrs.fortis.features.loslinks.model.WaveSpawnRecord;
import com.duckblade.osrs.fortis.module.PluginLifecycleComponent;
import com.duckblade.osrs.fortis.util.ColosseumState;
import com.duckblade.osrs.fortis.util.ColosseumStateChanged;
import com.duckblade.osrs.fortis.util.ColosseumStateTracker;
import com.duckblade.osrs.fortis.util.Modifier;
import com.duckblade.osrs.fortis.util.spawns.Enemy;
import com.duckblade.osrs.fortis.util.spawns.WaveSpawns;
import com.google.common.collect.ImmutableMap;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ActorSpotAnim;
import net.runelite.api.Client;
import net.runelite.api.GameObject;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.Point;
import net.runelite.api.Tile;
import net.runelite.api.WorldView;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.NpcID;
import net.runelite.api.gameval.ObjectID;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.eventbus.Subscribe;

@Slf4j
@Singleton
@RequiredArgsConstructor(onConstructor_ = @Inject)
public class LosLinks implements PluginLifecycleComponent
{

	private static final Comparator<Point> POINT_COMPARATOR =
		Comparator.comparingInt(Point::getX)
			.thenComparingInt(Point::getY);

	private static final Map<Integer, Enemy> NPC_ID_TO_ENEMY_TYPE = ImmutableMap.<Integer, Enemy>builder()
		.put(NpcID.COLOSSEUM_STANDARD_MAGER, Enemy.SERPENT_SHAMAN) // Serpent shaman
		.put(NpcID.COLOSSEUM_JAVELIN_COLOSSUS, Enemy.JAVELIN_COLOSSUS) // Javelin Colossus
		.put(NpcID.COLOSSEUM_JAGUAR_WARRIOR, Enemy.JAGUAR_WARRIOR) // Jaguar warrior
		.put(NpcID.COLOSSEUM_MANTICORE, Enemy.MANTICORE) // Manticore
		.put(NpcID.COLOSSEUM_MINOTAUR, Enemy.MINOTAUR) // Minotaur
		.put(NpcID.COLOSSEUM_MINOTAUR_ROUTEFIND, Enemy.MINOTAUR) // Minotaur (Red Flag)
		.put(NpcID.COLOSSEUM_SHOCKWAVE_COLOSSUS, Enemy.SHOCKWAVE_COLOSSUS) // Shockwave Colossus
		.build();

	private final Client client;
	private final EventBus eventBus;
	private final ColosseumStateTracker stateTracker;

	private final Set<Integer> trackedNpcs = new HashSet<>();
	private final Set<Integer> reinforcementNpcs = new HashSet<>();
	private final Map<Integer, ManticoreOrbOrder> manticoreOrbData = new HashMap<>();

	@Getter
	private WaveSpawnRecord waveStartRecord;

	@Getter
	private WaveSpawnRecord reinforcementRecord;

	private int sceneOffsetX;
	private int sceneOffsetY;

	@Override
	public boolean isEnabled(FortisColosseumConfig config, ColosseumState colosseumState)
	{
		return config.losLinksEnabled() && colosseumState.isInColosseum();
	}

	@Override
	public void startUp()
	{
		eventBus.register(this);

		resetWaveState();
		determineSceneBase();
	}

	@Override
	public void shutDown()
	{
		eventBus.unregister(this);
		resetWaveState();
	}

	@Subscribe
	public void onColosseumStateChanged(ColosseumStateChanged event)
	{
		// Reset state when entering or leaving colosseum
		if (event.getNewState().getWaveNumber() != event.getPreviousState().getWaveNumber())
		{
			resetWaveState();
		}

		if (event.getNewState().isWaveStarted() && !event.getPreviousState().isWaveStarted() &&
			event.getNewState().getWaveNumber() != 12)
		{
			waveStartRecord = constructWaveRecord();
			eventBus.post(waveStartRecord);
		}
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged event)
	{
		if (event.getGameState() == GameState.LOGGED_IN)
		{
			determineSceneBase();
		}
	}

	@Subscribe
	public void onGameTick(GameTick event)
	{
		// Check all manticores for graphic changes every tick
		// This is more reliable than GraphicChanged events which may not fire when NPCs are behind pillars
		checkAllManticores();

		// if we've seen all the reinforcement spawns this tick, capture that record
		if (client.getTickCount() != stateTracker.getWaveStartTick() &&
			reinforcementRecord == null &&
			!reinforcementNpcs.isEmpty())
		{
			WorldView wv = client.getLocalPlayer().getWorldView();
			boolean seenAllReinforcements = WaveSpawns.forWave(client, stateTracker.getCurrentState(), false)
				.getReinforcements()
				.stream()
				.allMatch(expectedSpawn ->
					reinforcementNpcs.stream()
						.map(wv.npcs()::byIndex)
						.filter(Objects::nonNull)
						.map(NPC::getId)
						.map(NPC_ID_TO_ENEMY_TYPE::get)
						.anyMatch(actualSpawn -> expectedSpawn.getEnemy() == actualSpawn));

			if (seenAllReinforcements)
			{
				reinforcementRecord = constructWaveRecord();
				eventBus.post(reinforcementRecord);
			}
		}
	}

	@Subscribe
	public void onNpcSpawned(NpcSpawned event)
	{
		NPC npc = event.getNpc();
		Enemy enemyType = NPC_ID_TO_ENEMY_TYPE.get(npc.getId());
		if (enemyType == null)
		{
			return;
		}

		trackedNpcs.add(npc.getIndex());
		if (client.getTickCount() != stateTracker.getWaveStartTick())
		{
			reinforcementNpcs.add(npc.getIndex());
		}
		if (enemyType == Enemy.MANTICORE)
		{
			manticoreOrbData.put(npc.getIndex(), new ManticoreOrbOrder());
		}
	}

	@Subscribe
	public void onNpcDespawned(NpcDespawned event)
	{
		int index = event.getNpc().getIndex();
		trackedNpcs.remove(index);
		reinforcementNpcs.remove(index);
		manticoreOrbData.remove(index);
	}

	private void checkAllManticores()
	{
		manticoreOrbData.forEach((index, orbData) ->
		{
			NPC npc = client.getLocalPlayer()
				.getWorldView()
				.npcs()
				.byIndex(index);
			if (npc == null)
			{
				return;
			}

			for (ActorSpotAnim spotAnim : npc.getSpotAnims())
			{
				if (spotAnim != null)
				{
					orbData.saveOrb(ManticoreOrbType.forSpotAnim(spotAnim.getId()));
				}
			}
		});
	}

	private Point convertToLoSCoordinates(int sceneX, int sceneY)
	{
		return new Point(
			sceneX - sceneOffsetX,
			33 - (sceneY - sceneOffsetY) // inverted y coordinate, max y = 33
		);
	}

	private Point convertToLoSCoordinates(LocalPoint localPoint)
	{
		return convertToLoSCoordinates(localPoint.getSceneX(), localPoint.getSceneY());
	}

	private void resetWaveState()
	{
		waveStartRecord = null;
		reinforcementRecord = null;

		trackedNpcs.clear();
		reinforcementNpcs.clear();
		manticoreOrbData.clear();
	}

	private void determineSceneBase()
	{
		Optional<Point> swPillar = Arrays.stream(client.getLocalPlayer()
				.getWorldView()
				.getScene()
				.getTiles()[client.getLocalPlayer().getWorldView().getPlane()])
			.flatMap(Arrays::stream) // all tiles in scene
			.map(Tile::getGameObjects) // all gameobjects in scene
			.filter(Objects::nonNull)
			.flatMap(Arrays::stream)
			.filter(Objects::nonNull)
			.filter(obj -> obj.getId() == ObjectID.PILLAR_CIVITAS01_COLOSSEUM01) // pillars
			.map(GameObject::getSceneMinLocation) // scene min of each
			.min(POINT_COMPARATOR); // min point

		if (swPillar.isPresent())
		{
			sceneOffsetX = swPillar.get().getX() - 8;
			sceneOffsetY = swPillar.get().getY() - 8;
			log.debug("los scene base {} {}", sceneOffsetX, sceneOffsetY);
		}
	}

	WaveSpawnRecord constructWaveRecord()
	{
		assert client.isClientThread();

		WorldView wv = client.getLocalPlayer().getWorldView();
		List<NpcSpawn> spawns = trackedNpcs.stream()
			.map(index -> wv.npcs().byIndex(index))
			.filter(Objects::nonNull)
			.map(this::constructNpcSpawn)
			.collect(Collectors.toList());

		boolean waveSpawn = client.getTickCount() == stateTracker.getWaveStartTick();
		boolean mm3 = stateTracker.getCurrentState().getModifiers().contains(Modifier.MANTIMAYHEM) &&
			Modifier.MANTIMAYHEM.getLevel(client) == 3;

		return new WaveSpawnRecord(
			stateTracker.getCurrentState().getWaveNumber(),
			convertToLoSCoordinates(LocalPoint.fromWorld(client.getLocalPlayer().getWorldView(), client.getLocalPlayer().getWorldLocation())),
			spawns,
			waveSpawn,
			mm3
		);
	}

	private NpcSpawn constructNpcSpawn(NPC npc)
	{
		ManticoreOrbOrder orbData = manticoreOrbData.get(npc.getIndex());
		return new NpcSpawn(
			npc.getIndex(),
			convertToLoSCoordinates(LocalPoint.fromWorld(npc.getWorldView(), npc.getWorldLocation())),
			NPC_ID_TO_ENEMY_TYPE.get(npc.getId()),
			reinforcementNpcs.contains(npc.getIndex()),
			orbData,
			orbData != null && orbData.getThird() != null
		);
	}

}
