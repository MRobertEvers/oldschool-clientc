package com.zenyte.game.content.chambersofxeric.room;

import com.google.common.eventbus.Subscribe;
import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.map.RaidArea;
import com.zenyte.game.content.chambersofxeric.map.RaidRoom;
import com.zenyte.game.content.chambersofxeric.npc.CorruptedScavenger;
import com.zenyte.game.item.Item;
import com.zenyte.game.item.ItemId;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.util.Direction;
import com.zenyte.game.util.Utils;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.SoundEffect;
import com.zenyte.game.world.entity._Location;
import com.zenyte.game.world.entity.masks.Animation;
import com.zenyte.game.world.entity.masks.Graphics;
import com.zenyte.game.world.entity.masks.Hit;
import com.zenyte.game.world.entity.masks.HitType;
import com.zenyte.game.world.entity.pathfinding.Flags;
import com.zenyte.game.world.entity.player.Action;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.SkillConstants;
import com.zenyte.game.world.object.ObjectId;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.game.world.region.Region;
import com.zenyte.game.world.region.area.plugins.CycleProcessPlugin;
import com.zenyte.plugins.dialogue.PlainChat;
import com.zenyte.plugins.events.ServerLaunchEvent;
import it.unimi.dsi.fastutil.ints.Int2ObjectMap;
import it.unimi.dsi.fastutil.ints.Int2ObjectOpenHashMap;
import it.unimi.dsi.fastutil.ints.IntArrayList;
import it.unimi.dsi.fastutil.ints.IntOpenHashSet;
import it.unimi.dsi.fastutil.objects.ObjectArrayList;
import it.unimi.dsi.fastutil.objects.ObjectCollection;
import mgi.utilities.CollectionUtils;
import org.apache.commons.lang3.mutable.MutableInt;
import org.jetbrains.annotations.NotNull;

import java.util.List;

