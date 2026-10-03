package com.toamistaketracker.detector.puzzle;

import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.tracker.AppliedHitsplatsTracker;
import com.toamistaketracker.detector.BaseMistakeDetector;
import com.toamistaketracker.detector.tracker.DelayedMistakeTracker;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.client.eventbus.Subscribe;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.HET_PUZZLE;
import static com.toamistaketracker.ToaMistake.HET_PUZZLE_DARK_ORB;
import static com.toamistaketracker.ToaMistake.HET_PUZZLE_LIGHT;

/**
 * There are a few ways to detect an orb of darkness hitting a player. The easiest is probably to check if a graphics
 * ID of 379 spawns under the player's location. If multiple players are standing on the same location, it seems like
 * only one of them gets hit. Currently, we try to resolve by checking to see which players also had a hitsplat applied
 * to them on that same tick. This can obviously be bypassed, like with locator orb, or by both players standing and
 * taking a light attack on that same tick, but that should be pretty rare and there doesn't seem to be a great way
 * to resolve that otherwise.
 *
 * For detecting the light beam, we can see if the graphics ids are spawned on the player's tile. ALL players standing
 * in the light's path should be hit, so we don't need to resolve to a single player. One other thing is to delay the
 * mistake by one tick, as technically the damage doesn't come in until one tick after the light spawns, for some reason
 */
@Slf4j
@Singleton
public class HetPuzzleDetector extends BaseMistakeDetector {

    private static final Set<Integer> LIGHT_BEAM_GRAPHICS_IDS = ImmutableSet.of(2064, 2114);
    private static final int ORB_OF_DARKNESS_GRAPHICS_ID = 379;

    private final Set<WorldPoint> orbHitTiles;
    private final Set<WorldPoint> lightHitTiles;

    private final AppliedHitsplatsTracker appliedHitsplats;
    private final DelayedMistakeTracker delayedMistakes;

    public HetPuzzleDetector() {
        orbHitTiles = new HashSet<>();
        lightHitTiles = new HashSet<>();

        appliedHitsplats = new AppliedHitsplatsTracker();
        delayedMistakes = new DelayedMistakeTracker();
    }

    @Override
    public void cleanup() {
        orbHitTiles.clear();
        lightHitTiles.clear();
        appliedHitsplats.clear();
        delayedMistakes.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return HET_PUZZLE;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (orbHitTiles.contains(raider.getPreviousWorldLocation()) &&
                appliedHitsplats.popHitsplatApplied(raider.getName())) {
            mistakes.add(HET_PUZZLE_DARK_ORB);
        }

        if (lightHitTiles.contains(raider.getPreviousWorldLocation())) {
            // Delay this mistake until 1 tick, since it takes 1 tick for the hitsplat to show up
            delayedMistakes.addDelayedMistake(raider.getName(), HET_PUZZLE_LIGHT, client.getTickCount(), 1);
        }

        // Add any delayed mistakes from previous ticks
        mistakes.addAll(delayedMistakes.popDelayedMistakes(raider.getName(), client.getTickCount()));

        return mistakes;
    }

    @Override
    public void afterDetect() {
        orbHitTiles.clear();
        lightHitTiles.clear();
        appliedHitsplats.clear();
        // Don't clear delayedMistakes on afterDetect, since we need it to persist across ticks.
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        if (event.getGraphicsObject().getId() == ORB_OF_DARKNESS_GRAPHICS_ID) {
            orbHitTiles.add(getWorldPoint(event.getGraphicsObject()));
        } else if (LIGHT_BEAM_GRAPHICS_IDS.contains(event.getGraphicsObject().getId())) {
            lightHitTiles.add(getWorldPoint(event.getGraphicsObject()));
        }
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event) {
        if (raidState.isRaider(event.getActor())) {
            appliedHitsplats.addHitsplatForRaider(event.getActor().getName());
        }
    }
}
