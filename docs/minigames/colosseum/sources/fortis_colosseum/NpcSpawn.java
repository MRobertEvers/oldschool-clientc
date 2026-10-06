package com.duckblade.osrs.fortis.features.loslinks.model;

import com.duckblade.osrs.fortis.util.spawns.Enemy;
import lombok.Data;
import net.runelite.api.Point;

@Data
public class NpcSpawn
{
	private final int npcIndex;
	private final Point swTile; // in colosim coords
	private final Enemy enemyType;
	private final boolean reinforcement;
	private final ManticoreOrbOrder orbOrder;
	private final boolean manticoreCharged;

	public String toLosUrlSegment()
	{
		StringBuilder sb = new StringBuilder();
		sb.append(String.format(
			"%02d%02d%d",
			swTile.getX(),
			swTile.getY(),
			enemyType.getColosimLosId(reinforcement)
		));

		if (enemyType == Enemy.MANTICORE)
		{
			if (!manticoreCharged)
			{
				sb.append('u');
			}
			sb.append(orbOrder.toLoSCode());
		}

		sb.append('.');
		return sb.toString();
	}
}
