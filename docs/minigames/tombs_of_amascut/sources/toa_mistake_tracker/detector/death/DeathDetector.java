package com.toamistaketracker.detector.death;

import com.google.common.collect.ImmutableMap;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Player;
import net.runelite.api.events.ActorDeath;
import net.runelite.client.eventbus.Subscribe;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import static com.toamistaketracker.ToaMistake.DEATH;
import static com.toamistaketracker.ToaMistake.DEATH_APMEKEN;
import static com.toamistaketracker.ToaMistake.DEATH_CRONDIS;
import static com.toamistaketracker.ToaMistake.DEATH_HET;
import static com.toamistaketracker.ToaMistake.DEATH_SCABARAS;
import static com.toamistaketracker.ToaMistake.DEATH_WARDENS;

/**
 * Track deaths for each player in the raid using the {@link ActorDeath} event. Also map each room to the room death.
 */
@Slf4j
@Singleton
public class DeathDetector extends BaseMistakeDetector {

    private static final Map<RaidRoom, ToaMistake> ROOM_DEATHS = ImmutableMap.<RaidRoom, ToaMistake>builder()
            .put(RaidRoom.HET_PUZZLE, DEATH_HET)
            .put(RaidRoom.AKKHA, DEATH_HET)
            .put(RaidRoom.CRONDIS_PUZZLE, DEATH_CRONDIS)
            .put(RaidRoom.ZEBAK, DEATH_CRONDIS)
            .put(RaidRoom.SCABARAS_PUZZLE, DEATH_SCABARAS)
            .put(RaidRoom.KEPHRI, DEATH_SCABARAS)
            .put(RaidRoom.APMEKEN_PUZZLE, DEATH_APMEKEN)
            .put(RaidRoom.BABA, DEATH_APMEKEN)
            .put(RaidRoom.WARDENS_P1_P2, DEATH_WARDENS)
            .put(RaidRoom.WARDENS_P3, DEATH_WARDENS)
            .build();

    private final Set<String> raiderDeaths;

    public DeathDetector() {
        raiderDeaths = new HashSet<>();
    }

    @Override
    public void cleanup() {
        raiderDeaths.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return null; // null means *all* rooms
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        if (raiderDeaths.contains(raider.getName())) {
            mistakes.add(DEATH);

            if (!ROOM_DEATHS.containsKey(raidState.getCurrentRoom())) {
                // Should never happen. If it does, log and return empty mistakes for this death
                log.error("Unknown room death: {}", raidState.getCurrentRoom());
                return Collections.emptyList();
            }

            mistakes.add(ROOM_DEATHS.get(raidState.getCurrentRoom()));
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        raiderDeaths.clear();
    }

    @Subscribe
    public void onActorDeath(ActorDeath event) {
        if (!(event.getActor() instanceof Player)) return;

        if (raidState.isRaider(event.getActor()))
            raiderDeaths.add(event.getActor().getName());
    }
}