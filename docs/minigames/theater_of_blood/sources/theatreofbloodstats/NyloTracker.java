/*
 * Copyright (c) 2026, HSJ (https://github.com/HarrySJ96)
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
package hsj.external.theatreofbloodstats.rooms;

import hsj.external.theatreofbloodstats.Boss;
import hsj.external.theatreofbloodstats.RoomResult;
import hsj.external.theatreofbloodstats.RoomStatsFormatter;
import hsj.external.theatreofbloodstats.RoomTracker;
import hsj.external.theatreofbloodstats.Split;
import hsj.external.theatreofbloodstats.TheatreOfBloodStatsPlugin;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_PERSONAL_DAMAGE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_ROOM_COMPLETE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_TOTAL_HEALING;
import static hsj.external.theatreofbloodstats.TobConstants.NYLOCAS_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.NYLOCAS_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.NYLOCAS_VALID_SPAWNS;
import static hsj.external.theatreofbloodstats.TobConstants.NYLOCAS_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.NYLOCAS_WAVES_TOTAL;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import static hsj.external.theatreofbloodstats.TobConstants.packRegionCoords;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.NpcID;

public class NyloTracker extends RoomTracker
{
	private int currentNylos;
	private boolean nyloWavesFinished;
	private boolean nyloCleanupFinished;
	private boolean waveThisTick = false;
	private int waveTime;
	private int cleanupTime;
	private int bossSpawnTime;
	private int nyloWave = 0;

	public NyloTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return NYLOCAS_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return NYLOCAS_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.NYLOCAS;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.NYLOCASPET;
	}

	@Override
	protected RoomResult buildResult(List<String> messages)
	{
		double percent = formatter.percentOf(personalDamage, totalDamage);
		String roomTime = "";
		String splits = "";
		String healing = MSG_TOTAL_HEALING + " - " + formatDamage(totalHealing);
		String damage = (personalDamage > 0) ? MSG_PERSONAL_DAMAGE + " - " + formatDamage(personalDamage) : "";

		if (startTick > 0)
		{
			int roomTicks = client.getTickCount() - startTick;
			roomTime = formatter.formatTime(roomTicks);

			splits = buildSplitSummary(messages, Arrays.asList(
				new Split("Waves", waveTime),
				new Split("Cleanup", cleanupTime),
				new Split("Boss Spawn", bossSpawnTime),
				new Split(MSG_ROOM_COMPLETE, roomTicks)
			));

			formatter.buildSplitMessage(messages, "Waves", waveTime, 0);
			formatter.buildSplitMessage(messages, "Cleanup", cleanupTime, waveTime);
			formatter.buildSplitMessage(messages, "Boss Spawn", bossSpawnTime, cleanupTime);
			formatter.buildSplitMessage(messages, MSG_ROOM_COMPLETE, roomTicks, bossSpawnTime);
		}

		formatter.buildDamageMessage(messages, MSG_PERSONAL_DAMAGE, personalDamage, totalDamage);
		formatter.buildHealedMessage(messages, MSG_TOTAL_HEALING, totalHealing);

		return RoomResult.builder()
			.roomTime(roomTime)
			.percent(percent)
			.damage(damage)
			.splits(splits)
			.healing(healing)
			.build();
	}

	@Override
	protected void onNpcSpawned(NpcSpawned event)
	{
		NPC npc = event.getNpc();
		int npcId = npc.getId();
		switch (npcId)
		{
			case NpcID.TOB_NYLOCAS_SUPPORT:
			case NpcID.TOB_NYLOCAS_SUPPORT_STORY:
			case NpcID.TOB_NYLOCAS_SUPPORT_HARD:
				startTick = client.getTickCount();
				break;
			case NpcID.NYLOCAS_BOSS_SPAWNING:
			case NpcID.NYLOCAS_BOSS_SPAWNING_STORY:
			case NpcID.NYLOCAS_BOSS_SPAWNING_HARD:
				bossSpawnTime = client.getTickCount() - startTick;
				break;
			case NpcID.NYLOCAS_BOSS_MELEE:
			case NpcID.NYLOCAS_BOSS_MELEE_STORY:
			case NpcID.NYLOCAS_BOSS_MELEE_HARD:
				bossNpc = event.getNpc();
				break;
			default:
				break;
		}

		if (!NYLOCAS_IDS.contains(npcId))
		{
			return;
		}

		currentNylos++;
		WorldPoint worldPoint = WorldPoint.fromLocalInstance(client, npc.getLocalLocation());
		int packedLoc = packRegionCoords(worldPoint.getRegionX(), worldPoint.getRegionY());

		if (!NYLOCAS_VALID_SPAWNS.contains(packedLoc))
		{
			return;
		}

		if (!waveThisTick)
		{
			nyloWave++;
			waveThisTick = true;
		}

		if (nyloWave == NYLOCAS_WAVES_TOTAL && !nyloWavesFinished)
		{
			waveTime = client.getTickCount() - startTick;
			nyloWavesFinished = true;
		}
	}

	@Override
	protected void onNpcDespawned(NpcDespawned event)
	{
		int npcId = event.getNpc().getId();
		if (!NYLOCAS_IDS.contains(npcId))
		{
			return;
		}

		currentNylos--;

		if (nyloWavesFinished && !nyloCleanupFinished && currentNylos == 0)
		{
			cleanupTime = client.getTickCount() - startTick;
			nyloCleanupFinished = true;
		}
	}

	@Override
	protected void onGameTick(GameTick event)
	{
		if (waveThisTick)
		{
			waveThisTick = false;
		}
	}

	@Override
	protected void resetRoomState()
	{
		currentNylos = 0;
		nyloWavesFinished = false;
		nyloCleanupFinished = false;
		waveTime = 0;
		cleanupTime = 0;
		bossSpawnTime = 0;
		waveThisTick = false;
		nyloWave = 0;
	}
}