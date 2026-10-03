package com.settimer;

import com.google.inject.Provides;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.ItemID;
import net.runelite.api.events.GameStateChanged;
import net.runelite.client.config.ConfigManager;
import net.runelite.client.events.ConfigChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.game.ItemManager;
import net.runelite.client.input.KeyManager;
import net.runelite.client.plugins.Plugin;
import net.runelite.client.plugins.PluginDescriptor;
import net.runelite.client.ui.ClientToolbar;
import net.runelite.client.ui.NavigationButton;
import net.runelite.client.ui.overlay.infobox.InfoBoxManager;
import net.runelite.client.util.HotkeyListener;
import net.runelite.client.util.ImageUtil;
import org.apache.commons.lang3.ArrayUtils;

import javax.inject.Inject;
import java.awt.event.KeyEvent;

@Slf4j
@PluginDescriptor(
	name = "Set Timer",
	description = "Panel to show TzKal-Zuk timer",
	tags = {"timer", "inferno", "pvm"}
)
public class SetTimerPlugin extends Plugin
{
	private SetTimer setTimer;
	private SetTimerPanel panel;
	private NavigationButton navButton;

	private static boolean inInferno = false;
	private static final int INFERNO_REGION_ID = 9043;

	@Inject
	private Client client;

	@Inject
	private ClientToolbar clientToolbar;

	@Inject
	private ItemManager itemManager;

	@Inject
	private InfoBoxManager infoBoxManager;

	@Inject
	private KeyManager keyManager;

	@Inject
	private SetTimerConfig config;

	private final HotkeyListener setTimerKeyListener = new HotkeyListener(() -> config.timerHotkey()) {
		@Override
		public void keyPressed(KeyEvent e) {
			if (config.timerHotkey().matches(e)) {
				next();
			}
		}
	};

	@Provides
	SetTimerConfig getConfig(ConfigManager configManager) {
		return configManager.getConfig(SetTimerConfig.class);
	}

	@Override
	protected void startUp()
	{
		setTimer = new SetTimer(itemManager.getImage(ItemID.TZREKZUK), this);

		panel = injector.getInstance(SetTimerPanel.class);
		navButton = NavigationButton.builder()
			.tooltip("Set Timer")
			.icon(ImageUtil.loadImageResource(getClass(), "/tzkal-zuk.png"))
			.priority(6)
			.panel(panel)
			.build();

		if (isInInferno() || !config.hide())
		{
			clientToolbar.addNavigation(navButton);
		}

		keyManager.registerKeyListener(setTimerKeyListener);
	}

	@Override
	protected void shutDown()
	{
		keyManager.unregisterKeyListener(setTimerKeyListener);
		clientToolbar.removeNavigation(navButton);
		infoBoxManager.removeIf(SetTimer.class::isInstance);

		panel.reset();
	}

	@Subscribe
	public void onConfigChanged(ConfigChanged event)
	{
		if (!event.getGroup().equals(SetTimerConfig.GROUP))
		{
			return;
		}

		if (event.getKey().equals("hide"))
		{
			if (!inInferno && config.hide())
			{
				clientToolbar.removeNavigation(navButton);
			}
			else
			{
				clientToolbar.addNavigation(navButton);
			}
		}

		if (event.getKey().equals("infobox"))
		{
			infoBoxManager.removeIf(SetTimer.class::isInstance);

			if (config.infobox() && setTimer.getState() != SetTimerState.IDLE)
			{
				infoBoxManager.addInfoBox(setTimer);
			}
		}
	}

	@Subscribe
	public void onGameStateChanged(GameStateChanged e)
	{
		inInferno = isInInferno();
		if (!inInferno && config.hide())
		{
			clientToolbar.removeNavigation(navButton);
		}
		else
		{
			clientToolbar.addNavigation(navButton);
		}
	}

	public void next()
	{
		switch (setTimer.getState()){
			case IDLE:
				setTimer.start();
				setTimer.setState(SetTimerState.STARTED);
				panel.setButtonText("Pause");

				if (config.infobox())
				{
					infoBoxManager.addInfoBox(setTimer);
				}
				break;
			case STARTED:
				setTimer.stop();
				setTimer.setState(SetTimerState.PAUSED);
				panel.setButtonText("Resume");
				break;
			case PAUSED:
				setTimer.start();
				setTimer.setState(SetTimerState.RESUMED);
				panel.setButtonText("Reset");
				break;
			case RESUMED:
				setTimer.stop();
				setTimer.setState(SetTimerState.IDLE);
				panel.setButtonText("Start");

				infoBoxManager.removeIf(SetTimer.class::isInstance);
				break;
			default:
				break;
		}

		update();
	}

	public void update()
	{
		panel.setTimerText(setTimer.getText());
	}

	private boolean isInInferno()
	{
		return client.getMapRegions() != null && ArrayUtils.contains(client.getMapRegions(), INFERNO_REGION_ID);
	}
}
