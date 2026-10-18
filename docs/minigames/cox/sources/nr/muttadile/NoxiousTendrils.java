package com.zenyte.game.content.chambersofxeric.plugins.object;

import com.zenyte.game.content.chambersofxeric.room.MuttadileRoom;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.object.ObjectAction;
import com.zenyte.game.world.object.ObjectId;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.utils.TimeUnit;
import org.jetbrains.annotations.NotNull;

/**
 * @author Kris | 06/07/2019 04:07
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class NoxiousTendrils implements ObjectAction {

    @Override
    public void handleObjectAction(final Player player, final WorldObject object, final String name, final int optionId, final String option) {
        player.getRaid().ifPresent(raid -> raid.ifInRoom(player, MuttadileRoom.class, room -> {
            if (room.isFinished()) {
                player.sendMessage("The tendril is about to collapse!");
                return;
            }
            if (player.getNumericTemporaryAttribute("tendril_cox_delay").longValue() > System.currentTimeMillis()) {
                return;
            }
            player.getTemporaryAttributes().put("tendril_cox_delay", System.currentTimeMillis() + TimeUnit.TICKS.toMillis(15));
            player.lock();
            player.resetWalkSteps();
            player.setRunSilent(true);
            room.setLaunched(true);
            if (!room.getTendrilPlayers().add(player.getUsername())) {
                final Hit hit = new Hit((int) ((player.getHitpoints() * 0.15F) + (player.getHitpoints() * 0.075F)), HitType.REGULAR);
                hit.setExecuteIfLocked();
                player.applyHit(hit);
            }
            final Location destTile = new Location(player.getLocation());
            if (object.getX() > player.getX()) {
                destTile.moveLocation(2, 0, 0);
            } else if (object.getX() < player.getX()) {
                destTile.moveLocation(-2, 0, 0);
            } else if (object.getY() > player.getY()) {
                destTile.moveLocation(0, 2, 0);
            } else {
                destTile.moveLocation(0, -2, 0);
            }
            final WorldObject obj = World.getObjectWithType(destTile, 10);
            if (obj != null && obj.getId() == object.getId()) {
                destTile.setLocation((player.getX() + destTile.getX()) / 2, (player.getY() + destTile.getY()) / 2, player.getPlane());
                final Location secondaryTile = new Location(destTile);
                if (destTile.getX() > player.getX()) {
                    secondaryTile.moveLocation(0, 1, 0);
                } else if (destTile.getX() < player.getX()) {
                    secondaryTile.moveLocation(0, -1, 0);
                } else if (destTile.getY() > player.getY()) {
                    secondaryTile.moveLocation(-1, 0, 0);
                } else {
                    secondaryTile.moveLocation(1, 0, 0);
                }
                player.addWalkSteps(destTile.getX(), destTile.getY(), 1, false);
                WorldTasksManager.schedule(() -> player.addWalkSteps(secondaryTile.getX(), secondaryTile.getY(), 1, false));
                player.getTemporaryAttributes().put("tendril_cox_delay", System.currentTimeMillis() + TimeUnit.TICKS.toMillis(2));
                unlock(player);
                return;
            }
            player.addWalkSteps(destTile.getX(), destTile.getY(), 2, false);
            player.getTemporaryAttributes().put("tendril_cox_delay", System.currentTimeMillis() + TimeUnit.TICKS.toMillis(2));
            unlock(player);
        }));
    }

    private void unlock(@NotNull final Player player) {
        WorldTasksManager.schedule(() -> {
            player.setRunSilent(false);
            player.unlock();
        }, 1);
    }

    @Override
    public Object[] getObjects() {
        return new Object[] { ObjectId.NOXIOUS_TENDRILS };
    }
}
