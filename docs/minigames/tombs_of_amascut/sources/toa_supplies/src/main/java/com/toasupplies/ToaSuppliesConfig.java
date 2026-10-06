package com.toasupplies;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;

@ConfigGroup("Toa Supplies")
public interface ToaSuppliesConfig extends Config
{
	@ConfigItem(
		keyName = "showSips",
		name = "show sips",
		description = "displays the potion count in amount of sips"
	)
	default boolean showSips()
	{
		return true;
	}
}
