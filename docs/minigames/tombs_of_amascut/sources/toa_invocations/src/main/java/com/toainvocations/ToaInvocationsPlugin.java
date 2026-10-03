package com.toainvocations;

import com.google.inject.Provides;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import javax.inject.Inject;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.ScriptPostFired;
import net.runelite.api.events.WidgetLoaded;
import net.runelite.api.widgets.Widget;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.overlay.OverlayManager;

@Slf4j
@PluginDescriptor(
	name = "ToA Invocations",
	description = "Shows relevant invocations when entering ToA boss rooms",
	tags = {"toa", "tombs", "amascut", "invocations", "raid"}
)
public class ToaInvocationsPlugin extends Plugin
{
	/** Group ID of the ToA invocations/party setup interface. */
	private static final int WIDGET_INVOCATIONS_GROUP = 774;

	/** Child ID of the scrollable invocations list within the group. */
	private static final int WIDGET_INVOCATIONS_CHILD = 52;

	/** Script fired whenever the ToA party/invocations interface is built or a tab is changed. */
	private static final int SCRIPT_TOA_PARTY_BUILD = 6729;

	/** Script fired when toggling between the invocations and reward-panel tabs. */
	private static final int SCRIPT_TOA_PARTY_TOGGLE = 6732;

	@Inject
	private Client client;

	@Inject
	private OverlayManager overlayManager;

	@Inject
	private ToaInvocationsOverlay overlay;

	/** The boss room the player is currently in, or null if outside a boss room. */
	@Getter
	private ToaRoom currentRoom;

	/**
	 * Active invocations for {@link #currentRoom}.
	 * Empty when not in a boss room or when no invocations have been cached yet.
	 */
	@Getter
	private List<ToaInvocation> activeInvocations = Collections.emptyList();

	/**
	 * All boss-specific invocations that were active the last time the invocations
	 * widget was readable (i.e., when the player had the panel open in the lobby).
	 * Cached across room transitions since invocations are fixed for the raid.
	 */
	private final List<ToaInvocation> cachedActiveInvocations = new ArrayList<>();

	@Override
	protected void startUp()
	{
		overlayManager.add(overlay);
	}

	@Override
	protected void shutDown()
	{
		overlayManager.remove(overlay);
		currentRoom = null;
		activeInvocations = Collections.emptyList();
		cachedActiveInvocations.clear();
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged event)
	{
		GameState state = event.getGameState();
		if (state == GameState.LOGIN_SCREEN || state == GameState.HOPPING)
		{
			currentRoom = null;
			activeInvocations = Collections.emptyList();
			cachedActiveInvocations.clear();
		}
	}

	@Subscribe
	public void onGameTick(GameTick event)
	{
		if (client.getLocalPlayer() == null)
		{
			setCurrentRoom(null);
			return;
		}

		LocalPoint lp = client.getLocalPlayer().getLocalLocation();
		if (lp == null)
		{
			setCurrentRoom(null);
			return;
		}

		int regionId = WorldPoint.fromLocalInstance(client, lp).getRegionID();
		setCurrentRoom(ToaRoom.fromRegionId(regionId));
	}

	@Subscribe
	public void onWidgetLoaded(WidgetLoaded event)
	{
		if (event.getGroupId() == WIDGET_INVOCATIONS_GROUP)
		{
			readInvocationsFromWidget();
		}
	}

	@Subscribe
	public void onScriptPostFired(ScriptPostFired event)
	{
		int id = event.getScriptId();
		if (id == SCRIPT_TOA_PARTY_BUILD || id == SCRIPT_TOA_PARTY_TOGGLE)
		{
			readInvocationsFromWidget();
		}
	}

	/**
	 * Reads the currently selected invocations from widget 774 child 52.
	 * This widget is only populated when the ToA invocations panel is open (lobby area).
	 * Results are stored in {@link #cachedActiveInvocations} and used throughout the raid.
	 */
	private void readInvocationsFromWidget()
	{
		Widget parent = client.getWidget(WIDGET_INVOCATIONS_GROUP, WIDGET_INVOCATIONS_CHILD);
		if (parent == null || parent.isHidden() || parent.getChildren() == null)
		{
			return;
		}

		List<ToaInvocation> found = new ArrayList<>();
		for (ToaInvocation invoc : ToaInvocation.values())
		{
			Widget invocWidget = parent.getChild(invoc.getWidgetChildIndex());
			if (invocWidget == null)
			{
				continue;
			}

			Object[] ops = invocWidget.getOnOpListener();
			if (ops == null || ops.length < 4 || !(ops[3] instanceof Integer))
			{
				continue;
			}

			if ((Integer) ops[3] == 1)
			{
				found.add(invoc);
			}
		}

		cachedActiveInvocations.clear();
		cachedActiveInvocations.addAll(found);
		log.debug("Cached {} active boss-specific invocations", cachedActiveInvocations.size());

		// Refresh the displayed invocations for the current room.
		refreshActiveInvocations();
	}

	private void setCurrentRoom(ToaRoom room)
	{
		if (room == currentRoom)
		{
			return;
		}
		currentRoom = room;
		refreshActiveInvocations();
	}

	private void refreshActiveInvocations()
	{
		if (currentRoom == null)
		{
			activeInvocations = Collections.emptyList();
			return;
		}

		List<ToaInvocation> filtered = new ArrayList<>();
		for (ToaInvocation inv : cachedActiveInvocations)
		{
			if (inv.getRoom() == currentRoom)
			{
				filtered.add(inv);
			}
		}
		activeInvocations = filtered;
	}

	@Provides
	ToaInvocationsConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(ToaInvocationsConfig.class);
	}
}
