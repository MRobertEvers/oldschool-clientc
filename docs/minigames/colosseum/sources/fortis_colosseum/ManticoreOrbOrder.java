package com.duckblade.osrs.fortis.features.loslinks.model;

import lombok.Data;

@Data
public class ManticoreOrbOrder
{

	private ManticoreOrbType first = null;
	private ManticoreOrbType second = null;
	private ManticoreOrbType third = null;

	public String toLoSCode()
	{
		if (first == null || second == null || third == null)
		{
			return "";
		}

		if (third == ManticoreOrbType.MELEE)
		{
			return String.valueOf(first.getCode());
		}

		// unless we have all 3, just go with unknown variant
		return "" + first.getCode() + second.getCode() + third.getCode();
	}

	public void saveOrb(ManticoreOrbType orbType)
	{
		if (first == null || first == orbType)
		{
			first = orbType; // nop if dupe, but guards against saving repeat
			return;
		}

		if (second == null || second == orbType)
		{
			second = orbType;
			return;
		}

		if (third == null || third == orbType)
		{
			third = orbType;
		}
	}

}
