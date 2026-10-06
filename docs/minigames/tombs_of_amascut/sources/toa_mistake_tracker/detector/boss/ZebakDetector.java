package com.toamistaketracker.detector.boss;

import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.HitsplatID;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.events.PlayerChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.ZEBAK;
import static com.toamistaketracker.ToaMistake.ZEBAK_ACID_TILE;
import static com.toamistaketracker.ToaMistake.ZEBAK_BLOOD_CLOUD;
import static com.toamistaketracker.ToaMistake.ZEBAK_EARTHQUAKE;
import static com.toamistaketracker.ToaMistake.ZEBAK_WAVE;

/**
 * Zebak has quite a few things to keep track of. The acidTiles spawn and are detected through their game object IDs,
 * though they don't actually damage the player on the first tick that they spawn, so they get delayed.
 *
 * The blood clouds move around the arena and damage the player (and heal themselves) when they come into contact with
 * a player. Here we check HitsplatApplied events for HEAL and find all raiders on the actor's tile.
 *
 * The earthquake is also straightforward, as it spawns a graphics object when it hits. The one issue here is that when
 * jugs spawn, they spawn the same graphics object on the tile they land for a tick, since they can land on the player
 * and deal damage. However, I don't want to track this (at least not the same as earthquakes), so there's extra logic
 * to only detect the earthquake graphics IDs when zebak is doing the scream animation.
 *
 * The waves are pretty inconsistent. Again, detecting tile collisions with other pathing NPCs is a bit tricky. In fact,
 * there's an even instance I have on video of my *skipping over* a wave, which shouldn't be possible. Anyway, I ended
 * up just checking to see if the raider's current tile is >2 |distance| away, since that's not normally possible. The
 * waves seem to push you back at least 3 tiles (sometimes 4 if you're at the edge tile which you can't stand on). This
 * seems to be the easiest and most consistent way to detect if you got pushed, since there's no other animation.
 *
 * I decided to get rid of the chomp mistake, as Jagex is changing the timing of when the chomp actually hits you, and
 * also it doesn't always cause bleed or do damage.
 */
@Slf4j
@Singleton
public class ZebakDetector extends BaseMistakeDetector {

    private static final int CHOMP_ANIMATION_ID = 9620;
    private static final int CHOMP_HIT_DELAY_IN_TICKS = 2;

    private static final Set<Integer> SWIMMING_POSE_IDS = ImmutableSet.of(772, 773);
    private static final Set<Integer> ACID_TILE_GAME_OBJECT_IDS = ImmutableSet.of(
            45570, 45571, 45572, 45573, 45574, 45575, 45576);
    private static final int EARTHQUAKE_GRAPHICS_ID = 2184;
    private static final int ZEBAK_SCREAM_ANIMATION_ID = 9628;
    private static final String ZEBAK_NAME = "Zebak";
    private static final String WAVE_NAME = "Wave";
    private static final String BLOOD_CLOUD_NAME = "Blood Cloud";

    private final Set<WorldPoint> acidTilesToSpawn;
    private final Set<WorldPoint> acidTiles;
    private final Set<WorldPoint> bloodHealedTiles;
    private final Set<WorldPoint> earthquakeHitTiles;
    private final Set<String> raidersCurrentlySwimming;
    private final Set<String> raidersPreviouslySwimming;

    private final List<NPC> waves;
    private boolean isZebakScreaming;

    public ZebakDetector() {
        acidTilesToSpawn = new HashSet<>();
        acidTiles = new HashSet<>();
        bloodHealedTiles = new HashSet<>();
        earthquakeHitTiles = new HashSet<>();
        raidersCurrentlySwimming = new HashSet<>();
        raidersPreviouslySwimming = new HashSet<>();

        waves = new ArrayList<>();
        isZebakScreaming = false;

    }

    @Override
    public void cleanup() {
        acidTilesToSpawn.clear();
        acidTiles.clear();
        bloodHealedTiles.clear();
        earthquakeHitTiles.clear();
        raidersCurrentlySwimming.clear();
        raidersPreviouslySwimming.clear();
        waves.clear();
        isZebakScreaming = false;
    }

