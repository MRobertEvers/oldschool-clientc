package Toacito;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;
import net.runelite.client.config.ConfigSection;

@ConfigGroup("toacito")
public interface ToacitoConfig extends Config {

	@ConfigSection(
			name = "Lobby 1",
			description = "Settings for first Lobby",
			position = 11
	)
	String lobby1Section = "lobby1";

	@ConfigSection(
			name = "Lobby 2",
			description = "Settings for second Lobby (just before warden fight)",
			position = 12
	)
	String lobby2Section = "lobby2";

	//--------------------------
	@ConfigItem(
			position = 1,
			keyName = "life1",
			name = "Remove Life",
			description = "Remove life option at first Supplies Pack",
			section = lobby1Section
	)
	default boolean life1Config() { return true;}
	@ConfigItem(
			position = 2,
			keyName = "chaos1",
			name = "Remove Chaos",
			description = "Remove chaos option at first Supplies Pack",
			section = lobby1Section
	)
	default boolean chaos1Config() { return false;}
	@ConfigItem(
			position = 3,
			keyName = "power1",
			name = "Remove Power",
			description = "Remove power option at first Supplies Pack",
			section = lobby1Section
	)
	default boolean power1Config() { return false;}

	@ConfigItem(
			position = 1,
			keyName = "life2",
			name = "Remove Life",
			description = "Remove life option at second Supplies Pack",
			section = lobby2Section
	)
	default boolean life2Config() { return false;}
	@ConfigItem(
			position = 2,
			keyName = "chaos2",
			name = "Remove Chaos",
			description = "Remove chaos option at second Supplies Pack",
			section = lobby2Section
	)
	default boolean chaos2Config() { return false;}
	@ConfigItem(
			position = 3,
			keyName = "power2",
			name = "Remove Power",
			description = "Remove power option at second Supplies Pack",
			section = lobby2Section
	)
	default boolean power2Config() { return true;}

	//--------------------------


}
