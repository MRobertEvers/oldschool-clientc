package com.zenyte.game.content.chambersofxeric.npc;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.util.CollisionUtil;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.Projectile;
import com.zenyte.game.world.World;
import com.zenyte.game.world.WorldThread;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.npc.CombatScriptsHandler;
import com.zenyte.game.world.entity.npc.NPCCombat;
import com.zenyte.game.world.entity.npc.combat.CombatScript;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.object.WorldObject;

import java.util.List;

/**
 * @author Kris | 18/08/2019 15:17
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class AbyssalPortal extends RaidNPC<VespulaRoom> implements CombatScript {
    private static final Projectile projectile = new Projectile(1366, 10, 10, 0, 0, 60, 0, 0);
    private static final int HEALTH_REGENERATION_RATE = 17;

    public AbyssalPortal(final Raid raid, final VespulaRoom room, final Location tile) {
        super(raid, room, 7533, tile);
        this.combat = new NPCCombat(this) {
            @Override
            public int combatAttack() {
                if (target == null) {
                    return 0;
                }
                if (CollisionUtil.collides(npc.getX(), npc.getY(), npc.getSize(), target.getX(), target.getY(), target.getSize())) {
                    return 0;
                }
                if (outOfRange(target, 6, target.getSize(), false)) {
                    return 0;
                }
                addAttackedByDelay(target);
                return CombatScriptsHandler.specialAttack(npc, target);
            }
        };
    }

    boolean isUnderAttack() {
        return this.getAttackedByDelay() + 9 > WorldThread.getCurrentCycle();
    }

    private static final Animation pillarVanishAnimation = new Animation(7500);
    private WorldObject pillarObject;
    private int ticks;

    @Override
    public void applyHit(final Hit hit) {
        super.applyHit(hit);
        room.spawnTendrils();
    }

    @Override
    public void onDeath(final Entity source) {
        super.onDeath(source);
        if (pillarObject != null) {
            final WorldObject depletedPillar = new WorldObject(pillarObject);
            World.sendObjectAnimation(depletedPillar, pillarVanishAnimation);
            WorldTasksManager.schedule(() -> {
                depletedPillar.setId(30073);
                World.spawnObject(depletedPillar);
            }, 1);
        }
        room.clear();
    }

    @Override
    public void processNPC() {
        super.processNPC();
        if (++ticks % 8 == 0) {
            int count = 0;
            for (final LuxGrub grub : room.getGrubs()) {
                if (grub.isSoldierAvailable()) {
                    count++;
                }
            }
            if (count > 0) {
                heal(HEALTH_REGENERATION_RATE * count);
            }
        }
    }

    @Override
    public List<Entity> getPossibleTargets(final EntityType type) {
        if (!possibleTargets.isEmpty()) {
            possibleTargets.clear();
        }
        loop:
        for (final Player player : raid.getPlayers()) {
            if (player == null || player.isNulled()) {
                continue;
            }
            for (final Location tile : room.getHitTiles()) {
                if (player.matches(tile)) {
                    possibleTargets.add(player);
                    continue loop;
                }
            }
        }
        return possibleTargets;
    }

    @Override
    public int attack(Entity target) {
        final Location destinationTile = new Location(target.getLocation());
        World.sendProjectile(room.getPortalProjectileStartTile(), destinationTile, projectile);
        WorldTasksManager.schedule(() -> room.getPlayers().forEach(p -> {
            if (p.getLocation().matches(destinationTile)) {
                if (p.getPrayerManager().getPrayerPoints() > 0) {
                    p.getPrayerManager().drainPrayerPoints(1 + (int) Math.round((p.getPrayerManager().getPrayerPoints() * 0.03)));
                } else {
                    p.applyHit(new Hit(AbyssalPortal.this, Utils.random(1, 2), HitType.DEFAULT));
                }
            }
        }), 1);
        return 2;
    }

    public WorldObject getPillarObject() {
        return pillarObject;
    }

    public void setPillarObject(WorldObject pillarObject) {
        this.pillarObject = pillarObject;
    }
}