/**
 * @author Kris | 16. nov 2017 : 2:36.10
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class CreatureKeeperRoom extends RaidArea implements CycleProcessPlugin {

    public static final int CAP_MAXIMUM_CAVERN_GRUBS_STACK = 28;

    /**
     * A list of lists of game objects, mapped on server start, used to locate where the actual thievable chests are located. Necessary to know them all at once.
     */
    private static final List<List<WorldObject>> staticChests = new ObjectArrayList<>();

    /**
     * A 2D array containing coordinates to the scavenger for all four rotations, and the hay.
     */
    private static final Location[][] scavengerPositions = new Location[][] { new Location[] { new Location(3271, 5390, 0), new Location(3272, 5390, 0), new Location(3272, 5391, 0), new Location(3271, 5391, 0), new Location(3271, 5394, 0) }, new Location[] { new Location(3318, 5397, 0), new Location(3319, 5397, 0), new Location(3319, 5398, 0), new Location(3318, 5398, 0), new Location(3321, 5398, 0) }, new Location[] { new Location(3350, 5397, 0), new Location(3351, 5397, 0), new Location(3351, 5398, 0), new Location(3350, 5398, 0), new Location(3351, 5394, 0) } };

    /**
     * The storage unit hotspots locations.
     */
    private static final Location[] storageUnits = new Location[] { new Location(3274, 5388, 0), new Location(3316, 5395, 0), new Location(3348, 5400, 0) };

    /**
     * The animation that's performed upon depositing the worms in the trough.
     */
    private static final Animation depositAnimation = new Animation(832);

    /**
     * The sound effect played when the player deposits a grub.
     */
    private static final SoundEffect depositSound = new SoundEffect(3637, 5, 0);

    /**
     * The three position hashes where the poison chests will be located at.
     */
    private final IntArrayList poisonHashes = new IntArrayList(3);

    /**
     * A map of previously opened chests <location hash, chest object>.
     */
    private final Int2ObjectMap<Chest> chests = new Int2ObjectOpenHashMap<>(64);

    /**
     * The scavenger NPC who is being fed.
     */
    private CorruptedScavenger scavenger;

    /**
     * The trough object, in which the worms are placed.
     */
    private Trough trough;

    /**
     * The location of the hay, ontop of which the scavenger sleeps after it's fed.
     */
    private Location hay;

    /**
     * The location hash where the psykk bat chest is positioned at.
     */
    private int batHash;

    private int pointsCap;

    public CreatureKeeperRoom(final RaidRoom type, final Raid raid, final int rotation, final int size, final int regionX, final int regionY, final int chunkX, final int chunkY, final int fromPlane, final int toPlane) {
        super(type, raid, rotation, size, regionX, regionY, chunkX, chunkY, fromPlane, toPlane);
    }

    /**
     * Loads two regions which contain the creature keeper room on the static map and iterates the objects inside them, mapping down all thievable chests.
     *
     * @param event the server launch event.
     */
    @Subscribe
    public static final void onServerLaunch(final ServerLaunchEvent event) {
        final ObjectArrayList<WorldObject> firstRoomList = new ObjectArrayList<WorldObject>(64);
        final ObjectArrayList<WorldObject> secondRoomList = new ObjectArrayList<WorldObject>(64);
        final ObjectArrayList<WorldObject> thirdRoomList = new ObjectArrayList<WorldObject>(64);
        staticChests.add(firstRoomList);
        staticChests.add(secondRoomList);
        staticChests.add(thirdRoomList);
        final ObjectCollection<WorldObject> objects = World.getRegion(13140, true).getObjects().values();
        for (final WorldObject object : objects) {
            if (object == null || object.getId() != 29742) {
                continue;
            }
            final ObjectArrayList<WorldObject> list = object.getX() < 3294 ? firstRoomList : secondRoomList;
            list.add(object);
        }
        for (final WorldObject object : World.getRegion(13396, true).getObjects().values()) {
            if (object == null || object.getId() != 29742) {
                continue;
            }
            thirdRoomList.add(object);
        }
    }

    @Override
    public Location[] getStorageUnitLocations() {
        return storageUnits;
    }

    /**
     * Deposits the cavern grubs the player is carrying into the trough.
     *
     * @param player the player depositing the grubs.
     * @param object the trough object that's being deposited to.
     */
    public void deposit(@NotNull final Player player, @NotNull final WorldObject object) {
        if (trough == null) {
            trough = new Trough(object);
        }
        if (scavenger.isFed()) {
            player.sendMessage("That's not going to help now.");
            return;
        }
        final int amountInInventory = player.getInventory().getAmountOf(ItemId.CAVERN_GRUBS);
        if (amountInInventory == 0) {
            player.sendMessage("You need some cavern grubs to feed the scavenger.");
            return;
        }
        final int amountToDeposit = Math.max(0, Math.min(CAP_MAXIMUM_CAVERN_GRUBS_STACK, amountInInventory));
        if (amountToDeposit > 0) {
            World.sendSoundEffect(player.getLocation(), depositSound);
            player.setAnimation(depositAnimation);
            player.sendMessage("You deposit some cavern grubs in the trough.");
            player.getInventory().deleteItem(ItemId.CAVERN_GRUBS, amountInInventory);
            final int points = Math.min(pointsCap, 100 * amountToDeposit);
            trough.stage += amountToDeposit;
            pointsCap -= points;
            raid.addPoints(player, points);
            trough.setId(29874);
            World.spawnObject(trough);
        }
    }

    /**
     * @param player the player opening the chest.
     * @param object the chest object.
     */
    public void openChest(@NotNull final Player player, @NotNull final WorldObject object) {
        player.getActionManager().setAction(new ChestLootingAction(chests.computeIfAbsent(object.getPositionHash(), id -> new Chest(object)), this));
    }

    /**
     * Processes the trough object, if there are any worms in it, lowers the amount of those and returns the amount.
     *
     * @return the amount of worms that were eaten.
     */
    public int processTrough() {
        if (trough == null) {
            return 0;
        }
        if (trough.stage > 0) {
            final int amount = Math.max(Math.min(Math.min(5, trough.stage / 6), trough.stage), 1);
            trough.stage -= amount;
            return amount;
        }
        return 0;
    }

    /* @Override
    public boolean canPass(final Player player, final WorldObject object) {
        if (scavenger == null || !scavenger.isFed() && scavenger.getLocation().withinDistance(object, 7)) {
            player.sendMessage("You need to feed the corrupted scavenger to pass to the next room.");
            return false;
        }
        return true;
    }*/
    @Override
    public void process() {
        final long time = Utils.currentTimeMillis();
        chests.forEach((k, v) -> {
            if (v.getId() != 29742 && v.time < time) {
                v.setId(29742);
                v.refresh();
            }
        });
    }

    @Override
    public void loadRoom() {
        final List<WorldObject> staticSpawns = staticChests.get(index);
        assert staticSpawns != null;
        final IntOpenHashSet usedChests = new IntOpenHashSet();
        this.batHash = getLocation(staticSpawns.get(Utils.random(staticSpawns.size() - 1))).getPositionHash();
        usedChests.add(this.batHash);
        while (this.poisonHashes.size() < 3) {
            final int hash = getLocation(staticSpawns.get(Utils.random(staticSpawns.size() - 1))).getPositionHash();
            if (usedChests.contains(hash)) {
                continue;
            }
            usedChests.add(hash);
            poisonHashes.add(hash);
        }
        scavenger = new CorruptedScavenger(raid, this, 7602, getLocation(scavengerPositions[index][getRotation()]), Direction.cardinalDirections[(index + getRotation()) & 3]);
        scavenger.spawn();
        for (int x = scavenger.getX(); x < scavenger.getX() + scavenger.getSize(); x++) {
            for (int y = scavenger.getY(); y < scavenger.getY() + scavenger.getSize(); y++) {
                World.getRegion(_Location.getRegionId(x, y), true).addFlag(scavenger.getPlane(), x & 63, y & 63, Flags.FLOOR);
            }
        }
        WorldTasksManager.schedule(() -> {
            hay = getLocation(scavengerPositions[index][4]);
            recalculateHayPosition();
        }, 5);
        pointsCap = (int) (((scavenger.getLifepoints() / 10) * 100));
        pointsCap *= 1.25F;
    }

    /**
     * Recalculates the hay position. Due to the odd nature of the creature keeper NPC, it is positioned client-side between two tiles in an odd manner.
     * We cannot physically figure out a correct position for it, so we iterate the hay objects themselves and reposition the destination tile so it looks proper.
     */
    private final void recalculateHayPosition() {
        final Location hay = new Location(Integer.MAX_VALUE);
        for (int x = this.hay.getX() - 4; x < this.hay.getX() + 4; x++) {
            for (int y = this.hay.getY() - 4; y < this.hay.getY() + 4; y++) {
                final Region region = World.getRegion(_Location.getRegionId(x, y), true);
                final WorldObject[] objects = region.getObjects(getToPlane(), x & 63, y & 63);
                final WorldObject matchingObject = CollectionUtils.findMatching(objects, obj -> obj != null && (obj.getId() == 3471 || obj.getId() == 3472));
                if (matchingObject != null) {
                    if (x < hay.getX() || y < hay.getY()) {
                        hay.setLocation(x, y, getToPlane());
                    }
                }
            }
        }
        this.hay = hay;
    }

    @Override
    public String name() {
        return "Chambers of Xeric: Creature keeper room";
    }

    public Trough getTrough() {
        return trough;
    }

    /**
     * A private class for the chest objects; {@code Chest#time } is the time in milliseconds after which the chest is shut close.
     */
    private static final class Chest extends WorldObject {

        private long time;

        Chest(final WorldObject object) {
            super(object);
        }

        public void refresh() {
            World.spawnObject(this);
        }

        @Override
        public boolean equals(final Object o) {
            if (o == this)
                return true;
            if (!(o instanceof CreatureKeeperRoom.Chest))
                return false;
            final CreatureKeeperRoom.Chest other = (CreatureKeeperRoom.Chest) o;
            if (!other.canEqual(this))
                return false;
            if (!super.equals(o))
                return false;
            return this.time == other.time;
        }

        protected boolean canEqual(final Object other) {
            return other instanceof CreatureKeeperRoom.Chest;
        }

        @Override
        public int hashCode() {
            final int PRIME = 59;
            int result = super.hashCode();
            final long $time = this.time;
            result = result * PRIME + (int) ($time >>> 32 ^ $time);
            return result;
        }
    }

    /**
     * An action class for looting the chest.
     */
    private static final class ChestLootingAction extends Action {

        /**
         * The animation that's performed for each attempt to open the chest.
         */
        private static final Animation openAnimation = new Animation(536);

        /**
         * Bats, rarely found within the chest.
         */
        private static final Item batsItem = new Item(20883, 5);

        /**
         * The graphics that are played when the player gets hit by the chest.
         */
        private static final Graphics graphics = new Graphics(184);

        /**
         * The sound effect that is played when the player attempts to open the chest.
         */
        private static final SoundEffect openingSoundEffect = new SoundEffect(2402, 5, 0);

        /**
         * The sound effect that is played whe nthe player successfully opens a chest.
         */
        private static final SoundEffect successfulOpenSound = new SoundEffect(52, 5, 0);

        /**
         * The sound effect that plays when the player opens a chest that contains a poison gas cloud.
         */
        private static final SoundEffect poisonGasSound = new SoundEffect(2138, 10, 0);

        /**
         * The sound effect that plays when the player opens the chest with the psykk bats.
         */
        private static final SoundEffect batsSound = new SoundEffect(2655, 10, 0);

        /**
         * The creature keeper's room.
         */
        private final CreatureKeeperRoom room;

        /**
         * The chest that is being stolen from.
         */
        private final Chest chest;

        /**
         * Constructor for the chest looting action.
         *
         * @param chest the chest that is being looted.
         * @param room  the room in which the chest is located.
         */
        ChestLootingAction(@NotNull final Chest chest, @NotNull final CreatureKeeperRoom room) {
            this.chest = chest;
            this.room = room;
        }

        @Override
        public boolean start() {
            player.faceObject(chest);
            player.lock(25);
            return true;
        }

        @Override
        public boolean process() {
            if (chest.getId() != ObjectId.CHEST_29742) {
                player.sendMessage("The chest is already opened.");
                player.unlock();
                return false;
            }
            return true;
        }

        @Override
        public void stop() {
            player.unlock();
        }



        @Override
        public int processWithDelay() {
            World.sendSoundEffect(player.getLocation(), openingSoundEffect);
            player.setAnimation(openAnimation);
            player.setFaceLocation(chest);
            if (!player.getInventory().hasFreeSlots()) {
                player.unlock();
                player.getDialogueManager().start(new PlainChat(player, "You wouldn't have space to take anything from " + "the chest."));
                return -1;
            }

            final boolean lockpick = player.getInventory().containsAnyOf(1523, 1) || player.getInventory().containsItem(11682, 1);
            final int success = (int) (lockpick ? (49.0F + (player.getSkills().getLevel(SkillConstants.THIEVING) / 4.95F)) : (35.0F + (player.getSkills().getLevel(SkillConstants.THIEVING) / 7.0F)));
            if (Utils.random(100) <= success) {
                player.unlock();
                final MutableInt typeInt = new MutableInt(room.batHash == chest.getPositionHash() ? 20 : room.poisonHashes.contains(chest.getPositionHash()) ? 19 : Utils.random(18));
                // Cannot find anymore grubs once the scavenger has been fed.
                if (room.scavenger.isFed() && typeInt.intValue() < 12) {
                    typeInt.setValue(12);
                }
                final int type = typeInt.intValue();
                if (type != 20) {
                    World.sendSoundEffect(player.getLocation(), successfulOpenSound);
                }
                if (type < 10) {
                    final int amountInInventory = player.getInventory().getAmountOf(ItemId.CAVERN_GRUBS);
                    if (amountInInventory >= CAP_MAXIMUM_CAVERN_GRUBS_STACK) {
                        player.sendMessage("You already have the maximum amount of cavern grubs.");
                    } else {
                        player.sendFilteredMessage("Some cavern grubs have hatched; you take them.");
                        chest.setId(29745);
                        final int thievingLevel = player.getSkills().getLevelForXp(SkillConstants.THIEVING);
                        final MutableInt amount = new MutableInt(1);
                        final int random = Utils.random(49);
                        if (thievingLevel >= 95) {
                            amount.setValue(random < 11 ? 3 : 2);
                        } else if (thievingLevel >= 75) {
                            amount.setValue(random < 11 ? 3 : random < (11 + 13) ? 2 : 1);
                        } else if (thievingLevel >= 50) {
                            amount.setValue(random < 17 ? 2 : 1);
                        }
                        final int intAmount = amount.intValue();
                        final int amountToAdd = Math.max(0, Math.min(intAmount, CAP_MAXIMUM_CAVERN_GRUBS_STACK - amountInInventory));
                        if (amountToAdd > 0) {
                            player.getSkills().addXp(SkillConstants.THIEVING, amountToAdd == 1 ? 40 : amountToAdd == 2 ? 60 : 73);
                            player.getInventory().addItem(new Item(ItemId.CAVERN_GRUBS, intAmount));
                        }
                    }
                } else if (type < 19) {
                    player.sendFilteredMessage("No cocoons in this chest have hatched.");
                    chest.setId(29744);
                } else if (type < 20) {
                    room.getRaid().getPlayers().forEach(target -> {
                        if (target.getLocation().withinDistance(player.getLocation(), 1)) {
                            target.applyHit(new Hit(Utils.random(1, 3), HitType.POISON));
                        }
                    });
                    World.sendSoundEffect(player.getLocation(), poisonGasSound);
                    World.sendGraphics(graphics, new Location(chest));
                    chest.setId(29743);
                } else {
                    Raid.incrementPoints(player, 250);
                    World.sendSoundEffect(player.getLocation(), batsSound);
                    chest.setId(29743);
                    player.sendMessage("The chest contained some nicely cooked bats.");
                    final Item psykk = new Item(batsItem);
                    psykk.setAttribute("finder", player.getUsername());
                    player.getInventory().addOrDrop(psykk);
                    chest.time = Long.MAX_VALUE;
                    chest.refresh();
                    return -1;
                }
                chest.time = Utils.currentTimeMillis() + (type == 19 ? 6000 : 5000);
                chest.refresh();
                return -1;
            }
            return 1;
        }
    }

    public Location getHay() {
        return hay;
    }

    public int getPointsCap() {
        return pointsCap;
    }

    public void setPointsCap(int pointsCap) {
        this.pointsCap = pointsCap;
    }

    /**
     * A private class for the trough object; {@code Trough#stage } is the amount of worms in it.
     */
    public static final class Trough extends WorldObject {

        private int stage;

        Trough(final WorldObject object) {
            super(object);
        }

        public int getStage() {
            return stage;
        }

        @Override
        public boolean equals(final Object o) {
            if (o == this)
                return true;
            if (!(o instanceof CreatureKeeperRoom.Trough))
                return false;
            final CreatureKeeperRoom.Trough other = (CreatureKeeperRoom.Trough) o;
            if (!other.canEqual(this))
                return false;
            if (!super.equals(o))
                return false;
            return this.getStage() == other.getStage();
        }

        protected boolean canEqual(final Object other) {
            return other instanceof CreatureKeeperRoom.Trough;
        }

        @Override
        public int hashCode() {
            final int PRIME = 59;
            int result = super.hashCode();
            result = result * PRIME + this.getStage();
            return result;
        }
    }
}
