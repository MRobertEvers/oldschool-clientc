package com.toamistaketracker.detector.boss;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import com.toamistaketracker.detector.tracker.DelayedHitTilesTracker;
import com.toamistaketracker.detector.tracker.InstantHitTilesTracker;
import com.toamistaketracker.detector.tracker.OverheadTracker;
import lombok.Getter;
import lombok.NonNull;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Animation;
import net.runelite.api.DynamicObject;
import net.runelite.api.GameObject;
import net.runelite.api.HeadIcon;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.ProjectileMoved;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.WARDENS_P1_P2;
import static com.toamistaketracker.ToaMistake.WARDENS_P1_PYRAMID;
import static com.toamistaketracker.ToaMistake.WARDENS_P2_BIND;
import static com.toamistaketracker.ToaMistake.WARDENS_P2_BOMBS;
import static com.toamistaketracker.ToaMistake.WARDENS_P2_DDR;
import static com.toamistaketracker.ToaMistake.WARDENS_P2_SPECIAL_PRAYER;
import static com.toamistaketracker.ToaMistake.WARDENS_P2_WINDMILL;

/**
 * The pyramids are GameObjects that change their animations through changing their Renderables. Since we can't
 * listen for an AnimationChanged event for GameObjects, we manually detect when the animation is the one that hurts
 * the player. Also, the pyramid animation starts 1 tick earlier and ends 1 tick earlier than when the player actually
 * takes damage, so we delay the detection by a tick.
 *
 * DDR tiles spawn all of their graphics objects at once and delay their activation for some set number of ticks. We
 * calculate that and delay the hit tile activations with the new {@link DelayedHitTilesTracker}. This means we only
 * need to check for the player standing on the hit tile once the activation tick is played.
 *
 * The Bombs are the same, as the outer ring activates one tile later than the inner ones, using graphics objects to
 * detect
 *
 * The windmill attack is similar, but activates instantly on the current game tick. For that, we use the aptly named
 * {@link InstantHitTilesTracker} which is also a new class that helps track these hit tiles and when they should
 * activate. Once again graphics objects are used for detection here.
 *
 * Sometimes the obelisk can be in different phases but use an attack from a separate special. For example, the DDR
 * phase starts with graphics ids that are the same as the windmill, but we should count that as screwing up the DDR
 * phase not the windmill phase, and as such we detect the obelisk phase to account for that.
 */
@Slf4j
@Singleton
public class WardensP1P2Detector extends BaseMistakeDetector {

    // P1 constants
    private static final int RED_PYRAMID_GAME_OBJECT_ID = 45750;
    private static final int YELLOW_PYRAMID_GAME_OBJECT_ID = 45751;
    private static final int PYRAMID_ACTIVE_ANIMATION_ID = 9524;
    private final int PYRAMID_HIT_DELAY_IN_TICKS = 1;
    private static final int DEATH_DOT_PROJECTILE_ID = 2237;
    private static final int DISPERSE_PROJECTILE_ID = 2238;

    // P2 constants
    private static final String OBELISK_NAME = "Obelisk";
    private static final int DDR_GRAPHICS_ID = 2235;
    private static final int WINDMILL_SHADOW_GRAPHICS_ID = 2236;
    private static final int WINDMILL_HIT_GRAPHICS_ID = 2234;
    private static final int BOMB_GRAPHICS_ID = 2198;
    private static final int DDR_HIT_DELAY_IN_TICKS = 1;
    private static final int LIGHTNING_HIT_DELAY_IN_TICKS = 0;
    private static final int OBELISK_DDR_ANIMATION_ID = 9732;
    private static final int OBELISK_WINDMILL_ANIMATION_ID = 9733;
    private static final int OBELISK_BOMB_ANIMATION_ID = 9727;
    private static final int OBELISK_DEATH_ANIMATION_ID = 9734;
    private static final int OBELISK_DDR_LIGHTNING_GRAPHICS_ID = 2199;
    private static final int OBELISK_WINDMILL_LIGHTNING_GRAPHICS_ID = 2200;
    private static final int PLAYER_BIND_ANIMATION_ID = 9714;
    // Projectile ID -> correct overhead icon
    private static final Map<Integer, HeadIcon> SPECIAL_PRAYER_ATTACKS = ImmutableMap.of(
            2204, HeadIcon.MELEE,
            2206, HeadIcon.RANGED,
            2208, HeadIcon.MAGIC
    );
    private static final Set<Integer> WARDENS_HEALTH_PHASE = ImmutableSet.of(11755, 11758);

    @RequiredArgsConstructor
    enum ObeliskPhase {
        DDR(OBELISK_DDR_ANIMATION_ID),
        WINDMILL(OBELISK_WINDMILL_ANIMATION_ID),
        BOMBS(OBELISK_BOMB_ANIMATION_ID),
        DEATH(OBELISK_DEATH_ANIMATION_ID);

        @NonNull
        @Getter
        private final Integer animationId;

        static ObeliskPhase fromAnimationId(int animationId) {
            for (ObeliskPhase obeliskPhase : ObeliskPhase.values()) {
                if (obeliskPhase.getAnimationId() == animationId) {
                    return obeliskPhase;
                }
            }

            return null;
        }
    }

    // P1 fields
    @Getter
    private final List<GameObject> activePyramids = new ArrayList<>();
    @Getter
    private final DelayedHitTilesTracker pyramidHitTiles = new DelayedHitTilesTracker();

    // P2 fields
    private ObeliskPhase obeliskPhase;
    @Getter
    private final DelayedHitTilesTracker ddrHitTiles = new DelayedHitTilesTracker();
    @Getter
    private final InstantHitTilesTracker windmillHitTiles = new InstantHitTilesTracker();
    @Getter
    private final DelayedHitTilesTracker bombHitTiles = new DelayedHitTilesTracker();
    private final Set<String> raidersBound = new HashSet<>();

