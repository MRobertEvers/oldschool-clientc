package com.toamistaketracker.detector.tracker;

import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;

import java.util.HashSet;
import java.util.Set;

/**
 * Wrapper class for a map tracking objects that are active on the tick they are added
 */
@Slf4j
public class InstantObjectsTracker<T> implements ObjectsTracker<T> {

    private final Set<T> activeObjects = new HashSet<>();
    private final Set<T> newObjects = new HashSet<>();

    /**
     * Add the specified object to be activated this tick
     *
     * @param object The object to track
     */
    public void add(@NonNull T object) {
        newObjects.add(object);
    }

    /**
     * Sets the active objects for the given game tick, and removes retrieved objects from the delayed map. This
     * should be called once every GameTick update.
     *
     * @param gameTick the game tick
     */
    @Override
    public void onGameTick(@NonNull Integer gameTick) {
        activeObjects.clear();
        activeObjects.addAll(newObjects);
        newObjects.clear();
    }

    @Override
    public Set<T> getActiveObjects() {
        return activeObjects;
    }

    @Override
    public void clear() {
        activeObjects.clear();
        newObjects.clear();
    }
}
