package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.Toxins;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.Graphics;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.npc.combat.CombatScript;
import com.zenyte.game.world.entity.npc.combatdefs.NPCCDLoader;
import com.zenyte.game.world.entity.npc.combatdefs.NPCCombatDefinitions;
import com.zenyte.game.world.entity.npc.combatdefs.StatDefinitions;
import com.zenyte.game.world.entity.npc.combatdefs.StatType;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.region.CharacterLoop;

import java.util.List;

/**
 * @author Kris | 11/09/2019 20:59
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class VespineSoldier extends RaidNPC<VespulaRoom> implements CombatScript {
    private static final Animation riseAnimation = new Animation(7460);
    private static final SoundEffect riseSound = new SoundEffect(552, 10, 0);
    public static final int FLYING = 7538;
    private static final int CRAWLING = 7539;
    private static final Animation goingOnGround = new Animation(7457);

    VespineSoldier(final LuxGrub parent, final Raid raid, final VespulaRoom room, final Location postRiseTile) {
        super(raid, room, FLYING, new Location(parent.getLocation()));
        setFaceLocation(postRiseTile.transform(1, 1, 0));
        this.forceAggressive = true;
        this.postRiseTile = postRiseTile;
    }

    private final Location postRiseTile;
    private Entity target;
    private int ticks;
    private int delayUntilCombat;
    private boolean grantPoints;

    @Override
    public void applyHit(final Hit hit) {
        super.applyHit(hit);
        if (getId() == CRAWLING) {
            final HitType type = hit.getHitType();
            if (type == HitType.RANGED || type == HitType.MAGIC) {
                hit.setDamage(0);
            }
        }
    }

    @Override
    public float getXpModifier(final Hit hit) {
        if (getId() == CRAWLING) {
            final HitType type = hit.getHitType();
            if (type == HitType.RANGED || type == HitType.MAGIC) {
                return 0;
            }
        }
        return 1;
    }

    @Override
    public void processNPC() {
        if (isLocked() || isDead()) {
            return;
        }
        if (getId() == FLYING) {
            if (getHitpoints() <= getMaxHitpoints() * 0.2F) {
                setAnimation(goingOnGround);
                setTransformation(CRAWLING);
                updateCombatDefinitions();
                return;
            }
            if (--delayUntilCombat > 0) {
                return;
            }
            super.processNPC();
        } else if (getId() == CRAWLING) {
            if (target == null) {
                final Entity combatTarget = combat.getTarget();
                if (combatTarget != null && combatTarget.getLocation().withinDistance(getMiddleLocation(), 15)) {
                    this.target = combatTarget;
                } else {
                    final List<Entity> possibleTargets = getPossibleTargets(EntityType.PLAYER);
                    if (!possibleTargets.isEmpty()) {
                        target = possibleTargets.get(Utils.random(possibleTargets.size() - 1));
                    }
                }
            }
            if (target != null) {
                calcFollow(target, 2, true, false, false);
            }
            if (++ticks >= 17) {
                explode();
            }
        } else {
            throw new IllegalStateException();
        }
    }

    @Override
    public void updateCombatDefinitions() {
        if (getId() == FLYING) {
            super.updateCombatDefinitions();
            return;
        }
        setCombatDefinitions(combatDefinitionsMap.computeIfAbsent(getId(), a -> NPCCombatDefinitions.clone(getId(), NPCCDLoader.get(getId()))));
        final NPCCombatDefinitions cachedDefs = NPCCombatDefinitions.clone(getId(), NPCCDLoader.get(getId()));
        final double challengeModeMultiplier = raid.isChallengeMode() ? 1.5 : 1.0;
        final StatDefinitions statDefinitions = cachedDefs.getStatDefinitions();
        for (final StatType aggressiveStat : aggressiveStats) {
            statDefinitions.set(aggressiveStat, (int) Math.floor(statDefinitions.get(aggressiveStat) * aggressiveLevelMultiplier * challengeModeMultiplier));
        }
        statDefinitions.set(StatType.DEFENCE, (int) Math.floor(statDefinitions.get(StatType.DEFENCE) * defenceMultiplier * challengeModeMultiplier));
        combatDefinitions.setHitpoints((int) Math.floor(cachedDefs.getHitpoints() * hitpointsMultiplier * challengeModeMultiplier));
    }

    public void rise() {
        if (isLocked()) {
            return;
        }
        lock(3);
        setAnimation(riseAnimation);
        World.sendSoundEffect(getMiddleLocation(), riseSound);
        final Vespula vespula = room.getVespula();
        final AbyssalPortal portal = room.getAbyssalPortal();
        if (!portal.isDead()) {
            vespula.setHitpoints(vespula.getMaxHitpoints());
            portal.setHitpoints(portal.getMaxHitpoints());
        }
        this.delayUntilCombat = 5;
        addWalkSteps(postRiseTile.getX(), postRiseTile.getY(), 3, false);
    }

    private static final Graphics explodeGraphics = new Graphics(1368);
    private static final SoundEffect explodeSound = new SoundEffect(1021, 10, 0);

    private void explode() {
        final Location tile = new Location(getMiddleLocation());
        finish();
        World.sendGraphics(explodeGraphics, tile);
        World.sendSoundEffect(tile, explodeSound);
        CharacterLoop.forEach(tile, 3, Player.class, player -> delayHit(null, 0, player, new Hit(null, Utils.random(15, 30), HitType.REGULAR)));
    }

    @Override
    public void onFinish(final Entity source) {
        super.onFinish(source);
        target = null;
        for (final LuxGrub grub : this.room.getGrubs()) {
            final VespineSoldier relatedSoldier = grub.getSoldier();
            if (relatedSoldier != null && !relatedSoldier.isDead() && !relatedSoldier.isFinished()) {
                return;
            }
        }
        room.getVespula().setSkipStinging(false);
    }

    @Override
    public double getMeleePrayerMultiplier() {
        return 0.5;
    }

    private static final Animation attackAnimation = new Animation(7454);
    private static final SoundEffect attackSound = new SoundEffect(819, 5, 0);

    @Override
    public int attack(final Entity target) {
        setAnimation(attackAnimation);
        World.sendSoundEffect(getMiddleLocation(), attackSound);
        delayHit(0, target, melee(target, raid.isChallengeMode() ? 21 : 16));
        if (Utils.random(2) == 0) {
            target.getToxins().applyToxin(Toxins.ToxinType.POISON, 15, this);
        }
        return combatDefinitions.getAttackSpeed();
    }

    public boolean grantPoints() {
        return grantPoints;
    }

    public void grantPoints(boolean grantPoints) {
        this.grantPoints = grantPoints;
    }
}