    private final OverheadTracker specialPrayerOverheadTracker = new OverheadTracker(SPECIAL_PRAYER_ATTACKS);

    @Override
    public void cleanup() {
        activePyramids.clear();
        pyramidHitTiles.clear();

        obeliskPhase = null;
        ddrHitTiles.clear();
        windmillHitTiles.clear();
        bombHitTiles.clear();
        raidersBound.clear();
        specialPrayerOverheadTracker.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return WARDENS_P1_P2;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (pyramidHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P1_PYRAMID);
        }

        if (ddrHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P2_DDR);
        }

        if (windmillHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P2_WINDMILL);
        }

        if (bombHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            mistakes.add(WARDENS_P2_BOMBS);
        }

        if (raidersBound.contains(raider.getName())) {
            mistakes.add(WARDENS_P2_BIND);
        }

        if (isSpecialPrayerHit(raider)) {
            mistakes.add(WARDENS_P2_SPECIAL_PRAYER);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        raidersBound.clear();
    }

    @Subscribe
    public void onAnimationChanged(AnimationChanged event) {
        if (event.getActor() == null || event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (event.getActor() instanceof NPC && OBELISK_NAME.equals(name)) {
            ObeliskPhase newObeliskPhase = ObeliskPhase.fromAnimationId(event.getActor().getAnimation());
            if (newObeliskPhase != null) {
                obeliskPhase = newObeliskPhase;
            }
        } else if (event.getActor() instanceof Player && raidState.getRaiders().containsKey(name)) {
            if (event.getActor().getAnimation() == PLAYER_BIND_ANIMATION_ID) {
                raidersBound.add(name);
            }
        }
    }

    @Subscribe
    public void onGameTick(GameTick event) {
        computePyramidHitTiles();
        pyramidHitTiles.onGameTick(client.getTickCount());

        ddrHitTiles.onGameTick(client.getTickCount());
        windmillHitTiles.onGameTick(client.getTickCount());
        bombHitTiles.onGameTick(client.getTickCount());
        specialPrayerOverheadTracker.onGameTick(client.getTickCount());
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (isPyramid(event.getGameObject())) {
            activePyramids.add(event.getGameObject());
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event) {
        if (isPyramid(event.getGameObject())) {
            activePyramids.remove(event.getGameObject());
        }
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        int activationTick;
        switch (event.getGraphicsObject().getId()) {
            case DDR_GRAPHICS_ID:
                activationTick = getActivationTick(event.getGraphicsObject(), DDR_HIT_DELAY_IN_TICKS);
                ddrHitTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
                break;
            case OBELISK_DDR_LIGHTNING_GRAPHICS_ID:
                activationTick = getActivationTick(event.getGraphicsObject(), LIGHTNING_HIT_DELAY_IN_TICKS);
                ddrHitTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
                break;
            case WINDMILL_HIT_GRAPHICS_ID:
                if (obeliskPhase == ObeliskPhase.DDR) {
                    ddrHitTiles.put(client.getTickCount(), getWorldPoint(event.getGraphicsObject()));
                } else if (obeliskPhase == ObeliskPhase.WINDMILL) {
                    windmillHitTiles.add(getWorldPoint(event.getGraphicsObject()));
                }
                break;
            case OBELISK_WINDMILL_LIGHTNING_GRAPHICS_ID:
                windmillHitTiles.add(getWorldPoint(event.getGraphicsObject()));
                break;
            case BOMB_GRAPHICS_ID:
                if (obeliskPhase == ObeliskPhase.BOMBS) {
                    activationTick = getActivationTick(event.getGraphicsObject(), LIGHTNING_HIT_DELAY_IN_TICKS);
                    bombHitTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
                }
                break;
        }
    }

    @Subscribe
    public void onProjectileMoved(ProjectileMoved event) {
        if (!SPECIAL_PRAYER_ATTACKS.containsKey(event.getProjectile().getId())) return;

        specialPrayerOverheadTracker.trackProjectile(event, getActivationTick(event.getProjectile()));
    }

    @Subscribe
    public void onNpcChanged(NpcChanged event) {
        if (WARDENS_HEALTH_PHASE.contains(event.getNpc().getId())) {
            // Wardens changed to health phase with core. DDR special can no longer deal damage here as of 09/21/2022
            ddrHitTiles.clear();
            // Clear all other sources of damage too just in case
            windmillHitTiles.clear();
            bombHitTiles.clear();
            specialPrayerOverheadTracker.clear();
        }
    }

    private boolean isSpecialPrayerHit(Raider raider) {
        if (vengeanceTracker.didPopVengeance(raider)) {
            return false;
        }

        return specialPrayerOverheadTracker.didMissPrayer(raider);
    }

    private void computePyramidHitTiles() {
        activePyramids.forEach(pyramid -> {
            if (pyramid.getRenderable() instanceof DynamicObject) {
                Animation animation = ((DynamicObject) pyramid.getRenderable()).getAnimation();
                if (animation != null && animation.getId() == PYRAMID_ACTIVE_ANIMATION_ID) {
                    int activationTick = client.getTickCount() + PYRAMID_HIT_DELAY_IN_TICKS;
                    pyramidHitTiles.putAll(activationTick, compute3By3TilesFromCenter(pyramid.getWorldLocation()));
                }
            }
        });
    }

    private boolean isPyramid(GameObject gameObject) {
        return gameObject.getId() == RED_PYRAMID_GAME_OBJECT_ID || gameObject.getId() == YELLOW_PYRAMID_GAME_OBJECT_ID;
    }
}