package com.toamistaketracker.detector.tracker;

import com.toamistaketracker.ToaMistake;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

/**
 * Class to track mistakes that should be delayed for a specified number of ticks, or until actively retrieved
 */
public class DelayedMistakeTracker {

    private final Logger log = LoggerFactory.getLogger(DelayedMistakeTracker.class);

    private final Map<String, List<DelayedMistake>> delayedMistakes; // name -> list of delayed mistakes

    public DelayedMistakeTracker() {
        delayedMistakes = new HashMap<>();
    }

    /**
     * Add a delayed mistake which can be retrieved after a specified number of ticks in the future
     *
     * @param raiderName  The raider to retrieve mistakes for
     * @param mistake     The mistake
     * @param currentTick The current game tick
     * @param tickDelay   The amount of ticks to delay until the mistake can successfully be retrieved
     */
    public void addDelayedMistake(@NonNull String raiderName, @NonNull ToaMistake mistake, @NonNull Integer currentTick,
                                  Integer tickDelay) {
        delayedMistakes.computeIfAbsent(raiderName, k -> new ArrayList<>())
                .add(DelayedMistake.builder()
                        .raiderName(raiderName)
                        .mistake(mistake)
                        .tickAdded(currentTick)
                        .tickDelay(tickDelay)
                        .build());
    }

    /**
     * Retrieve the delayed mistakes for the specified raider if enough ticks have passed. This removes all found
     * mistakes
     *
     * @param raiderName  The raider to retrieve mistakes for
     * @param currentTick The current game tick
     * @return The tracked mistakes that have had enough ticks passed
     */
    public List<ToaMistake> popDelayedMistakes(@NonNull String raiderName, @NonNull Integer currentTick) {
        if (delayedMistakes.get(raiderName) == null) return Collections.emptyList();

        List<DelayedMistake> foundMistakes = new ArrayList<>();
        for (DelayedMistake delayedMistake : delayedMistakes.get(raiderName)) {
            int ticksSince = currentTick - delayedMistake.getTickAdded();
            if (ticksSince >= delayedMistake.getTickDelay()) {
                foundMistakes.add(delayedMistake);
            }
        }

        delayedMistakes.get(raiderName).removeAll(foundMistakes);
        return foundMistakes.stream().map(DelayedMistake::getMistake).collect(Collectors.toList());
    }

    /**
     * Forcibly retrieve all tracked delayed mistakes for the specified raider. This removes all tracked delayed
     * mistakes for that raider.
     *
     * @param raiderName The raider to retrieve mistakes for
     * @return The tracked mistakes
     */
    public List<ToaMistake> popAllDelayedMistakes(@NonNull String raiderName) {
        if (delayedMistakes.get(raiderName) == null) return Collections.emptyList();

        List<ToaMistake> mistakes = delayedMistakes.get(raiderName).stream()
                .map(DelayedMistake::getMistake)
                .collect(Collectors.toList());
        delayedMistakes.get(raiderName).clear();

        return mistakes;
    }

    public void clear() {
        delayedMistakes.clear();
    }

    @Value
    @Builder
    private static class DelayedMistake {

        @NonNull String raiderName;
        @NonNull ToaMistake mistake;
        @NonNull Integer tickAdded;
        Integer tickDelay;
    }
}
