package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.item.Item;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.UpdateFlag;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.container.impl.Inventory;
import com.zenyte.plugins.dialogue.ItemChat;
import org.jetbrains.annotations.NotNull;

import java.util.OptionalInt;

/**
 * @author Kris | 11. mai 2018 : 19:46:38
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class LuxGrub extends RaidNPC<VespulaRoom> {
	public static final int UNSTINGED = 7534;
	public static final int EXPLODING = 7537;
	public static final int STINGED_P1 = 7535;
	public static final int STINGED_P2 = 7536;
	private static final Animation preExplodingAnimation = new Animation(7466);
	private static final Animation explodingAnimation = new Animation(7467);
	private static final Animation feedAnimation = new Animation(827);
	private final Location soldierTile;
	private VespineSoldier soldier;
	private int ticks;

	public LuxGrub(final Raid raid, final VespulaRoom room, final Location tile, final Location soldierTile) {
		super(raid, room, UNSTINGED, tile);
		this.soldierTile = soldierTile;
	}

	@Override
	protected void updateCombatDefinitions() {
		super.updateCombatDefinitions();
		this.combatDefinitions.setHitpoints(30);
		this.setHitpoints(30);
	}

	private static final SoundEffect feedSound = new SoundEffect(2393, 5, 0);

	@Override
	public boolean isCycleHealable() {
		return false;
	}

	/**
	 * Feeds the lux grub for 20% of its health with some medivaemia roots.
	 * @param player the player feeding the lux grub.
	 * @param optionalSlot the slot of the item, if an item was used on the lux grub; empty if it was a left-click action.
	 */
	public void feed(@NotNull final Player player, @NotNull final OptionalInt optionalSlot) {
		final Inventory inventory = player.getInventory();
		final Item item = !optionalSlot.isPresent() ? null : inventory.getItem(optionalSlot.getAsInt());
		if (item != null && item.getId() != VespulaRoom.MEDIVAEMIA_ROOT) {
			player.sendMessage("The creature doesn't seem to want that.");
			return;
		}
		if (getId() == UNSTINGED) {
			player.sendMessage("It doesn't look like it needs feeding.");
			return;
		}
		if (getId() != STINGED_P1 && getId() != STINGED_P2) {
			return;
		}
		final long delaySinceLastFeed = player.getNumericTemporaryAttribute("last lux grub feed").longValue();
		if (delaySinceLastFeed > System.currentTimeMillis()) {
			return;
		}
		if (!inventory.containsItem(VespulaRoom.MEDIVAEMIA_ROOT, 1)) {
			player.getDialogueManager().start(new ItemChat(player, new Item(VespulaRoom.MEDIVAEMIA_ROOT), "You need some medivaemia roots to feed the grubs."));
			return;
		}
		if (item != null) {
			inventory.deleteItem(optionalSlot.getAsInt(), item);
		} else {
			inventory.deleteItem(VespulaRoom.MEDIVAEMIA_ROOT, 1);
		}
		heal((int) (getMaxHitpoints() * 0.2F));
		World.sendSoundEffect(getMiddleLocation(), feedSound);
		player.setAnimation(feedAnimation);
		player.getTemporaryAttributes().put("last lux grub feed", System.currentTimeMillis() + 200);
	}

	public void kill() {
		//Slight delay before removing the grubs.
		WorldTasksManager.schedule(() -> {
			finish();
			//Kill the associated soldier if they're still alive.
			if (soldier != null && !soldier.isDead() && !soldier.isFinished()) {
				soldier.setHitpoints(0);
			}
		});
	}

	@Override
	public void processNPC() {
		if (isLocked() || getId() == EXPLODING) {
			if (++ticks >= 16) {
				setTransformation(UNSTINGED);
				setHitpoints(getMaxHitpoints());
			}
			return;
		}
		final int id = getId();
		if (id != UNSTINGED) {
			if (getHitBars().isEmpty()) {
				getHitBars().add(hitBar);
			}
			updateFlags.flag(UpdateFlag.HIT);
			if (++ticks % 16 == 0) {
				setHitpoints(Math.max(getHitpoints() - 1, 0));
				if (isDead()) {
					ticks = 0;
					return;
				}
			}
		}
		final float health = (float) getHitpoints() / getMaxHitpoints();
		if (health <= 0.5F && id != STINGED_P2) {
			setTransformation(STINGED_P2);
		} else if (health < 1.0F && id != STINGED_P1) {
			setTransformation(STINGED_P1);
		} else if (health == 1.0F && id != UNSTINGED) {
			setTransformation(UNSTINGED);
		}
	}

	@Override
	public void sendDeath() {
		if (isLocked()) {
			return;
		}
		lock(11);
		setAnimation(preExplodingAnimation);
		WorldTasksManager.schedule(() -> {
			final boolean grantPoints = soldier == null;
			soldier = new VespineSoldier(this, raid, room, soldierTile);
			soldier.spawn();
			soldier.rise();
			soldier.grantPoints(grantPoints);
			setTransformation(EXPLODING);
			setAnimation(explodingAnimation);
			for (final LuxGrub grub : this.room.getGrubs()) {
				final VespineSoldier relatedSoldier = grub.soldier;
				if (relatedSoldier == null || relatedSoldier.isDead() || relatedSoldier.isFinished()) {
					return;
				}
			}
			room.getVespula().setSkipStinging(true);
		});
	}

	boolean canBeStung() {
		if (isSoldierAvailable()) {
			return false;
		}
		return getHitpoints() > getMaxHitpoints() / 2;
	}

	boolean isSoldierAvailable() {
		return soldier != null && !soldier.isDead() && !soldier.isFinished();
	}

	void sting() {
		setHitpoints((getMaxHitpoints() / 2));
	}

	public VespineSoldier getSoldier() {
		return soldier;
	}
}
