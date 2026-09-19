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

package io.blert.challenges.tob.rooms.nylocas;

import com.google.common.collect.ImmutableSet;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.ChallengeMode;
import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import io.blert.core.TrackedNpc;
import io.blert.events.NpcAttackEvent;
import io.blert.events.tob.NyloBossSpawnEvent;
import io.blert.events.tob.NyloCleanupEndEvent;
import io.blert.events.tob.NyloWaveEvent;
import java.util.*;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.NullNpcID;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;

@Slf4j
public class NylocasDataTracker extends RoomDataTracker {
    private static final int CAP_INCREASE_WAVE = 20;
    private static final int LAST_NYLO_WAVE = 31;
    private static final int WAVE_TICK_CYCLE = 4;
    private static final int[] NATURAL_STALLS = new int[] {
        0, 4, 4, 4, 4, 16, 4, 12, 4, 12, 8, 8, 8, 8, 8, 8, 4, 12, 8, 12, 16, 8, 12, 8, 8, 8, 4, 8, 4, 4, 4,
    };

    private static final int NYLO_BOSS_MAGE_ANIMATION = 7989;
    private static final int NYLO_BOSS_RANGE_ANIMATION = 7999;
    private static final int NYLO_BOSS_MELEE_ANIMATION = 8004;

    private int currentWave;
    private int nextWaveSpawnCheckTick;
    private int bossSpawnTick;

    private final Map<Integer, Nylo> nylosInRoom = new HashMap<>();
    private @Nullable NyloBoss nyloBoss = null;
    private final List<Nylo> bigDeathsThisTick = new ArrayList<>();
    private final List<Nylo> laneSpawnsThisTick = new ArrayList<>();

    private static final ImmutableSet<Integer> NYLOCAS_PILLAR_NPC_IDS =
            ImmutableSet.of(NullNpcID.NULL_10790, NullNpcID.NULL_8358, NullNpcID.NULL_10811);

