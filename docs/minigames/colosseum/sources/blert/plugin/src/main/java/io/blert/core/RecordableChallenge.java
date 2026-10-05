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

package io.blert.core;

import io.blert.events.*;
import java.util.*;
import java.util.concurrent.CompletableFuture;
import javax.annotation.Nullable;
import lombok.AccessLevel;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Client;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.util.Text;

@Slf4j
public abstract class RecordableChallenge implements RuneliteEventHandler {
    @Getter
    private final Challenge challenge;

    @Getter
    private @NonNull ChallengeMode challengeMode;

    protected final Client client;

    @Getter
    private final ClientThread clientThread;

    @Getter
    private AttackRegistry attackRegistry;

    @Getter
    private SpellRegistry spellRegistry;

    private final List<EventHandler> eventHandlers = new ArrayList<>();
    List<Event> pendingEvents = new ArrayList<>();

    @Getter
    private ChallengeState state = ChallengeState.INACTIVE;

    private final List<CompletableFuture<Status>> statusUpdateFutures = new ArrayList<>();

    @Getter
    @AllArgsConstructor(access = AccessLevel.PROTECTED)
    public static class Status {
        private final Challenge challenge;
        private final ChallengeMode mode;
        private final Stage stage;
        private final List<String> party;

        @Override
        public String toString() {
            return "Status(" + "challenge="
                    + challenge + ", mode="
                    + mode + ", stage="
                    + stage + ", party="
                    + party + ')';
        }
    }

    /**
     * Players in the challenge party.
     */
    private final Map<String, Raider> party = new LinkedHashMap<>();

    protected RecordableChallenge(Challenge challenge, Client client, ClientThread clientThread) {
        this.challenge = challenge;
        this.challengeMode = ChallengeMode.NO_MODE;
        this.client = client;
        this.clientThread = clientThread;
    }

    /**
     * Checks if the given world point is within the location of the challenge.
     *
     * @param worldPoint The world point to check.
     * @return True if the world point is within the location of the challenge, false otherwise.
     */
    public abstract boolean containsLocation(WorldPoint worldPoint);

    /**
     * Implementation-specific initialization handler.
     */
    protected abstract void onInitialize();

    /**
     * Implementation-specific termination handler.
     */
    protected abstract void onTerminate();

    /**
     * Implementation-specific game tick handler.
     */
    protected abstract void onTick();

    protected abstract @Nullable Stage getStage();

    public String getName() {
        return challenge.getName();
    }

    public int getScale() {
        return inChallenge() ? party.size() : 0;
    }

    public int getLivingRaiderCount() {
        return (int) party.values().stream().filter(Raider::isAlive).count();
    }

    public Collection<Raider> getParty() {
        return party.values();
    }

    public boolean playerIsInChallenge(@Nullable String username) {
        return username != null && party.containsKey(Text.standardize(username));
    }

    public @Nullable Raider getRaider(@Nullable String username) {
        return username != null ? party.get(Text.standardize(username)) : null;
    }

    /**
     * Resets the challenge mode to {@link ChallengeMode#NO_MODE}. Must be called when a challenge ends to prevent
     * stale mode values from carrying over to the next challenge.
     */
    public void resetMode() {
        if (challengeMode != ChallengeMode.NO_MODE) {
            log.debug("Mode reset from {} (state={})", challengeMode, state);
        }
        challengeMode = ChallengeMode.NO_MODE;
    }

    /**
     * Updates the challenge mode. If the challenge is active and the mode has changed, an update event is dispatched.
     * Updates to {@link ChallengeMode#NO_MODE} are ignored.
     *
     * @param mode The new challenge mode.
     */
    public void updateMode(ChallengeMode mode) {
        if (mode == ChallengeMode.NO_MODE) {
            return;
        }

        if (challengeMode != mode) {
            log.debug("Mode changed from {} to {}", challengeMode, mode);
            challengeMode = mode;

            if (state == ChallengeState.STARTING || state == ChallengeState.ACTIVE) {
                dispatchEvent(new ChallengeUpdateEvent(mode));
            }
        }
    }

    protected void setState(ChallengeState state) {
        if (this.state == ChallengeState.PREPARING && state != ChallengeState.PREPARING) {
            Status status = currentStatus();
            statusUpdateFutures.forEach(future -> future.complete(status));
            statusUpdateFutures.clear();
        }

        this.state = state;
    }

    protected void addRaider(Raider raider) {
        party.put(Text.standardize(raider.getUsername()), raider);
    }

    protected void resetParty() {
        party.clear();
    }

