package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.room.CrabPuzzleRoom;
import com.zenyte.game.item.Item;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.Graphics;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.npc.combat.CombatScript;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.container.impl.equipment.Equipment;
import com.zenyte.game.world.entity.player.container.impl.equipment.EquipmentSlot;
import mgi.types.config.items.ItemDefinitions;
import org.jetbrains.annotations.NotNull;

/**
 * @author Kris | 18. nov 2017 : 2:04.29
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class JewelledCrab extends RaidNPC<CrabPuzzleRoom> implements CombatScript {
	public static final int WHITE_CRAB = 7576;
	public static final int RED_CRAB = 7577;
	public static final int GREEN_CRAB = 7578;
	public static final int CYAN_CRAB = 7579;
	private static final SoundEffect attackSound = new SoundEffect(718, 5, 0);
	private long time;

	public JewelledCrab(final CrabPuzzleRoom room, final Location tile) {
		super(room.getRaid(), room, WHITE_CRAB, tile);
		this.setForceAggressive(true);
		this.setAggressionDistance(3);
	}

	public static final boolean isEquippableHammer(final ItemDefinitions weapon) {
		if (weapon == null || weapon.isNoted()) {
			return false;
		}
		final String name = weapon.getName().toLowerCase();
		return name.equals("dragon warhammer") || name.equals("elder maul") || name.equals("abyssal bludgeon") || name.contains("torag's hammers") || name.contains("warhammer");
	}

	@Override
	public boolean addWalkStep(final int nextX, final int nextY, final int lastX, final int lastY, final boolean check) {
		if (isFrozen()) {
			return false;
		}
		return super.addWalkStep(nextX, nextY, lastX, lastY, check);
	}

	@Override
	public float getXpModifier(final Hit hit) {
		return 0;
	}

	public void applyHit(final Hit hit) {
		hit.setDamage(0);
		super.applyHit(hit);
	}

	@Override
	public void processNPC() {
		super.processNPC();
		if (getId() != WHITE_CRAB && Utils.currentTimeMillis() > time) {
			setTransformation(WHITE_CRAB);
		}
	}

	private static final Animation hammerAnimation = new Animation(401);
	private static final SoundEffect smashSound = new SoundEffect(556, 5, 0);
	private static final SoundEffect stunSound = new SoundEffect(2727, 5, 0);
	private static final Graphics stunGraphics = new Graphics(245, 0, 140);

	public void smash(@NotNull final Player player, final Item weapon) {
		if (!isEquippableHammer(weapon == null ? null : weapon.getDefinitions())) {
			if (!player.getInventory().containsItem(2347, 1)) {
				player.sendMessage("You don't have a suitable weapon equipped for hammering the crab.");
				return;
			}
			player.getAppearance().forceAppearance(EquipmentSlot.WEAPON.getSlot(), 2347);
			WorldTasksManager.schedule(() -> player.getAppearance().clearForcedAppearance());
			player.setAnimation(hammerAnimation);
		} else {
			player.getAppearance().forceAppearance(EquipmentSlot.WEAPON.getSlot(), weapon.getId());
			player.setAnimation(new Animation(Equipment.getAttackAnimation(weapon.getId(), player.getCombatDefinitions().getStyle())));
			WorldTasksManager.schedule(() -> player.getAppearance().clearForcedAppearance());
		}
		World.sendSoundEffect(getLocation(), smashSound);
		World.sendSoundEffect(getLocation(), stunSound);
		setTransformation(RED_CRAB);
		time = Utils.currentTimeMillis() + 5000;
		setGraphics(stunGraphics);
		setAnimation(new Animation(1313));
		resetWalkSteps();
		forceFreezeDelay(Utils.random(46, 53));
	}

	@Override
	public boolean isFreezeable() {
		return false;
	}

	@Override
	public boolean isEntityClipped() {
		return true;
	}

	@Override
	public int attack(final Entity target) {
		animate();
		delayHit(0, target, melee(target, 10));
		World.sendSoundEffect(getLocation(), attackSound);
		return 4;
	}

	public void setTime(long time) {
		this.time = time;
	}
}
