package com.xarpusexhumed;

import com.google.inject.Provides;
import javax.inject.Inject;
import lombok.Getter;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.ObjectComposition;
import net.runelite.api.TileObject;
import net.runelite.api.events.GroundObjectSpawned;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.gameval.ItemID;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.game.ItemManager;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.overlay.infobox.InfoBoxManager;

@PluginDescriptor(
	name = "Xarpus Exhumed Counter",
	description = "Counts the Exhumed that spawn on the ground during Xarpus phase 1",
	tags = {"tob", "theatre of blood", "xarpus", "exhumed", "counter", "raids"}
)
@SuppressWarnings("unused") // @Provides and @Subscribe methods are called reflectively, not directly
public class XarpusExhumedCounterPlugin extends Plugin
{
	private static final String XARPUS_NAME = "Xarpus";
	private static final String EXHUMED_NAME = "Exhumed";

	@Inject
	private Client client;

	@Inject
	private XarpusExhumedCounterConfig config;

	@Inject
	private InfoBoxManager infoBoxManager;

	@Inject
	private ItemManager itemManager;

	@Getter
	private int exhumedCount = 0;

	private ExhumedCounterBox counterBox;

	@Override
	protected void startUp()
	{
		exhumedCount = 0;
	}

	@Override
	protected void shutDown()
	{
		removeCounterBox();
	}

	@Provides
	XarpusExhumedCounterConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(XarpusExhumedCounterConfig.class);
	}

	@Subscribe
	public void onNpcSpawned(NpcSpawned event)
	{
		NPC npc = event.getNpc();
		if (npc.getName() == null || !npc.getName().equalsIgnoreCase(XARPUS_NAME))
		{
			return;
		}

		exhumedCount = 0;

		if (config.showInfobox())
		{
			addCounterBox();
		}
	}

	@Subscribe
	public void onNpcDespawned(NpcDespawned event)
	{
		NPC npc = event.getNpc();
		if (npc.getName() == null || !npc.getName().equalsIgnoreCase(XARPUS_NAME))
		{
			return;
		}

		// Xarpus despawns when the fight ends or the room is left.
		removeCounterBox();
	}

	@Subscribe
	public void onGroundObjectSpawned(GroundObjectSpawned event)
	{
		checkObject(event.getGroundObject());
	}

	private void checkObject(TileObject object)
	{
		if (object == null)
		{
			return;
		}

		ObjectComposition composition = client.getObjectDefinition(object.getId());
		if (composition == null || composition.getName() == null)
		{
			return;
		}

		if (!composition.getName().equalsIgnoreCase(EXHUMED_NAME))
		{
			return;
		}

		exhumedCount++;

		if (config.showInfobox())
		{
			addCounterBox();
		}
	}

	private void addCounterBox()
	{
		if (counterBox != null)
		{
			return;
		}

		counterBox = new ExhumedCounterBox(itemManager.getImage(ItemID.BONES), this);
		infoBoxManager.addInfoBox(counterBox);
	}

	private void removeCounterBox()
	{
		if (counterBox != null)
		{
			infoBoxManager.removeInfoBox(counterBox);
			counterBox = null;
		}
	}
}