    public void initialize(EventHandler handler, AttackRegistry attackRegistry, SpellRegistry spellRegistry) {
        this.attackRegistry = attackRegistry;
        this.spellRegistry = spellRegistry;
        onInitialize();
        addEventHandler(handler);
    }

    public void terminate() {
        if (state == ChallengeState.STARTING) {
            // Player left the challenge area without ever entering their new challenge.
            dispatchEvent(new ChallengeEndEvent(-1, -1, true));
        }

        onTerminate();

        state = ChallengeState.INACTIVE;
        eventHandlers.clear();
        party.clear();
    }

    public void addEventHandler(EventHandler handler) {
        eventHandlers.add(handler);
    }

    public void removeEventHandler(EventHandler handler) {
        eventHandlers.remove(handler);
    }

    public void tick() {
        onTick();
    }

    public boolean inChallenge() {
        return state.inChallenge();
    }

    public CompletableFuture<Status> getStatus() {
        if (state == ChallengeState.INACTIVE) {
            return CompletableFuture.completedFuture(null);
        }

        if (state == ChallengeState.PREPARING) {
            CompletableFuture<Status> future = new CompletableFuture<>();
            statusUpdateFutures.add(future);
            return future;
        }

        return CompletableFuture.completedFuture(currentStatus());
    }

    public void dispatchEvent(Event event) {
        if (state == ChallengeState.INACTIVE || state == ChallengeState.PREPARING) {
            if (event.getType() != EventType.CHALLENGE_START && event.getType() != EventType.CHALLENGE_END) {
                pendingEvents.add(event);
                return;
            }
        }

        for (EventHandler eventHandler : eventHandlers) {
            eventHandler.handleEvent(client.getTickCount(), event);
        }
    }

    protected void dispatchPendingEvents() {
        pendingEvents.forEach(this::dispatchEvent);
        pendingEvents.clear();
    }

    protected void clearPendingEvents() {
        pendingEvents.clear();
    }

    private Status currentStatus() {
        return new Status(challenge, challengeMode, getStage(), new ArrayList<>(party.keySet()));
    }

    /**
     * Returns the currently active data tracker for this challenge, if any.
     */
    @Nullable
    protected abstract DataTracker getActiveTracker();

    @Override
    public void onGameStateChanged(GameStateChanged event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGameStateChanged(event);
        }
    }

    @Override
    public void onNpcSpawned(NpcSpawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onNpcSpawned(event);
        }
    }

    @Override
    public void onNpcDespawned(NpcDespawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onNpcDespawned(event);
        }
    }

    @Override
    public void onNpcChanged(NpcChanged event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onNpcChanged(event);
        }
    }

    @Override
    public void onPlayerChanged(PlayerChanged event) {
        // Bypass the data tracker for equipment snapshots to catch changes
        // occurring between stages.
        Player player = event.getPlayer();
        Raider raider = getRaider(player.getName());
        if (raider != null) {
            raider.snapshotEquipmentChanges(client, player.getPlayerComposition());
        }

        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onPlayerChanged(event);
        }
    }

    @Override
    public void onAnimationChanged(AnimationChanged event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onAnimationChanged(event);
        }
    }

    @Override
    public void onProjectileMoved(ProjectileMoved event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onProjectileMoved(event);
        }
    }

    @Override
    public void onChatMessage(ChatMessage event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onChatMessage(event);
        }
    }

    @Override
    public void onHitsplatApplied(HitsplatApplied event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onHitsplatApplied(event);
        }
    }

    @Override
    public void onGameObjectSpawned(GameObjectSpawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGameObjectSpawned(event);
        }
    }

    @Override
    public void onGameObjectDespawned(GameObjectDespawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGameObjectDespawned(event);
        }
    }

    @Override
    public void onGroundObjectSpawned(GroundObjectSpawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGroundObjectSpawned(event);
        }
    }

    @Override
    public void onGroundObjectDespawned(GroundObjectDespawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGroundObjectDespawned(event);
        }
    }

    @Override
    public void onGraphicChanged(GraphicChanged event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGraphicChanged(event);
        }
    }

    @Override
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onGraphicsObjectCreated(event);
        }
    }

    @Override
    public void onActorDeath(ActorDeath event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onActorDeath(event);
        }
    }

    @Override
    public void onItemSpawned(ItemSpawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onItemSpawned(event);
        }
    }

    @Override
    public void onItemDespawned(ItemDespawned event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onItemDespawned(event);
        }
    }

    @Override
    public void onVarbitChanged(VarbitChanged event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onVarbitChanged(event);
        }
    }

    @Override
    public void onScriptPreFired(ScriptPreFired event) {
        DataTracker tracker = getActiveTracker();
        if (tracker != null) {
            tracker.onScriptPreFired(event);
        }
    }
}
