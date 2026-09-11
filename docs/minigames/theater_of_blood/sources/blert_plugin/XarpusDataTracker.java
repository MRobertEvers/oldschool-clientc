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

package io.blert.challenges.tob.rooms.xarpus;

import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.ChallengeMode;
import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import io.blert.core.TrackedNpc;
import io.blert.events.NpcAttackEvent;
import io.blert.events.tob.XarpusExhumedEvent;
import io.blert.events.tob.XarpusPhaseEvent;
import io.blert.events.tob.XarpusSplatEvent;
import io.blert.util.Location;
import java.util.*;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;

@Slf4j
public class XarpusDataTracker extends RoomDataTracker {
    private static final int FIRST_P2_TURN_TICK = 7;
    private static final int TICKS_PER_TURN_P2 = 4;
    private static final int TICKS_PER_TURN_P3 = 8;

    private static final int EXHUMED_GROUND_OBJECT_ID = 32743;
    private static final int EXHUMED_PROJECTILE_ID = 1550;
    private static final int SPLAT_GROUND_OBJECT_ID = 32744;
    private static final int SPLAT_PROJECTILE_ID = 1555;
    private static final int SPLAT_GRAPHICS_OBJECT_ID = 1556;

    private XarpusPhase phase;
    private @Nullable HpVarbitTrackedNpc xarpus = null;

    private int exhumedHealAmount;
    private final Map<GroundObject, Exhumed> exhumeds = new HashMap<>();
    private final Map<WorldPoint, ActiveSplat> splatsByTarget = new HashMap<>();
    private int nextTurnTick = -1;

    private static class Exhumed {
        final int spawnTick;
        final List<Integer> healTicks = new ArrayList<>();
        final Set<Projectile> projectiles = new HashSet<>();

        Exhumed(int spawnTick) {
            this.spawnTick = spawnTick;
        }
    }

    private static class ActiveSplat {
        final Set<Projectile> projectiles = new HashSet<>();
        LocalPoint landedLocal;

        ActiveSplat(Projectile projectile) {
            this.projectiles.add(projectile);
            this.landedLocal = null;
        }

        ActiveSplat(LocalPoint landedLocal) {
            this.landedLocal = landedLocal;
        }

        boolean hasLanded() {
            return landedLocal != null;
        }
    }

