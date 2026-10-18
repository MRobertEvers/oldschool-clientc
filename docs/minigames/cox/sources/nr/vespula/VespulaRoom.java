package com.zenyte.game.content.chambersofxeric.room;

import com.zenyte.game.content.chambersofxeric.CombatPointCapCalculator;
import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.map.RaidArea;
import com.zenyte.game.content.chambersofxeric.map.RaidRoom;
import com.zenyte.game.content.chambersofxeric.npc.AbyssalPortal;
import com.zenyte.game.content.chambersofxeric.npc.LuxGrub;
import com.zenyte.game.content.chambersofxeric.npc.VespineSoldier;
import com.zenyte.game.content.chambersofxeric.npc.Vespula;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.npc.NPC;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.object.ObjectId;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.game.world.region.area.plugins.PlayerCombatPlugin;
import it.unimi.dsi.fastutil.objects.ObjectArrayList;

import java.util.List;

/**
 * @author Kris | 11. mai 2018 : 19:33:30
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class VespulaRoom extends RaidArea implements PlayerCombatPlugin {

    /**
     * The id of the medivaemia root objet.
     */
    public static final int MEDIVAEMIA_ROOT = 20892;

    /**
     * The spawn locations of the lux grubs.
     */
    private static final Location[][] luxGrubs = new Location[][] { new Location[] { new Location(3287, 5289, 2), new Location(3285, 5294, 2), new Location(3277, 5286, 2), new Location(3278, 5294, 2) }, new Location[] { new Location(3312, 5289, 2), new Location(3304, 5291, 2), new Location(3312, 5297, 2), new Location(3304, 5297, 2) }, new Location[] { new Location(3344, 5294, 2), new Location(3337, 5286, 2), new Location(3337, 5294, 2), new Location(3334, 5290, 2) } };

    /**
     * The locations to which the vespine solders force-walk to when they transform from the grubs.
     */
    private static final Location[][] vespineSoldiers = new Location[][] { new Location[] { new Location(3284, 5289, 2), new Location(3285, 5291, 2), new Location(3277, 5289, 2), new Location(3278, 5291, 2) }, new Location[] { new Location(3309, 5289, 2), new Location(3307, 5291, 2), new Location(3309, 5297, 2), new Location(3307, 5297, 2) }, new Location[] { new Location(3344, 5291, 2), new Location(3337, 5289, 2), new Location(3337, 5291, 2), new Location(3337, 5290, 2) } };

    /**
     * The tiles on which the boils blocking the exit of the room spawn on.
     */
    private static final Location[][] boilLocations = new Location[][] { new Location[] { new Location(3267, 5295, 2), new Location(3267, 5296, 2) }, new Location[] { new Location(3311, 5308, 2), new Location(3312, 5308, 2) }, new Location[] { new Location(3356, 5296, 2), new Location(3356, 5295, 2) } };

    /**
     * The locations on which the septic tendrils spawn.
     */
    private static final Location[][] tendrilLocations = new Location[][] { new Location[] { new Location(3279, 5280, 2), new Location(3280, 5280, 2) }, new Location[] { new Location(3311, 5280, 2), new Location(3312, 5280, 2) }, new Location[] { new Location(3343, 5280, 2), new Location(3344, 5280, 2) } };

    /**
     * The tiles on which the abyssal portal spawns.
     */
    private static final Location[] portalLocations = new Location[] { new Location(3281, 5300, 2), new Location(3318, 5291, 2), new Location(3339, 5300, 2) };

    /**
     * The tiles on which Vespula spawns.
     */
    private static final Location[] vespulaLocations = new Location[] { new Location(3281, 5290, 2), new Location(3308, 5292, 2), new Location(3341, 5290, 2) };

    /**
     * The start locations of where portal's projectile attack starts from.
     */
    private static final Location[] portalProjectileStartLocations = new Location[] { new Location(3282, 5302, 2), new Location(3320, 5292, 2), new Location(3341, 5302, 2) };

    /**
     * The tiles which are damaged by the portal's projectile attack.
     */
    private static final Location[][] portalHitTiles = new Location[][] { new Location[] { new Location(3281, 5293, 2), new Location(3282, 5293, 2) }, new Location[] { new Location(3311, 5293, 2), new Location(3311, 5292, 2) }, new Location[] { new Location(3340, 5293, 2), new Location(3341, 5293, 2) } };

    /**
     * A list of lux grubs.
     */
    private final List<LuxGrub> grubs = new ObjectArrayList<>(4);

    /**
     * A list of boils blocking the exit of the room.
     */
    private final List<WorldObject> boils = new ObjectArrayList<>(2);

    /**
     * A list of the original medivaemia root objects.
     */
    private final List<WorldObject> originalMedivaemiaRoots = new ObjectArrayList<>(6);

    /**
     * The septic tendril objects.
     */
    private final List<WorldObject> tendrils = new ObjectArrayList<>();

    /**
     * A list of two tiles where the projectile can hit.
     */
    private final List<Location> hitTiles = new ObjectArrayList<>(2);

    /**
     * The tile from which the portal's projectile attack starts.
     */
    private Location portalProjectileStartTile;

    /**
     * The abyssal portal in the center of the room.
     */
    private AbyssalPortal abyssalPortal;

    /**
     * The Vespula.
     */
    private Vespula vespula;

    /**
     * Whether or not the room has been loaded yet.
     */
    private boolean loaded;

    /**
     * Whether or not the room has been finished yet.
     */
    private boolean finished;

    public VespulaRoom(final RaidRoom type, final Raid raid, final int rotation, final int size, final int regionX, final int regionY, final int chunkX, final int chunkY, final int fromPlane, final int toPlane) {
        super(type, raid, rotation, size, regionX, regionY, chunkX, chunkY, fromPlane, toPlane);
    }

    @Override
    public CombatPointCapCalculator buildPointsCap() {
        return new CombatPointCapCalculator().appendNPC(vespula).appendNPC(abyssalPortal).appendExtra(100 * 4 * 4);
    }

    @Override
    public void loadRoom() {
        abyssalPortal = new AbyssalPortal(getRaid(), this, getNPCLocation(portalLocations[index], 6));
        vespula = new Vespula(getRaid(), this, getLocation(vespulaLocations[index]).transform(-2, -2, 0));
        final Location[] array = portalHitTiles[index];
        for (final Location tile : array) {
            hitTiles.add(getLocation(tile));
        }
        this.portalProjectileStartTile = getLocation(portalProjectileStartLocations[index]);
    }

    @Override
    public void enter(final Player player) {
        super.enter(player);
        load();
    }

    /**
     * Loads the room if it hasn't been loaded yet, spawning all the objects, grubs, vespula and everything else. Occurs when any player enters the room's boundaries.
     */
    public void load() {
        if (loaded) {
            return;
        }
        loaded = true;
        for (int i = 0; i < 4; i++) {
            final Location spawnTile = getLocation(luxGrubs[index][i]).transform(-1, -1, 0);
            final Location soldierTile = getLocation(vespineSoldiers[index][i]).transform(-1, -1, 0);
            final LuxGrub grub = new LuxGrub(raid, this, spawnTile, soldierTile);
            grub.spawn();
            final Location faceLocation = getLocation(vespineSoldiers[index][i]);
            grub.setFaceLocation(faceLocation);
            grubs.add(grub);
        }
        for (int i = 0; i < 2; i++) {
            final WorldObject object = new WorldObject(30070, 10, 0, this.getLocation(boilLocations[index][i]));
            boils.add(object);
            World.spawnObject(object);
        }
        abyssalPortal.spawn();
        assert boils.size() > 0;
        World.forEachObject(new Location(chunkX << 3, chunkY << 3, getToPlane()), 64, object -> {
            if (object.getId() == ObjectId.MEDIVAEMIA_ROOT_30069) {
                originalMedivaemiaRoots.add(object);
            } else if (object.getId() == ObjectId.SLIMY_PILLARS) {
                abyssalPortal.setPillarObject(object);
            }
        });
        for (final WorldObject root : originalMedivaemiaRoots) {
            final WorldObject newRoot = new WorldObject(root);
            newRoot.setId(30068);
            World.spawnObject(newRoot);
        }
        vespula.spawn();
    }

    /**
     * Spawns the septic tendril objects when the portal is first hit.
     */
    public void spawnTendrils() {
        if (finished || !tendrils.isEmpty()) {
            return;
        }
        for (int i = 0; i < 2; i++) {
            final WorldObject object = new WorldObject(30024, 10, 0, this.getLocation(tendrilLocations[index][i]));
            tendrils.add(object);
            World.spawnObject(object);
        }
    }

    /**
     * Clears the room out by killing all the creatures alive in the room, and blowing up the boils.
     */
    public void clear() {
        this.finished = true;
        vespula.kill();
        // Grubs will kill their respective soldiers as well if any are alive.
        for (final LuxGrub grub : this.grubs) {
            grub.kill();
        }
        for (final WorldObject root : originalMedivaemiaRoots) {
            World.spawnObject(root);
        }
        // TODO: Boils should also play an animation which I do not have right now.
        WorldTasksManager.schedule(() -> {
            for (final WorldObject boil : boils) {
                final WorldObject burstBoil = new WorldObject(boil);
                burstBoil.setId(30071);
                World.spawnObject(burstBoil);
            }
            for (final WorldObject tendril : tendrils) {
                World.removeObject(tendril);
            }
            raid.sendGlobalMessage("As the link to the abyss disintegrates, the abyssal creatures perish.");
        }, 1);
    }

    @Override
    public String name() {
        return "Chambers of Xeric: Vespula room";
    }

    @Override
    public boolean processCombat(final Player player, final Entity entity, final String style) {
        if (!(entity instanceof NPC)) {
            return true;
        }
        final NPC npc = (NPC) entity;
        if (style.equals("Melee") && (npc.getId() == 7530 || npc.getId() == 7531 || npc.getId() == VespineSoldier.FLYING)) {
            player.sendMessage("The creature is flying too high for you to attack using melee.");
            return false;
        }
        return true;
    }

    public List<LuxGrub> getGrubs() {
        return grubs;
    }

    public List<WorldObject> getBoils() {
        return boils;
    }

    public List<WorldObject> getOriginalMedivaemiaRoots() {
        return originalMedivaemiaRoots;
    }

    public List<WorldObject> getTendrils() {
        return tendrils;
    }

    public List<Location> getHitTiles() {
        return hitTiles;
    }

    public Location getPortalProjectileStartTile() {
        return portalProjectileStartTile;
    }

    public AbyssalPortal getAbyssalPortal() {
        return abyssalPortal;
    }

    public Vespula getVespula() {
        return vespula;
    }

    public boolean isLoaded() {
        return loaded;
    }

    public boolean isFinished() {
        return finished;
    }
}
