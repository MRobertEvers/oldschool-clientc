package com.toainvocations;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;

@ConfigGroup("toainvocations")
public interface ToaInvocationsConfig extends Config
{
	enum DisplayMode
	{
		COUNT_ONLY,
		NAMES
	}

	@ConfigItem(
		keyName = "displayMode",
		name = "Display Mode",
		description = "Show only the count of active invocations, or list their names"
	)
	default DisplayMode displayMode()
	{
		return DisplayMode.NAMES;
	}
}
