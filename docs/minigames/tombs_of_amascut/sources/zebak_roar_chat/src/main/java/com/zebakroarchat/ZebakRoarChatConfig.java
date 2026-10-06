package com.zebakroarchat;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;

@ConfigGroup("Zebak roar chat")
public interface ZebakRoarChatConfig extends Config
{
	@ConfigItem(
		keyName = "zebakMessage",
		name = "Zebak message",
		description = "Message to display when Zebak roars"
	)
	default String zebakMessage()
	{
		return "Rawr XD";
	}
}
