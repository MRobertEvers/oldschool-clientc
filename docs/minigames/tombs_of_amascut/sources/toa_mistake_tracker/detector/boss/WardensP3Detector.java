package com.toamistaketracker.detector.boss;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import com.toamistaketracker.detector.tracker.DelayedHitTilesTracker;
import com.toamistaketracker.detector.tracker.DelayedMistakeTracker;
import com.toamistaketracker.detector.tracker.InstantHitTilesTracker;
import com.toamistaketracker.detector.tracker.OverheadTracker;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.HeadIcon;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.ProjectileMoved;
import net.runelite.client.eventbus.Subscribe;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.WARDENS_P3;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_AKKHA;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_BABA;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_EARTHQUAKE;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_KEPHRI;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_LIGHTNING;
import static com.toamistaketracker.ToaMistake.WARDENS_P3_ZEBAK;

/**
 * Wardens P3 is actually one of the most straightforward rooms, especially now that I've written all of the other
 * detectors, trackers, etc. and am more familiar with the detection flow.
 *
 * Earthquakes are just graphics objects that start their animation at some point in the future, and so we make use of
 * our {@link DelayedHitTilesTracker} to track the tiles that activate after a delay.
 *
 * Kephri bombs create a graphics object on the tick that they detonate, so we just look for that
 *
 * Ba-Ba boulders are similar to how they work in their boss room. We delay the graphics object spawn by a set number
 * of ticks before detecting
 *
 * Finally, the lightning attack is similar to Kephri bombs in that it spawns a graphics object on the tick it detonates
 *
 * Also lastly added, overhead tracker for Akkha and Zebak to give a mistake if a prayer is missed. Since this isn't
 * the crux of the fight, and the punish has already happened (unless you tanked), then this is not considered cheaty.
 * Since the overhead detection on the server happens the same tick that the projectile, we calculate the mistake
 * right away but delay it for the remaining cycles of the projectile. Also, I ended up deciding to just not announce
 * this mistake at all anyway, and only put it in the tracker panel, so that it can't be used to know to switch prayers
 * as easily.
 */
@Slf4j
@Singleton
public class WardensP3Detector extends BaseMistakeDetector {

    private final Set<Integer> EARTHQUAKE_GRAPHICS_IDS = ImmutableSet.of(2220, 2221, 2222, 2223);
    private final Set<Integer> KEPHRI_BOMB_GRAPHICS_IDS = ImmutableSet.of(2156, 2157, 2158, 2159);
    // Projectile ID -> correct overhead icon
    private static final Map<Integer, HeadIcon> AKKHA_ATTACKS = ImmutableMap.of(
            2253, HeadIcon.MAGIC,
            2255, HeadIcon.RANGED
    );
    // Projectile ID -> correct overhead icon
    private static final Map<Integer, HeadIcon> ZEBAK_ATTACKS = ImmutableMap.of(
            2181, HeadIcon.MAGIC,
            2187, HeadIcon.RANGED
    );

    // Graphics ID -> tick delay
    private final Map<Integer, Integer> BABA_BOULDERS = ImmutableMap.of(
            2250, 6,
            2251, 4
    );

    private static final int EARTHQUAKE_HIT_DELAY_IN_TICKS = 0;
    private static final int P3_LIGHTNING_GRAPHICS_ID = 2197;

    @Getter
    private final DelayedHitTilesTracker earthquakeHitTiles = new DelayedHitTilesTracker();
    @Getter
    private final InstantHitTilesTracker kephriBombHitTiles = new InstantHitTilesTracker();
    @Getter
    private final DelayedHitTilesTracker babaBoulderTiles = new DelayedHitTilesTracker();
    @Getter
    private final InstantHitTilesTracker lightningHitTiles = new InstantHitTilesTracker();

    private final OverheadTracker akkhaOverheadTracker = new OverheadTracker(AKKHA_ATTACKS);
    private final OverheadTracker zebakOverheadTracker = new OverheadTracker(ZEBAK_ATTACKS);
    private final DelayedMistakeTracker delayedMistakeTracker = new DelayedMistakeTracker();

    @Override
    public void cleanup() {
        earthquakeHitTiles.clear();
        kephriBombHitTiles.clear();
        babaBoulderTiles.clear();
        lightningHitTiles.clear();
        akkhaOverheadTracker.clear();
        zebakOverheadTracker.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return WARDENS_P3;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (earthquakeHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P3_EARTHQUAKE);
        }

        if (kephriBombHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P3_KEPHRI);
        }

        if (babaBoulderTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P3_BABA);
        }

        if (lightningHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P3_LIGHTNING);
        }

        mistakes.addAll(delayedMistakeTracker.popDelayedMistakes(raider.getName(), client.getTickCount()));

        if (akkhaOverheadTracker.didMissPrayer(raider)) {
            delayedMistakeTracker.addDelayedMistake(raider.getName(),
                    WARDENS_P3_AKKHA,
                    client.getTickCount(),
                    getActivationTick(akkhaOverheadTracker.getActiveProjectileForRaider(raider)) -
                            client.getTickCount());
        }

        if (zebakOverheadTracker.didMissPrayer(raider)) {
            delayedMistakeTracker.addDelayedMistake(raider.getName(),
                    WARDENS_P3_ZEBAK,
                    client.getTickCount(),
                    getActivationTick(zebakOverheadTracker.getActiveProjectileForRaider(raider)) -
                            client.getTickCount());
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
    }

    @Subscribe
    public void onGameTick(GameTick event) {
        earthquakeHitTiles.onGameTick(client.getTickCount());
        kephriBombHitTiles.onGameTick(client.getTickCount());
        babaBoulderTiles.onGameTick(client.getTickCount());
        lightningHitTiles.onGameTick(client.getTickCount());
        akkhaOverheadTracker.onGameTick(client.getTickCount());
        zebakOverheadTracker.onGameTick(client.getTickCount());
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        int id = event.getGraphicsObject().getId();
        if (EARTHQUAKE_GRAPHICS_IDS.contains(id)) {
            int activationTick = getActivationTick(event.getGraphicsObject(), EARTHQUAKE_HIT_DELAY_IN_TICKS);
            earthquakeHitTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
        } else if (KEPHRI_BOMB_GRAPHICS_IDS.contains(id)) {
            kephriBombHitTiles.add(getWorldPoint(event.getGraphicsObject()));
        } else if (BABA_BOULDERS.containsKey(id)) {
            int activationTick = getActivationTick(event.getGraphicsObject(), BABA_BOULDERS.get(id));
            babaBoulderTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
        } else if (id == P3_LIGHTNING_GRAPHICS_ID) {
            lightningHitTiles.add(getWorldPoint(event.getGraphicsObject()));
        }
    }

    @Subscribe
    public void onProjectileMoved(ProjectileMoved event) {
        if (!AKKHA_ATTACKS.containsKey(event.getProjectile().getId()) &&
                !ZEBAK_ATTACKS.containsKey(event.getProjectile().getId())) {
            return;
        }

        // Akkha prayer matters on spawn tick
        int activationTick = client.getTickCount();
        akkhaOverheadTracker.trackProjectile(event, activationTick);

        // Zebak prayer matters on projectile hit (3 ticks later)
        activationTick = getActivationTick(event.getProjectile());
        zebakOverheadTracker.trackProjectile(event, activationTick);
    }
}