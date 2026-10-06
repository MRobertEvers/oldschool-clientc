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
import static hsj.external.theatreofbloodstats.TobConstants.SOTETSEG_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.SOTE_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Client;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.NpcID;

public class SoteTracker extends RoomTracker
{
	private boolean sote66;
	private int sote66time;
	private boolean sote33;
	private int sote33time;

	public SoteTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return SOTE_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return SOTETSEG_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.SOTETSEG;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.SOTETSEGPET;
	}

	@Override
	protected RoomResult buildResult(List<String> messages)
	{
		double percent = formatter.percentOf(personalDamage, totalDamage);
		String roomTime = "";
		String splits = "";
		String damage = (personalDamage > 0) ? MSG_PERSONAL_DAMAGE + " - " + formatDamage(personalDamage) : "";

		if (startTick > 0)
		{
			int roomTicks = client.getTickCount() - startTick;
			roomTime = formatter.formatTime(roomTicks);

			splits = buildSplitSummary(messages, Arrays.asList(
				new Split("66%", sote66time),
				new Split("33%", sote33time),
				new Split(MSG_ROOM_COMPLETE, roomTicks)
			));

			formatter.buildSplitMessage(messages, "66%", sote66time, 0);
			formatter.buildSplitMessage(messages, "33%", sote33time, sote66time);
			formatter.buildSplitMessage(messages, MSG_ROOM_COMPLETE, roomTicks, sote33time);
		}

		formatter.buildDamageMessage(messages, MSG_PERSONAL_DAMAGE, personalDamage, totalDamage);

		return RoomResult.builder()
			.roomTime(roomTime)
			.percent(percent)
			.damage(damage)
			.splits(splits)
			.healing("")
			.build();
	}

	@Override
	protected void onNpcSpawned(NpcSpawned event)
	{
		switch (event.getNpc().getId())
		{
			case NpcID.TOB_SOTETSEG_COMBAT:
			case NpcID.TOB_SOTETSEG_COMBAT_STORY:
			case NpcID.TOB_SOTETSEG_COMBAT_HARD:
				bossNpc = event.getNpc();
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
			case NpcID.TOB_SOTETSEG_COMBAT:
			case NpcID.TOB_SOTETSEG_COMBAT_STORY:
			case NpcID.TOB_SOTETSEG_COMBAT_HARD:
				bossNpc = event.getNpc();
				if (startTick == -1)
				{
					startTick = client.getTickCount();
				}
				break;
			case NpcID.TOB_SOTETSEG_NONCOMBAT:
			case NpcID.TOB_SOTETSEG_NONCOMBAT_STORY:
			case NpcID.TOB_SOTETSEG_NONCOMBAT_HARD:
				if (startTick != -1)
				{
					if (!sote66)
					{
						sote66time = client.getTickCount() - startTick;
						sote66 = true;
					}
					else if (!sote33)
					{
						sote33time = client.getTickCount() - startTick;
						sote33 = true;
					}
				}
				break;
			default:
				break;
		}
	}

	@Override
	protected void resetRoomState()
	{
		sote66 = false;
		sote66time = 0;
		sote33 = false;
		sote33time = 0;
	}
}