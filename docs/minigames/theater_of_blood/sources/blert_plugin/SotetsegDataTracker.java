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

package io.blert.challenges.tob.rooms.sotetseg;

import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.Location;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.*;
import io.blert.events.NpcAttackEvent;
import io.blert.events.tob.SoteMazeEvent;
import io.blert.events.tob.SoteMazePathEvent;
import java.util.HashSet;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.client.util.Text;

@Slf4j
public class SotetsegDataTracker extends RoomDataTracker {
    private static final int MAZE_TELEPORT_ANIMATION = 1816;
    private static final int SOTE_MELEE_ANIMATION = 8138;
    private static final int SOTE_BALL_ANIMATION = 8139;
    private static final int SOTE_DEATH_BALL_PROJECTILE = 1604;
    private static final int MAZE_DISABLED_TILE_GROUND_OBJECT = 33033;
    private static final int MAZE_INACTIVE_TILE_GROUND_OBJECT = 33034;
    private static final int MAZE_ACTIVE_TILE_GROUND_OBJECT = 33035;
    private static final int MAZE_RAG_GRAPHICS_OBJECT = 505;
    private static final int SOTE_ATTACK_SPEED = 5;

    private Maze maze = Maze.MAZE_66;
    private @Nullable NpcAttack attackThisTick = null;
    private int lastAttackTick = -1;
    private int deathBallSpawnTick = -1;
    private @Nullable HpVarbitTrackedNpc sotetseg = null;
    private final MazeTracker mazeTracker = new MazeTracker();
    boolean inMaze = false;
    boolean isUnder = false;
    private String chosenPlayer = null;
    private int invulnerableUntilTick = -1;
    private final Set<GroundObject> activeMazeTiles = new HashSet<>();

