package com.duckblade.osrs.fortis.util.spawns;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
@Getter
public enum Enemy
{

	FREMENNIK("Fremennik", "Fremmy", -1, -1),
	SERPENT_SHAMAN("Serpent Shaman", "Mage", 1, 7),
	JAGUAR_WARRIOR("Jaguar Warrior", "Melee", 3, -1),
	JAVELIN_COLOSSUS("Javelin Colossus", "Ranger", 2, -1),
	MANTICORE("Manticore", "Lion", 4, -1),
	SHOCKWAVE_COLOSSUS("Shockwave Colossus", "Shocker", 6, -1),
	MINOTAUR("Minotaur", "Minotaur", 5, -1),
	SOL_HEREDIT("Sol Heredit", "Sol Heredit", -1, -1),

	ANGRY_BEES("Angry Bees", "BEES!!", -1, -1),
	;

	private final String npcName;
	private final String colloquialName;
	private final int colosimLosId;
	private final int reinforcementColosimLosId;

	public int getColosimLosId(boolean reinforcement)
	{
		return reinforcement && reinforcementColosimLosId != -1 ? reinforcementColosimLosId : colosimLosId;
	}

}
