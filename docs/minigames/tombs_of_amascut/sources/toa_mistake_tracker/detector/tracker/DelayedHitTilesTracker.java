package com.toamistaketracker.detector.tracker;

import net.runelite.api.coords.WorldPoint;

import java.util.Set;

/**
 * Wrapper class for a map tracking hit tiles delayed for a specified activation tick
 */
public class DelayedHitTilesTracker extends DelayedObjectsTracker<WorldPoint> {

    public Set<WorldPoint> getActiveHitTiles() {
        return getActiveObjects();
    }

}
