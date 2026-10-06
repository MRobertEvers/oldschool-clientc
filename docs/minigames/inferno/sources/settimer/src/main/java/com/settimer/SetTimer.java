package com.settimer;

import lombok.AccessLevel;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import net.runelite.client.ui.overlay.infobox.InfoBox;

import java.awt.*;
import java.awt.image.BufferedImage;

import java.util.Timer;
import java.util.TimerTask;

@Getter
@ToString
public class SetTimer extends InfoBox
{
	private static Timer timer;

	@Getter(AccessLevel.PACKAGE)
	@Setter(AccessLevel.PACKAGE)
	private SetTimerState state;

	@Getter(AccessLevel.PACKAGE)
	private int currentTime = 3 * 60 + 30;

	private static final int setTime = 3 * 60 + 30;
	private static final int jadTime = 1 * 60 + 45;

	final SetTimerPlugin plugin;

	public SetTimer(BufferedImage image, SetTimerPlugin plugin)
	{
		super(image, plugin);

		this.plugin = plugin;
		this.state = SetTimerState.IDLE;
	}

	public void start()
	{
		timer = new Timer();
		timer.scheduleAtFixedRate(new TimerTask() {
			public void run() {
				if (currentTime-- == 0)
				{
					currentTime = setTime;
				}
				plugin.update();
			}
		}, 0, 1000);
	}

	public void stop()
	{
		timer.cancel();
		switch (state)
		{
			case STARTED:
				currentTime += jadTime;
				break;
			case IDLE:
			case RESUMED:
				currentTime = setTime;
				break;
			case PAUSED:
			default:
				break;
		}
	}

	@Override
	public String getText()
	{
		final int minutes = currentTime / 60;
		final int seconds = currentTime % 60;
		return String.format("%01d:%02d", minutes, seconds);
	}

	@Override
	public Color getTextColor()
	{
		if (currentTime < 10)
		{
			return Color.RED.brighter();
		}

		return Color.WHITE;
	}
}