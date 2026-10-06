package com.toamistaketracker.detector;

import com.google.common.collect.ImmutableSet;
import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.RaidState;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.tracker.VengeanceTracker;
import lombok.NonNull;
import lombok.Setter;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.Constants;
import net.runelite.api.GraphicsObject;
import net.runelite.api.Projectile;
import net.runelite.api.coords.WorldPoint;
import net.runelite.client.eventbus.EventBus;

import javax.inject.Inject;
import java.util.List;
import java.util.Set;

/**
 * Interface for detecting mistakes during The Tombs of Amascut
 */
public abstract class BaseMistakeDetector {

    protected static final int CYCLES_PER_GAME_TICK = Constants.GAME_TICK_LENGTH / Constants.CLIENT_TICK_LENGTH;

    @Inject
    @Setter
    protected Client client;

    @Inject
    @Setter
    protected EventBus eventBus;

    @Inject
    @Setter
    protected RaidState raidState;

    @Inject
    @Setter
    protected VengeanceTracker vengeanceTracker;

    /**
     * Used to tell a detector to start listening for events.
     */
    public void startup() {
        cleanup();
        eventBus.register(this);
    }

    /**
     * Shutdown and cleanup state. This is always called when the plugin is shutdown, or when a detector is finished.
     */
    public void shutdown() {
        eventBus.unregister(this);
        cleanup();
    }

    /**
     * Cleanup all relevant state in the detector. This is called during startup to reset state, and shutdown to cleanup
     * This is also called for active detectors whenever raiders are loaded, which is during room transitions and room
     * resets (wipes).
     */
    public abstract void cleanup();

    /**
     * Retrieve the raid room that this detector should startup in. A null value means *all* rooms
     *
     * @return The raid room that the detector should startup in, or null for *all* rooms
     */
    public abstract RaidRoom getRaidRoom();

    /**
     * Detects mistakes for the given raider.
     * This is called during handling the {@link net.runelite.api.events.GameTick} event, each tick.
     *
     * @param raider - The raider to detect mistakes for
     * @return The list of {@link ToaMistake} detected on this tick
     */
    public abstract List<ToaMistake> detectMistakes(@NonNull Raider raider);

    /**
     * This method allows detectors to handle some logic after all detectMistakes methods have been invoked
     * for this {@link net.runelite.api.events.GameTick}. Commonly, this is to cleanup state from after this tick.
     */
    public abstract void afterDetect();

    /**
     * Determines whether or not this detector is currently detecting mistakes. Commonly this is by checking the current
     * {@link RaidRoom} in the {@link RaidState}
     *
     * @return True if the detector is detecting mistakes, else false
     */
    public boolean isDetectingMistakes() {
        if (getRaidRoom() == null) { // null means *all* rooms
            return raidState.isInRaid();
        }

        return raidState.getCurrentRoom() == getRaidRoom();
    }

    protected WorldPoint getWorldPoint(Actor actor) {
        return WorldPoint.fromLocal(client, actor.getLocalLocation());
    }

    protected WorldPoint getWorldPoint(GraphicsObject graphicsObject) {
        return WorldPoint.fromLocal(client, graphicsObject.getLocation());
    }

    /**
     * This method computes the WorldPoints in a 3x3 area given a center point.
     *
     * @param center The center point of the 3x3 area
     * @return The set of WorldPoints around and including the center
     */
    protected Set<WorldPoint> compute3By3TilesFromCenter(WorldPoint center) {
        WorldPoint west = center.dx(-1);
        WorldPoint east = center.dx(1);

        return ImmutableSet.of(
                west, center, east,
                west.dy(-1), center.dy(-1), east.dy(-1),
                west.dy(1), center.dy(1), east.dy(1));
    }

    /**
     * Calculates and retrieves the activation tick for the specified GraphicsObject based on the start cycle and
     * the given hit delay.
     *
     * @param graphicsObject The graphics object
     * @param hitDelay       The delay in ticks from when the graphics object animation starts and when it causes a hit
     * @return The activation tick for when the graphics object will denote a hit on the player
     */
    protected int getActivationTick(GraphicsObject graphicsObject, int hitDelay) {
        int ticksToStart = (graphicsObject.getStartCycle() - client.getGameCycle()) / CYCLES_PER_GAME_TICK;
        return client.getTickCount() + ticksToStart + hitDelay; // Add the hit delay for how long between start to hit
    }

    /**
     * Calculates and retrieves the activation tick for the specified Projectile based on the remaining cycles.
     *
     * @param projectile The projectile object
     * @return The activation tick for when the projectile object will reach its target
     */
    protected int getActivationTick(Projectile projectile) {
        int ticksRemaining = projectile.getRemainingCycles() / CYCLES_PER_GAME_TICK;
        return client.getTickCount() + ticksRemaining;
    }
}
