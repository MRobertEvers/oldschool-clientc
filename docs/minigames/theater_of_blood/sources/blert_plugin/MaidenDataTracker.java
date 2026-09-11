/*
 * Copyright (c) 2023-2024 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the “Software”), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.challenges.tob.rooms.maiden;

import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.Location;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.BasicTrackedNpc;
import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import io.blert.core.TrackedNpc;
import io.blert.events.NpcAttackEvent;
import io.blert.events.tob.MaidenBloodSplatsEvent;
import io.blert.events.tob.MaidenCrabLeakEvent;
import java.util.*;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;

@Slf4j
public class MaidenDataTracker extends RoomDataTracker {
    private static final int MAIDEN_BLOODSPLAT_GRAPHIC_ID = 1579;
    private static final int MAIDEN_BLOOD_TRAIL_OBJECT_ID = 32984;

    private static final int MAIDEN_BLOOD_THROW_ANIMATION = 8091;
    private static final int MAIDEN_AUTO_ANIMATION = 8092;

    private CrabSpawn currentSpawn = CrabSpawn.SEVENTIES;
    private boolean crabsSpawnedThisTick = false;

    private @Nullable HpVarbitTrackedNpc maiden;
    private @Nullable NpcAttack attackThisTick = null;

    private final Set<GameObject> bloodTrails = new HashSet<>();
    private final Map<Integer, MaidenCrab> crabs = new HashMap<>();

    public MaidenDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.MAIDEN, true);
    }

    @Override
    protected void onRoomStart() {
        if (maiden != null) {
            theatreChallenge.updateMode(maiden.getMode());
        }
    }

    @Override
    protected void onTick() {
        super.onTick();

        List<WorldPoint> bloodSplats = new ArrayList<>();

        // Search for active blood splats thrown by Maiden, and report them if they exist.
        for (GraphicsObject object : client.getTopLevelWorldView().getGraphicsObjects()) {
            WorldPoint point = WorldPoint.fromLocalInstance(client, object.getLocation());
            if (Location.fromWorldPoint(point).inMaiden() && object.getId() == MAIDEN_BLOODSPLAT_GRAPHIC_ID) {
                bloodSplats.add(point);
            }
        }

        // Add any blood trails left by blood spawns to the list. In the future, this may be a separate event.
        for (GameObject trail : bloodTrails) {
            bloodSplats.add(getWorldLocation(trail));
        }

        final int tick = getTick();

        if (!bloodSplats.isEmpty()) {
            dispatchEvent(new MaidenBloodSplatsEvent(tick, bloodSplats));
        }

        if (maiden == null) {
            return;
        }

        if (crabsSpawnedThisTick) {
            markNewSpawn(tick);
        }

        for (MaidenCrab crab : crabs.values()) {
            NPC crabNpc = crab.getNpc();

            if (!crabNpc.isDead()
                    && crabNpc.getWorldArea().distanceTo2D(maiden.getNpc().getWorldArea()) <= 1) {
                crab.setUpdatedProperties(true);
                dispatchEvent(new MaidenCrabLeakEvent(tick, getWorldLocation(crabNpc), crab));
            }
        }

        if (attackThisTick != null) {
            dispatchEvent(
                    new NpcAttackEvent(getStage(), tick, getWorldLocation(maiden.getNpc()), attackThisTick, maiden));
        }

        crabsSpawnedThisTick = false;
        attackThisTick = null;
    }

    @Override
    protected void onGameObjectSpawn(GameObjectSpawned spawned) {
        // The blood trails left by blood spawns are game objects with stable hash codes, so store them in a set when
        // they spawn and remove them on de-spawn. The `onTick` handler will dispatch events with the active set of
        // blood trails.
        GameObject object = spawned.getGameObject();
        if (object.getId() != MAIDEN_BLOOD_TRAIL_OBJECT_ID) {
            return;
        }

        if (Location.fromWorldPoint(getWorldLocation(object)).inMaiden()) {
            bloodTrails.add(object);
        }
    }

    @Override
    protected void onGameObjectDespawn(GameObjectDespawned despawned) {
        GameObject object = despawned.getGameObject();
        if (object.getId() == MAIDEN_BLOOD_TRAIL_OBJECT_ID) {
            bloodTrails.remove(object);
        }
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned spawned) {
        NPC npc = spawned.getNpc();
        return TobNpc.withId(npc.getId()).flatMap(tobNpc -> {
            if (TobNpc.isMaiden(tobNpc.getId())) {
                if (maiden == null) {
                    startRoom();
                    maiden = new HpVarbitTrackedNpc(
                            npc,
                            tobNpc,
                            generateRoomId(npc),
                            new Hitpoints(tobNpc.getBaseHitpoints(theatreChallenge.getScale())));
                }
                return Optional.of(maiden);
            }

            if (TobNpc.isMaidenMatomenos(tobNpc.getId())) {
                return handleMaidenCrabSpawn(npc);
            }

            if (TobNpc.isMaidenBloodSpawn(tobNpc.getId())) {
                return handleMaidenBloodSpawnSpawn(npc);
            }

            return Optional.empty();
        });
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned despawned, TrackedNpc trackedNpc) {
        NPC npc = despawned.getNpc();
        crabs.remove(npc.getIndex());
        // Every maiden NPC despawn is final.
        return true;
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        if (maiden == null || actor != maiden.getNpc()) {
            return;
        }

        switch (actor.getAnimation()) {
            case MAIDEN_BLOOD_THROW_ANIMATION:
                attackThisTick = NpcAttack.TOB_MAIDEN_BLOOD_THROW;
                break;
            case MAIDEN_AUTO_ANIMATION:
                attackThisTick = NpcAttack.TOB_MAIDEN_AUTO;
                break;
            default:
        }
    }

    private Optional<TrackedNpc> handleMaidenBloodSpawnSpawn(NPC npc) {
        return TobNpc.withId(npc.getId())
                .map(tobNpc -> new BasicTrackedNpc(
                        npc,
                        tobNpc,
                        generateRoomId(npc),
                        new Hitpoints(tobNpc.getBaseHitpoints(theatreChallenge.getScale()))));
    }

    private Optional<MaidenCrab> handleMaidenCrabSpawn(NPC npc) {
        crabsSpawnedThisTick = true;

        WorldPoint spawnLocation = getWorldLocation(npc);

        Optional<MaidenCrab> maybeCrab = MaidenCrab.fromSpawnLocation(
                theatreChallenge.getScale(), npc, generateRoomId(npc), currentSpawn, spawnLocation);
        if (maybeCrab.isPresent()) {
            MaidenCrab maidenCrab = maybeCrab.get();
            log.debug("Crab position: {} scuffed: {}", maidenCrab.getPosition(), maidenCrab.isScuffed());
            crabs.put(npc.getIndex(), maidenCrab);
        }

        return maybeCrab;
    }

    private void markNewSpawn(int tick) {
        if (maiden != null) {
            // Correct the crab spawn based on Maiden's NPC ID in case the room
            // was started late.
            int idOffset = maiden.getNpc().getId() - maiden.getTobNpc().getId();
            if (idOffset < 2) {
                currentSpawn = CrabSpawn.SEVENTIES;
            } else if (idOffset == 2) {
                currentSpawn = CrabSpawn.FIFTIES;
            } else {
                currentSpawn = CrabSpawn.THIRTIES;
            }
        }

        log.debug("Maiden {} spawned on tick {} ({})", currentSpawn, tick, formattedRoomTime());

        crabs.values().stream()
                .filter(crab -> crab.getSpawnTick() == tick)
                .forEach(crab -> crab.setSpawn(currentSpawn));
    }
}
