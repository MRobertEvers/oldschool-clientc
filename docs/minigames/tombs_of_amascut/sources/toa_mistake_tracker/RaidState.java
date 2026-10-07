package com.toamistaketracker;

import com.google.common.collect.ImmutableList;
import com.toamistaketracker.events.InRaidChanged;
import com.toamistaketracker.events.RaidEntered;
import com.toamistaketracker.events.RaidRoomChanged;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.GameState;
import net.runelite.api.Player;
import net.runelite.api.coords.LocalPoint;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.GameStateChanged;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.ScriptPostFired;
import net.runelite.client.eventbus.EventBus;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.RAID_LOBBY_INSIDE;
import static com.toamistaketracker.RaidRoom.RAID_LOBBY_OUTSIDE;

@Slf4j
@Singleton
@RequiredArgsConstructor(onConstructor_ = @Inject)
public class RaidState {

    private static final int TOA_HUD_INIT_STATUS_NAMES_SCRIPT_ID = 6585;
    private static final int TOA_RAIDERS_VARC_START = 1099;
    private static final int MAX_RAIDERS = 8;

    private final Client client;
    private final EventBus eventBus;

    @Getter
    private boolean inRaid;
    @Getter
    private RaidRoom currentRoom;
    @Getter
    private final Map<String, Raider> raiders = new HashMap<>(); // name -> raider

    private int prevRegion;
    private boolean newRaid;

    public void startUp() {
        clearState();
        eventBus.register(this);
    }

    public void shutDown() {
        eventBus.unregister(this);
        clearState();
    }

    private void clearState() {
        inRaid = false;
        currentRoom = null;
        raiders.clear();
        prevRegion = -1;
        newRaid = false;
    }

    @Subscribe(priority = 5)
    public void onGameTick(GameTick e) {
        if (client.getGameState() != GameState.LOGGED_IN) return;

        int newRegion = getRegion();
        if (newRegion == -1) return;

        if (prevRegion != newRegion) {
            regionChanged(newRegion);
        }
        prevRegion = newRegion;

        if (!inRaid) {
            raiders.clear();
            return;
        }

        // If we still haven't loaded any raiders, keep trying. This can happen if the plugin is turned on mid-raid,
        // after the script can run. Or if the script runs but the relevant players aren't actually in the raid
        // yet for the client to retrieve.
        if (raiders.isEmpty()) {
            tryLoadRaiders();
        }
    }

    @Subscribe
    public void onGameStateChanged(GameStateChanged event) {
        if (event.getGameState() == GameState.LOADING) {
            // If there are still raiders, they can't be dead anymore after loading.
            for (Raider raider : raiders.values()) {
                raider.setDead(false);
            }
        }
    }

    @Subscribe
    public void onScriptPostFired(ScriptPostFired event) {
        if (event.getScriptId() == TOA_HUD_INIT_STATUS_NAMES_SCRIPT_ID) {
            tryLoadRaiders();
        }
    }

    public boolean isRaider(Actor actor) {
        return raiders.containsKey(actor.getName());
    }

    private int getRegion() {
        LocalPoint localPoint = client.getLocalPlayer().getLocalLocation();
        if (localPoint == null) {
            return -1;
        } else {
            return WorldPoint.fromLocalInstance(client, localPoint).getRegionID();
        }
    }

    private void regionChanged(int newRegion) {
        currentRoom = RaidRoom.forRegionId(newRegion);
        if (currentRoom == null) {
            return;
        }

        log.debug("New room: {}", currentRoom);

        boolean newInRaid = currentRoom != RAID_LOBBY_OUTSIDE;
        if (newInRaid != inRaid) {
            log.debug("In Raid changed: {}", newInRaid);
            eventBus.post(new InRaidChanged(newInRaid));
        }
        inRaid = newInRaid;

        RaidRoom prevRoom = RaidRoom.forRegionId(prevRegion);
        if (prevRoom == RAID_LOBBY_OUTSIDE && currentRoom == RAID_LOBBY_INSIDE) {
            // We can't load the raiders yet, as they're not set in the varcs until a few ticks later. The toa hud init
            // script will take care of it for us
            newRaid = true;
        } else {
            log.debug("Raid room changed: {}", currentRoom);
            eventBus.post(RaidRoomChanged.builder().newRaidRoom(currentRoom).prevRaidRoom(prevRoom).build());
        }

        // TODO: RaidFinished?
    }

    private void tryLoadRaiders() {
        log.debug("Setting raiders");
        raiders.clear();

        Set<String> raiderNames = new HashSet<>(MAX_RAIDERS);
        for (int i = 0; i < MAX_RAIDERS; i++) {
            String name = client.getVarcStrValue(TOA_RAIDERS_VARC_START + i);
            if (name != null && !name.isEmpty()) {
                raiderNames.add(Text.sanitize(name));
            }
        }

        for (Player player : client.getPlayers()) {
            if (player != null &&
                    player.getName() != null &&
                    !raiders.containsKey(player.getName()) &&
                    raiderNames.contains(player.getName())) {
                raiders.put(player.getName(), new Raider(player));
            }
        }

        log.debug("Loaded raiderNames: {}", raiderNames);
        log.debug("Loaded raiders: {}", raiders.keySet());

        if (raiders.isEmpty() || raiders.size() < raiderNames.size()) {
            log.debug("Not enough raiders loaded. Will try again later...");
            raiders.clear();
            return;
        }

        if (newRaid) {
            log.debug("New raid");
            eventBus.post(new RaidEntered(ImmutableList.copyOf(raiders.keySet())));
            newRaid = false;
        }
    }
}
