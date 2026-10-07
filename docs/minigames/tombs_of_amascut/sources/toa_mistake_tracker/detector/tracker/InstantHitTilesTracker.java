package com.toamistaketracker.detector.tracker;

import net.runelite.api.coords.WorldPoint;

import java.util.Set;

/**
 * Wrapper class for a map tracking hit tiles that are active on the tick they are added
 */
public class InstantHitTilesTracker extends InstantObjectsTracker<WorldPoint> {

    public Set<WorldPoint> getActiveHitTiles() {
        return getActiveObjects();
    }
}
