package com.zenyte.game.content.chambersofxeric.room;

import com.zenyte.game.content.chambersofxeric.Raid;
import com.zenyte.game.content.chambersofxeric.map.RaidArea;
import com.zenyte.game.content.chambersofxeric.map.RaidRoom;
import com.zenyte.game.content.chambersofxeric.npc.EnergyFocus;
import com.zenyte.game.content.chambersofxeric.npc.JewelledCrab;
import com.zenyte.game.item.Item;
import com.zenyte.game.task.WorldTasksManager;
import com.zenyte.game.world.World;
import com.zenyte.game.world.entity.Entity;
import com.zenyte.game.world.entity.Location;
import com.zenyte.game.world.entity.masks.Graphics;
import com.zenyte.game.world.entity.player.Action;
import com.zenyte.game.world.entity.player.Player;
import com.zenyte.game.world.entity.player.action.combat.magic.CombatSpell;
import com.zenyte.game.world.entity.player.calog.CAType;
import com.zenyte.game.world.object.WorldObject;
import com.zenyte.game.world.region.area.plugins.PlayerCombatPlugin;
import mgi.types.config.ObjectDefinitions;
import org.apache.commons.lang3.ArrayUtils;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

import static com.zenyte.game.item.ItemId.HAMMER;

/**
 * @author Kris | 16. nov 2017 : 2:48.42
 * @see <a href="https://www.rune-server.ee/members/kris/">Rune-Server profile</a>
 */
public final class CrabPuzzleRoom extends RaidArea implements PlayerCombatPlugin {
   /**
    * An array containing coordinates to where the light focus spawns.
    */
   private static final Location[] lightFocusSpawnLocations = new Location[] {new Location(3277, 5353, 2), new Location(3318, 5363, 2), new Location(3346, 5358, 2)};
   /**
    * A 2D array containing locations for the crab spawns.
    */
   private static final Location[][] crabSpawnLocations = new Location[][] {new Location[] {new Location(3286, 5354, 2), new Location(3280, 5358, 2), new Location(3281, 5365, 2), new Location(3287, 5361, 2)}, new Location[] {new Location(3306, 5358, 2), new Location(3311, 5353, 2), new Location(3313, 5361, 2), new Location(3318, 5356, 2)}, new Location[] {new Location(3337, 5356, 2), new Location(3335, 5362, 2), new Location(3341, 5366, 2), new Location(3344, 5361, 2)}};
   /**
    * An array containing coordinates to where the crystal blocking the exit spawns.
    */
   private static final Location[] crystalSpawnLocations = new Location[] {new Location(3272, 5365, 2), new Location(3308, 5366, 2), new Location(3349, 5366, 2)};
   /**
    * The blocking crystal's shatter graphics.
    */
   private static final Graphics shatterGraphics = new Graphics(1326);
   /**
    * A list of jewelled crabs using whom the player must crack the puzzle.
    */
   private final List<JewelledCrab> crabs;
   /**
    * An array defining the state of the crystals per index - {@code true means the crystal has been turned.}
    */
   private final boolean[] crystals = new boolean[4];
   /**
    * The id of the crystal that blocks the exit - initially pitch black.
    */
   private int crystalId = 29753;
   private int startedPlayingWithLasersTask = 0;

   public CrabPuzzleRoom(final RaidRoom type, final Raid raid, final int rotation, final int size, final int regionX, final int regionY, final int chunkX, final int chunkY, final int fromPlane, final int toPlane) {
      super(type, raid, rotation, size, regionX, regionY, chunkX, chunkY, fromPlane, toPlane);
      crabs = new ArrayList<>(4);
   }

   /**
    * Calculates the location of the blocking crystal.
    *
    * @return location of the blocking crystal.
    */
   private Location getCrystalTile() {
      final ObjectDefinitions defs = Objects.requireNonNull(ObjectDefinitions.get(29753));
      return getObjectLocation(crystalSpawnLocations[index], defs.getSizeX(), defs.getSizeY(), getRotation());
   }

   /**
    * Transforms the blocking crystal into the next stage if applicable, otherwise destroys it completely.
    *
    * @param index the index of the small crystal that transformed.
    */
   public void transformCrystal(final int index) {
      final WorldObject crystal = new WorldObject(crystalId, 10, getRotation(), getCrystalTile());
      if (!ArrayUtils.contains(crystals, true)) {
         startedPlayingWithLasersTask = 1;
      }
      if (!crystals[index]) {
         crystals[index] = true;
         crystal.setId(++crystalId);
         World.spawnObject(crystal);
      }
      if (ArrayUtils.contains(crystals, false)) {
         return;
      }
      if (startedPlayingWithLasersTask == 4) {
         getRaid().getPlayers().forEach(p -> p.getCombatAchievements().complete(CAType.PLAYING_WITH_LASERS));
      }
      //Crabs die one tick before the crystals shatter.
      WorldTasksManager.schedule(() -> {
         for (final JewelledCrab crab : crabs) {
            crab.sendDeath();
         }
      }, 1);
      WorldTasksManager.schedule(() -> {
         World.sendGraphics(shatterGraphics, crystal);
         World.removeObject(crystal);
      }, 2);
   }

   /**
    * Sends another light focus as long as the crystal hasn't shattered.
    */
   public void sendFocus() {
      if (ArrayUtils.contains(crystals, false)) {
         if (startedPlayingWithLasersTask >= 1) {
            startedPlayingWithLasersTask++;
         }
         new EnergyFocus(this, getLocation(lightFocusSpawnLocations[index])).spawn();
      }
   }

   /* @Override
    public boolean canPass(final Player player, final WorldObject object) {
        if (ArrayUtils.contains(crystals, false)) {
            player.sendMessage("You need to solve the puzzle before going further.");
            return false;
        }
        return true;
    }*/
   @Override
   public void loadRoom() {
      for (int i = 0; i < 4; i++) {
         crabs.add((JewelledCrab) new JewelledCrab(this, getLocation(crabSpawnLocations[index][i])).spawn());
      }
      World.spawnObject(new WorldObject(crystalId, 10, getRotation(), getCrystalTile()));
      World.getRegion(getLocation(crabSpawnLocations[index][0]).getRegionId(), true);
      WorldTasksManager.schedule(this::sendFocus, 5);
   }


   @Override
   public String name() {
      return "Chambers of Xeric: Crab puzzle room";
   }

   @Override
   public boolean processCombat(final Player player, final Entity entity, final String style) {
      return true;
   }

   @Override
   public void onAttack(final Player player, final Entity entity, final String style, CombatSpell spell, boolean splash) {
      if (entity instanceof JewelledCrab) {
         final Action action = player.getActionManager().getAction();
         WorldTasksManager.schedule(() -> {
            if (player.getActionManager().getAction() == action) {
               player.getActionManager().forceStop();
            }
         });
      }
   }

   public List<JewelledCrab> getCrabs() {
      return crabs;
   }
}
