package com.duckblade.osrs.fortis.features.loslinks.model;

import java.util.List;
import lombok.Value;
import net.runelite.api.Point;

@Value
public class WaveSpawnRecord
{

	int wave;
	Point playerTile;
	List<NpcSpawn> spawns;
	boolean waveSpawn;
	boolean mm3;

	public String toLoSUrl()
	{
		StringBuilder urlBuilder = new StringBuilder("https://los.colosim.com/?");

		for (NpcSpawn spawn : spawns)
		{
			urlBuilder.append(spawn.toLosUrlSegment());
		}

		int playerEncoded = playerTile.getX() + (256 * playerTile.getY());
		urlBuilder.append("#").append(playerEncoded);

		if (waveSpawn)
		{
			urlBuilder.append("_ws");
		}

		if (mm3)
		{
			urlBuilder.append("_mm3");
		}

		return urlBuilder.toString();
	}

}
