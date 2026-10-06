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
import static hsj.external.theatreofbloodstats.TobConstants.MAIDEN_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.MAIDEN_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_PERSONAL_DAMAGE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_ROOM_COMPLETE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_TOTAL_HEALING;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Client;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.NpcID;

public class MaidenTracker extends RoomTracker
{
	private int maiden70time = 0;
	private int maiden50time = 0;
	private int maiden30time = 0;

	public MaidenTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return MAIDEN_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return MAIDEN_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.MAIDEN;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.MAIDENPET;
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
				new Split("70%", maiden70time),
				new Split("50%", maiden50time),
				new Split("30%", maiden30time),
				new Split(MSG_ROOM_COMPLETE, roomTicks)
			));

			formatter.buildSplitMessage(messages, "70%", maiden70time, 0);
			formatter.buildSplitMessage(messages, "50%", maiden50time, maiden70time);
			formatter.buildSplitMessage(messages, "30%", maiden30time, maiden50time);
			formatter.buildSplitMessage(messages, MSG_ROOM_COMPLETE, roomTicks, maiden30time);
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
		switch (event.getNpc().getId())
		{
			case NpcID.TOB_MAIDEN_100:
			case NpcID.TOB_MAIDEN_100_STORY:
			case NpcID.TOB_MAIDEN_100_HARD:
				bossNpc = event.getNpc();
				startTick = client.getTickCount();
				break;
			default:
				break;
		}
	}

	@Override
	protected void onNpcChanged(NpcChanged event)
	{
		int npcId = event.getNpc().getId();
		switch (npcId)
		{
			case NpcID.TOB_MAIDEN_70:
			case NpcID.TOB_MAIDEN_70_STORY:
			case NpcID.TOB_MAIDEN_70_HARD:
				if (startTick != -1)
				{
					maiden70time = client.getTickCount() - startTick;
				}
				break;
			case NpcID.TOB_MAIDEN_50:
			case NpcID.TOB_MAIDEN_50_STORY:
			case NpcID.TOB_MAIDEN_50_HARD:
				if (startTick != -1)
				{
					maiden50time = client.getTickCount() - startTick;
				}
				break;
			case NpcID.TOB_MAIDEN_30:
			case NpcID.TOB_MAIDEN_30_STORY:
			case NpcID.TOB_MAIDEN_30_HARD:
				if (startTick != -1)
				{
					maiden30time = client.getTickCount() - startTick;
				}
				break;
			default:
				break;
		}
	}

	@Override
	protected void resetRoomState()
	{
		maiden70time = 0;
		maiden50time = 0;
		maiden30time = 0;
	}
}