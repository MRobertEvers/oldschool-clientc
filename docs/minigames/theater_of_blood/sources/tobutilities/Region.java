package com.tobutilities.common.enums;

public enum Region
{
	MAIDEN(0, 1, 12613),
	BLOAT(5, 2, 13125),
	NYLOCAS(4, 3, 13122),
	SOTETSEG(4, 4, 13123),
	XARPUS(4, 5, 12612),
	VERZIK(7, 6, 12611),
	UNKNOWN(0, 0, -1);
	private final int tickCount;
	private final int waveNumber;
	private final int regionId;

	Region(int tickCount, int waveNumber, int regionId)
	{
		this.tickCount = tickCount;
		this.waveNumber = waveNumber;
		this.regionId = regionId;
	}

	public int getTickCount()
	{
		return this.tickCount;
	}

	public int getWaveNumber()
	{
		return this.waveNumber;
	}

	public int getRegionId()
	{
		return this.regionId;
	}
}
