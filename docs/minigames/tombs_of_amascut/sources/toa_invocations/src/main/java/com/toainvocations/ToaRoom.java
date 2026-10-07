package com.toainvocations;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@Getter
@RequiredArgsConstructor
public enum ToaRoom
{
	KEPHRI("Kephri", new int[]{14164}),
	ZEBAK("Zebak", new int[]{15700}),
	AKKHA("Akkha", new int[]{14676}),
	BA_BA("Ba-Ba", new int[]{15188}),
	WARDENS("The Wardens", new int[]{15184, 15696});

	private final String displayName;
	private final int[] regionIds;

	public static ToaRoom fromRegionId(int regionId)
	{
		for (ToaRoom room : values())
		{
			for (int id : room.regionIds)
			{
				if (id == regionId)
				{
					return room;
				}
			}
		}
		return null;
	}
}
