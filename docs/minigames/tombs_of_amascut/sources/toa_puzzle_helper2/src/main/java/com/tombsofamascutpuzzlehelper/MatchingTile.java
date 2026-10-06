package com.tombsofamascutpuzzlehelper;

import lombok.AllArgsConstructor;
import lombok.Data;
import net.runelite.api.Tile;

@Data
@AllArgsConstructor
public class MatchingTile
{
	private String tileName;
	private Tile tile;
}
