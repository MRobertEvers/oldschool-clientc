package com.tim.tombshelper;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;

@ConfigGroup(TombsHelperConfig.GROUP)
public interface TombsHelperConfig extends Config
{
	String GROUP = "toahelper";
	@ConfigItem(
		keyName = "Auto hide panel",
		name = "Hide panel when outside of ToA",
		description = "Removes the sidebar panel when not in the Tombs of Amascut",
		position = 1
	)
	default boolean autoHide() {
		return true;
	}
}
