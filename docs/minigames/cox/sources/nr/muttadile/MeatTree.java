package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.room.MuttadileRoom;
import com.zenyte.game.util.Direction;
import com.zenyte.game.world.entity.EntityHitBar;
import com.zenyte.game.world.entity.HitBar;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.masks.UpdateFlag;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.SkillConstants;

/**
 * @author Kris | 13. jaan 2018 : 0:25.42
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class MeatTree extends NPC {
	/**
	 * The npc id of the meat tree.
	 */
	public static final int MEAT_TREE = 7564;

	public MeatTree(final MuttadileRoom room, final Location tile) {
		super(MEAT_TREE, tile, Direction.SOUTH, 5);
		this.room = room;
		int collectiveWoodcuttingLevel = 0;
		for (final Player player : room.getRaid().getPlayers()) {
			collectiveWoodcuttingLevel += player.getSkills().getLevelForXp(SkillConstants.WOODCUTTING);
		}
		this.collectiveWoodcuttingLevel = collectiveWoodcuttingLevel;
	}

	private final int collectiveWoodcuttingLevel;

	@Override
	protected void updateCombatDefinitions() {
		super.updateCombatDefinitions();
		this.combatDefinitions.setHitpoints(Math.max(100, collectiveWoodcuttingLevel * 5));
		this.setHitpoints(this.combatDefinitions.getHitpoints());
	}

	private final HitBar hitbar = new EntityHitBar(this);
	private final MuttadileRoom room;

	@Override
	public void sendDeath() {
		finish();
		room.spawnEmptyTree();
	}

	@Override
	public void processNPC() {
	}

	public void flagBar() {
		getUpdateFlags().flag(UpdateFlag.HIT);
		getHitBars().add(hitbar);
	}

	@Override
	public boolean checkProjectileClip(final Player player, boolean melee) {
		return false;
	}
}
