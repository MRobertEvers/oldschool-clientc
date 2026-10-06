package com.toamistaketracker.detector.tracker;

import com.google.common.annotations.VisibleForTesting;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Wrapper class for a map tracking objects delayed for a specified activation tick
 */
@Slf4j
public class DelayedObjectsTracker<T> implements ObjectsTracker<T> {

    protected final Set<T> activeObjects = new HashSet<>();

    @Getter
    @VisibleForTesting
    protected final Map<Integer, Set<T>> delayedObjects = new HashMap<>(); // activationTick -> set of tracked objects

    /**
     * Put the specified object to be retrieved at the specified activationTick
     *
     * @param activationTick The game tick to retrieve the object
     * @param object         The object to track
     */
    public void put(@NonNull Integer activationTick, @NonNull T object) {
        delayedObjects.computeIfAbsent(activationTick, k -> new HashSet<>()).add(object);
    }

    /**
     * Put the specified objects to be retrieved at the specified activationTick
     *
     * @param activationTick The game tick to retrieve the object
     * @param objects        The objects to track
     */
    public void putAll(@NonNull Integer activationTick, @NonNull Set<T> objects) {
        delayedObjects.computeIfAbsent(activationTick, k -> new HashSet<>()).addAll(objects);
    }

    /**
     * Sets the active objects  for the given game tick, and removes retrieved objects from the delayed map. This
     * should be called once every GameTick update.
     *
     * @param gameTick the game tick
     */
    @Override
    public void onGameTick(@NonNull Integer gameTick) {
        activeObjects.clear();
        if (delayedObjects.containsKey(gameTick)) {
            activeObjects.addAll(delayedObjects.remove(gameTick));
        }
    }

    @Override
    public Set<T> getActiveObjects() {
        return activeObjects;
    }

    @Override
    public void clear() {
        activeObjects.clear();
        delayedObjects.clear();
    }
}
