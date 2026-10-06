package com.tombsofamascutpuzzlehelper;

import lombok.AllArgsConstructor;
import lombok.Data;
import net.runelite.api.Tile;

@Data
@AllArgsConstructor
public class SimonSaysTile
{
	private long timestamp;
	private Tile tile;
}
