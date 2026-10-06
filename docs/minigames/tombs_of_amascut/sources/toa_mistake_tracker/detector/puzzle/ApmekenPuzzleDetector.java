package com.toamistaketracker.detector.puzzle;

import com.toamistaketracker.RaidRoom;
import com.toamistaketracker.Raider;
import com.toamistaketracker.ToaMistake;
import com.toamistaketracker.detector.BaseMistakeDetector;
import lombok.Getter;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.ChatMessageType;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GraphicsObjectCreated;
import net.runelite.client.eventbus.Subscribe;
import net.runelite.client.util.Text;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static com.toamistaketracker.RaidRoom.APMEKEN_PUZZLE;
import static com.toamistaketracker.ToaMistake.APMEKEN_PUZZLE_CORRUPTION;
import static com.toamistaketracker.ToaMistake.APMEKEN_PUZZLE_PILLAR;
import static com.toamistaketracker.ToaMistake.APMEKEN_PUZZLE_VENOM;
import static com.toamistaketracker.ToaMistake.APMEKEN_PUZZLE_VENT;
import static com.toamistaketracker.ToaMistake.APMEKEN_PUZZLE_VOLATILE;

/**
 * Apmeken has some group mechanics to detect mistakes for, and then personal ones.
 *
 * For the group mechanics, the most consistent way to detect these mistakes is seemingly by checking for the
 * corresponding game message. Since these are always sent to *everyone* in the raid, it should be safe to use. If these
 * messages ever change in the future, then these mistakes will unfortunately break.
 *
 * While I'd like to also count accidentally venting or repairing pillars when you're not supposed to, there's no real
 * way to consistently detect pillars if two players are on the same tile as the graphics spawn, since only one takes
 * damage. The hitsplat is also not always consistently applied on the first tick. Could come back to this in the future
 * but for now I think it's fine to only count the team mechanic mistakes.
 *
 * For venom, it's straight forward and almost identical to Zebak acid. And for volatile, when it explodes it spawns
 * a graphics object at the center of the explosion, and all tiles in a 3x3 around that center are damaging.
 */
@Slf4j
@Singleton
public class ApmekenPuzzleDetector extends BaseMistakeDetector {

    private static final int VENT_GRAPHICS_ID = 2138;
    private static final int PILLAR_GRAPHICS_ID = 2139;
    private static final int VENOM_TILE_GAME_OBJECT_ID = 45493;
    private static final int VOLATILE_GRAPHICS_ID = 131;

    // TODO: Should these be partials that we regex match on in case they slightly change?
    private static final String VENT_FAILURE_MESSAGE = "The fumes filling the room suddenly ignite!";
    private static final String PILLAR_FAILURE_MESSAGE = "Damaged roof supports cause some debris to fall on you!";
    private static final String CORRUPTION_FAILURE_MESSAGE = "Your group is overwhelmed by Amascut's corruption!";

    private ToaMistake teamMistake;

    private final Set<WorldPoint> venomTilesToSpawn;
    private final Set<WorldPoint> venomTiles;

    @Getter
    private final Set<WorldPoint> volatileHitTiles;

    public ApmekenPuzzleDetector() {
        venomTilesToSpawn = new HashSet<>();
        venomTiles = new HashSet<>();
        volatileHitTiles = new HashSet<>();
    }

    @Override
    public void cleanup() {
        teamMistake = null;
        venomTilesToSpawn.clear();
        venomTiles.clear();
        volatileHitTiles.clear();
    }

    @Override
    public RaidRoom getRaidRoom() {
        return APMEKEN_PUZZLE;
    }

    @Override
    public List<ToaMistake> detectMistakes(@NonNull Raider raider) {
        List<ToaMistake> mistakes = new ArrayList<>();

        // All raiders get team mistakes
        if (teamMistake != null) {
            mistakes.add(teamMistake);
        }

        if (venomTiles.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(APMEKEN_PUZZLE_VENOM);
        }

        if (volatileHitTiles.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(APMEKEN_PUZZLE_VOLATILE);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        teamMistake = null;
        volatileHitTiles.clear();

        venomTiles.addAll(venomTilesToSpawn);
        venomTilesToSpawn.clear();
    }

    @Subscribe
    public void onChatMessage(ChatMessage event) {
        if (event.getType() != ChatMessageType.GAMEMESSAGE) return;

        String message = Text.removeTags(event.getMessage());
        if (VENT_FAILURE_MESSAGE.equals(message)) {
            teamMistake = APMEKEN_PUZZLE_VENT;
        } else if (PILLAR_FAILURE_MESSAGE.equals(message)) {
            teamMistake = APMEKEN_PUZZLE_PILLAR;
        } else if (CORRUPTION_FAILURE_MESSAGE.equals(message)) {
            teamMistake = APMEKEN_PUZZLE_CORRUPTION;
        }
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (event.getGameObject().getId() == VENOM_TILE_GAME_OBJECT_ID) {
            // Venom tiles don't hit you for the first tick they're spawned, so delay their detection by a tick.
            venomTilesToSpawn.add(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event) {
        if (event.getGameObject().getId() == VENOM_TILE_GAME_OBJECT_ID) {
            venomTilesToSpawn.remove(event.getGameObject().getWorldLocation());
            venomTiles.remove(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        if (event.getGraphicsObject().getId() != VOLATILE_GRAPHICS_ID) return;

        // Hits in 3x3 around center, so compute and add all
        Set<WorldPoint> newHitTiles = compute3By3TilesFromCenter(getWorldPoint(event.getGraphicsObject()));
        volatileHitTiles.addAll(newHitTiles);
    }
}