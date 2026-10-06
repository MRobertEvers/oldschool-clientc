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
import static hsj.external.theatreofbloodstats.TobConstants.LINE_BREAK;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_PERSONAL_DAMAGE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_ROOM_COMPLETE;
import static hsj.external.theatreofbloodstats.TobConstants.MSG_TOTAL_HEALING;
import static hsj.external.theatreofbloodstats.TobConstants.XARPUS_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.XARPUS_SCREECH;
import static hsj.external.theatreofbloodstats.TobConstants.XARPUS_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import static hsj.external.theatreofbloodstats.TobConstants.formatPercent;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Client;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.OverheadTextChanged;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.NpcID;

public class XarpusTracker extends RoomTracker
{
	private int xarpusAcidTime;
	private int xarpusRecoveryTime;
	private int xarpusPreScreech;
	private int xarpusPreScreechTotal;

	public XarpusTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return XARPUS_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return XARPUS_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.XARPUS;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.XARPUSPET;
	}

	@Override
	protected RoomResult buildResult(List<String> messages)
	{
		int xarpusPostScreech = personalDamage - xarpusPreScreech;
		double percent = formatter.percentOf(personalDamage, totalDamage);
		double preScreechPercent = formatter.percentOf(xarpusPreScreech, xarpusPreScreechTotal);

		int xarpusPostTotal = totalDamage - xarpusPreScreechTotal;
		double postScreechPercent = formatter.percentOf(xarpusPostScreech, xarpusPostTotal);

		String roomTime = "";
		String splits = "";
		String damage = "";
		String healing = MSG_TOTAL_HEALING + " - " + formatDamage(totalHealing);

		if (startTick > 0)
		{
			int roomTicks = client.getTickCount() - startTick;
			roomTime = formatter.formatTime(roomTicks);

			splits = buildSplitSummary(messages, Arrays.asList(
				new Split("Recovery Phase", xarpusRecoveryTime),
				new Split("Screech Time", xarpusAcidTime),
				new Split(MSG_ROOM_COMPLETE, roomTicks)
			));

			formatter.buildSplitMessage(messages, "Recovery Phase", xarpusRecoveryTime, 0);
			formatter.buildSplitMessage(messages, "Screech Time", xarpusAcidTime, xarpusRecoveryTime);
			formatter.buildSplitMessage(messages, MSG_ROOM_COMPLETE, roomTicks, xarpusAcidTime);
		}

		if (xarpusPreScreech > 0)
		{
			damage += "Pre Screech Damage - " + formatDamage(xarpusPreScreech) + " (" + formatPercent(preScreechPercent) + "%)" + LINE_BREAK;
			formatter.buildDamageMessage(messages, "Pre Screech Damage", xarpusPreScreech, xarpusPreScreechTotal);
		}
		if (xarpusPostScreech > 0)
		{
			damage += "Post Screech Damage - " + formatDamage(xarpusPostScreech) + " (" + formatPercent(postScreechPercent) + "%)" + LINE_BREAK;
			formatter.buildDamageMessage(messages, "Post Screech Damage", xarpusPostScreech, xarpusPostTotal);
		}
		if (personalDamage > 0)
		{
			damage += MSG_PERSONAL_DAMAGE + " - " + formatDamage(personalDamage);
			formatter.buildDamageMessage(messages, MSG_PERSONAL_DAMAGE, personalDamage, totalDamage);
		}

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
	protected void onNpcChanged(NpcChanged event)
	{
		int npcId = event.getNpc().getId();
		switch (npcId)
		{
			case NpcID.TOB_XARPUS_FEEDING:
			case NpcID.TOB_XARPUS_FEEDING_STORY:
			case NpcID.TOB_XARPUS_FEEDING_HARD:
				startTick = client.getTickCount();
				bossNpc = event.getNpc();
				break;
			case NpcID.TOB_XARPUS_COMBAT:
			case NpcID.TOB_XARPUS_COMBAT_STORY:
			case NpcID.TOB_XARPUS_COMBAT_HARD:
				xarpusRecoveryTime = client.getTickCount() - startTick;
				break;
			default:
				break;
		}
	}

	@Override
	protected void onOverheadTextChanged(OverheadTextChanged event)
	{
		if (!Boss.XARPUS.getName().equals(event.getActor().getName()) && !XARPUS_SCREECH.equals(event.getOverheadText()))
		{
			return;
		}

		xarpusAcidTime = client.getTickCount() - startTick;
		xarpusPreScreech = personalDamage;
		xarpusPreScreechTotal = totalDamage;
	}

	@Override
	protected void resetRoomState()
	{
		xarpusRecoveryTime = 0;
		xarpusAcidTime = 0;
		xarpusPreScreech = 0;
		xarpusPreScreechTotal = 0;
	}
}