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

package io.blert.challenges.tob.rooms;

import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.Location;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.core.ChallengeState;
import io.blert.core.DataTracker;
import io.blert.core.Hitpoints;
import io.blert.core.Raider;
import io.blert.events.ChallengeUpdateEvent;
import io.blert.events.Event;
import io.blert.events.EventHandler;
import io.blert.events.PlayerDeathEvent;
import io.blert.util.Tick;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.HitsplatApplied;
import net.runelite.api.events.VarbitChanged;
import net.runelite.client.util.Text;

@Slf4j
public abstract class RoomDataTracker extends DataTracker implements EventHandler {
    private static final int TOB_HITPOINTS_VARBIT = 6448;

    protected final TheatreChallenge theatreChallenge;

    @Getter
    private final Room room;

    private final Pattern waveEndRegex;

    private final boolean startOnEntry;
    private boolean shouldUpdateHitpoints;
    private int healTick = -1;

    protected RoomDataTracker(TheatreChallenge theatreChallenge, Client client, Room room, boolean startOnEntry) {
        super(theatreChallenge, client, room.toStage());
        this.theatreChallenge = theatreChallenge;
        this.room = room;
        this.waveEndRegex = Pattern.compile("Wave '" + room.waveName() + "' \\(\\w+ Mode\\) " + "complete!Duration: ("
                + Tick.TIME_STRING_REGEX + ")");
        this.startOnEntry = startOnEntry;
    }

    protected RoomDataTracker(TheatreChallenge theatreChallenge, Client client, Room room) {
        this(theatreChallenge, client, room, false);
    }

    public void startRoom() {
        if (getState() != State.NOT_STARTED) {
            return;
        }

        super.start();

        // Players may leave between rooms; send the current party when the room starts.
        if (theatreChallenge.getState() != ChallengeState.PREPARING) {
            List<String> party = new ArrayList<>();
            theatreChallenge.forEachOrb((orb, username) -> party.add(username));
            dispatchEvent(new ChallengeUpdateEvent(theatreChallenge.getChallengeMode(), party));
        }

        onRoomStart();
    }

    /**
     * Checks whether players are in the room and starts the room if they are.
     */
    public void checkEntry() {
        if (getState() == State.NOT_STARTED && this.startOnEntry) {
            if (playersAreInRoom()) {
                log.debug("Room {} started because player entered", room);
                startRoom();
            }
        }
    }

    @Override
    protected void onTick() {
        int tick = getTick();

        checkForDeaths();

        // The hitpoints varbit is delayed by up to 3 ticks, so don't update immediately following a heal as it may
        // undo the hitpoints added by the heal.
        if (shouldUpdateHitpoints && (healTick == -1 || tick - healTick > 3)) {
            getTrackedNpcs().stream()
                    .filter(npc -> npc instanceof HpVarbitTrackedNpc)
                    .map(npc -> (HpVarbitTrackedNpc) npc)
                    .findFirst()
                    .ifPresent(npc -> npc.updateHitpointsFromVarbit(client.getVarbitValue(TOB_HITPOINTS_VARBIT)));
            shouldUpdateHitpoints = false;
        }
    }

    /**
     * Checks if any players are located within the boundaries of the boss room.
     *
     * @return True if there is at least one player within the room.
     */
    public boolean playersAreInRoom() {
        return client.getTopLevelWorldView().players().stream()
                .filter(player -> theatreChallenge.playerIsInChallenge(player.getName()))
                .anyMatch(player ->
                        Location.fromWorldPoint(getWorldLocation(player)).inRoom(room));
    }

    /**
     * Updates the base and current hitpoints of all NPCs in the room to match the given raid scale.
     *
     * @param scale The raid scale.
     */
    public void correctNpcHitpointsForScale(int scale) {
        getTrackedNpcs().forEach(trackedNpc -> {
            NPC npc = trackedNpc.getNpc();
            int healthRatio = npc.getHealthRatio();
            int healthScale = npc.getHealthScale();

            if (healthScale > 0 && healthRatio != -1) {
                double percent = healthRatio / (double) healthScale;
                TobNpc tobNpc = TobNpc.withId(npc.getId()).orElseThrow();
                trackedNpc.setHitpoints(Hitpoints.fromRatio(percent, tobNpc.getBaseHitpoints(scale)));
            }
        });
    }

    /**
     * Initialization method invoked when the room is first started.
     */
    protected abstract void onRoomStart();

    /**
     * Handler function for Blert events which originate from the data tracker.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event to handle.
     */
    protected void onBlertEvent(Event event) {}

    public void handleEvent(int clientTick, Event event) {
        if (!terminating()) {
            onBlertEvent(event);
        }
    }

    @Override
    protected void onGameState(GameStateChanged event) {
        if (event.getGameState() == GameState.LOGIN_SCREEN && inProgress()) {
            Player localPlayer = client.getLocalPlayer();
            if (localPlayer == null) {
                return;
            }
            Raider raider = theatreChallenge.getRaider(localPlayer.getName());
            if (raider != null && raider.isAlive()) {
                // Logging out during a ToB room is considered a death.
                int tick = getTick();
                raider.setDead(tick);
                WorldPoint deathPoint = getWorldLocation(localPlayer);
                dispatchEvent(new PlayerDeathEvent(getStage(), tick, deathPoint, raider.getUsername()));
            }
        }
    }

    @Override
    protected final void onMessage(ChatMessage chatMessage) {
        String stripped = Text.removeTags(chatMessage.getMessage());
        Matcher matcher = waveEndRegex.matcher(stripped);
        if (matcher.find()) {
            try {
                String inGameTime = matcher.group(1);
                finish(inGameTime);
            } catch (IndexOutOfBoundsException e) {
                log.warn("Could not parse timestamp from wave end message: {}", stripped);
                finish(true);
            }
        }
    }

    @Override
    protected void onHitsplat(HitsplatApplied event) {
        if (event.getActor() instanceof NPC && event.getHitsplat().getHitsplatType() == HitsplatID.HEAL) {
            getTrackedNpcs().getByNpc((NPC) event.getActor()).ifPresent(npc -> {
                if (npc instanceof HpVarbitTrackedNpc) {
                    healTick = getTick();
                }
            });
        }
    }

    @Override
    protected void onVarbit(VarbitChanged event) {
        if (event.getVarbitId() == TOB_HITPOINTS_VARBIT) {
            shouldUpdateHitpoints = true;
        }
    }

    protected String formattedRoomTime() {
        return Tick.asTimeString(getTick());
    }

    protected final int getBossHitpointsVarbitValue() {
        return client.getVarbitValue(TOB_HITPOINTS_VARBIT);
    }

    private void checkForDeaths() {
        final int tick = getTick();

        getChallenge().getParty().forEach(raider -> {
            if (!raider.isActive() && raider.isAlive()) {
                // Disconnecting during a ToB room is considered a death.
                raider.setDead(tick);
            }
        });

        // Check party orb health for dead players to catch any deaths that occurred before the client joined as a
        // spectator or while the client was disconnected.
        theatreChallenge.forEachOrb((orb, username) -> {
            Raider raider = theatreChallenge.getRaider(Text.standardize(username));
            if (raider == null) {
                log.warn("Player {} is in orb list but not in party", username);
                return;
            }

            // ToB orb health. 0 = hide, 1-27 = health percentage (0-100%), 30 = dead.
            int orbHealth = client.getVarbitValue(Varbits.THEATRE_OF_BLOOD_ORB1 + orb);
            if (orbHealth == 30 && !raider.isDead()) {
                raider.setDead(tick);
            }
        });
    }
}
