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

package io.blert.challenges.colosseum;

import io.blert.core.*;
import io.blert.events.ChallengeEndEvent;
import io.blert.events.ChallengeStartEvent;
import io.blert.util.ChatText;
import io.blert.util.DeferredTask;
import io.blert.util.Location;
import io.blert.util.Tick;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.coords.WorldArea;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.client.callback.ClientThread;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public final class ColosseumChallenge extends RecordableChallenge {
    static final Pattern COLOSSEUM_END_REGEX = Pattern.compile("Colosseum duration: (" + Tick.TIME_STRING_REGEX + ")");

    private static final int COLOSSEUM_REGION_ID = 7216;
    private static final int COLOSSEUM_LOBBY_REGION_ID = 7316;
    private static final WorldArea COLOSSEUM_AREA = new WorldArea(1806, 3088, 38, 38, 0);

    private static final int MINIMUS_NPC_ID = 12808;
    private static final int REWARD_CHEST_OBJECT_ID = 50741;

    private static final int HANDICAP_SELECTION_SCRIPT_ID = 4931;
    private static final int HANDICAP_SELECTION_VARBIT_ID = 9788;

    private int currentWave;
    private WaveDataTracker waveDataTracker;
    private int recordedChallengeTicks;
    private int reportedChallengeTicks;

    private final List<Handicap> waveHandicapOptions = new ArrayList<>(3);

    private @Nullable DeferredTask deferredTask = null;
    private boolean stateChangeCooldown;

    public ColosseumChallenge(Client client, ClientThread clientThread) {
        super(Challenge.COLOSSEUM, client, clientThread);
    }

    @Nullable
    @Override
    protected DataTracker getActiveTracker() {
        return waveDataTracker;
    }

    @Override
    public boolean containsLocation(WorldPoint worldPoint) {
        return worldPoint.getRegionID() == COLOSSEUM_REGION_ID || worldPoint.getRegionID() == COLOSSEUM_LOBBY_REGION_ID;
    }

    @Override
    protected void onInitialize() {
        reset();
        addRaider(new Raider(client.getLocalPlayer(), true));
    }

    @Override
    protected void onTerminate() {
        if (getState().inChallenge()) {
            log.warn("Terminating Colosseum challenge while still active");
            finishColosseum(ChallengeState.INACTIVE);
        }
        cleanup();
    }

    @Override
    protected void onTick() {
        if (!stateChangeCooldown) {
            checkColosseumState();
        }

        if (getState().inChallenge()) {
            if (waveDataTracker != null) {
                waveDataTracker.tick();
            }
        }

        if (deferredTask != null) {
            deferredTask.tick();
        }
    }

    @Nullable
    @Override
    protected Stage getStage() {
        return waveDataTracker != null ? waveDataTracker.getStage() : null;
    }

    @Override
    public void onNpcSpawned(NpcSpawned event) {
        if (!stateChangeCooldown && event.getNpc().getId() == MINIMUS_NPC_ID) {
            prepareNextWave();
        }
        super.onNpcSpawned(event);
    }

    @Override
    public void onNpcDespawned(NpcDespawned event) {
        if (event.getNpc().getId() == MINIMUS_NPC_ID) {
            if (getState() == ChallengeState.STARTING) {
                setState(ChallengeState.ACTIVE);
            }

            if (getState() == ChallengeState.ACTIVE) {
                stateChangeCooldown = true;
                deferredTask = new DeferredTask(
                        () -> {
                            stateChangeCooldown = false;

                            if (!waveHandicapOptions.isEmpty()) {
                                // The debuff selection varbit stores the index of the selected debuff option, starting
                                // from 1.
                                int selectedDebuffIndex = client.getVarbitValue(HANDICAP_SELECTION_VARBIT_ID) - 1;
                                Handicap selectedHandicap = waveHandicapOptions.get(selectedDebuffIndex);
                                if (waveDataTracker != null) {
                                    waveDataTracker.setHandicapOptions(waveHandicapOptions.toArray(new Handicap[3]));
                                    waveDataTracker.setHandicap(selectedHandicap);
                                }
                            }
                        },
                        3);
            }
        }
        super.onNpcDespawned(event);
    }

    @Override
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (event.getGameObject().getId() == REWARD_CHEST_OBJECT_ID) {
            if (deferredTask != null) {
                deferredTask.cancel();
            }
            queueFinishColosseum(ChallengeState.COMPLETE);
        }
        super.onGameObjectSpawned(event);
    }

    @Override
    public void onChatMessage(ChatMessage event) {
        if (!getState().isInactive() && event.getType() == ChatMessageType.GAMEMESSAGE) {
            Matcher matcher =
                    ColosseumChallenge.COLOSSEUM_END_REGEX.matcher(ChatText.stripFormatting(event.getMessage()));
            if (matcher.find()) {
                try {
                    reportedChallengeTicks = Tick.fromTimeString(matcher.group(1))
                            .map(Pair::getLeft)
                            .orElse(-1);
                } catch (Exception e) {
                    reportedChallengeTicks = -1;
                }
            }
        }
        super.onChatMessage(event);
    }

    @Override
    public void onScriptPreFired(ScriptPreFired event) {
        if (!getState().isInactive() && event.getScriptId() == HANDICAP_SELECTION_SCRIPT_ID) {
            waveHandicapOptions.clear();
            Object[] scriptArgs = event.getScriptEvent().getArguments();
            waveHandicapOptions.add(Handicap.withId((int) scriptArgs[2]));
            waveHandicapOptions.add(Handicap.withId((int) scriptArgs[3]));
            waveHandicapOptions.add(Handicap.withId((int) scriptArgs[4]));
        }

        super.onScriptPreFired(event);
    }

    private void checkColosseumState() {
        WorldPoint playerLocation =
                Location.getWorldLocation(client, client.getLocalPlayer().getWorldLocation());
        if (playerLocation == null) {
            return;
        }

        if (getState().isInactive()) {
            if (COLOSSEUM_AREA.contains(playerLocation)) {
                startColosseum();
            }
        } else if (playerLocation.getRegionID() != COLOSSEUM_REGION_ID) {
            clearWaveDataTracker();
            if (getState() == ChallengeState.COMPLETE) {
                setState(ChallengeState.INACTIVE);
            } else {
                queueFinishColosseum(ChallengeState.INACTIVE);
            }
        }
    }

    private void startColosseum() {
        // TODO(frolv): Check for late starts (e.g. plugin being turned on mid-challenge).
        log.debug("Starting Colosseum challenge");
        setState(ChallengeState.STARTING);
        reportedChallengeTicks = -1;

        List<String> usernames = getParty().stream().map(Raider::getUsername).collect(Collectors.toList());
        dispatchEvent(new ChallengeStartEvent(
                getChallenge(), ChallengeMode.NO_MODE, Stage.COLOSSEUM_WAVE_1, usernames, false));
    }

    private void queueFinishColosseum(ChallengeState state) {
        stateChangeCooldown = true;
        deferredTask = new DeferredTask(() -> finishColosseum(state), 3);
    }

    private void finishColosseum(ChallengeState state) {
        log.debug("Ending Colosseum challenge: {}", state);
        stateChangeCooldown = false;
        int reported = reportedChallengeTicks;

        cleanup();
        dispatchEvent(new ChallengeEndEvent(reported, -1));
        setState(state);
    }

    private void prepareNextWave() {
        if (waveDataTracker != null) {
            clearWaveDataTracker();
        }

        currentWave++;
        if (currentWave < 13) {
            waveDataTracker = new WaveDataTracker(this, client, currentWave, recordedChallengeTicks);
        }
    }

    private void clearWaveDataTracker() {
        if (waveDataTracker != null) {
            waveDataTracker.terminate();
            recordedChallengeTicks += waveDataTracker.getTotalTicks();
            waveDataTracker = null;
        }
    }

    private void cleanup() {
        clearWaveDataTracker();
        reset();
    }

    private void reset() {
        currentWave = 0;
        recordedChallengeTicks = 0;
        reportedChallengeTicks = -1;
        stateChangeCooldown = false;
        waveHandicapOptions.clear();
    }
}
