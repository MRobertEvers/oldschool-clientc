package com.duckblade.osrs.fortis.features.loslinks.model;

import lombok.Getter;
import lombok.RequiredArgsConstructor;
import net.runelite.api.gameval.SpotanimID;

@RequiredArgsConstructor
public enum ManticoreOrbType
{
	MAGIC('m'),
	RANGED('r'),
	MELEE('M');

	@Getter
	private final char code;

	public static ManticoreOrbType forSpotAnim(int animId)
	{
		switch (animId)
		{
			case SpotanimID.VFX_MANTICORE_01_PROJECTILE_MAGIC_01:
				return MAGIC;
			case SpotanimID.VFX_MANTICORE_01_PROJECTILE_RANGED_01:
				return RANGED;
			case SpotanimID.VFX_MANTICORE_01_PROJECTILE_MELEE_01:
				return MELEE;

			default:
				return null;
		}
	}

}
