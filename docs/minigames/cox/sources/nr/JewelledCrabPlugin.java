package com.zenyte.game.content.chambersofxeric.plugins.npc;

import com.zenyte.game.content.chambersofxeric.npc.JewelledCrab;
import com.zenyte.game.item.Item;
import com.zenyte.game.model.item.ItemOnNPCAction;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.npc.actions.NPCPlugin;
import com.zenyte.game.world.entity.pathfinding.events.player.EntityEvent;
import com.zenyte.game.world.entity.pathfinding.strategy.EntityStrategy;
import com.zenyte.game.world.entity.player.Player;
import it.unimi.dsi.fastutil.ints.IntOpenHashSet;
import mgi.types.config.items.ItemDefinitions;

/**
 * @author Kris | 06/07/2019 17:15
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public class JewelledCrabPlugin extends NPCPlugin implements ItemOnNPCAction {
    /**
     * The item id of the broken torag's hammers.
     */
    private static final int BROKEN_TORAGS_HAMMERS = 4962;
    /**
     * The item id of a hammer.
     */
    private static final int HAMMER = 2347;

    @Override
    public void handle() {
        bind("Smash", new OptionHandler() {
            @Override
            public void handle(final Player player, final NPC npc) {
                if (!(npc instanceof JewelledCrab)) {
                    return;
                }
                ((JewelledCrab) npc).smash(player, player.getWeapon());
            }
            @Override
            public void click(final Player player, final NPC npc, final NPCOption option) {
                if (player.getLocation().withinDistance(npc.getLocation(), player.isRun() ? 2 : 1) && !player.isProjectileClipped(npc, true)) {
                    execute(player, npc);
                    player.setRouteEvent(new EntityEvent(player, new EntityStrategy(npc), null, true));
                    return;
                }
                player.setRouteEvent(new EntityEvent(player, new EntityStrategy(npc), () -> execute(player, npc), true));
            }
        });
    }

    @Override
    public int[] getNPCs() {
        return new int[] {JewelledCrab.WHITE_CRAB, JewelledCrab.RED_CRAB, JewelledCrab.GREEN_CRAB, JewelledCrab.CYAN_CRAB};
    }

    @Override
    public void handleItemOnNPCAction(final Player player, final Item item, final int slot, final NPC npc) {
        if (!(npc instanceof JewelledCrab)) {
            return;
        }
        if (item.getId() == BROKEN_TORAGS_HAMMERS) {
            player.sendMessage("Maybe you should fix that if you want to smash crabs with it.");
            return;
        }
        if (item.getId() != HAMMER) {
            player.sendMessage("Maybe you should equip it if you want to smash crabs with it.");
            return;
        }
        ((JewelledCrab) npc).smash(player, item);
    }

    @Override
    public Object[] getItems() {
        final IntOpenHashSet set = new IntOpenHashSet();
        for (final ItemDefinitions item : ItemDefinitions.getDefinitions()) {
            if (JewelledCrab.isEquippableHammer(item)) {
                set.add(item.getId());
            }
        }
        set.add(HAMMER);
        return set.toArray();
    }

    @Override
    public Object[] getObjects() {
        return new Object[] {JewelledCrab.WHITE_CRAB, JewelledCrab.RED_CRAB, JewelledCrab.GREEN_CRAB, JewelledCrab.CYAN_CRAB};
    }
}
