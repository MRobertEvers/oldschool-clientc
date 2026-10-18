package com.zenyte.game.content.chambersofxeric.plugins.npc;

import com.zenyte.game.content.chambersofxeric.npc.LuxGrub;
import com.zenyte.game.item.Item;
import com.zenyte.game.model.item.ItemOnNPCAction;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.npc.actions.NPCPlugin;
import com.zenyte.game.world.entity.player.Player;

import java.util.OptionalInt;

/**
 * @author Kris | 11/09/2019 20:23
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class LuxGrubNPC extends NPCPlugin implements ItemOnNPCAction {

    @Override
    public void handle() {
        bind("Feed", new OptionHandler() {
            @Override
            public void handle(final Player player, final NPC npc) {
                if (npc instanceof LuxGrub) {
                    ((LuxGrub) npc).feed(player, OptionalInt.empty());
                }
            }

            @Override
            public void execute(final Player player, final NPC npc) {
                player.stopAll();
                player.setFaceEntity(npc);
                handle(player, npc);
            }
        });
    }

    @Override
    public int[] getNPCs() {
        return new int[]{
                LuxGrub.STINGED_P1, LuxGrub.STINGED_P2
        };
    }

    @Override
    public void handleItemOnNPCAction(final Player player, final Item item, final int slot, final NPC npc) {
        if (npc instanceof LuxGrub) {
            ((LuxGrub) npc).feed(player, OptionalInt.of(slot));
        }
    }

    @Override
    public Object[] getItems() {
        return null;
    }

    @Override
    public Object[] getObjects() {
        return new Object[]{
                LuxGrub.UNSTINGED, LuxGrub.EXPLODING, LuxGrub.STINGED_P1, LuxGrub.STINGED_P2
        };
    }
}
