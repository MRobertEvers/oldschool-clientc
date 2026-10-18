package com.zenyte.game.content.chambersofxeric.plugins.object;

import com.zenyte.game.content.chambersofxeric.room.VespulaRoom;
import com.zenyte.game.item.Item;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.object.ObjectAction;
import com.zenyte.game.world.object.ObjectId;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.plugins.dialogue.ItemChat;

/**
 * @author Kris | 18/08/2019 15:05
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class MedivaemiaRootObject implements ObjectAction {

    private static final Animation animation = new Animation(832);

    private static final SoundEffect pickingMedivaemiaRootSound = new SoundEffect(2581, 5, 0);

    @Override
    public void handleObjectAction(final Player player, final WorldObject object, final String name, final int optionId, final String option) {
        player.getRaid().ifPresent(raid -> raid.ifInRoom(player.getLocation(), VespulaRoom.class, room -> {
            if (option.equalsIgnoreCase("Pick")) {
                if (object.isLocked() || room.isFinished()) {
                    return;
                }
                if (!player.getInventory().hasFreeSlots()) {
                    player.getDialogueManager().start(new ItemChat(player, new Item(20892, 1), "You don\'t have space to hold it."));
                    return;
                }
                player.setAnimation(animation);
                player.sendSound(pickingMedivaemiaRootSound);
                player.getInventory().addOrDrop(new Item(20892, 1));
                final WorldObject emptyRoot = new WorldObject(object);
                emptyRoot.setId(30069);
                World.spawnObject(emptyRoot);
                player.lock(1);
                WorldTasksManager.schedule(() -> {
                    if (room.isFinished()) {
                        return;
                    }
                    World.spawnObject(object);
                }, 8);
            }
        }));
    }

    @Override
    public Object[] getObjects() {
        return new Object[] { ObjectId.MEDIVAEMIA_ROOT };
    }
}
