/*
 * Copyright (c) 2024 Alexei Frolov
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

package io.blert.challenges.tob.rooms.bloat;

import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import io.blert.core.TrackedNpc;
import io.blert.events.NpcAttackEvent;
import io.blert.events.tob.BloatDownEvent;
import io.blert.events.tob.BloatHandsEvent;
import io.blert.events.tob.BloatUpEvent;
import io.blert.util.Location;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.GraphicsObject;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;

@Slf4j
public class BloatDataTracker extends RoomDataTracker {
    private static final int BLOAT_DOWN_ANIMATION = 8082;
    private static final int BLOAT_DOWN_CYCLE_TICKS = 32;
    private static final int BLOAT_STOMP_TICK = 3;

    private static final int BLOAT_HANDS_GRAPHICS_START_ID = 1570;
    private static final int BLOAT_HANDS_GRAPHICS_END_ID = 1573;
    private static final int BLOAT_SPLAT_GRAPHIC_ID = 1576;

    private enum State {
        WALKING,
        DOWN,
    }

    private State state;
    private HpVarbitTrackedNpc bloat;

    private int currentDown;
    private int currentDownTick;
    private int lastDownTick;
    private int lastUpTick;
    private int nextUpTick;

    private final List<WorldPoint> fallingHands = new ArrayList<>(16);
    private final List<WorldPoint> splatHands = new ArrayList<>(16);

    public BloatDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.BLOAT, true);
        state = State.WALKING;
        currentDown = 0;
        lastDownTick = -1;
        lastUpTick = -1;
        nextUpTick = -1;
        bloat = null;
    }

    @Override
    protected void onRoomStart() {
        if (bloat == null) {
            client.getTopLevelWorldView().npcs().stream()
                    .filter(npc -> TobNpc.isBloat(npc.getId()))
                    .findFirst()
                    .ifPresent(npc -> {
                        TobNpc tobNpc = TobNpc.withId(npc.getId()).orElseThrow();
                        bloat = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                        addTrackedNpc(bloat);
                    });
        }

        if (bloat != null && bloat.getNpc().getAnimation() == BLOAT_DOWN_ANIMATION) {
            state = State.DOWN;
        } else {
            state = State.WALKING;
        }
    }

    @Override
    protected void onTick() {
        super.onTick();
        final int tick = getTick();

        if (state == State.DOWN) {
            if (lastDownTick == tick) {
                currentDownTick = BLOAT_DOWN_CYCLE_TICKS;
            } else {
                currentDownTick--;
            }

            if (tick == nextUpTick) {
                handleBloatUp(bloat.getNpc(), tick);
            } else if (currentDownTick == BLOAT_STOMP_TICK && bloat != null) {
                WorldPoint point = getWorldLocation(bloat);
                dispatchEvent(new NpcAttackEvent(getStage(), tick, point, NpcAttack.TOB_BLOAT_STOMP, bloat));
            }
        }

        if (!fallingHands.isEmpty()) {
            dispatchEvent(BloatHandsEvent.drop(tick, new ArrayList<>(fallingHands)));
            fallingHands.clear();
        }
        if (!splatHands.isEmpty()) {
            dispatchEvent(BloatHandsEvent.splat(tick, new ArrayList<>(splatHands)));
            splatHands.clear();
        }
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        NPC npc = event.getNpc();

        return TobNpc.withId(npc.getId())
                .filter(tobNpc -> TobNpc.isBloat(tobNpc.getId()))
                .map(tobNpc -> {
                    if (bloat == null) {
                        bloat = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                    }
                    return bloat;
                });
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, TrackedNpc trackedNpc) {
        return trackedNpc == bloat;
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        if (!(actor instanceof NPC)) {
            return;
        }

        NPC npc = (NPC) actor;
        if (!TobNpc.isBloat(npc.getId())) {
            return;
        }

        final int tick = getTick();
        if (npc.getAnimation() == BLOAT_DOWN_ANIMATION) {
            handleBloatDown(npc, tick);
        } else if (state == State.DOWN && npc.getAnimation() == -1) {
            handleBloatUp(npc, tick);
        }
    }

    @Override
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {
        GraphicsObject object = event.getGraphicsObject();
        if (object.getId() >= BLOAT_HANDS_GRAPHICS_START_ID && object.getId() <= BLOAT_HANDS_GRAPHICS_END_ID) {
            WorldPoint point = Location.getWorldLocation(client, WorldPoint.fromLocal(client, object.getLocation()));
            fallingHands.add(point);
        }
        if (object.getId() == BLOAT_SPLAT_GRAPHIC_ID) {
            WorldPoint point = Location.getWorldLocation(client, WorldPoint.fromLocal(client, object.getLocation()));
            splatHands.add(point);
        }
    }

    private void handleBloatDown(NPC bloat, int tick) {
        currentDown++;
        state = State.DOWN;
        lastDownTick = tick;
        nextUpTick = tick + BLOAT_DOWN_CYCLE_TICKS + 1;
        log.debug("Bloat down {} tick {}", currentDown, lastDownTick);

        dispatchEvent(new BloatDownEvent(tick, getWorldLocation(bloat), currentDown, tick - lastUpTick));
    }

    private void handleBloatUp(NPC bloat, int tick) {
        lastUpTick = tick;
        nextUpTick = -1;
        state = State.WALKING;
        log.debug("Bloat up {} tick {}", currentDown, lastUpTick);

        dispatchEvent(new BloatUpEvent(tick, getWorldLocation(bloat)));
    }
}
