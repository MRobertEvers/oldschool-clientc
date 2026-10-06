package com.toamistaketracker.detector.tracker;

import lombok.NonNull;

import java.util.Set;

/**
 * Track objects that should be activated at a specified {@link net.runelite.api.events.GameTick}
 */
public interface ObjectsTracker<T> {

    /**
     * Sets the active objects for the given game tick. This should be called once every GameTick update.
     *
     * @param gameTick the game tick
     */
    void onGameTick(@NonNull Integer gameTick);

    /**
     * Retrieve the active objects for this game tick
     *
     * @return The set of active objects this game tick
     */
    Set<T> getActiveObjects();

    /**
     * Clears all active objects and other state
     */
    void clear();
}
