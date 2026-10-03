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
import static hsj.external.theatreofbloodstats.TobConstants.VERZIK_REGION_IDS;
import static hsj.external.theatreofbloodstats.TobConstants.VERZIK_WAVE;
import static hsj.external.theatreofbloodstats.TobConstants.formatDamage;
import static hsj.external.theatreofbloodstats.TobConstants.formatPercent;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Pattern;
import net.runelite.api.Client;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.ItemID;
import net.runelite.api.gameval.NpcID;

public class VerzikTracker extends RoomTracker
{
	private int verzikP1time;
	private int verzikP2time;
	private int verzikP1personal;
	private int verzikP1total;
	private int verzikP2personal;
	private int verzikP2total;
	private int verzikP2healed;

	public VerzikTracker(Client client, RoomStatsFormatter formatter, TheatreOfBloodStatsPlugin plugin)
	{
		super(client, formatter, plugin);
	}

	@Override
	protected int[] getRegionIds()
	{
		return VERZIK_REGION_IDS;
	}

	@Override
	protected Pattern getCompletionPattern()
	{
		return VERZIK_WAVE;
	}

	@Override
	protected Boss getBoss()
	{
		return Boss.VERZIK;
	}

	@Override
	protected int getImageId()
	{
		return ItemID.VERZIKPET;
	}

	@Override
	protected RoomResult buildResult(List<String> messages)
	{
		int p3personal = personalDamage - (verzikP1personal + verzikP2personal);
		int p3total = totalDamage - (verzikP1total + verzikP2total);
		int p3healed = totalHealing - verzikP2healed;

		double percent = formatter.percentOf(personalDamage, totalDamage);

		String roomTime = "";
		String splits = "";

		String healing = formatter.buildSplitString(
			"P2 Healed - " + formatDamage(verzikP2healed),
			"P3 Healed - " + formatDamage(p3healed),
			"Total Healed - " + formatDamage(totalHealing)
		);

		if (startTick > 0)
		{
			int roomTicks = client.getTickCount() - startTick;
			roomTime = formatter.formatTime(roomTicks);

			splits = buildSplitSummary(messages, Arrays.asList(
				new Split("P1", verzikP1time),
				new Split("P2", verzikP2time),
				new Split("P3", roomTicks)
			));

			formatter.buildSplitMessage(messages, "P1", verzikP1time, 0);
			formatter.buildSplitMessage(messages, "P2", verzikP2time, verzikP1time);
			formatter.buildSplitMessage(messages, "P3", roomTicks, verzikP2time);
		}

		StringBuilder damageBuilder = new StringBuilder();

		appendPhaseDamage(damageBuilder, messages, "P1", verzikP1personal, verzikP1total);
		appendPhaseDamage(damageBuilder, messages, "P2", verzikP2personal, verzikP2total);
		appendPhaseDamage(damageBuilder, messages, "P3", p3personal, p3total);

		if (personalDamage > 0)
		{
			damageBuilder.append("Total Personal Damage - ").append(formatDamage(personalDamage)).append(" (")
				.append(formatPercent(formatter.percentOf(personalDamage, totalDamage))).append("%)");
			formatter.buildDamageMessage(messages, "Total Personal Damage", personalDamage, totalDamage);
		}

		formatter.buildHealedMessage(messages, "P2 Healed", verzikP2healed);
		formatter.buildHealedMessage(messages, "P3 Healed", p3healed);
		formatter.buildHealedMessage(messages, "Total Healed", totalHealing);

		return RoomResult.builder()
			.roomTime(roomTime)
			.percent(percent)
			.damage(damageBuilder.toString())
			.splits(splits)
			.healing(healing)
			.build();
	}

	@Override
	protected void onNpcSpawned(NpcSpawned event)
	{
		switch (event.getNpc().getId())
		{
			case NpcID.VERZIK_PHASE1_TO2_TRANSITION:
			case NpcID.VERZIK_PHASE1_TO2_TRANSITION_STORY:
			case NpcID.VERZIK_PHASE1_TO2_TRANSITION_HARD:
				bossNpc = event.getNpc();
				verzikP1time = client.getTickCount() - startTick;
				verzikP1personal = personalDamage;
				verzikP1total = totalDamage;
				break;
			case NpcID.VERZIK_PHASE2_TO3_TRANSITION:
			case NpcID.VERZIK_PHASE2_TO3_TRANSITION_STORY:
			case NpcID.VERZIK_PHASE2_TO3_TRANSITION_HARD:
				bossNpc = event.getNpc();
				verzikP2time = client.getTickCount() - startTick;
				verzikP2personal = personalDamage - verzikP1personal;
				verzikP2total = totalDamage - verzikP1total;
				verzikP2healed = totalHealing;
				break;
			case NpcID.VERZIK_PHASE3:
			case NpcID.VERZIK_PHASE3_STORY:
			case NpcID.VERZIK_PHASE3_HARD:
				bossNpc = event.getNpc();
				break;
			default:
				break;
		}
	}

	@Override
	protected void onNpcChanged(NpcChanged event)
	{
		switch (event.getNpc().getId())
		{
			case NpcID.VERZIK_PHASE1:
			case NpcID.VERZIK_PHASE1_STORY:
			case NpcID.VERZIK_PHASE1_HARD:
				bossNpc = event.getNpc();
				startTick = client.getTickCount();
				break;
			default:
				break;
		}
	}

	@Override
	protected void resetRoomState()
	{
		verzikP1time = 0;
		verzikP2time = 0;
		verzikP1personal = 0;
		verzikP1total = 0;
		verzikP2personal = 0;
		verzikP2total = 0;
		verzikP2healed = 0;
	}

	private void appendPhaseDamage(StringBuilder sb, List<String> messages, String phase, int personal, int total)
	{
		if (personal > 0)
		{
			double phasePercent = formatter.percentOf(personal, total);
			String label = phase + " Personal Damage";

			sb.append(label).append(" - ").append(formatDamage(personal))
				.append(" (").append(formatPercent(phasePercent)).append("%)").append(LINE_BREAK);

			formatter.buildDamageMessage(messages, label, personal, total);
		}
	}
}