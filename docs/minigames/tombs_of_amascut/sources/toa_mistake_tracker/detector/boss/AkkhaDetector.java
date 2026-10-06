package com.toamistaketracker.detector.boss;

import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import com.toamistaketracker.detector.tracker.AppliedHitsplatsTracker;
import com.toamistaketracker.detector.tracker.DelayedHitTilesTracker;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.GameObject;
import net.runelite.api.GraphicsObject;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicChanged;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.AKKHA;
import static com.toamistaketracker.ToaMistake.AKKHA_SPECIAL_ELEMENTAL_ORBS;
import static com.toamistaketracker.ToaMistake.AKKHA_SPECIAL_QUADRANT_BOMB;
import static com.toamistaketracker.ToaMistake.AKKHA_UNSTABLE_ORB;

/**
 * Akkha's memory/simon says special is the same attack as the hourglass special, which is also the same attack as
 * the explode special, and as such they are all counted as the same mistake. It's fairly straightforward, just looking
 * at if the graphics object spawned under the player's tile. ALL players on that tile are affected.
 *
 * UPDATE: As of 10/03/2022 Jagex recently changed Akkha's quadrant special behavior in the past week or two. Now, all
 * of the graphics objects spawn immediately, with a 1-tick delay. Seemingly this is to stop cheesing in the corners
 * where some of the orbs spawn 1-tick after others and you can "skip". We still need to account for the explode
 * special, as there is a 0-tick delay for those hitting the player.
 *
 * UPDATE: On 02/08/2023 an issue (#6) was filed that mentions a bug with Akkha's quadrant special detection. This may
 * have been introduced during the 10/03/2022 update, or was recently changed by Jagex. In any case, it's been fixed for
 * memory special.
 * The hourglass special graphics objects are misleading, though, as their animation plays out in
 * reverse, which in theory would make the inner tiles (up to distance 3 from center) the delayed ones. However, it
 * looks like in practice it's the same delayed tiles as the normal special, which means the outer tiles are on tick-1
 * and everything else is on tick-2. To account for this, there's a slight hack of subtracting a few cycles from the
 * startCycles.
 * Additionally, it looks like there is a 1-tick cooldown between when you can actually get hit by the quadrant special,
 * meaning if you mess up the skip and get "hit" two ticks in a row, the game doesn't actually give you a second
 * hitsplat, so we account for that in order to not double-count the mistake.
 *
 * For the elemental orbs special, those are detected through NPC despawn events, again checking the player's tile
 *
 * For the unstable orbs in the final phase, checking for despawn events isn't reliable as the NPC doesn't despawn until
 * a tick after it hits the player. Since a single orb can only
 * affect one player, we check for hitsplatApplied events to resolve which player took the damage from the orb, in the
 * case that multiple players are on the same tile.
 * Trying to find the correct local tile for a moving NPC is a real pain. By using the graphics update of the unstable
 * orb "popping", the server WorldPoint is 1 tile too far of where the player was standing, and the client's local
 * position mapped to WorldPoint is sometimes correct and sometimes 1 tile too early. Since the server's WorldPoint is
 * always consistent, we use that and subtract 1 in the direction of the client's local position in order to obtain
 * the tile that the orb was actually popped on, and compare with the player. Checking for despawn events isn't reliable
 * as the NPC doesn't despawn until a tick after it hits the player.
 */
@Slf4j
@Singleton
public class AkkhaDetector extends BaseMistakeDetector {

    private static final Set<Integer> FULL_HOURGLASS_GAME_OBJECT_IDS = ImmutableSet.of(46064, 46065, 46066, 46067);
    private static final Set<Integer> QUADRANT_BOMB_GRAPHICS_IDS = ImmutableSet.of(2256, 2257, 2258, 2259);
    private static final Set<String> ELEMENTAL_ORB_NAMES = ImmutableSet.of(
            "Orb of Lightning", "Orb of Darkness", "Burning Orb", "Frozen Orb");
    private static final String UNSTABLE_ORB_NAME = "Unstable Orb";
    private static final int UNSTABLE_ORB_POPPED_GRAPHICS_ID = 2260;

