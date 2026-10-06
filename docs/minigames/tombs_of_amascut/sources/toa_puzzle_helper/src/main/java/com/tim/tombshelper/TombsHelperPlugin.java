package com.tim.tombshelper;

import com.google.inject.Provides;

import javax.inject.Inject;
import java.util.Arrays;
import java.util.List;

import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.events.GameTick;
import net.runelite.client.config.ConfigManager;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.Player;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.util.ImageUtil;


@Slf4j
@PluginDescriptor(
	name = "ToA Puzzle Helper"
)
public class TombsHelperPlugin extends Plugin
{
	@Inject
	private Client client;
	@Inject
	private ClientToolbar clientToolbar;

	@Inject
	private TombsHelperConfig config;

	private TombsHelperPanel panel;
	private NavigationButton navButton;
	private boolean panelEnabled = false;
	private int lastRegionId = -1;

	//all of them so the panel doesn't continuously hide during the raid
	private static final List<Integer> TOA_INSTANCE_IDS = Arrays.asList( 13454, 14164,14676,15188,15444,
																		15700,15955,13906,14162,
																		14674,15186,15698,15954,
																		15953,14160,14672,15184,15696);
	//specific rooms
	private static final List<Integer> TOA_PUZZLE_IDS = Arrays.asList(14674,15186,14162,15698);
	private static final int TOA_PUZZLE_AKKHA = 14674;
	private static final int TOA_PUZZLE_BABA = 15186;
	private static final int TOA_PUZZLE_KEPHRI = 14162;
	private static final int TOA_PUZZLE_ZEBAK = 15698;


	public enum PuzzleRoom {
		NONE("Not in a puzzle room",0),
		AKKHA("Akkha",TOA_PUZZLE_AKKHA),
		BABA("Ba-Ba",TOA_PUZZLE_BABA),
		KEPHRI("Kephri",TOA_PUZZLE_KEPHRI),
		ZEBAK("Zebak",TOA_PUZZLE_ZEBAK);

		private String roomName;
		private int roomId;
		private PuzzleRoom(String roomName, int roomId) {
			this.roomName = roomName;
			this.roomId = roomId;
		}
		public String getRoomName() {
			return this.roomName;
		}
		public int getRoomId() {
			return this.roomId;
		}

	}
	private PuzzleRoom currentRoom = PuzzleRoom.NONE;

	@Override
	protected void startUp() throws Exception
	{
		log.info("TombsHelper started!");
		panel = new TombsHelperPanel(this);
		navButton = NavigationButton.builder()
				.tooltip("ToA Helper")
				.icon(ImageUtil.loadImageResource(getClass(), "/com/tim/tombshelper/toa_nav_icon.png"))
				.priority(50)
				.panel(panel)
				.build();
		togglePanel(!config.autoHide());
		panel.update(currentRoom);
	}

	@Override
	protected void shutDown() throws Exception
	{
		log.info("TombsHelper stopped!");
		clientToolbar.removeNavigation(navButton);
	}

	@Subscribe
	public void onConfigChanged(ConfigChanged event) {
		if (!event.getGroup().equals(TombsHelperConfig.GROUP)) {
			return;
		}
		if (event.getKey().equals("Auto hide panel")) {
			boolean inToA = TOA_INSTANCE_IDS.contains(getRegionId());
			togglePanel(inToA || !config.autoHide());
		}
	}

	@Subscribe
	public void onGameTick(GameTick tick) {
		checkRegion();
	}

	public PuzzleRoom getRoom() {
		return currentRoom;
	}

	private void setRoom(int regionId) {
		switch(regionId) {
			case TOA_PUZZLE_AKKHA:
				currentRoom = PuzzleRoom.AKKHA;
				return;
			case TOA_PUZZLE_BABA:
				currentRoom = PuzzleRoom.BABA;
				return;
			case TOA_PUZZLE_KEPHRI:
				currentRoom = PuzzleRoom.KEPHRI;
				return;
			case TOA_PUZZLE_ZEBAK:
				currentRoom = PuzzleRoom.ZEBAK;
				return;
			default:
				currentRoom = PuzzleRoom.NONE;
				return;
		}
	}
	private void checkRegion() {
		final int regionId = getRegionId();
		if (!TOA_INSTANCE_IDS.contains(regionId)) {
			if (config.autoHide()) {
				togglePanel(false);
			}
			lastRegionId = regionId;
			return;
		}

		if (!TOA_INSTANCE_IDS.contains(lastRegionId)) {
			if (!panelEnabled && config.autoHide()) {
				togglePanel(true);
			}
		}
		if(regionId != lastRegionId) {
			if (TOA_INSTANCE_IDS.contains(regionId)) {
				setRoom(regionId);
				panel.update(currentRoom);
			}
		}
		lastRegionId = regionId;
	}

	private int getRegionId() {
		Player player = client.getLocalPlayer();
		if (player == null) {
			return -1;
		}
		return WorldPoint.fromLocalInstance(client, player.getLocalLocation()).getRegionID();
	}
	private void togglePanel(boolean enable) {
		panelEnabled = enable;
		if (enable) {
			clientToolbar.addNavigation(navButton);
		} else {
			clientToolbar.removeNavigation(navButton);
		}
	}

	@Provides
	TombsHelperConfig provideConfig(ConfigManager configManager)
	{
		return configManager.getConfig(TombsHelperConfig.class);
	}
}
