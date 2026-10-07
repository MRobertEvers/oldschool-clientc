package com.toamistaketracker.detector.puzzle;

import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.HitsplatID;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

import static com.toamistaketracker.RaidRoom.CRONDIS_PUZZLE;
import static com.toamistaketracker.ToaMistake.CRONDIS_PUZZLE_LOW_WATER;

/**
 * Path of Crondis puzzle is a bit of a tricky one. If a watering amount < 100 is applied to the palm tree, a mistake
 * was made. The question is, by whom?
 *
 * To answer this, we track a few things:
 * 1. The raiders that have filled their jugs with water. This is done by checking AnimationChanged events for players
 * that are standing on a "waterfall" tile.
 * 2. The raiders that have taken damage with water. This is done by checking HitsplatApplied events for damage done
 * on raiders that have already filled their jugs.
 * 3. The raiders that have watered the palm tree this tick. Once again, we check AnimationChanged events for players
 * that are standing on a "palm tree" tile.
 *
 * With all of this information, we also defensively check for HitsplatApplied events on the palm tree itself being
 * lower than 100, and then we can try to resolve the player that was damaged when detecting. Most of this special
 * logic is really only needed to handle the edge case of two players watering on the same tick, with only one having
 * less than 100.
 */
@Slf4j
@Singleton
public class CrondisPuzzleDetector extends BaseMistakeDetector {

    private static final Set<WorldPoint> WATERFALL_REGION_TILES = ImmutableSet.of(
            // SW
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 23, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 24, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 26, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 27, 7, 0),

            // SE
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 37, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 38, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 40, 7, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 41, 7, 0),

            // NW
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 23, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 24, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 26, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 27, 57, 0),

            // NE
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 37, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 38, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 40, 57, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 41, 57, 0)
    );

    private static final Set<WorldPoint> PALM_TREE_REGION_TILES = ImmutableSet.of(
            // S
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 30, 29, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 31, 29, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 32, 29, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 33, 29, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 34, 29, 0),

            // E
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 35, 30, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 35, 31, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 35, 32, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 35, 33, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 35, 34, 0),

            // N
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 30, 35, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 31, 35, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 32, 35, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 33, 35, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 34, 35, 0),

            // W
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 29, 30, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 29, 31, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 29, 32, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 29, 33, 0),
            WorldPoint.fromRegion(CRONDIS_PUZZLE.getRegionId(), 29, 34, 0)
    );

    private static final String PALM_TREE_NAME = "Palm of Resourcefulness";
    private static final int PLAYER_WATER_ANIMATION_ID = 827;
    private static final int WATER_HITSPLAT_UP_ID = 11;
    private static final int WATER_HITSPLAT_DOWN_ID = 15;
    private static final int MAX_WATER_HITSPLAT_UP_AMOUNT = 100;

    @Getter
    private Set<WorldPoint> waterFallTiles;
    @Getter
    private Set<WorldPoint> palmTreeTiles;

    private final Set<String> raidersAnimatingWater;
    private final Set<String> raidersWithWater;
    private final Set<String> raidersLostWater;
    private final Set<String> raidersWatering;
    private int lowWaterHitsplats;

    public CrondisPuzzleDetector() {
        waterFallTiles = new HashSet<>();
        palmTreeTiles = new HashSet<>();

        raidersAnimatingWater = new HashSet<>();
        raidersWithWater = new HashSet<>();
        raidersLostWater = new HashSet<>();
        raidersWatering = new HashSet<>();
    }

    @Override
    public void startup() {
        super.startup();
        computeTiles();
    }

    @Override
    public void cleanup() {
        waterFallTiles.clear();
        palmTreeTiles.clear();

        raidersAnimatingWater.clear();
        raidersWithWater.clear();
        raidersLostWater.clear();
        raidersWatering.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return CRONDIS_PUZZLE;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        // This needs to be done here, as the player's location hasn't been updated yet during HitsplatApplied-time.
        if (raidersAnimatingWater.contains(raider.getName())) {
            computeRaiderWatering(raider);
        }

        List<ToaMistake> mistakes = new ArrayList<>();

        if (raidersWatering.contains(raider.getName())) {
            if (lowWaterHitsplats > 0 && raidersLostWater.contains(raider.getName())) {
                mistakes.add(CRONDIS_PUZZLE_LOW_WATER);
                lowWaterHitsplats -= 1;
            }

            raidersWithWater.remove(raider.getName());
            raidersLostWater.remove(raider.getName());
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        raidersAnimatingWater.clear();
        raidersWatering.clear();
        lowWaterHitsplats = 0;
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event) {
        if (event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (raidState.isRaider(event.getActor()) &&
                raidersWithWater.contains(name) &&
                isDamageHitsplat(event.getHitsplat().getHitsplatType())) {
            raidersLostWater.add(name);
        } else if (event.getActor() instanceof NPC && PALM_TREE_NAME.equals(name) &&
                event.getHitsplat().getHitsplatType() == WATER_HITSPLAT_UP_ID &&
                event.getHitsplat().getAmount() < MAX_WATER_HITSPLAT_UP_AMOUNT) {
            lowWaterHitsplats += 1;
        }
    }

    @Subscribe
    public void onAnimationChanged(AnimationChanged event) {
        if (!(event.getActor() instanceof Player) ||
                event.getActor().getAnimation() != PLAYER_WATER_ANIMATION_ID ||
                !raidState.isRaider(event.getActor())) {
            return;
        }

        raidersAnimatingWater.add(event.getActor().getName());
    }

    private boolean isDamageHitsplat(int hitsplatType) {
        return hitsplatType == HitsplatID.DAMAGE_ME || hitsplatType == HitsplatID.DAMAGE_OTHER;
    }

    private void computeTiles() {
        WorldPoint wpPlayer = client.getLocalPlayer().getWorldLocation();
        LocalPoint lpPlayer = LocalPoint.fromWorld(client, wpPlayer);
        if (lpPlayer == null) return;

        int dx = lpPlayer.getSceneX() - wpPlayer.getRegionX();
        int dy = lpPlayer.getSceneY() - wpPlayer.getRegionY();

        waterFallTiles = WATERFALL_REGION_TILES.stream()
                .map(wp -> WorldPoint.fromScene(client, wp.getRegionX() + dx, wp.getRegionY() + dy, wp.getPlane()))
                .collect(Collectors.toSet());
        palmTreeTiles = PALM_TREE_REGION_TILES.stream()
                .map(wp -> WorldPoint.fromScene(client, wp.getRegionX() + dx, wp.getRegionY() + dy, wp.getPlane()))
                .collect(Collectors.toSet());
    }

    private void computeRaiderWatering(Raider raider) {
        if (waterFallTiles.contains(raider.getCurrentWorldLocation())) {
            raidersWithWater.add(raider.getName());
            raidersLostWater.remove(raider.getName());
        } else if (palmTreeTiles.contains(raider.getCurrentWorldLocation())) {
            raidersWatering.add(raider.getName());
        }
    }
}