package com.zenyte.game.content.chambersofxeric.room;

import com.zenyte.game.content.chambersofxeric.CombatPointCapCalculator;
import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.map.RaidArea;
import com.zenyte.game.content.chambersofxeric.map.RaidRoom;
import com.zenyte.game.content.chambersofxeric.npc.LargeMuttadile;
import com.zenyte.game.content.chambersofxeric.npc.MeatTree;
import com.zenyte.game.content.chambersofxeric.npc.SmallMuttadile;
import com.zenyte.game.content.skills.woodcutting.actions.Woodcutting;
import com.zenyte.game.task.WorldTask;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.util.Direction;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.player.Action;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.SkillConstants;
import com.zenyte.game.world.entity.player.calog.CAType;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.game.world.region.area.plugins.CycleProcessPlugin;
import it.unimi.dsi.fastutil.objects.ObjectOpenHashSet;

import java.util.Optional;
import java.util.Set;

/**
 * @author Kris | 16. nov 2017 : 2:41.01
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class MuttadileRoom extends RaidArea implements CycleProcessPlugin {
    /**
     * The animation that's performed by the large muttadile when walking out of the water.
     */
    private static final Animation walkingOutOfWaterAnimation = new Animation(7423);
    /**
     * The id of the crystal that blocks the exit.
     */
    private static final int BLOCKING_CRYSTAL = 30018;
    /**
     * An array containing the coordinates to the meat trees.
     */
    private static final Location[] treeSpawnLocations = new Location[] {new Location(3269, 5320, 1), new Location(3301, 5320, 1), new Location(3350, 5317, 1)};
    /**
     * A 2D array containing coordinates to the tendrils that block the entrance.
     */
    private static final Location[][] tendrilSpawnLocations = new Location[][] {new Location[] {new Location(3279, 5314, 1), new Location(3280, 5314, 1)}, new Location[] {new Location(3311, 5314, 1), new Location(3312, 5314, 1)}, new Location[] {new Location(3343, 5313, 1), new Location(3344, 5313, 1)}};
    /**
     * An array containing coordinates that the large muttadile cannot reach.
     */
    private static final Location[] ignoredTiles = new Location[] {new Location(3280, 5312, 1), new Location(3311, 5312, 1), new Location(3343, 5312, 1)};
    /**
     * An array containing coordinates to the crystal that blocks the exit.
     */
    private static final Location[] crystalSpawnLocations = new Location[] {new Location(3270, 5328, 1), new Location(3308, 5335, 1), new Location(3352, 5327, 1)};
    /**
     * An array containing coordinates to where the large muttadile initially lies at.
     */
    private static final Location[] largeMuttadileSpawnLocations = new Location[] {new Location(3282, 5332, 1), new Location(3314, 5330, 1), new Location(3336, 5330, 1)};
    /**
     * An array containing coordinates to where the small muttadile spawns.
     */
    private static final Location[] smallMuttadileSpawnLocations = new Location[] {new Location(3276, 5328, 1), new Location(3307, 5325, 1), new Location(3344, 5323, 1)};
    /**
     * A set of players' usernames who have already crossed the tendrils at least once, used to prevent damaging them for the first crossing.
     */
    private final Set<String> tendrilPlayers = new ObjectOpenHashSet<>();
    /**
     * Defines whether a player has walked across the tendrils yet or not.
     */
    private boolean launched;
    /**
     * The large muttadile NPC.
     */
    private LargeMuttadile largeMuttadile;
    /**
     * The small muttadile NPC.
     */
    private SmallMuttadile smallMuttadile;
    /**
     * The meat tree object.
     */
    private WorldObject meatTree;
    /**
     * The meat tree NPC.
     */
    private MeatTree tree;
    /**
     * The crystal object blocking the exit.
     */
    private WorldObject blockingCrystal;
    /**
     * The tile which is ignored by the large muttadiles' magic attacks. The tiles around it in a 3x3 formation are also ignored.
     */
    private Location ignoredTile;
    /**
     * Whether or not the room has been finished.
     */
    private boolean finished;
    /**
     * The approximate location of the tendril, used to see if any player is near it and if so, spawn the tendrils.
     */
    private Location tendril;
    /**
     * Whether or not the tendrils have yet been spawned.
     */
    private boolean spawnedTendril;
    private boolean hasFed;

    public MuttadileRoom(final RaidRoom type, final Raid raid, final int rotation, final int size, final int regionX, final int regionY, final int chunkX, final int chunkY, final int fromPlane, final int toPlane) {
        super(type, raid, rotation, size, regionX, regionY, chunkX, chunkY, fromPlane, toPlane);
    }

    @Override
    public CombatPointCapCalculator buildPointsCap() {
        return new CombatPointCapCalculator().appendExtra(Integer.MAX_VALUE >> 1);
    }

    /**
     * Unleashes the large muttadile by walking it out of the water.
     */
    public void unleashLargeMuttadile() {
        largeMuttadile.setCantInteract(true);
        largeMuttadile.setAnimation(walkingOutOfWaterAnimation);
        final Direction dir = largeMuttadile.getMovementDirection();
        final int x = largeMuttadile.getX();
        final int y = largeMuttadile.getY();
        WorldTasksManager.schedule(new WorldTask() {
            private int ticks;
            @Override
            public void run() {
                if (raid.isDestroyed()) {
                    stop();
                    return;
                }
                switch (ticks++) {
                case 0: 
                    largeMuttadile.addWalkSteps(dir == Direction.EAST ? x + 5 : dir == Direction.WEST ? x - 5 : x, dir == Direction.NORTH ? y + 5 : dir == Direction.SOUTH ? y - 5 : y, 5, false);
                    break;
                case 4: 
                    largeMuttadile.setTransformation(7563);
                    break;
                case 5: 
                    largeMuttadile.setCantInteract(false);
                    largeMuttadile.setUnleashed(true);
                    largeMuttadile.setTicks(5);
                    stop();
                }
            }
        }, 0, 0);
    }

    /**
     * Spawns the empty meat tree object.
     */
    public void spawnEmptyTree() {
        World.spawnObject(meatTree = new WorldObject(30013, 10, 0, new Location(meatTree)));
    }

    /**
     * Destroys the crystal blocking the exit.
     */
    public void clearPath() {
        finished = true;
        Raid.shatterCrystal(blockingCrystal);
        if (!hasFed) {
            getRaid().getPlayers().forEach(p -> p.getCombatAchievements().complete(CAType.MUTTA_DIET));
        }
        WorldTasksManager.schedule(() -> {
            for (final Location tendril : tendrilSpawnLocations[index]) {
                World.removeObject(World.getObjectWithType(getLocation(tendril), 10));
            }
        }, 2);
    }

    @Override
    public void loadRoom() {
        tendril = getLocation(tendrilSpawnLocations[index][0]);
        final Location location = getObjectLocation(treeSpawnLocations[index], 2, 2, getRotation());
        World.spawnObject(meatTree = new WorldObject(30012, 10, 0, location));
        tree = new MeatTree(this, location);
        tree.spawn();
        World.spawnObject(blockingCrystal = new WorldObject(BLOCKING_CRYSTAL, 10, getRotation(), getCrystalTile(crystalSpawnLocations[index])));
        ignoredTile = this.getLocation(ignoredTiles[index]);
        smallMuttadile = new SmallMuttadile(this, getNPCLocation(smallMuttadileSpawnLocations[index], 3));
        largeMuttadile = new LargeMuttadile(this, Direction.mainDirections[((index == 2 ? 0 : 1) + getRotation()) & 3], getNPCLocation(largeMuttadileSpawnLocations[index], 8));
    }

    @Override
    public void process() {
        if (spawnedTendril || tendril == null) {
            return;
        }
        for (final Player player : raid.getPlayers()) {
            if (player.getLocation().withinDistance(tendril, 6)) {
                spawnedTendril = true;
                for (final Location tendril : tendrilSpawnLocations[index]) {
                    World.spawnObject(new WorldObject(29767, 10, 0, this.getLocation(tendril)));
                }
                smallMuttadile.spawn();
                largeMuttadile.spawn();
                return;
            }
        }
    }

    @Override
    public String name() {
        return "Chambers of Xeric: Muttadile room";
    }


    /**
     * The meat tree woodcutting action.
     */
    public static final class MeatTreeWoodcutting extends Action {
        /**
         * The sound effect played when the player chops a piece of the meat tree.
         */
        private static final SoundEffect chopSound = new SoundEffect(2070, 5, 0);
        /**
         * The sound effect played wehen the meat tree falls down.
         */
        private static final SoundEffect deathSound = new SoundEffect(2734, 5, 0);
        /**
         * The meat tree NPC.
         */
        private final NPC tree;
        /**
         * The raid the player is currently in.
         */
        private final Raid raid;
        /**
         * The axe result, providing the best axe the player is carrying that they may use.
         */
        private Woodcutting.AxeResult axe;
        /**
         * The number of ticks that the player has been woodcutting for, used to properly animate the player with the hatchet animation.
         */
        private int ticks;

        public MeatTreeWoodcutting(final NPC tree, final Raid raid) {
            this.tree = tree;
            this.raid = raid;
        }

        @Override
        public boolean interruptedByCombat() {
            return false;
        }

        @Override
        public boolean start() {
            if (!checkAll(player)) {
                return false;
            }
            player.sendMessage("You swing your hatchet at the meat tree...");
            delay(3);
            return true;
        }

        /**
         * Checks all the necessary settings, and sets the hatchet to the best available one.
         */
        private boolean checkAll(final Player player) {
            final Optional<Woodcutting.AxeResult> optionalAxe = Woodcutting.getAxe(player);
            if (!optionalAxe.isPresent()) {
                player.sendMessage("You do not have an axe which you have the woodcutting level to use.");
                return false;
            }
            this.axe = optionalAxe.get();
            if (tree.isDead()) {
                player.sendMessage("The tree has died.");
                return false;
            }
            return true;
        }

        @Override
        public boolean process() {
            if (ticks++ % 4 == 0) {
                player.setAnimation(axe.getDefinition().getTreeCutAnimation());
            }
            return checkTree();
        }

        @Override
        public int processWithDelay() {
            final int successRate = 20 + (int) Math.floor(player.getSkills().getLevel(SkillConstants.WOODCUTTING) / 1.7F);
            if (Utils.random(100) > successRate) {
                return 4;
            }
            if (tree instanceof MeatTree) {
                ((MeatTree) tree).flagBar();
            }
            player.getSkills().addXp(SkillConstants.WOODCUTTING, 45);
            raid.addPoints(player, 2);
            tree.heal(-Utils.random(1, player.getSkills().getLevelForXp(SkillConstants.WOODCUTTING)));
            if (tree.getHitpoints() <= 0) {
                tree.setHitpoints(0);
                World.sendSoundEffect(tree.getMiddleLocation(), deathSound);
                player.sendMessage("You hack away the final pieces of meat.");
                return -1;
            } else {
                World.sendSoundEffect(tree.getMiddleLocation(), chopSound);
                player.sendMessage("You hack away some of the meat.");
            }
            return 4;
        }

        /**
         * Returns whether the tree is still alive or not.
         */
        private boolean checkTree() {
            return tree.getHitpoints() > 0;
        }
    }

    public Set<String> getTendrilPlayers() {
        return tendrilPlayers;
    }

    public boolean isLaunched() {
        return launched;
    }

    public void setLaunched(boolean launched) {
        this.launched = launched;
    }

    public MeatTree getTree() {
        return tree;
    }

    public Location getIgnoredTile() {
        return ignoredTile;
    }

    public boolean isFinished() {
        return finished;
    }

    public void setFinished(boolean finished) {
        this.finished = finished;
    }

    public void setHasFed() { hasFed = true; }
}
