/*
 * Copyright (c) 2025 Alexei Frolov
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

package io.blert.challenges.inferno;

import io.blert.core.*;
import io.blert.events.ChallengeEndEvent;
import io.blert.events.ChallengeStartEvent;
import io.blert.util.ChatText;
import io.blert.util.DeferredTask;
import io.blert.util.Location;
import io.blert.util.Tick;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.NPC;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.NpcSpawned;
import net.runelite.client.callback.ClientThread;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class InfernoChallenge extends RecordableChallenge {
    private static final int INFERNO_REGION_ID = 9043;
    private static final int MOR_UL_REK_SOUTHWEST_REGION_ID = 9807;
    private static final int MOR_UL_REK_NORTHWEST_REGION_ID = 9808;
    private static final int MOR_UL_REK_SOUTHEAST_REGION_ID = 10063;
    private static final int MOR_UL_REK_NORTHEAST_REGION_ID = 10064;

    private static final String WAVE_1_START_MESSAGE = "Wave: 1";
    static final Pattern INFERNO_END_REGEX = Pattern.compile("Duration: (" + Tick.TIME_STRING_REGEX + ")");
    // The inferno timer begins 6 seconds (10 ticks) before the first wave.
    private static final int WAVE_1_TIME_OFFSET_TICKS = 10;

    private int wave;
    private int challengeStartTick;
    private int reportedChallengeTicks;

    private @Nullable DeferredTask deferredTask = null;

    private boolean hasLogged;

    private WaveDataTracker waveDataTracker;
    private Pillar westPillar;
    private Pillar eastPillar;
    private Pillar southPillar;

    public InfernoChallenge(Client client, ClientThread clientThread) {
        super(Challenge.INFERNO, client, clientThread);
        this.wave = 0;
        this.challengeStartTick = -1;
        this.reportedChallengeTicks = -1;
        this.hasLogged = false;
    }

    @Nullable
    @Override
    protected DataTracker getActiveTracker() {
        return waveDataTracker;
    }

    public int recordedDurationTicks() {
        if (getState().isInactive() || challengeStartTick == -1) {
            return 0;
        }
        int currentTick = client.getTickCount();
        return currentTick - challengeStartTick;
    }

    public boolean hasLogged() {
        return hasLogged;
    }

    @Override
    public boolean containsLocation(WorldPoint worldPoint) {
        return worldPoint.getRegionID() == INFERNO_REGION_ID
                || worldPoint.getRegionID() == MOR_UL_REK_SOUTHWEST_REGION_ID
                || worldPoint.getRegionID() == MOR_UL_REK_NORTHWEST_REGION_ID
                || worldPoint.getRegionID() == MOR_UL_REK_SOUTHEAST_REGION_ID
                || worldPoint.getRegionID() == MOR_UL_REK_NORTHEAST_REGION_ID;
    }

    @Override
    protected void onInitialize() {
        reset();
        addRaider(new Raider(client.getLocalPlayer(), true));
    }

    @Override
    protected void onTerminate() {
        if (getState().inChallenge()) {
            log.warn("Terminating Inferno challenge while still active");
            finishInferno(ChallengeState.INACTIVE);
        }
        cleanup();
    }

    @Override
    protected void onTick() {
        if (deferredTask != null) {
            deferredTask.tick();
        } else {
            // The deferred task is used for state changes, so only check state
            // manually if it's not pending.
            updateChallengeState();
        }

        if (getState().isInactive()) {
            return;
        }

        if (waveDataTracker != null) {
            waveDataTracker.tick();

            if (waveDataTracker.completed() && wave < 69) {
                prepareNextWave();
            }
        }
    }

    @Nullable
    @Override
    protected Stage getStage() {
        return waveDataTracker != null ? waveDataTracker.getStage() : null;
    }

    @Override
    public void onGameStateChanged(GameStateChanged event) {
        if (event.getGameState() == GameState.LOGIN_SCREEN && getState().inChallenge()) {
            hasLogged = true;
        }
        super.onGameStateChanged(event);
    }

    @Override
    public void onChatMessage(ChatMessage event) {
        if (!getState().isInactive() && event.getType() == ChatMessageType.GAMEMESSAGE) {
            String stripped = ChatText.stripFormatting(event.getMessage());
            if (stripped.equals(WAVE_1_START_MESSAGE)) {
                challengeStartTick = client.getTickCount() - WAVE_1_TIME_OFFSET_TICKS;
            } else {
                Matcher matcher = INFERNO_END_REGEX.matcher(stripped);
                if (matcher.find()) {
                    try {
                        reportedChallengeTicks = Tick.fromTimeString(matcher.group(1))
                                .map(Pair::getLeft)
                                .orElse(-1);
                    } catch (Exception e) {
                        reportedChallengeTicks = -1;
                    }
                    deferredTask = new DeferredTask(() -> finishInferno(ChallengeState.COMPLETE), 3);
                }
            }
        }
        super.onChatMessage(event);
    }

    @Override
    public void onNpcSpawned(NpcSpawned event) {
        NPC npc = event.getNpc();
        if (npc.getId() == InfernoNpc.ROCKY_SUPPORT.getId()) {
            Pillar pillar = new Pillar(npc, npc.getIndex(), Location.getWorldLocation(client, npc.getWorldLocation()));
            switch (pillar.getLocation()) {
                case WEST:
                    westPillar = pillar;
                    break;
                case EAST:
                    eastPillar = pillar;
                    break;
                case SOUTH:
                    southPillar = pillar;
                    break;
            }
        }
        super.onNpcSpawned(event);
    }

    public void removePillar(TrackedNpc pillar) {
        if (!(pillar instanceof Pillar)) {
            return;
        }

        if (pillar == westPillar) {
            westPillar = null;
        } else if (pillar == eastPillar) {
            eastPillar = null;
        } else if (pillar == southPillar) {
            southPillar = null;
        }
    }

    private void updateChallengeState() {
        WorldPoint playerLocation =
                Location.getWorldLocation(client, client.getLocalPlayer().getWorldLocation());
        if (playerLocation == null) {
            return;
        }

        boolean inInferno = playerLocation.getRegionID() == INFERNO_REGION_ID;

        if (getState().isInactive()) {
            if (inInferno) {
                startInferno();
            }
        } else if (!inInferno) {
            if (getState() == ChallengeState.COMPLETE) {
                setState(ChallengeState.INACTIVE);
            } else {
                finishInferno(ChallengeState.INACTIVE);
            }
        }
    }

    private void startInferno() {
        log.info("Starting Inferno challenge");
        setState(ChallengeState.ACTIVE);
        reportedChallengeTicks = -1;

        // TODO(frolv): What happens if someone turns on their plugin mid-challenge?
        Stage stage = Stage.INFERNO_WAVE_1;

        List<String> usernames = getParty().stream().map(Raider::getUsername).collect(Collectors.toList());
        dispatchEvent(new ChallengeStartEvent(getChallenge(), ChallengeMode.NO_MODE, stage, usernames, false));

        prepareNextWave();
    }

    private void finishInferno(ChallengeState state) {
        log.info("Finishing Inferno challenge with state: {}", state);
        int reported = reportedChallengeTicks;
        cleanup();
        dispatchEvent(new ChallengeEndEvent(reported, reported));
        setState(state);
    }

    private void prepareNextWave() {
        clearWaveDataTracker();

        wave++;
        waveDataTracker = new WaveDataTracker(this, client, wave);

        if (westPillar != null) {
            waveDataTracker.addTrackedNpc(westPillar);
        }
        if (eastPillar != null) {
            waveDataTracker.addTrackedNpc(eastPillar);
        }
        if (southPillar != null) {
            waveDataTracker.addTrackedNpc(southPillar);
        }
    }

    private void clearWaveDataTracker() {
        if (waveDataTracker != null) {
            waveDataTracker.terminate();
            waveDataTracker = null;
        }
    }

    private void cleanup() {
        clearWaveDataTracker();
        reset();
    }

    private void reset() {
        deferredTask = null;
        wave = 0;
        challengeStartTick = -1;
        reportedChallengeTicks = -1;
        hasLogged = false;
        westPillar = null;
        eastPillar = null;
        southPillar = null;
    }
}