    private static final String AKKHA_NAME = "Akkha";
    private static final int HOURGLASS_SPECIAL_SUBTRACTED_CYCLES = 9; // 3 cycles per tile, up to 3 tiles away
    private static final int HOURGLASS_SPECIAL_HIT_DELAY_IN_TICKS = 1; // extra hit delay to offset, due to subtraction

    private static final int QUADRANT_EXPLODE_HIT_DELAY_IN_TICKS = 0;

    private boolean isAkkhaPresent;
    private final Set<String> raidersPreviouslyMadeQuadrantMistake = new HashSet<>();
    private final List<GameObject> activeFullHourglasses = new ArrayList<>();

    private final DelayedHitTilesTracker quadrantBombTiles = new DelayedHitTilesTracker();
    private final Set<WorldPoint> elementalOrbHitTiles;
    private final Set<WorldPoint> unstableOrbHitTiles;

    private final AppliedHitsplatsTracker appliedHitsplats;

    public AkkhaDetector() {
        elementalOrbHitTiles = new HashSet<>();
        unstableOrbHitTiles = new HashSet<>();
        appliedHitsplats = new AppliedHitsplatsTracker();
    }

    @Override
    public void cleanup() {
        isAkkhaPresent = false;
        activeFullHourglasses.clear();
        raidersPreviouslyMadeQuadrantMistake.clear();

        quadrantBombTiles.clear();
        elementalOrbHitTiles.clear();
        unstableOrbHitTiles.clear();
        appliedHitsplats.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return AKKHA;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (isQuadrantHit(raider)) {
            mistakes.add(AKKHA_SPECIAL_QUADRANT_BOMB);
            raidersPreviouslyMadeQuadrantMistake.add(raider.getName());
        } else {
            raidersPreviouslyMadeQuadrantMistake.remove(raider.getName());
        }

        if (elementalOrbHitTiles.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(AKKHA_SPECIAL_ELEMENTAL_ORBS);
        }

        if (unstableOrbHitTiles.contains(raider.getPreviousWorldLocation()) &&
                appliedHitsplats.popHitsplatApplied(raider.getName())) {
            mistakes.add(AKKHA_UNSTABLE_ORB);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        elementalOrbHitTiles.clear();
        unstableOrbHitTiles.clear();
        appliedHitsplats.clear();
    }

    @Subscribe
    public void onGameTick(GameTick event) {
        quadrantBombTiles.onGameTick(client.getTickCount());
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        if (!QUADRANT_BOMB_GRAPHICS_IDS.contains(event.getGraphicsObject().getId())) return;

        final int activationTick;
        if (isHourglassSpecial()) {
            // Hourglass special works differently. The animation is reversed but the hit tiles are *not* reversed,
            // they're inversed from the memory special. Instead of hardcoding the tiles, we can do some math.
            activationTick = getActivationTickWithSubtractedCycles(event.getGraphicsObject(),
                    HOURGLASS_SPECIAL_HIT_DELAY_IN_TICKS, HOURGLASS_SPECIAL_SUBTRACTED_CYCLES);
        } else {
            activationTick = getActivationTick(event.getGraphicsObject(), QUADRANT_EXPLODE_HIT_DELAY_IN_TICKS);
        }
        quadrantBombTiles.put(activationTick, getWorldPoint(event.getGraphicsObject()));
    }

    @Subscribe
    public void onGraphicChanged(GraphicChanged event) {
        if (!(event.getActor() instanceof NPC) || event.getActor().getName() == null) return;

        String name = Text.removeTags(event.getActor().getName());
        if (event.getActor() instanceof NPC &&
                UNSTABLE_ORB_NAME.equals(name) &&
                event.getActor().getGraphic() == UNSTABLE_ORB_POPPED_GRAPHICS_ID) {
            // We need to use the *actual* world location here, since the moving orb despawns too late and changes
            // graphics too early. Transform the world location 1 tile towards the client's local location.
            unstableOrbHitTiles.add(getWorldPointTransformedTowardsLocal(event.getActor()));
        }
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (isFullHourglass(event.getGameObject())) {
            activeFullHourglasses.add(event.getGameObject());
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event) {
        if (isFullHourglass(event.getGameObject())) {
            activeFullHourglasses.remove(event.getGameObject());
        }
    }

    private boolean isFullHourglass(GameObject gameObject) {
        return FULL_HOURGLASS_GAME_OBJECT_IDS.contains(gameObject.getId());
    }

    /**
     * Try to get the actor's WorldPoint, transformed towards the direction of the LocalPoint by 1 tile. This is to try
     * to retrieve the "previous" WorldPoint of a moving actor. It seems that the server-side WorldPoint is always
     * consistent, but the client's LocalPoint translated to WorldPoint can be off by 1 at times, but always before the
     * server's.
     *
     * @param actor The actor
     * @return The actor's WorldPoint that is 1 tile towards the client's local point
     */
    private WorldPoint getWorldPointTransformedTowardsLocal(Actor actor) {
        WorldPoint worldPoint = actor.getWorldLocation();
        WorldPoint worldPointFromLocal = getWorldPoint(actor);

        int distX = worldPoint.getX() - worldPointFromLocal.getX();
        int distY = worldPoint.getY() - worldPointFromLocal.getY();

        int dx = Integer.compare(0, distX);
        int dy = Integer.compare(0, distY);

        WorldPoint transformed = worldPoint.dx(dx).dy(dy);
        return transformed;
    }

    @Subscribe
    public void onNpcSpawned(NpcSpawned event) {
        if (event.getNpc().getName() == null) return;

        String name = Text.removeTags(event.getNpc().getName());
        if (AKKHA_NAME.equals(name)) {
            isAkkhaPresent = true;
        }
    }

    @Subscribe
    public void onNpcDespawned(NpcDespawned event) {
        if (event.getNpc().getName() == null) return;

        String name = Text.removeTags(event.getNpc().getName());
        if (ELEMENTAL_ORB_NAMES.contains(name)) {
            elementalOrbHitTiles.add(getWorldPoint(event.getActor()));
        } else if (AKKHA_NAME.equals(name)) {
            isAkkhaPresent = false;
        }
    }

    @Subscribe
    public void onHitsplatApplied(HitsplatApplied event) {
        if (raidState.isRaider(event.getActor())) {
            appliedHitsplats.addHitsplatForRaider(event.getActor().getName());
        }
    }

    public boolean isHourglassSpecial() {
        // If there's at least one full hourglass, *and* Akkha is around, it's an hourglass special.
        // The hourglass special cannot create its graphics objects while Akkha is not around (from what I've seen)
        // If Akkha despawns to do the memory special right when the hourglass fills up, the memory special happens
        // first and then once Akkha spawns back in, the hourglass special kicks off.
        return !activeFullHourglasses.isEmpty() && isAkkhaPresent;
    }

    private boolean isQuadrantHit(Raider raider) {
        return quadrantBombTiles.getActiveHitTiles().contains(raider.getPreviousWorldLocation()) &&
                !raidersPreviouslyMadeQuadrantMistake.contains(raider.getName());
    }

    private int getActivationTickWithSubtractedCycles(GraphicsObject graphicsObject,
                                                      int hitDelay,
                                                      int subtractedCycles) {
        int ticksToStart =
                (graphicsObject.getStartCycle() - client.getGameCycle() - subtractedCycles) / CYCLES_PER_GAME_TICK;
        return client.getTickCount() + ticksToStart + hitDelay;
    }
}
