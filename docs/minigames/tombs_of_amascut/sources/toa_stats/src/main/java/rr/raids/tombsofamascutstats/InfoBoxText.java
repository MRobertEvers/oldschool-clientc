package rr.raids.tombsofamascutstats;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@Getter
@RequiredArgsConstructor
public enum InfoBoxText
{
	DAMAGE_PERCENT("Damage Percent"),
	TIME("Room Time"),
	NONE("None");

	private final String type;

	@Override
	public String toString()
	{
		return type;
	}
}
