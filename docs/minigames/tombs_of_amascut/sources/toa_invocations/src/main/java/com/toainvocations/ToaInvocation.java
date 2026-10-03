package com.toainvocations;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

/**
 * Boss-specific ToA invocations.
 *
 * Widget child indices are derived from the ordinal position in the full invocation list
 * (which includes general invocations before the boss-specific ones), multiplied by 3.
 * The full ordering matches the in-game invocations panel widget children.
 *
 * To verify/update indices: open the invocations panel in-game, check widget 774 child 52,
 * and note that each invocation occupies 3 widget child slots.
 */
@Getter
@RequiredArgsConstructor
public enum ToaInvocation
{
	// Kephri (Path of Scabaras) — full ordinals 20-24
	LIVELY_LARVAE("Lively Larvae", ToaRoom.KEPHRI, 60),
	MORE_OVERLORDS("More Overlords", ToaRoom.KEPHRI, 63),
	BLOWING_MUD("Blowing Mud", ToaRoom.KEPHRI, 66),
	MEDIC("Medic!", ToaRoom.KEPHRI, 69),
	AERIAL_ASSAULT("Aerial Assault", ToaRoom.KEPHRI, 72),

	// Zebak (Path of Crondis) — full ordinals 25-28
	NOT_JUST_A_HEAD("Not Just a Head", ToaRoom.ZEBAK, 75),
	ARTERIAL_SPRAY("Arterial Spray", ToaRoom.ZEBAK, 78),
	BLOOD_THINNERS("Blood Thinners", ToaRoom.ZEBAK, 81),
	UPSET_STOMACH("Upset Stomach", ToaRoom.ZEBAK, 84),

	// Akkha (Path of Het) — full ordinals 29-32
	DOUBLE_TROUBLE("Double Trouble", ToaRoom.AKKHA, 87),
	KEEP_BACK("Keep Back", ToaRoom.AKKHA, 90),
	STAY_VIGILANT("Stay Vigilant", ToaRoom.AKKHA, 93),
	FEELING_SPECIAL("Feeling Special?", ToaRoom.AKKHA, 96),

	// Ba-Ba (Path of Apmeken) — full ordinals 33-37
	MIND_THE_GAP("Mind the Gap!", ToaRoom.BA_BA, 99),
	GOTTA_HAVE_FAITH("Gotta Have Faith", ToaRoom.BA_BA, 102),
	JUNGLE_JAPES("Jungle Japes", ToaRoom.BA_BA, 105),
	SHAKING_THINGS_UP("Shaking Things Up", ToaRoom.BA_BA, 108),
	BOULDERDASH("Boulderdash", ToaRoom.BA_BA, 111),

	// The Wardens — full ordinals 38-43
	ANCIENT_HASTE("Ancient Haste", ToaRoom.WARDENS, 114),
	ACCELERATION("Acceleration", ToaRoom.WARDENS, 117),
	PENETRATION("Penetration", ToaRoom.WARDENS, 120),
	OVERCLOCKED("Overclocked", ToaRoom.WARDENS, 123),
	OVERCLOCKED_2("Overclocked 2", ToaRoom.WARDENS, 126),
	INSANITY("Insanity", ToaRoom.WARDENS, 129);

	private final String displayName;
	private final ToaRoom room;

	/**
	 * Index of this invocation's container widget within the parent widget's children array.
	 * Active state is read from {@code widget.getOnOpListener()[3] == 1}.
	 */
	private final int widgetChildIndex;
}
