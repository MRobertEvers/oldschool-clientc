package com.tobutilities.maiden;

import com.tobutilities.TobUtilitiesConfig;
import com.tobutilities.TobUtilitiesPlugin;
import com.tobutilities.common.RoomHandler;
import static com.tobutilities.maiden.MaidenConstants.OVERLAY_DISPLAY_TICKS;
import static com.tobutilities.maiden.MaidenConstants.MAIDEN_BOSS_IMAGE;
import static com.tobutilities.maiden.MaidenConstants.NYLOCAS_MATOMENOS;
import static com.tobutilities.maiden.MaidenConstants.THE_MAIDEN_OF_SUGADINTI;
import java.awt.Color;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.Point;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.client.chat.ChatColorType;
import net.runelite.client.chat.ChatMessageBuilder;
import net.runelite.client.chat.ChatMessageManager;
import net.runelite.client.chat.QueuedMessage;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.game.ItemManager;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class MaidenHandler extends RoomHandler
{
	@Getter
	private final List<NPC> nylocasMatomenosSpawns = new ArrayList<>();
	private ScuffWarningInfoBox scuffWarningInfoBox;
	@Inject
	ItemManager itemManager;
	@Getter
	private int maidenIndex;
	@Getter
	private boolean overlayDisplayed = false;
	private int displayTicksRemaining = 0;

	@Inject
	protected MaidenHandler(TobUtilitiesPlugin plugin, TobUtilitiesConfig config, Client client)
	{
		super(plugin, config, client);
	}

	@Inject
	private ChatMessageManager chatMessageManager;

	@Subscribe
	public void onNpcSpawned(NpcSpawned event)
	{
		NPC npc = event.getNpc();
		if (npc == null || StringUtils.isBlank(npc.getName()))
		{
			return;
		}

		if (THE_MAIDEN_OF_SUGADINTI.equals(npc.getName()))
		{
			handleMaidenSpawn(npc);
			return;
		}

		if (shouldHighlightScuffedNylocas(npc))
		{
			nylocasMatomenosSpawns.add(npc);
		}
	}

	private void handleMaidenSpawn(NPC maiden)
	{
		maidenIndex = maiden.getIndex();
		nylocasMatomenosSpawns.clear();

		ScuffWarningDisplay displayType = config.scuffWarningDisplayType();
		switch (displayType)
		{
			case CHAT_MESSAGE:
				handleScuffedSpawnChatMessage();
				break;
			case INFO_BOX:
				handleScuffedSpawnInfoBox();
				break;
			case OVERLAY_PANEL:
				handleScuffedSpawnOverlayPanel();
				break;
			default:
				break;
		}
	}

	private boolean shouldHighlightScuffedNylocas(NPC npc)
	{
		if (!config.enabledScuffedSpawnHighlight() || !NYLOCAS_MATOMENOS.equals(npc.getName()))
		{
			return false;
		}

		int x = npc.getWorldLocation().getRegionX();
		int y = npc.getWorldLocation().getRegionY();
		Point position = new Point(x, y);

		return MaidenConstants.SCUFFED_SPAWNS.contains(position) && !nylocasMatomenosSpawns.contains(npc);
	}

	private void handleScuffedSpawnChatMessage()
	{
		ChatMessageBuilder messageBuilder = new ChatMessageBuilder()
			.append(ChatColorType.HIGHLIGHT);

		if (maidenIndex < 58000)
		{
			messageBuilder.append(Color.GREEN, String.format("There is no reasonable risk of a scuff. Maiden index: %d", maidenIndex));
		}
		else if (maidenIndex < 60000)
		{
			messageBuilder.append(Color.GREEN, String.format("A scuff is very unlikely (possible in small scales). Maiden index: %d", maidenIndex));
		}
		else if (maidenIndex < 62000)
		{
			messageBuilder.append(Color.GREEN, String.format("A scuff is unlikely (possible in small scales). Maiden index: %d", maidenIndex));
		}
		else if (maidenIndex < 63500)
		{
			messageBuilder.append(Color.ORANGE, String.format("A scuff is likely, especially in small scales. Maiden index: %d", maidenIndex));
		}
		else
		{
			messageBuilder.append(Color.RED, String.format("A scuff is very likely. Maiden index: %d", maidenIndex));
		}

		chatMessageManager.queue(QueuedMessage.builder()
			.type(ChatMessageType.CONSOLE)
			.name("ToB Utilities")
			.sender("ToB Utilities")
			.runeLiteFormattedMessage(messageBuilder.build())
			.build());
	}

	private void handleScuffedSpawnInfoBox()
	{
		this.scuffWarningInfoBox = new ScuffWarningInfoBox(itemManager.getImage(MAIDEN_BOSS_IMAGE), plugin, this);
		plugin.infoBoxManager.addInfoBox(scuffWarningInfoBox);
		displayTicksRemaining = OVERLAY_DISPLAY_TICKS;
	}

	private void handleScuffedSpawnOverlayPanel()
	{
		overlayDisplayed = true;
		displayTicksRemaining = OVERLAY_DISPLAY_TICKS;
	}

	public void onGameTick(GameTick tick)
	{
		if (!overlayDisplayed && !plugin.infoBoxManager.getInfoBoxes().contains(scuffWarningInfoBox))
		{
			return;
		}

		if (--displayTicksRemaining > 0)
		{
			return;
		}

		if (overlayDisplayed)
		{
			overlayDisplayed = false;
		}
		else
		{
			plugin.infoBoxManager.removeInfoBox(scuffWarningInfoBox);
		}
	}


	@Subscribe
	public void onNpcDespawned(NpcDespawned event)
	{
		nylocasMatomenosSpawns.remove(event.getNpc());
	}

	public void shutDown()
	{
		nylocasMatomenosSpawns.clear();
	}

}
