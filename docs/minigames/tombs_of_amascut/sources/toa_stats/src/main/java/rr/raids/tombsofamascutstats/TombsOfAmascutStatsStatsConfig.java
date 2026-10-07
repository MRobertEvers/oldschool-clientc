package rr.raids.tombsofamascutstats;

import net.runelite.client.config.Config;
import net.runelite.client.config.ConfigGroup;
import net.runelite.client.config.ConfigItem;
import net.runelite.client.config.ConfigSection;

@ConfigGroup("tombsofamascutstats")
public interface TombsOfAmascutStatsStatsConfig extends Config
{
	@ConfigSection(
		name = "Chatbox Messages",
		description = "Settings for messages in the chatbox",
		position = 0
	)
	String chatSettings = "chatSettings";

	@ConfigItem(
		keyName = "chatboxDmg",
		name = "Print Damage To Chat",
		description = "Print personal damage and percentage of total to chat",
		section = chatSettings,
		position = 0
	)
	default boolean chatboxDmg()
	{
		return true;
	}

//	@ConfigItem(
//		keyName = "chatboxHealed",
//		name = "Print Heals To Chat",
//		description = "Print amount healed to chat",
//		section = chatSettings,
//		position = 1
//	)
//	default boolean chatboxHealed()
//	{
//		return true;
//	}

	@ConfigItem(
		keyName = "chatboxSplits",
		name = "Print Splits To Chat",
		description = "Print detailed room splits to chat",
		section = chatSettings,
		position = 2
	)
	default boolean chatboxSplits()
	{
		return true;
	}

	@ConfigSection(
		name = "Info Boxes",
		description = "Settings for the infoboxes",
		position = 1
	)
	String infoBoxSettings = "infoBoxSettings";

	@ConfigItem(
		keyName = "showInfoBoxes",
		name = "Info Boxes",
		description = "Show info boxes",
		section = infoBoxSettings,
		position = 1
	)
	default boolean showInfoBoxes()
	{
		return true;
	}

	@ConfigItem(
		keyName = "infoBoxText",
		name = "Info Box Text",
		description = "The text displayed on the info box",
		section = infoBoxSettings,
		position = 1
	)
	default InfoBoxText infoBoxText()
	{
		return InfoBoxText.DAMAGE_PERCENT;
	}

	@ConfigItem(
		keyName = "infoBoxTooltip",
		name = "Info Box Tooltip",
		description = "Display info box tooltip",
		section = infoBoxSettings,
		position = 2
	)
	default boolean infoBoxTooltip()
	{
		return true;
	}

	@ConfigItem(
		keyName = "infoBoxTooltipDmg",
		name = "Info Box Tooltip Damage",
		description = "Display damage info in the info box tooltip",
		section = infoBoxSettings,
		position = 3
	)
	default boolean infoBoxTooltipDmg()
	{
		return true;
	}

//	@ConfigItem(
//		keyName = "infoBoxTooltipHealed",
//		name = "Info Box Tooltip Healed",
//		description = "Display amount healed in the info box tooltip",
//		section = infoBoxSettings,
//		position = 4
//	)
//	default boolean infoBoxTooltipHealed()
//	{
//		return true;
//	}

	@ConfigItem(
		keyName = "infoBoxTooltipSplits",
		name = "Info Box Tooltip Splits",
		description = "Display splits in the info box tooltip",
		section = infoBoxSettings,
		position = 5
	)
	default boolean infoBoxTooltipSplits()
	{
		return true;
	}
}