    public SotetsegDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.SOTETSEG);
    }

    @Override
    protected void onRoomStart() {
        if (sotetseg == null) {
            client.getTopLevelWorldView().npcs().stream()
                    .filter(npc -> TobNpc.isAnySotetseg(npc.getId()))
                    .findFirst()
                    .ifPresent(npc -> {
                        TobNpc tobNpc = TobNpc.withId(npc.getId()).orElseThrow();
                        sotetseg = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                        addTrackedNpc(sotetseg);
                    });
        }

        if (sotetseg != null && TobNpc.isSotetsegIdle(sotetseg.getNpcId())) {
            startMaze(getTick());
        }
    }

    @Override
    protected void onTick() {
        super.onTick();
        final int tick = getTick();

        if (invulnerableUntilTick != -1 && tick >= invulnerableUntilTick) {
            for (Raider raider : theatreChallenge.getParty()) {
                if (raider.isAlive()) {
                    raider.setInvulnerable(false);
                }
            }
            invulnerableUntilTick = -1;
        }

        Player deathBallTarget = checkForDeathBall();

        if (attackThisTick != null && sotetseg != null) {
            WorldPoint point = getWorldLocation(sotetseg);
            if (deathBallSpawnTick == tick) {
                // A regular attack animation may have occurred at the same time as the death ball; the death ball
                // should take priority.
                String target = deathBallTarget != null ? deathBallTarget.getName() : null;
                dispatchEvent(
                        new NpcAttackEvent(getStage(), tick, point, NpcAttack.TOB_SOTE_DEATH_BALL, sotetseg, target));
            } else {
                dispatchEvent(new NpcAttackEvent(getStage(), tick, point, attackThisTick, sotetseg));
            }
            lastAttackTick = tick;
            attackThisTick = null;
        }

        if (inMaze) {
            Location playerLocation = Location.fromWorldPoint(getWorldLocation(client.getLocalPlayer()));
            if (playerLocation.inSotetsegOverworld() && !activeMazeTiles.isEmpty()) {
                var activeTilePoints =
                        activeMazeTiles.stream().map(this::getWorldLocation).collect(Collectors.toList());
                dispatchEvent(SoteMazePathEvent.overworldTiles(tick, maze, activeTilePoints));
            }

            if (theatreChallenge.getChallengeMode().equals(ChallengeMode.TOB_HARD)) {
                if (playerLocation.inSotetsegUnderworld()) {
                    isUnder = true;
                    // In hard mode, the chosen player is up top.
                    if (chosenPlayer == null) {
                        chosenPlayer = findMissingPlayer();
                    }
                } else if (chosenPlayer == null && !activeMazeTiles.isEmpty()) {
                    chosenPlayer = client.getTopLevelWorldView().players().stream()
                            .filter(p ->
                                    Location.fromWorldPoint(getWorldLocation(p)).inSotetseg())
                            .map(p -> Objects.requireNonNull(p.getName()))
                            .findFirst()
                            .orElse(null);
                }
            } else {
                if (playerLocation.inSotetsegUnderworld()) {
                    isUnder = true;
                    chosenPlayer = client.getLocalPlayer().getName();
                } else if (chosenPlayer == null && !activeMazeTiles.isEmpty()) {
                    chosenPlayer = findMissingPlayer();
                }
            }

            if (isUnder && playerLocation.inSotetsegOverworld()) {
                if (sotetseg != null && !TobNpc.isSotetsegIdle(sotetseg.getNpcId())) {
                    finishMaze(tick);
                }
            }
        }
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        NPC npc = event.getNpc();

        return TobNpc.withId(npc.getId())
                .filter(tobNpc -> TobNpc.isAnySotetseg(tobNpc.getId()))
                .map(tobNpc -> {
                    if (sotetseg != null) {
                        sotetseg = new HpVarbitTrackedNpc(npc, tobNpc, sotetseg.getRoomId(), sotetseg.getHitpoints());
                    } else {
                        sotetseg = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                    }
                    return sotetseg;
                });
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, TrackedNpc trackedNpc) {
        if (sotetseg != null && trackedNpc == sotetseg) {
            if (sotetseg.getNpc().isDead()) {
                sotetseg = null;
            }
            // If the local player is sent to the underworld, Sotetseg disappears
            // from their client, creating a false despawn event. Ignore despawns
            // during maze phases.
            return !inMaze;
        }
        return false;
    }

    @Override
    protected void onNpcChange(NpcChanged changed) {
        if (TobNpc.isSotetsegIdle(changed.getOld().getId())
                && TobNpc.isSotetseg(changed.getNpc().getId())) {
            if (getState() == State.NOT_STARTED) {
                startRoom();
            } else {
                finishMaze(getTick());
            }
        }
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        int animationId = actor.getAnimation();
        final int tick = getTick();

        if (actor instanceof NPC) {
            if (sotetseg == null || actor != sotetseg.getNpc()) {
                return;
            }

            if (lastAttackTick != -1 && tick - lastAttackTick < SOTE_ATTACK_SPEED) {
                return;
            }

            if (animationId == SOTE_MELEE_ANIMATION) {
                attackThisTick = NpcAttack.TOB_SOTE_MELEE;
            } else if (animationId == SOTE_BALL_ANIMATION) {
                attackThisTick = NpcAttack.TOB_SOTE_BALL;
            }
            return;
        }

        if (!(actor instanceof Player) || animationId != MAZE_TELEPORT_ANIMATION) {
            return;
        }

        if (!inMaze) {
            // If a player dies on the tick the maze procs, they will be saved,
            // but the death event will still fire. Prevent the death from being
            // processed on this tick and several after in case orbs varbits do
            // something weird. Also, clear any deaths occurring on this tick
            // in case that event fired first.
            invulnerableUntilTick = tick + 4;
            for (Raider raider : theatreChallenge.getParty()) {
                if (raider.getDeathTick() == tick) {
                    raider.setAlive();
                }
                if (raider.isAlive()) {
                    raider.setInvulnerable(true);
                }
            }
            startMaze(tick);
        }
    }

    private Player checkForDeathBall() {
        final int tick = getTick();
        if (deathBallSpawnTick != -1) {
            if (tick >= deathBallSpawnTick + 20) {
                // The ball takes 15 ticks to land, but add in a safety buffer.
                deathBallSpawnTick = -1;
            } else {
                return null;
            }
        }

        for (Projectile projectile : client.getProjectiles()) {
            if (projectile.getId() == SOTE_DEATH_BALL_PROJECTILE) {
                deathBallSpawnTick = tick;
                attackThisTick = NpcAttack.TOB_SOTE_DEATH_BALL;
                Actor target = projectile.getTargetActor();
                return target instanceof Player ? (Player) target : null;
            }
        }

        return null;
    }

    @Override
    protected void onGroundObjectSpawn(GroundObjectSpawned event) {
        GroundObject groundObject = event.getGroundObject();
        if (groundObject.getId() == MAZE_ACTIVE_TILE_GROUND_OBJECT) {
            Location playerLocation = Location.fromWorldPoint(getWorldLocation(client.getLocalPlayer()));
            if (playerLocation.inSotetsegUnderworld()) {
                mazeTracker.addUnderworldPoint(getWorldLocation(groundObject));
            } else {
                mazeTracker.addPotentialOverworldPoint(getWorldLocation(groundObject));
                activeMazeTiles.add(groundObject);
            }
        }
    }

    @Override
    protected void onGroundObjectDespawn(GroundObjectDespawned event) {
        GroundObject groundObject = event.getGroundObject();
        if (groundObject.getId() == MAZE_ACTIVE_TILE_GROUND_OBJECT) {
            activeMazeTiles.remove(groundObject);
        }
    }

    @Override
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {
        GraphicsObject graphicsObject = event.getGraphicsObject();
        if (graphicsObject.getId() == MAZE_RAG_GRAPHICS_OBJECT) {
            WorldPoint point = WorldPoint.fromLocalInstance(client, graphicsObject.getLocation());
            mazeTracker.removeOverworldPoint(point);
        }
    }

    private void startMaze(int tick) {
        if (sotetseg != null) {
            int hitpointsVarbit = getBossHitpointsVarbitValue();
            // There are two cases here:
            // 1. The maze procs normally during the fight. In this case, at the
            //    time startMaze is called, the hitpoints varbit will reflect
            //    the boss's actual health (which will be at or slightly above
            //    666 on the first maze and 333 on the second).
            // 2. A spectator joins the raid while the maze is in progress. Once
            //    actually in the maze, the hitpoints varbit is locked at 2
            //    during the first maze and 1 for the second, regardless of the
            //    boss's health.
            maze = (hitpointsVarbit > 500 || hitpointsVarbit == 2) ? Maze.MAZE_66 : Maze.MAZE_33;
            sotetseg.setDisableVarbitUpdates(true);
        }

        log.debug("Sotetseg {} started on tick {} {}", maze, tick, formattedRoomTime());
        mazeTracker.reset();
        activeMazeTiles.clear();
        inMaze = true;
        isUnder = false;
        chosenPlayer = null;

        dispatchEvent(SoteMazeEvent.mazeProc(tick, maze));
    }

    private void finishMaze(int tick) {
        if (!inMaze) {
            return;
        }

        inMaze = false;
        isUnder = false;
        mazeTracker.finishMaze();
        log.debug("{} finished; pivots: {}, chosen: {}", maze, mazeTracker.getPivots(), chosenPlayer);

        if (mazeTracker.hasUnderworldPivots()) {
            dispatchEvent(SoteMazePathEvent.underworldPivots(getTick(), maze, mazeTracker.getUnderworldPivots()));
        }

        if (mazeTracker.hasOverworldPivots()) {
            dispatchEvent(SoteMazePathEvent.overworldPivots(getTick(), maze, mazeTracker.getOverworldPoints()));
        }

        dispatchEvent(SoteMazeEvent.mazeEnd(tick, maze, chosenPlayer));

        // Advance to the next maze.
        maze = Maze.MAZE_33;
        mazeTracker.reset();
        chosenPlayer = null;

        if (sotetseg != null) {
            sotetseg.setDisableVarbitUpdates(false);
        }
    }

    private String findMissingPlayer() {
        Set<String> allPlayers = theatreChallenge.getParty().stream()
                .filter(Raider::isAlive)
                .map(r -> Text.standardize(r.getUsername()))
                .collect(Collectors.toSet());

        client.getTopLevelWorldView().players().forEach(p -> allPlayers.remove(Text.standardize(p.getName())));

        if (allPlayers.isEmpty()) {
            return null;
        }
        if (allPlayers.size() > 1) {
            log.warn("Multiple missing players found in Sotetseg maze: {}", allPlayers);
            return null;
        }

        Raider r = theatreChallenge.getRaider(allPlayers.iterator().next());
        return r != null ? r.getUsername() : null;
    }
}
