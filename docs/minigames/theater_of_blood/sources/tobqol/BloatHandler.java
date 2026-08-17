/*
 * Copyright (c) 2022, Damen <gh: damencs>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:

 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.

 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package com.tobqol.rooms.bloat;

import com.tobqol.TheatreQOLConfig;
import com.tobqol.TheatreQOLPlugin;
import com.tobqol.api.game.Region;
import com.tobqol.rooms.RoomHandler;
import com.tobqol.rooms.bloat.commons.BloatConstants;
import com.tobqol.rooms.bloat.commons.BloatTable;
import com.tobqol.timetracking.RoomDataHandler;
import com.tobqol.timetracking.RoomDataItem;
import com.tobqol.timetracking.RoomInfoBox;
import com.tobqol.timetracking.RoomInfoUtil;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.Point;
import net.runelite.api.events.*;
import net.runelite.client.chat.ChatColorType;
import net.runelite.client.chat.ChatMessageBuilder;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.util.Text;

import javax.annotation.CheckForNull;
import javax.inject.Inject;
import java.awt.*;

import static com.tobqol.api.game.RaidConstants.THEATRE_OF_BLOOD_ROOM_STATUS;
import static com.tobqol.api.game.Region.BLOAT;
import static com.tobqol.api.game.Region.inRegion;
import static com.tobqol.rooms.bloat.commons.BloatConstants.*;
import static com.tobqol.timetracking.RoomInfoUtil.formatTime;

@Getter
@Slf4j
public class BloatHandler extends RoomHandler
{
	private RoomDataHandler dataHandler;

	@Getter
	@CheckForNull
	private NPC bloatNpc = null;

	private RoomInfoBox bloatInfoBox;

	private int downs = 0;

	@Inject
	protected BloatHandler(TheatreQOLPlugin plugin, TheatreQOLConfig config)
	{
		super(plugin, config);
		setRoomRegion(Region.BLOAT);

		dataHandler = plugin.getDataHandler();
	}

	@Override
	public void load()
	{
	}

	@Override
	public void unload()
	{
		reset();
	}

	@Override
	public void reset()
	{
		bloatNpc = null;

		if (instance.getRaidStatus() <= 1)
		{
			downs = 0;
			infoBoxManager.removeInfoBox(bloatInfoBox);
		}
	}

	@Override
	public boolean active()
	{
		return inRegion(client, BLOAT);
	}

	@Subscribe
	private void onConfigChanged(ConfigChanged e)
	{
		if (!e.getGroup().equals(TheatreQOLConfig.GROUP_NAME))
		{
			return;
		}

		switch (e.getKey())
		{
			case "hideCeilingChains":
				when(config.shouldNullCeilingChains(), this::nullCeilingChains, sceneManager::refreshScene);
				break;
		}
	}

	@Subscribe(priority = -1)
	private void onGameStateChanged(GameStateChanged e)
	{
		if (!active() || e.getGameState() != GameState.LOGGED_IN)
		{
			return;
		}

		when(config.shouldNullCeilingChains(), this::nullCeilingChains, null);
	}

	@Subscribe
	private void onNpcSpawned(NpcSpawned e)
	{
		if (!active() || bloatNpc != null)
		{
			return;
		}

		isNpcFromName(e.getNpc(), BloatConstants.BOSS_NAME, n ->
		{
			instance.lazySetMode(() -> BloatTable.findMode(n.getId()));
			bloatNpc = n;
		});

		when(config.shouldNullCeilingChains(), this::nullCeilingChains, null);
	}

	@Subscribe
	private void onNpcDespawned(NpcDespawned e)
	{
		if (!active())
		{
			return;
		}

		isNpcFromName(e.getNpc(), BloatConstants.BOSS_NAME, $ -> reset());
	}

	@Subscribe
	private void onGameTick(GameTick event)
	{
		if (instance.isInRaid() && instance.getCurrentRegion().isBloat())
		{
			if (!dataHandler.Find("Starting Tick").isPresent() && crossedLine(BLOAT, new Point(39, 30), new Point(39, 33), true, client))
			{
				dataHandler.getData().add(new RoomDataItem("Starting Tick", client.getTickCount(), true));
				dataHandler.setShouldTrack(true);
			}

			if (!dataHandler.getData().isEmpty() && dataHandler.isShouldTrack())
			{
				dataHandler.updateTotalTime();
			}
		}
	}

	@Subscribe
	private void onVarbitChanged(VarbitChanged event)
	{
		if (instance.isInRaid() && instance.getCurrentRegion().isBloat() && !dataHandler.Find("Starting Tick").isPresent())
		{
			if (client.getVarbitValue(THEATRE_OF_BLOOD_ROOM_STATUS) == 1)
			{
				dataHandler.getData().add(new RoomDataItem("Starting Tick", client.getTickCount(), true, true));
				dataHandler.setShouldTrack(true);
			}
		}
	}

	@Subscribe
	private void onAnimationChanged(AnimationChanged e)
	{
		if (!active() || !(e.getActor() instanceof NPC))
		{
			return;
		}

		NPC npc = (NPC) e.getActor();

		if (npc == bloatNpc && npc.getAnimation() == DOWN_ANIM)
		{
			downs++;
			dataHandler.getData().add(new RoomDataItem("Down " + downs, dataHandler.getTime(), downs, true));
		}
	}

	@Subscribe
	public void onChatMessage(ChatMessage event)
	{
		if (!active() || event.getType() != ChatMessageType.GAMEMESSAGE || !dataHandler.Find("Starting Tick").isPresent())
		{
			return;
		}

		String stripped = Text.removeTags(event.getMessage());

		if (BLOAT_WAVE.matcher(stripped).find())
		{
			dataHandler.setShouldTrack(false);
			dataHandler.Find("Room").get().setValue(dataHandler.getTime());

			if (config.displayRoomTimes().isInfobox())
			{
				buildInfobox();
			}

			if (config.displayRoomTimes().isChat())
			{
				sendChatTimes();
			}
		}
	}

	private void nullCeilingChains()
	{
		sceneManager.removeTheseGameObjects(1, BloatTable.CEILING_CHAINS);
	}

	private void buildInfobox()
	{
		if (dataHandler.FindValue("Starting Tick") > 0)
		{
			String roomTime = formatTime(dataHandler.FindValue("Room"));
			StringBuilder tooltip = new StringBuilder();

			if (!dataHandler.Find("Starting Tick").get().isException())
			{
				if (config.trackDowns())
				{
					if (downs > 0)
					{
						dataHandler.getData().forEach(d ->
						{
							if (d.getName().contains("Down"))
							{
								tooltip.append(d.getName() + " - " + formatTime(dataHandler.FindValue("Down " + d.getSort())) +
										(d.getSort() > 1 ? formatTime(dataHandler.FindValue("Down " + d.getSort()), dataHandler.FindValue("Down " + (d.getSort() - 1))) : "") + "</br>");
							}
						});
					}
					else
					{
						tooltip.append("No downs");
					}
				}

				tooltip.append("Complete - " + roomTime);
			}
			else
			{
				tooltip.append("Complete - " + roomTime + "*");
			}


			bloatInfoBox = RoomInfoUtil.createInfoBox(plugin, config, itemManager.getImage(BOSS_IMAGE), "Bloat", roomTime, tooltip.toString());
			plugin.infoBoxManager.addInfoBox(bloatInfoBox);
		}

		// Update total time infobox
		plugin.updateTotalTimeInfoBox();
	}

	private void sendChatTimes()
	{
		if (dataHandler.Find("Starting Tick").isPresent())
		{
			if (!dataHandler.Find("Starting Tick").get().isException())
			{
				if (downs > 0 && config.trackDowns())
				{
					ChatMessageBuilder chatMessageBuilder = new ChatMessageBuilder();

					int downsRemaining = downs - 1;

					for (RoomDataItem d : dataHandler.getData())
					{
						if (d.getName().contains("Down"))
						{
							chatMessageBuilder.append(Color.RED, d.getName())
									.append(ChatColorType.NORMAL)
									.append(" - " + formatTime(d.getValue()) + (d.getSort() > 1 ? formatTime(d.getValue(), dataHandler.FindValue("Down " + (d.getSort() - 1))) : "") + (downsRemaining > 0 ? " - " : ""));

							downsRemaining--;
						}
					}

					enqueueChatMessage(ChatMessageType.GAMEMESSAGE, chatMessageBuilder);
				}

				if (config.roomTimeValidation())
				{
					enqueueChatMessage(ChatMessageType.GAMEMESSAGE, b -> b
							.append(Color.RED, "Bloat - Room Complete")
							.append(ChatColorType.NORMAL)
							.append(" - " + formatTime(dataHandler.FindValue("Room")) + formatTime(dataHandler.FindValue("Room"), dataHandler.FindValue("Down " + downs))));
				}
			}
			else
			{
				if (config.roomTimeValidation())
				{
					enqueueChatMessage(ChatMessageType.GAMEMESSAGE, b -> b
							.append(Color.RED, "Bloat - Room Complete")
							.append(ChatColorType.NORMAL)
							.append(" - " + formatTime(dataHandler.FindValue("Room")) + "*"));
				}
			}
		}
	}
}
