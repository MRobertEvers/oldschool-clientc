package com.toamistaketracker.detector.boss;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import com.toamistaketracker.detector.tracker.DelayedHitTilesTracker;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.NPC;
import net.runelite.api.NPCComposition;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.events.NpcChanged;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.KEPHRI;
import static com.toamistaketracker.ToaMistake.KEPHRI_BOMB;

/**
 * The Kephri bomb, exploding scarabs, and exploding eggs all reuse the same set of graphics IDs on impact, though they
 * each behave differently. The Kephri bomb spawns a graphics object on *each* of its tiles (including the 3x3) on
 * impact, but the exploding scarab and the exploding egg only spawn it on the 1x1 hit tile. The exploding scarab still
 * deals damage in a 3x3 area if the Aerial Assault invocation is turned on. Therefore, using just GraphicsObjectCreated
 * events on the explosion won't suffice, so instead we use the shadows that spawn from the incoming bomb attacks.
 */
@Slf4j
@Singleton
public class KephriDetector extends BaseMistakeDetector {

    // Graphics ID -> tick delay
    private static final Map<Integer, Integer> KEPHRI_BOMB_SHADOW_GRAPHICS = ImmutableMap.of(
            1447, 4,
            1446, 3,
            2111, 2
    );

    private static final Set<Integer> KEPHRI_BOMB_GRAPHICS_ID = ImmutableSet.of(2156, 2157, 2158, 2159);
    private static final int SWARM_HEAL_ANIMATION_ID = 9607;
    private static final String SWARM_NAME = "Scarab Swarm";
    private static final int KEPHRI_BOMB_PROJECTILE_ID = 2266;
    private static final int EXPLODING_SCARAB_PROJECTILE_ID = 2147;

    private static final String KEPHRI_NAME = "Kephri";
    private static final Set<Integer> KEPHRI_PHASE_IDS = ImmutableSet.of(11719, 11720, 11721);
    private static final int KEPHRI_DEAD_ID = 11722;

    private int swarmsHealing = 0;

    private Actor kephri;
    private int kephriHealthInternal = -1;

    private static final int KEPHRI_UNVENGEABLE_PHASE = 11720;

    @Getter
    private final DelayedHitTilesTracker bombHitTiles = new DelayedHitTilesTracker();

    @Override
    public void cleanup() {
        swarmsHealing = 0;
        kephriHealthInternal = -1;
        kephri = null;
        bombHitTiles.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return KEPHRI;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        // Disabling Swarm mistakes for now, as currently it's not even possible to fully kill them all in solos, and
        // it can just be noisy.
//        for (int i = 0; i < swarmsHealing; i++) {
//            mistakes.add(KEPHRI_SWARM_HEAL);
//        }

        if (isBombHit(raider)) {
            mistakes.add(KEPHRI_BOMB);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        swarmsHealing = 0;
        kephri = null;
    }

    @Subscribe
    public void onGameTick(GameTick event) {
        if (kephri != null) {
            kephriHealthInternal = kephri.getHealthRatio();
        }
        bombHitTiles.onGameTick(client.getTickCount());
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        int id = event.getGraphicsObject().getId();
        if (KEPHRI_BOMB_SHADOW_GRAPHICS.containsKey(id)) {
            int activationTick = client.getTickCount() + KEPHRI_BOMB_SHADOW_GRAPHICS.get(id);
            bombHitTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
        }
    }

    @Subscribe
    public void onAnimationChanged(AnimationChanged event) {
        if (event.getActor() == null || event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (event.getActor() instanceof NPC && SWARM_NAME.equals(name) &&
                event.getActor().getAnimation() == SWARM_HEAL_ANIMATION_ID &&
                !event.getActor().isDead()) {
            swarmsHealing += 1;
        }
    }

    @Subscribe
    public void onNpcChanged(NpcChanged event) {
        if (!KEPHRI_NAME.equals(event.getNpc().getName())) return;

        if (isPhaseTransition(event.getOld(), event.getNpc().getComposition())) {
            kephriHealthInternal = -1; // something non-zero to initialize to
        } else if (event.getNpc().getId() == KEPHRI_DEAD_ID) {
            shutdown(); // Shut down and clean up all state. Any incoming bombs shouldn't count as mistakes.
        }
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event) {
        if (event.getActor() == null || event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (event.getActor() instanceof NPC && KEPHRI_NAME.equals(name)) {
            kephri = event.getActor();
        }
    }

    private boolean isBombHit(Raider raider) {
        if (!bombHitTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation())) {
            return false;
        }

        if (kephriHealthInternal == 0) {
            // Kephri bomb for some reason can't do damage when her health is 0, until she phase transitions in
            // which we update this to be non-zero and the next hitsplat corrects it.
            return false;
        }

        // Vengeance only counts for phases that allow it (non-swarm phase)
        if (vengeanceTracker.didPopVengeance(raider) && !isUnvengeablePhase()) {
            return false;
        }

        return true;
    }

    private boolean isUnvengeablePhase() {
        return client.getNpcs().stream().anyMatch(npc -> npc.getId() == KEPHRI_UNVENGEABLE_PHASE);
    }

    private boolean isPhaseTransition(NPCComposition oldComp, NPCComposition newComp) {
        return KEPHRI_PHASE_IDS.contains(oldComp.getId()) &&
                KEPHRI_PHASE_IDS.contains(newComp.getId()) &&
                oldComp.getId() != newComp.getId();
    }
}