    @Override
    public RaidRoom getRaidRoom() {
        return ZEBAK;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (acidTiles.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(ZEBAK_ACID_TILE);
        }

        if (bloodHealedTiles.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(ZEBAK_BLOOD_CLOUD);
        }

        if (isEarthquakeHit(raider)) {
            mistakes.add(ZEBAK_EARTHQUAKE);
        }

        if (isWaveHit(raider)) {
            mistakes.add(ZEBAK_WAVE);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        acidTiles.addAll(acidTilesToSpawn);
        acidTilesToSpawn.clear();
        bloodHealedTiles.clear();
        earthquakeHitTiles.clear();

        raidersPreviouslySwimming.clear();
        raidersPreviouslySwimming.addAll(raidersCurrentlySwimming);
        raidersCurrentlySwimming.clear();
    }

    private boolean isEarthquakeHit(Raider raider) {
        return earthquakeHitTiles.contains(raider.getPreviousWorldLocation()) && isZebakScreaming;
    }

    private boolean isWaveHit(Raider raider) {
        // Jumping back up from swimming can bring you forward 3 tiles, so don't allow that to count in the hack below.
        if (raider.getPreviousWorldLocation() == null ||
                waves.isEmpty() ||
                raidersPreviouslySwimming.contains(raider.getName())) {
            return false;
        }

        // HACK
        // Normally a player cannot move > 2 tiles in 1 tick. The waves push you back at least 3, so use that to detect.
        return Math.abs(raider.getCurrentWorldLocation().getY() - raider.getPreviousWorldLocation().getY()) > 2;
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (ACID_TILE_GAME_OBJECT_IDS.contains(event.getGameObject().getId())) {
            // Acid tiles don't hit you for the first tick they're spawned, so delay their detection by a tick.
            // The first ones sometimes only poison you and not damage you on the second tick, but we count that anyway.
            acidTilesToSpawn.add(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event) {
        if (ACID_TILE_GAME_OBJECT_IDS.contains(event.getGameObject().getId())) {
            acidTilesToSpawn.remove(event.getGameObject().getWorldLocation());
            acidTiles.remove(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event) {
        if (event.getActor().getName() != null &&
                BLOOD_CLOUD_NAME.equals(event.getActor().getName()) &&
                event.getHitsplat().getHitsplatType() == HitsplatID.HEAL) {
            bloodHealedTiles.add(event.getActor().getWorldLocation()); // Is this correct? Should it be local toWorld?
        }
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        if (event.getGraphicsObject().getId() == EARTHQUAKE_GRAPHICS_ID) {
            earthquakeHitTiles.add(getWorldPoint(event.getGraphicsObject()));
        }
    }

    @Subscribe
    public void onNpcSpawned(NpcSpawned event) {
        if (event.getNpc().getName() == null) return;

        String name = Text.removeTags(event.getNpc().getName());
        if (WAVE_NAME.equals(name)) {
            waves.add(event.getNpc());
        }
    }

    @Subscribe
    public void onNpcDespawned(NpcDespawned event) {
        if (event.getNpc().getName() == null) return;

        String name = Text.removeTags(event.getNpc().getName());
        if (WAVE_NAME.equals(name)) {
            waves.remove(event.getNpc());
        }
    }

    @Subscribe
    public void onPlayerChanged(PlayerChanged event) {
        if (event.getPlayer() != null && SWIMMING_POSE_IDS.contains(event.getPlayer().getPoseAnimation())) {
            raidersCurrentlySwimming.add(event.getPlayer().getName());
        }
    }

    @Subscribe
    public void onAnimationChanged(AnimationChanged event) {
        if (event.getActor() == null || event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (event.getActor() instanceof NPC && ZEBAK_NAME.equals(name)) {
            isZebakScreaming = event.getActor().getAnimation() == ZEBAK_SCREAM_ANIMATION_ID;
        }
    }
}