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

package io.blert.challenges.tob;

import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.challenges.tob.rooms.bloat.BloatDataTracker;
import io.blert.challenges.tob.rooms.maiden.MaidenDataTracker;
import io.blert.challenges.tob.rooms.nylocas.NylocasDataTracker;
import io.blert.challenges.tob.rooms.sotetseg.SotetsegDataTracker;
import io.blert.challenges.tob.rooms.verzik.VerzikDataTracker;
import io.blert.challenges.tob.rooms.xarpus.XarpusDataTracker;
import io.blert.core.*;
import io.blert.events.ChallengeEndEvent;
import io.blert.events.ChallengeStartEvent;
import io.blert.events.StageUpdateEvent;
import io.blert.util.DeferredTask;
import io.blert.util.Tick;
import java.util.Arrays;
import java.util.List;
import java.util.function.BiConsumer;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import joptsimple.internal.Strings;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.Client;
import net.runelite.api.Player;
import net.runelite.api.Varbits;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.NpcSpawned;
import net.runelite.api.events.VarbitChanged;
import net.runelite.client.callback.ClientThread;
import net.runelite.client.util.Text;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class TheatreChallenge extends RecordableChallenge {
    private static final int TOB_ROOM_STATUS_VARBIT = 6447;
    private static final int TOB_PARTY_LIST_COMPONENT_ID = 1835020;

    private static final int TOB_VARBIT_NO_PARTY = 0;
    private static final int TOB_VARBIT_IN_PARTY = 1;
    private static final int TOB_VARBIT_RAID_STARTED = 2;

    private static final Pattern RAID_ENTRY_REGEX_1P =
            Pattern.compile("You enter the Theatre of Blood \\((\\w+) Mode\\)\\.\\.\\.");
    private static final Pattern RAID_ENTRY_REGEX_3P = Pattern.compile(
            "(.+) has entered the Theatre of Blood \\((\\w+) Mode\\). Step inside to join (her|him|them)\\.\\.\\.");
    private static final Pattern RAID_COMPLETION_CHALLENGE_REGEX =
            Pattern.compile("^.+Theatre of Blood completion time: (" + Tick.TIME_STRING_REGEX + ").*");
    private static final Pattern RAID_COMPLETION_OVERALL_REGEX =
            Pattern.compile("Theatre of Blood total completion time: (" + Tick.TIME_STRING_REGEX + ").*");

    private Location location = Location.ELSEWHERE;
    private boolean locationChangedThisTick = false;

    private RoomState roomState = RoomState.INACTIVE;

    @Nullable
    RoomDataTracker roomDataTracker = null;

    private int reportedChallengeTime = -1;

    private @Nullable DeferredTask deferredTask = null;

    public TheatreChallenge(Client client, ClientThread clientThread) {
        super(Challenge.TOB, client, clientThread);
    }

    @Nullable
    @Override
    protected DataTracker getActiveTracker() {
        return roomDataTracker;
    }

    @Override
    public boolean containsLocation(WorldPoint worldPoint) {
        return Location.fromWorldPoint(worldPoint) != Location.ELSEWHERE;
    }

    @Override
    protected void onInitialize() {
        reportedChallengeTime = -1;
    }

    @Override
    protected void onTerminate() {
        clearRoomDataTracker();
        resetParty();
        reportedChallengeTime = -1;
    }

    @Override
    protected void onTick() {
        updateLocation();

        if (location == Location.LOBBY && client.getVarbitValue(Varbits.THEATRE_OF_BLOOD) == TOB_VARBIT_IN_PARTY) {
            // If the player is in a party in the raid lobby, grab the party information from the party widget.
            initializePartyFromLobby();
        }

        if (deferredTask != null) {
            deferredTask.tick();
        }

        if (getState().isInactive() && location.inRaid()) {
            queueRaidStart(ChallengeMode.NO_MODE, true);
        } else if (getState() == ChallengeState.STARTING && location.inRaid()) {
            setState(ChallengeState.ACTIVE);
        } else if (!getState().isInactive() && !location.inRaid()) {
            // The player has left the raid.
            switch (getState()) {
                case COMPLETE:
                    setState(ChallengeState.INACTIVE);
                    break;
                case ACTIVE:
                    queueRaidEnd(ChallengeState.INACTIVE);
                    break;
                case STARTING:
                case PREPARING:
                    if (locationChangedThisTick) {
                        // If the player leaves the raid before it starts, cancel the start.
                        queueRaidEnd(ChallengeState.INACTIVE);
                    }
                    break;
                case ENDING:
                case INACTIVE:
                    // Do nothing.
                    break;
            }
        }

        locationChangedThisTick = false;
        if (!inChallenge()) {
            return;
        }

        updatePartyInformation();

        if (roomDataTracker != null) {
            roomDataTracker.checkEntry();

            if (roomDataTracker.notStarted() && roomState.isActive() && roomDataTracker.playersAreInRoom()) {
                // The room may already be active when entered (e.g. as a spectator); start its tracker.
                roomDataTracker.startRoom();
                log.debug("Room {} started via activity check", roomDataTracker.getRoom());
            }

            roomDataTracker.tick();
        }
    }

    @Nullable
    @Override
    protected Stage getStage() {
        return roomDataTracker != null ? roomDataTracker.getStage() : null;
    }

    /**
     * Invokes a callback for each username displayed in the ToB orb list.
     *
     * @param callback Function to run on each user.
     */
    public void forEachOrb(BiConsumer<Integer, String> callback) {
        // ID of the client string containing the username of the first member in a ToB party. Subsequent party members'
        // usernames (if present) occupy the following four IDs.
        final int TOB_P1_VARCSTR_ID = 330;
        for (int player = 0; player < 5; player++) {
            String username = client.getVarcStrValue(TOB_P1_VARCSTR_ID + player);
            if (!Strings.isNullOrEmpty(username)) {
                callback.accept(player, Text.sanitize(username));
            }
        }
    }

    private void queueRaidStart(ChallengeMode mode, boolean reinitializeParty) {
        log.info("Starting new raid");
        updateMode(mode);
        setState(ChallengeState.PREPARING);
        reportedChallengeTime = -1;

        if (reinitializeParty) {
            // When players join the raid, the orb list does not immediately update, nor does it update all at once.
            // (Some players may take longer to load in, thanks Jagex!) Therefore, wait a few ticks before starting.
            // This value is arbitrary and may need to be adjusted.
            final int TICKS_TO_DELAY_ORB_CHECK = 5;
            deferredTask = new DeferredTask(
                    () -> {
                        initializePartyFromOrbs();
                        startRaid();
                    },
                    TICKS_TO_DELAY_ORB_CHECK);
        } else {
            deferredTask = new DeferredTask(this::startRaid, 1);
        }
    }

    private void startRaid() {
        deferredTask = null;

        updateLocation();
        setState(location.inRaid() ? ChallengeState.ACTIVE : ChallengeState.STARTING);

        boolean spectator = !playerIsInChallenge(client.getLocalPlayer().getName());

        List<String> names = getParty().stream().map(Raider::getUsername).collect(Collectors.toList());
        Stage stage = null;

        if (this.roomDataTracker != null) {
            // Raid scale information is not immediately available when the raid starts, so if any NPCs have already
            // spawned, their hitpoints must be corrected after the scale is known.
            this.roomDataTracker.correctNpcHitpointsForScale(getScale());
            stage = this.roomDataTracker.getStage();
        }

        dispatchEvent(new ChallengeStartEvent(getChallenge(), getChallengeMode(), stage, names, spectator));

        // Dispatch any events that were queued before the raid started.
        dispatchPendingEvents();
    }

    private void queueRaidEnd(ChallengeState state) {
        queueRaidEnd(state, -1);
    }

    private void queueRaidEnd(ChallengeState state, int overallTime) {
        if (getState() == ChallengeState.PREPARING) {
            // If a raid start is pending, cancel it.
            setState(ChallengeState.INACTIVE);
            resetMode();
            clearPendingEvents();
            resetParty();
            deferredTask = null;
            log.info("Raid ended before it started");
            return;
        }

        setState(ChallengeState.ENDING);
        deferredTask = new DeferredTask(() -> endRaid(state, overallTime), 3);
    }

    private void endRaid(ChallengeState state, int overallTime) {
        final int challengeTime = reportedChallengeTime;
        log.info(
                "Raid completed; challenge {}, overall {}",
                challengeTime == -1 ? "unknown" : Tick.asTimeString(reportedChallengeTime),
                overallTime == -1 ? "unknown" : Tick.asTimeString(overallTime));

        // Spectators should send a soft end so they don't affect other recorders.
        boolean soft = !playerIsInChallenge(client.getLocalPlayer().getName());

        clearRoomDataTracker();
        setState(state);
        resetMode();
        resetParty();

        getClientThread().invokeAtTickEnd(() -> dispatchEvent(new ChallengeEndEvent(challengeTime, overallTime, soft)));

        reportedChallengeTime = -1;
    }

    /**
     * Checks the location of the logged-in player, initializing the appropriate {@link RoomDataTracker} if a new room
     * has been entered.
     */
    private void updateLocation() {
        Player player = client.getLocalPlayer();
        if (player == null) {
            return;
        }

        Location loc = Location.fromWorldPoint(WorldPoint.fromLocalInstance(client, player.getLocalLocation()));
        if (location == loc) {
            return;
        }

        log.debug("Location changed to {}", loc);
        location = loc;

        if (location == Location.ELSEWHERE) {
            // ELSEWHERE should only occur during a raid when logging out then
            // back in (since actually going ELSEWHERE would terminate the
            // TheatreChallenge). Don't reinitialize the data tracker.
            locationChangedThisTick = true;
            return;
        }

        if (roomDataTracker == null || !location.inRoomInstance(roomDataTracker.getRoom())) {
            // When entering a new instance for the first time, its room data tracker must be initialized.
            initializeRoomDataTracker();
        }

        locationChangedThisTick = true;
    }

    @Override
    public void onNpcSpawned(NpcSpawned npcSpawned) {
        updateLocation();
        super.onNpcSpawned(npcSpawned);
    }

    @Override
    public void onVarbitChanged(VarbitChanged varbit) {
        if (varbit.getVarbitId() == Varbits.THEATRE_OF_BLOOD) {
            if (getState().isInactive() && varbit.getValue() == TOB_VARBIT_NO_PARTY) {
                // Party was disbanded while in the lobby.
                resetParty();
            } else if (getState().isInactive() && varbit.getValue() == TOB_VARBIT_RAID_STARTED) {
                // A raid start due to a varbit change usually means that the player is joining late, rejoining, or
                // spectating. Party and mode information is not immediately available.
                log.debug("Raid started via varbit change");
                queueRaidStart(ChallengeMode.NO_MODE, true);
            } else if (getState().inChallenge() && varbit.getValue() < TOB_VARBIT_RAID_STARTED) {
                switch (getState()) {
                    case COMPLETE:
                        setState(ChallengeState.INACTIVE);
                        break;
                    case STARTING:
                    case PREPARING:
                        // Raid ended while the player is still in the lobby.
                        log.debug("Raid ended via varbit change while in state {}", getState());
                        queueRaidEnd(ChallengeState.INACTIVE);
                        break;
                }
            }
        }

        int roomStatus = client.getVarbitValue(TOB_ROOM_STATUS_VARBIT);
        var state = RoomState.fromVarbit(roomStatus);
        if (state.isEmpty()) {
            log.error("Unknown value for room status varbit: {}", varbit.getValue());
            return;
        }

        RoomState previousState = roomState;
        roomState = state.get();
        if (previousState != roomState) {
            log.debug("Room status changed from {} to {}", previousState, roomState);
        }

        super.onVarbitChanged(varbit);
    }

    @Override
    public void onChatMessage(ChatMessage message) {
        updateLocation();

        if (message.getType() == ChatMessageType.GAMEMESSAGE) {
            String stripped = Text.removeTags(message.getMessage());

            if (getState().isInactive()) {
                // Listen for a chat message indicating the start of a raid, and queue the start action immediately
                // instead of waiting to enter.
                Matcher matcher = RAID_ENTRY_REGEX_1P.matcher(stripped);
                if (matcher.matches()) {
                    log.debug("Raid started via 1p chat message (mode: {})", matcher.group(1));
                    queueRaidStart(
                            ChallengeMode.parseTob(matcher.group(1)).orElse(ChallengeMode.NO_MODE),
                            getParty().isEmpty());
                    super.onChatMessage(message);
                    return;
                }

                matcher = RAID_ENTRY_REGEX_3P.matcher(stripped);
                if (matcher.matches()) {
                    log.debug(
                            "Raid started via 3p chat message (leader: {} mode: {})",
                            matcher.group(1),
                            matcher.group(2));
                    queueRaidStart(
                            ChallengeMode.parseTob(matcher.group(2)).orElse(ChallengeMode.NO_MODE),
                            getParty().isEmpty());
                }
                super.onChatMessage(message);
                return;
            }

            Matcher matcher = RAID_COMPLETION_CHALLENGE_REGEX.matcher(stripped);
            if (matcher.matches()) {
                reportedChallengeTime =
                        Tick.fromTimeString(matcher.group(1)).map(Pair::getLeft).orElse(-1);
            }

            matcher = RAID_COMPLETION_OVERALL_REGEX.matcher(stripped);
            if (matcher.matches()) {
                int overallTime =
                        Tick.fromTimeString(matcher.group(1)).map(Pair::getLeft).orElse(-1);
                queueRaidEnd(ChallengeState.COMPLETE, overallTime);
            }
        }

        super.onChatMessage(message);
    }

    /**
     * Collects party members' names from the lobby party widget.
     */
    private void initializePartyFromLobby() {
        var tobPartyWidget = client.getWidget(TOB_PARTY_LIST_COMPONENT_ID);
        if (tobPartyWidget == null) {
            return;
        }

        resetParty();

        Arrays.stream(tobPartyWidget.getText().split("<br>"))
                .filter(s -> !s.equals("-"))
                .map((name) -> Text.sanitize(name).replace(" (R)", ""))
                .forEach(s ->
                        addRaider(new Raider(s, s.equals(client.getLocalPlayer().getName()))));
    }

    private void initializePartyFromOrbs() {
        resetParty();
        String localPlayer = client.getLocalPlayer().getName();
        forEachOrb((orb, username) -> addRaider(new Raider(username, username.equals(localPlayer))));
    }

    private void updatePartyInformation() {
        // Toggle the players who are currently connected to the raid.
        getParty().forEach(r -> r.setActive(false));
        forEachOrb((orb, username) -> {
            Raider raider = getRaider(Text.standardize(username));
            if (raider != null) {
                raider.setActive(true);
            }
        });
    }

    private void clearRoomDataTracker() {
        if (roomDataTracker != null) {
            roomDataTracker.terminate();
            removeEventHandler(roomDataTracker);
            roomDataTracker = null;
        }
    }

    private void initializeRoomDataTracker() {
        clearRoomDataTracker();

        if (location.inMaidenInstance()) {
            roomDataTracker = new MaidenDataTracker(this, client);
        } else if (location.inBloatInstance()) {
            roomDataTracker = new BloatDataTracker(this, client);
        } else if (location.inNylocasInstance()) {
            roomDataTracker = new NylocasDataTracker(this, client);
        } else if (location.inSotetsegInstance()) {
            roomDataTracker = new SotetsegDataTracker(this, client);
        } else if (location.inXarpusInstance()) {
            roomDataTracker = new XarpusDataTracker(this, client);
        } else if (location.inVerzikInstance()) {
            roomDataTracker = new VerzikDataTracker(this, client);
        }

        if (roomDataTracker != null) {
            log.info("Initialized room data tracker for {} from {}", roomDataTracker.getRoom(), location);
            addEventHandler(roomDataTracker);
            dispatchEvent(new StageUpdateEvent(roomDataTracker.getStage(), 0, StageUpdateEvent.Status.ENTERED));
            getParty().forEach(Raider::resetForNewRoom);
        }
    }
}
