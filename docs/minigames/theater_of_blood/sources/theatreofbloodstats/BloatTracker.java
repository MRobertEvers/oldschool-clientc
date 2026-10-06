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
import static hsj.external.theatreofbloodstats.TobConstants.BLOAT_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.BLOAT_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_PERSONAL_DAMAGE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_ROOM_COMPLETE;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.WorldView;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.VarbitChanged;
import net.runelite.api.gameval.AnimationID;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.VarbitID;

public class BloatTracker extends RoomTracker
{
	private final List<Integer> downTimes = new ArrayList<>();

	public BloatTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return BLOAT_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return BLOAT_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.BLOAT;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.BLOATPET;
	}

	@Override
	protected RoomResult buildResult(List<String> messages)
	{
		double percent = formatter.percentOf(personalDamage, totalDamage);
		String damage = (personalDamage > 0) ? MSG_PERSONAL_DAMAGE + " - " + formatDamage(personalDamage) : "";

		String roomTime = "";
		String splits = "";

		if (startTick > 0)
		{
			int roomTicks = client.getTickCount() - startTick;
			roomTime = formatter.formatTime(roomTicks);

			if (!downTimes.isEmpty())
			{
				splits = buildSplitsAndMessages(messages, roomTicks);
			}
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
	protected void onVarbitChanged(VarbitChanged event)
	{
		if (startTick != -1)
		{
			return;
		}

		if (event.getVarbitId() == VarbitID.TOB_CLIENT_WAVEPROGRESS_TYPE && client.getVarbitValue(VarbitID.TOB_CLIENT_WAVEPROGRESS_TYPE) == 1)
		{
			startTick = client.getTickCount();
		}
	}

	@Override
	protected void onGameTick(GameTick event)
	{
		if (bossNpc != null)
		{
			return;
		}

		WorldView worldview = client.getTopLevelWorldView();
		if (worldview != null)
		{
			for (NPC npc : worldview.npcs())
			{
				if (npc != null && Boss.BLOAT.getName().equals(npc.getName()))
				{
					bossNpc = npc;
					break;
				}
			}
		}
	}

	@Override
	protected void onAnimationChanged(AnimationChanged event)
	{
		Actor npc = event.getActor();
		if (startTick == -1 || bossNpc != npc)
		{
			return;
		}

		String npcName = npc.getName();
		if (npcName == null || !npcName.equals(Boss.BLOAT.getName()))
		{
			return;
		}

		if (npc.getAnimation() == AnimationID.TOB_BLOAT_SLEEP)
		{
			downTimes.add(client.getTickCount() - startTick);
		}
	}

	@Override
	protected void resetRoomState()
	{
		downTimes.clear();
	}

	private String buildSplitsAndMessages(List<String> messages, int roomTicks)
	{
		List<Split> splits = new ArrayList<>();

		for (int i = 0; i < downTimes.size(); i++)
		{
			splits.add(new Split("Down " + (i + 1), downTimes.get(i)));
		}

		splits.add(new Split(MSG_ROOM_COMPLETE, roomTicks));

		return buildSplitSummary(messages, splits);
	}
}