    public NylocasDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.NYLOCAS);
        currentWave = 0;
        nextWaveSpawnCheckTick = -1;
        bossSpawnTick = -1;
    }

    private int roomNyloCount() {
        return nylosInRoom.size() + ((nyloBoss != null && nyloBoss.isPrince()) ? 3 : 0);
    }

    private int waveCap() {
        if (theatreChallenge.getChallengeMode() == ChallengeMode.TOB_HARD) {
            return currentWave < CAP_INCREASE_WAVE ? 15 : 24;
        }
        return currentWave < CAP_INCREASE_WAVE ? 12 : 24;
    }

    private boolean isPrinceWave() {
        return theatreChallenge.getChallengeMode() == ChallengeMode.TOB_HARD
                && (currentWave == 10 || currentWave == 20 || currentWave == 30);
    }

    @Override
    protected void onRoomStart() {}

    @Override
    protected void onTick() {
        super.onTick();
        final int tick = getTick();

        if (!laneSpawnsThisTick.isEmpty()) {
            OptionalInt wave = NyloWave.identifyWave(laneSpawnsThisTick, currentWave);
            if (wave.isPresent()) {
                handleWaveSpawn(tick, wave.getAsInt());
            }
            laneSpawnsThisTick.clear();
        }

        if (currentWave < LAST_NYLO_WAVE && tick == nextWaveSpawnCheckTick) {
            // The spawn event handler runs before the on tick handler, so if `nextWaveSpawnCheckTick` is ever reached,
            // it means that the next wave did not spawn when expected, i.e. a stall occurred.
            nextWaveSpawnCheckTick += WAVE_TICK_CYCLE;

            // A stall only happens when the room is at the nylo cap; avoid
            // emitting phantom stall events if under it.
            if (roomNyloCount() >= waveCap()) {
                log.debug("Stalled wave {} ({}/{})", currentWave, roomNyloCount(), waveCap());
                dispatchEvent(NyloWaveEvent.stall(tick, currentWave, roomNyloCount(), waveCap()));
            }
        }

        nylosInRoom.values().stream()
                .filter(nylo -> nylo.getSpawnTick() == tick)
                .forEach(nylo -> nylo.setWave(currentWave));

        assignParentsToSplits();
        bigDeathsThisTick.clear();
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned spawned) {
        NPC npc = spawned.getNpc();

        if (NYLOCAS_PILLAR_NPC_IDS.contains(npc.getId())) {
            startRoom();
            return Optional.empty();
        }

        if (TobNpc.isNylocas(npc.getId())) {
            return handleNylocasSpawn(npc);
        }

        Optional<TobNpc> maybeNpc = TobNpc.withId(npc.getId());
        if (maybeNpc.isEmpty()) {
            return Optional.empty();
        }
        TobNpc tobNpc = maybeNpc.get();

        if (TobNpc.isNylocasPrinkipas(tobNpc.getId())) {
            if (nyloBoss != null && nyloBoss.getNpc().isDead()) {
                despawnTrackedNpc(nyloBoss);
            }
            long roomId = generateRoomId(npc);
            nyloBoss = new NyloBoss(npc, tobNpc, roomId, new Hitpoints(tobNpc, theatreChallenge.getScale()));
            return Optional.of(nyloBoss);
        }

        if (TobNpc.isNylocasVasilias(tobNpc.getId())) {
            if (nyloBoss != null && nyloBoss.isPrince() && nyloBoss.getNpc().isDead()) {
                despawnTrackedNpc(nyloBoss);
            }

            currentWave = LAST_NYLO_WAVE;

            // Two spawn events are sent out for the Nylo king: one when it first drops down, and one when the fight
            // actually starts. Only generate a new room ID for the former.
            boolean bossAlreadySpawned = nyloBoss != null && !nyloBoss.isPrince();

            long roomId;
            if (bossAlreadySpawned) {
                roomId = nyloBoss.getRoomId();
            } else {
                handleBossSpawn(npc);
                roomId = generateRoomId(npc);
            }

            nyloBoss = new NyloBoss(npc, tobNpc, roomId, new Hitpoints(tobNpc, theatreChallenge.getScale()));
            return Optional.of(nyloBoss);
        }

        return Optional.empty();
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned despawned, TrackedNpc trackedNpc) {
        NPC npc = despawned.getNpc();

        if (TobNpc.isDroppingNyloBoss(npc.getId())) {
            // A dropping boss despawning is not a true despawn; it will be replaced by the actual boss.
            return false;
        }

        if (TobNpc.isNylocasPrinkipas(npc.getId())) {
            nyloBoss = null;
            checkCleanupComplete();
            return true;
        }

        if (trackedNpc == nyloBoss) {
            nyloBoss = null;
            return true;
        }

        if (!TobNpc.isNylocas(npc.getId())) {
            return false;
        }

        Nylo nylo = nylosInRoom.remove(npc.hashCode());
        if (nylo == null) {
            return false;
        }
        assert nylo == trackedNpc;

        final int tick = getTick();

        nylo.recordDeath(tick, getWorldLocation(npc));
        if (nylo.isBig()) {
            bigDeathsThisTick.add(nylo);
        } else {
            // Only check to see if the room is empty when a small dies.
            checkCleanupComplete();
        }

        return true;
    }

    @Override
    protected void onNpcChange(NpcChanged event) {
        NPC npc = event.getNpc();
        Nylo nylo = nylosInRoom.get(npc.hashCode());
        if (nylo != null) {
            nylo.setStyle(Nylo.Style.fromNpcId(npc.getId()));
        }
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        if (nyloBoss == null || actor != nyloBoss.getNpc()) {
            return;
        }

        final int tick = getTick();

        NpcAttack attack;
        switch (actor.getAnimation()) {
            case NYLO_BOSS_MAGE_ANIMATION:
                attack = NpcAttack.TOB_NYLO_BOSS_MAGE;
                break;
            case NYLO_BOSS_RANGE_ANIMATION:
                attack = NpcAttack.TOB_NYLO_BOSS_RANGE;
                break;
            case NYLO_BOSS_MELEE_ANIMATION:
                attack = NpcAttack.TOB_NYLO_BOSS_MELEE;
                break;
            default:
                return;
        }

        dispatchEvent(new NpcAttackEvent(getStage(), tick, getWorldLocation(actor), attack, nyloBoss));
    }

    private Optional<Nylo> handleNylocasSpawn(NPC npc) {
        Optional<TobNpc> tobNpc = TobNpc.withId(npc.getId());
        if (tobNpc.isEmpty()) {
            return Optional.empty();
        }

        final int tick = getTick();

        WorldPoint point = getWorldLocation(npc);
        Nylo nylo = new Nylo(
                npc,
                tobNpc.get(),
                generateRoomId(npc),
                point,
                tick,
                0,
                tobNpc.get().getBaseHitpoints(theatreChallenge.getScale()));
        nylosInRoom.put(npc.hashCode(), nylo);

        if (nylo.getSpawnType().isLaneSpawn()) {
            laneSpawnsThisTick.add(nylo);
        }

        return Optional.of(nylo);
    }

    private void handleWaveSpawn(int tick, int wave) {
        currentWave = wave;
        if (currentWave < LAST_NYLO_WAVE) {
            if (isPrinceWave()) {
                nextWaveSpawnCheckTick = tick + 4 * WAVE_TICK_CYCLE;
            } else {
                nextWaveSpawnCheckTick = tick + NATURAL_STALLS[currentWave];
            }
        }

        dispatchEvent(NyloWaveEvent.spawn(tick, currentWave, roomNyloCount(), waveCap()));

        if (currentWave == CAP_INCREASE_WAVE) {
            log.debug("Cap increase: {} ({})", tick, formattedRoomTime());
        } else if (currentWave == LAST_NYLO_WAVE) {
            log.debug("Waves: {} ({})", tick, formattedRoomTime());
        }
    }

    private void handleBossSpawn(NPC npc) {
        final int tick = getTick();

        if (bossSpawnTick == -1) {
            bossSpawnTick = tick;
            dispatchEvent(new NyloBossSpawnEvent(tick, getWorldLocation(npc)));
            log.debug("Boss: {} ({})", tick, formattedRoomTime());
        }
    }

    private void assignParentsToSplits() {
        final int tick = getTick();

        // Determine whether the nylo is a split, and if so, if it has an
        // unambiguous parent. If no parent can be identified, leave the nylo as
        // an UNKNOWN type.
        nylosInRoom.values().stream()
                .filter(nylo -> nylo.getSpawnTick() == tick && !nylo.isLaneSpawn())
                .forEach(nylo -> {
                    Nylo parent = null;
                    int possibleParents = 0;
                    for (Nylo big : bigDeathsThisTick) {
                        if (big.isPossibleParentOf(nylo)) {
                            parent = big;
                            possibleParents++;
                        }
                    }

                    if (possibleParents == 1) {
                        nylo.setSpawnType(SpawnType.SPLIT);
                        nylo.setParent(parent);
                    } else if (possibleParents > 1) {
                        nylo.setSpawnType(SpawnType.SPLIT);
                    }
                });
    }

    private void checkCleanupComplete() {
        boolean princeAlive =
                nyloBoss != null && nyloBoss.isPrince() && !nyloBoss.getNpc().isDead();
        if (currentWave == LAST_NYLO_WAVE && nylosInRoom.isEmpty() && !princeAlive) {
            dispatchEvent(new NyloCleanupEndEvent(getTick()));
            log.debug("Cleanup: {} ({})", getTick(), formattedRoomTime());
        }
    }
}
