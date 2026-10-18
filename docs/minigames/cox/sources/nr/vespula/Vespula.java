package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.item.Item;
import com.zenyte.game.task.TickTask;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.util.CollisionUtil;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.Projectile;
import com.zenyte.game.world.World;
import com.zenyte.game.world.WorldThread;
import com.zenyte.game.world.entity.*;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.npc.combat.CombatScript;
import com.zenyte.game.world.entity.npc.combatdefs.NPCCDLoader;
import com.zenyte.game.world.entity.npc.combatdefs.NPCCombatDefinitions;
import com.zenyte.game.world.entity.npc.combatdefs.SpawnDefinitions;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.action.combat.CombatUtilities;
import com.zenyte.game.world.entity.player.calog.CAType;
import com.zenyte.game.world.region.CharacterLoop;

import java.util.List;
import java.util.Optional;

/**
 * @author Kris | 18/08/2019 15:34
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class Vespula extends RaidNPC<VespulaRoom> implements CombatScript {
    private static final int NORMAL = 7530;
    private static final int ENRAGED = 7531;
    private static final int CRAWLING = 7532;
    private static final Animation stingingAnimation = new Animation(7454);
    private static final SoundEffect stingingSound = new SoundEffect(570, 10, 0);
    private static final Animation landOnGround = new Animation(7457);
    private static final Animation riseFromGround = new Animation(7452);

    public Vespula(final Raid raid, final VespulaRoom room, final Location tile) {
        super(raid, room, NORMAL, tile);
        this.forceAggressive = true;
    }

    private LuxGrub stinging;
    private boolean enraging;
    private int stingDelay;
    private boolean skipStinging;
    private int crawlingTicks;
    /**
     * The initial delay upon Vespula spawning, how long it takes before it starts moving towards the grubs.
     */
    private int initialDelay = 2;
    private boolean landedOnGround;

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
        if (room.getPlayers().isEmpty()) {
            return;
        }
        if (initialDelay > 0) {
            initialDelay--;
            return;
        }
        final int id = getId();
        if (id != CRAWLING) {
            if (getHitpoints() <= getMaxHitpoints() * 0.23F) {
                crawlingTicks = 0;
                landedOnGround = true;
                setAnimation(landOnGround);
                setTransformation(CRAWLING);
                updateCombatAnimations();
                combat.setCombatDelay(3);
                return;
            }
            if (room.getAbyssalPortal().isUnderAttack()) {
                if (id == ENRAGED) {
                    for (final Player player : room.getPlayers()) {
                        if (CollisionUtil.collides(getX(), getY(), getSize(), player.getX(), player.getY(), player.getSize())) {
                            player.applyHit(new Hit(Utils.random(1, 8), HitType.DEFAULT));
                        }
                    }
                    if (--stingDelay > 0) {
                        return;
                    }
                    final List<Player> targetsInMeleeDistance = CharacterLoop.find(getLocation(), getSize() + 1, Player.class, target -> CombatUtilities.isWithinMeleeDistance(this, target) && !CollisionUtil.collides(getX(), getY(), getSize(), target.getX(), target.getY(), target.getSize()));
                    stingDelay = 2;
                    Location tile;
                    if (!targetsInMeleeDistance.isEmpty()) {
                        tile = targetsInMeleeDistance.get(Utils.random(targetsInMeleeDistance.size() - 1)).getLocation();
                        for (final Player target : targetsInMeleeDistance) {
                            if (target.getLocation().withinDistance(tile, 1)) {
                                target.applyHit(new Hit(Utils.random(20), HitType.REGULAR));
                                if (Utils.random(3) == 0) {
                                    target.getToxins().applyToxin(Toxins.ToxinType.POISON, 20, this);
                                }
                            }
                        }
                    } else {
                        tile = getMiddleLocation().transform(Utils.random(-200, 200), Utils.random(-200, 200), 0);
                    }
                    setFaceLocation(tile);
                    return;
                }
                combat.removeTarget();
                if (enraging) {
                    if (getLocation().matches(getRespawnTile())) {
                        setTransformation(ENRAGED);
                    }
                } else {
                    enraging = true;
                    resetWalkSteps();
                }
                if (!hasWalkSteps()) {
                    calcFollow(getRespawnTile(), -1, true, true, false);
                }
                return;
            } else {
                if (id == ENRAGED) {
                    enraging = false;
                    setTransformation(NORMAL);
                }
            }
            if (id == NORMAL) {
                if (--this.stingDelay > 0) {
                    return;
                }
                if (this.stinging != null && this.stinging.canBeStung()) {
                    if (!hasWalkSteps()) {
                        final LuxGrub stinging = this.stinging;
                        this.stinging = null;
                        setAnimation(stingingAnimation);
                        World.sendSoundEffect(stinging.getMiddleLocation(), stingingSound);
                        setFaceEntity(stinging);
                        stinging.sting();
                        this.stingDelay = 4;
                        WorldTasksManager.schedule(() -> setFaceEntity(null));
                    }
                    return;
                }
                final Optional<LuxGrub> farthestGrub = getFarthestUnstingedGrub();
                if (farthestGrub.isPresent()) {
                    combat.removeTarget();
                    this.calcFollow(this.stinging = farthestGrub.get(), -1, true, true, false);
                } else {
                    if (removeTarget()) {
                        return;
                    }
                    super.processNPC();
                }
            }
            return;
        }
        if (++crawlingTicks >= 17) {
            setHitpoints(getMaxHitpoints());
            setAnimation(riseFromGround);
            setTransformation(NORMAL);
            combat.setCombatDelay(3);
            updateCombatAnimations();
            return;
        }
        if (removeTarget()) {
            return;
        }
        super.processNPC();
    }

    private boolean removeTarget() {
        if (combat.getTarget() != null) {
            if (getAttackedByDelay() < WorldThread.getCurrentCycle()) {
                if (room.getPlayers().isEmpty()) {
                    combat.removeTarget();
                    calcFollow(getRespawnTile(), -1, true, true, false);
                    return true;
                }
            }
        }
        return false;
    }

    private static final Animation crawlingDeathAnimation = new Animation(7458);
    private static final Animation flyingDeathAnimation = new Animation(7459);

    public void kill() {
        final Player source = getMostDamagePlayerCheckIronman();
        onDeath(source);
        WorldTasksManager.schedule(new TickTask() {
            @Override
            public void run() {
                if (ticks == 0) {
                    final SpawnDefinitions spawnDefinitions = combatDefinitions.getSpawnDefinitions();
                    setAnimation(getId() == CRAWLING ? crawlingDeathAnimation : flyingDeathAnimation);
                    final SoundEffect sound = spawnDefinitions.getDeathSound();
                    if (sound != null && source != null) {
                        source.sendSound(sound);
                    }
                } else if (ticks == deathDelay) {
                    onFinish(source);
                    stop();
                    if (!landedOnGround) {
                        getPlayersWithKillCredit().forEach(p -> p.getCombatAchievements().complete(CAType.REDEMPTION_ENTHUSIAST));
                    }
                    return;
                }
                ticks++;
            }
        }, 0, 1);
    }

    private static final int XERICS_AID = 20984;
    private static final int REVITALISATION = 20960;
    private static final int PRAYER_ENHANCE = 20972;

    @Override
    protected void drop(final Location tile) {
        World.spawnFloorItem(new Item(XERICS_AID, 2), tile, null, -1, Integer.MAX_VALUE);
        World.spawnFloorItem(new Item(REVITALISATION, 1), tile, null, -1, Integer.MAX_VALUE);
        World.spawnFloorItem(new Item(PRAYER_ENHANCE, 1), tile, null, -1, Integer.MAX_VALUE);
    }

    @Override
    public void autoRetaliate(final Entity source) {
    }

    @Override
    public void sendDeath() {
        setHitpoints(1);
    }

    private void updateCombatAnimations() {
        NPCCombatDefinitions.updateBaseDefinitions(combatDefinitions, NPCCDLoader.get(getId()));
    }

    @Override
    public boolean isFreezeable() {
        return false;
    }

    private final Optional<LuxGrub> getFarthestUnstingedGrub() {
        if (skipStinging) {
            return Optional.empty();
        }
        final List<LuxGrub> grubs = room.getGrubs();
        LuxGrub farthestGrub = null;
        final Location location = getMiddleLocation();
        for (final LuxGrub grub : grubs) {
            if (!grub.canBeStung()) {
                continue;
            }
            if (farthestGrub == null || location.getDistance(grub.getMiddleLocation()) > location.getDistance(farthestGrub.getMiddleLocation())) {
                farthestGrub = grub;
            }
        }
        return Optional.ofNullable(farthestGrub);
    }

    @Override
    public double getRangedPrayerMultiplier() {
        return 0.5;
    }

    private static final Projectile flyingAttackProjectile = new Projectile(1364, 60, 24, 20, 15, 20, 160, 5);
    private static final Projectile crawlingAttackProjectile = new Projectile(1364, 30, 24, 20, 15, 20, 160, 5);
    private static final Animation flyingAttackAnimation = new Animation(7455);
    private static final Animation crawlingAttackAnimation = new Animation(7450);

    @Override
    public int attack(final Entity target) {
        if (isWithinMeleeDistance(this, target)) {
            return 0;
        }
        final Projectile projectile = getId() == CRAWLING ? crawlingAttackProjectile : flyingAttackProjectile;
        setAnimation(getId() == CRAWLING ? crawlingAttackAnimation : flyingAttackAnimation);
        delayHit(World.sendProjectile(getMiddleLocation(), target, projectile), target, ranged(target, getMaxHit(15)));
        final Location destTile = RandomLocation.create(target.getLocation(),  2);
        WorldTasksManager.schedule(() -> CharacterLoop.forEach(destTile, 2, Player.class, player -> {
            //Vespula can only hit players which are standing on the dest tile, or on the four main directions of it.
            //Checking if decimal distance is above 1 naturally excludes diagonals as that comes to ~1.4 tiles distance.
            if (player.isDead() || player.isFinished() || player.getLocation().getDistance(destTile) > 1) {
                return;
            }
            delayHit(-1, player, ranged(target, getMaxHit(15)));
            if (Utils.random(3) == 0) {
                target.getToxins().applyToxin(Toxins.ToxinType.POISON, 20, this);
            }
        }), World.sendProjectile(getMiddleLocation(), destTile, projectile));
        return combatDefinitions.getAttackSpeed();
    }

    public boolean isSkipStinging() {
        return skipStinging;
    }

    public void setSkipStinging(boolean skipStinging) {
        this.skipStinging = skipStinging;
    }
}