    public XarpusDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.XARPUS);
    }

    @Override
    protected void onRoomStart() {
        if (xarpus == null) {
            client.getTopLevelWorldView().npcs().stream()
                    .filter(npc -> TobNpc.isAnyXarpus(npc.getId()))
                    .findFirst()
                    .ifPresent(npc -> {
                        TobNpc tobNpc = TobNpc.withId(npc.getId()).orElseThrow();
                        xarpus = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                        addTrackedNpc(xarpus);
                    });
        }

        phase = XarpusPhase.P1;
    }

    @Override
    protected void onTick() {
        super.onTick();
        final int tick = getTick();

        if (xarpus != null && xarpus.getNpc().getOverheadText() != null && phase != XarpusPhase.P3) {
            phase = XarpusPhase.P3;
            nextTurnTick = tick + TICKS_PER_TURN_P3;
            dispatchEvent(new XarpusPhaseEvent(tick, getWorldLocation(xarpus.getNpc()), phase));
            log.debug("Screech: {} ({})", tick, formattedRoomTime());
        }

        if (tick == nextTurnTick && xarpus != null) {
            WorldPoint point = getWorldLocation(xarpus);
            if (phase == XarpusPhase.P2) {
                nextTurnTick += TICKS_PER_TURN_P2;
                dispatchEvent(new NpcAttackEvent(getStage(), tick, point, NpcAttack.TOB_XARPUS_SPIT, xarpus));
            } else if (phase == XarpusPhase.P3) {
                if (theatreChallenge.getChallengeMode() != ChallengeMode.TOB_HARD) {
                    nextTurnTick += TICKS_PER_TURN_P3;
                    dispatchEvent(new NpcAttackEvent(getStage(), tick, point, NpcAttack.TOB_XARPUS_TURN, xarpus));
                }
            }
        }
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        NPC npc = event.getNpc();

        Optional<TobNpc> maybeXarpus = TobNpc.withId(npc.getId()).filter(TobNpc::isAnyXarpus);
        if (maybeXarpus.isPresent()) {
            if (xarpus == null) {
                xarpus = new HpVarbitTrackedNpc(
                        npc,
                        maybeXarpus.get(),
                        generateRoomId(npc),
                        new Hitpoints(maybeXarpus.get(), theatreChallenge.getScale()));
            }
            return Optional.of(xarpus);
        }

        return Optional.empty();
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, TrackedNpc trackedNpc) {
        if (trackedNpc == xarpus) {
            nextTurnTick = -1;
            return true;
        }
        return false;
    }

    @Override
    protected void onNpcChange(NpcChanged changed) {
        int idBefore = changed.getOld().getId();
        int idAfter = changed.getNpc().getId();

        if (TobNpc.isXarpusIdle(idBefore) && TobNpc.isXarpusP1(idAfter)) {
            startRoom();
            return;
        }

        final int tick = getTick();

        if (TobNpc.isXarpusP1(idBefore) && TobNpc.isXarpus(idAfter)) {
            phase = XarpusPhase.P2;
            nextTurnTick = tick + FIRST_P2_TURN_TICK;
            dispatchEvent(new XarpusPhaseEvent(tick, getWorldLocation(changed.getNpc()), phase));
            log.debug("Exhumes: {} ({})", tick, formattedRoomTime());
        }
    }

    @Override
    protected void onHitsplat(HitsplatApplied event) {
        if (xarpus == null || event.getActor() != xarpus.getNpc()) {
            return;
        }

        Hitsplat hitsplat = event.getHitsplat();
        if (phase == XarpusPhase.P1 && hitsplat.getHitsplatType() == HitsplatID.HEAL) {
            exhumedHealAmount = hitsplat.getAmount();
        }
    }

    @Override
    protected void onGroundObjectSpawn(GroundObjectSpawned event) {
        GroundObject groundObject = event.getGroundObject();
        int id = groundObject.getId();

        if (id == EXHUMED_GROUND_OBJECT_ID) {
            exhumeds.put(groundObject, new Exhumed(getTick()));
            return;
        }

        if (id == SPLAT_GROUND_OBJECT_ID) {
            // If a splat's ground object spawns without it having been previously recorded, it indicates that
            // the has client missed the projectile that spawned it (perhaps by joining late).
            // Record the splat as existing, but without a known source.
            WorldPoint splatWorld = getWorldLocation(groundObject);
            if (!splatsByTarget.containsKey(splatWorld)) {
                log.warn("Splat ground object spawned at {} without a projectile", splatWorld);
                LocalPoint splatLocal = groundObject.getLocalLocation();
                splatsByTarget.put(getWorldLocation(groundObject), new ActiveSplat(splatLocal));
                dispatchEvent(new XarpusSplatEvent(getTick(), splatWorld, XarpusSplatEvent.Source.UNKNOWN, null));
            }
        }
    }

    @Override
    protected void onGroundObjectDespawn(GroundObjectDespawned event) {
        GroundObject groundObject = event.getGroundObject();

        if (groundObject.getId() != EXHUMED_GROUND_OBJECT_ID) {
            return;
        }

        Exhumed exhumed = exhumeds.remove(groundObject);
        if (exhumed != null) {
            dispatchEvent(new XarpusExhumedEvent(
                    getTick(),
                    getWorldLocation(groundObject),
                    exhumed.spawnTick,
                    exhumedHealAmount,
                    exhumed.healTicks));
        }
    }

    @Override
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {
        if (event.getGraphicsObject().getId() == SPLAT_GRAPHICS_OBJECT_ID) {
            try {
                recordAndSendSplat(event.getGraphicsObject().getLocation());
            } catch (IllegalStateException e) {
                log.warn("Failed to record splat: {}", e.getMessage());
            }
        }
    }

    @Override
    protected void onProjectile(ProjectileMoved event) {
        Projectile projectile = event.getProjectile();

        if (projectile.getId() == EXHUMED_PROJECTILE_ID) {
            if (phase != XarpusPhase.P1) {
                return;
            }

            exhumeds.entrySet().stream()
                    .filter(entry -> {
                        LocalPoint location = entry.getKey().getLocalLocation();
                        return location.getX() == projectile.getX1() && location.getY() == projectile.getY1();
                    })
                    .findFirst()
                    .ifPresent(entry -> {
                        Exhumed exhumed = entry.getValue();
                        if (!exhumed.projectiles.contains(projectile)) {
                            exhumed.healTicks.add(getTick());
                            exhumed.projectiles.add(projectile);
                        }
                    });

            return;
        }

        if (projectile.getId() == SPLAT_PROJECTILE_ID) {
            // The splat projectile is spawned at its target location.
            WorldPoint target = Location.getWorldLocation(client, WorldPoint.fromLocal(client, event.getPosition()));
            splatsByTarget.compute(target, (point, splat) -> {
                if (splat == null) {
                    splat = new ActiveSplat(projectile);
                } else if (!splat.hasLanded()) {
                    splat.projectiles.add(projectile);
                }
                return splat;
            });
        }
    }

    void recordAndSendSplat(LocalPoint splatLocal) throws IllegalStateException {
        WorldPoint splatWorld = Location.getWorldLocation(client, WorldPoint.fromLocal(client, splatLocal));
        ActiveSplat splat = splatsByTarget.get(splatWorld);

        XarpusSplatEvent.Source source = XarpusSplatEvent.Source.UNKNOWN;
        WorldPoint bounceFrom = null;

        if (splat != null) {
            if (splat.hasLanded()) {
                // This splat has already been recorded, no need to do it again.
                return;
            }

            Projectile firstLanded = splat.projectiles.stream()
                    .min(Comparator.comparingInt(Projectile::getEndCycle))
                    .orElseThrow(() -> new IllegalStateException("No projectiles to splat at " + splatWorld));
            LocalPoint xarpusLocal = xarpus != null ? xarpus.getNpc().getLocalLocation() : null;

            if (xarpusLocal != null
                    && firstLanded.getX1() == xarpusLocal.getX()
                    && firstLanded.getY1() == xarpusLocal.getY()) {
                source = XarpusSplatEvent.Source.XARPUS;
            } else {
                // if the splat did not come from Xarpus, try to find a previously landed splat
                // matching its starting coordinates.
                Optional<Map.Entry<WorldPoint, ActiveSplat>> bouncedFrom = splatsByTarget.entrySet().stream()
                        .filter((entry) -> {
                            LocalPoint l = entry.getValue().landedLocal;
                            return l != null && l.getX() == firstLanded.getX1() && l.getY() == firstLanded.getY1();
                        })
                        .findFirst();
                if (bouncedFrom.isPresent()) {
                    source = XarpusSplatEvent.Source.BOUNCE;
                    bounceFrom = bouncedFrom.get().getKey();
                }
            }
            splat.landedLocal = splatLocal;
        } else {
            log.warn("Splat at {} without projectiles", splatWorld);
            splatsByTarget.put(splatWorld, new ActiveSplat(splatLocal));
        }

        dispatchEvent(new XarpusSplatEvent(getTick(), splatWorld, source, bounceFrom));
    }
}
