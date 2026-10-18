package com.zenyte.game.content.chambersofxeric.plugins.object;

import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.object.ObjectAction;
import com.zenyte.game.world.object.ObjectId;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.utils.TimeUnit;

/**
 * @author Kris | 12/09/2019 15:51
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class SepticTendrils implements ObjectAction {

    @Override
    public void handleObjectAction(final Player player, final WorldObject object, final String name, final int optionId, final String option) {
        player.getRaid().ifPresent(raid -> {
            raid.ifInRoom(object, VespulaRoom.class, room -> {
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
                if (object.getX() > player.getX()) {
                    player.addWalkSteps(player.getX() + 2, player.getY(), 2, false);
                } else if (object.getX() < player.getX()) {
                    player.addWalkSteps(player.getX() - 2, player.getY(), 2, false);
                } else if (object.getY() > player.getY()) {
                    player.addWalkSteps(player.getX(), player.getY() + 2, 2, false);
                } else {
                    player.addWalkSteps(player.getX(), player.getY() - 2, 2, false);
                }
                player.applyHit(new Hit((int) (player.getMaxHitpoints() * 0.15F), HitType.POISON));
                WorldTasksManager.schedule(() -> {
                    player.setRunSilent(false);
                    player.unlock();
                    player.getTemporaryAttributes().put("tendril_cox_delay", System.currentTimeMillis() + TimeUnit.TICKS.toMillis(2));
                }, 1);
            });
        });
    }

    @Override
    public Object[] getObjects() {
        return new Object[] { ObjectId.SEPTIC_TENDRILS };
    }
}
