package com.toamistaketracker.detector.tracker;

import com.toamistaketracker.Raider;
import lombok.NonNull;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.HeadIcon;
import net.runelite.api.Projectile;
import net.runelite.api.events.ProjectileMoved;

import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

import static com.toamistaketracker.ToaMistakeTrackerPlugin.CYCLES_PER_GAME_TICK;

@RequiredArgsConstructor
@Slf4j
public class OverheadTracker extends DelayedObjectsTracker<Projectile>{

    // Projectile ID -> correct overhead icon
    private final Map<Integer, HeadIcon> attacks;

    private final Set<Projectile> trackedProjectiles = new HashSet<>();

    /**
     * Tracks a Projectile from the specified ProjectileMoved event
     *
     * @param event    The event containing the Projectile and metadata
     * @param activationTick The activationTick for when this projectile activates its overhead detection
     */
    public void trackProjectile(@NonNull ProjectileMoved event, @NonNull Integer activationTick) {
        Projectile projectile = event.getProjectile();
        if (!attacks.containsKey(projectile.getId()) ||
                trackedProjectiles.contains(projectile)) {
            return;
        }

        if (!hasEnoughRemainingCycles(projectile)) {
            // There's no way there's a new projectile that only has at most 1 game tick left. It's probably
            // hanging around from a previous attack, so let's ignore
            return;
        }

        put(activationTick, projectile);
        trackedProjectiles.add(projectile);
    }

    @Override
    public void onGameTick(@NonNull Integer gameTick) {
        super.onGameTick(gameTick);

        // Clear state of projectiles that have at most 1 game tick left, as they can't be added anyway
        trackedProjectiles.removeIf(projectile ->!hasEnoughRemainingCycles(projectile));
    }

    @Override
    public void clear() {
        super.clear();
        trackedProjectiles.clear();
    }

    public Set<Projectile> getActiveProjectiles() {
        return getActiveObjects();
    }

    /**
     * Determine whether the given raider missed their prayer for the active projectile on them for this tick
     * @param raider The raider
     * @return True if the raider missed their prayer, false otherwise
     */
    public boolean didMissPrayer(Raider raider) {
        if (getActiveProjectiles().isEmpty()) {
            return false;
        }

        HeadIcon playerHeadIcon = raider.getPlayer().getOverheadIcon();
        if (playerHeadIcon == null) {
            return true;
        }

        Projectile activeProjectile = getActiveProjectileForRaider(raider);
        if (activeProjectile == null) {
            // Should never happen
            return false;
        }

        HeadIcon requiredHeadIcon = attacks.get(activeProjectile.getId());
        if (requiredHeadIcon == null) {
            // Can't happen, but just in case, not a miss
            return false;
        }

        return playerHeadIcon != requiredHeadIcon;
    }

    /**
     * Return the current active projectile for the specified Raider
     *
     * @param raider The raider
     * @return The projectile that activated on this tick for the raider
     */
    public Projectile getActiveProjectileForRaider(Raider raider) {
        for (Projectile projectile : getActiveProjectiles()) {
            if (projectile != null &&
                    raider.getPlayer().equals(projectile.getInteracting())) {
                return projectile;
            }
        }

        return null;
    }

    private boolean hasEnoughRemainingCycles(Projectile projectile) {
        // There's no way there's a new projectile that only has at most 1 game tick left. It's probably
        // hanging around from a previous attack, so let's ignore
        return projectile.getRemainingCycles() > CYCLES_PER_GAME_TICK;
    }
}
