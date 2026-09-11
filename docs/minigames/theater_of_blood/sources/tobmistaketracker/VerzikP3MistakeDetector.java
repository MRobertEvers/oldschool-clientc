package com.tobmistaketracker.detector;

import com.google.common.annotations.VisibleForTesting;
import com.tobmistaketracker.TobBossNames;
import com.tobmistaketracker.TobMistake;
import com.tobmistaketracker.TobRaider;
import com.tobmistaketracker.detector.MistakeDetectors.VerzikMeleeChancedTracker;
import com.tobmistaketracker.detector.MistakeDetectors.VerzikMeleeChancedTracker.MeleeChanceData;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.NPC;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.ActorDeath;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.GameObjectDespawned;
import net.runelite.api.events.GameObjectSpawned;
import net.runelite.api.events.GameTick;
import net.runelite.api.events.GraphicChanged;
import net.runelite.api.events.NpcChanged;
import net.runelite.api.events.NpcSpawned;
import net.runelite.client.eventbus.Subscribe;

import javax.inject.Inject;
import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * Verzik P3 is also pretty straightforward -- detect for webs/purples and show them.
 * <p>
 * We are intentionally waiting until the web has *despawned* and checking if a player was standing on it in order
 * to combat any form of "cheating" where the mistake detection can be seen as a communication mechanic.
 * <p>
 * In the future, this will also add verzik melee for the player tanking, but we currently can't easily
 * detect when verzik is meleeing, unless the dev team unmask certain animations/graphics for P3.
 */
@Slf4j
@Singleton
public class VerzikP3MistakeDetector extends BaseTobMistakeDetector {
    private VerzikMeleeChancedTracker verzikMeleeChancedTracker = new VerzikMeleeChancedTracker();

    private static final Set<Integer> VERZIK_P3_IDS = Set.of(
            10835, // Entry
            8374, // Normal
            10852 // Hard
    );

    private static final int VERZIK_WEB_GAME_OBJECT_ID = 32734;
    private static final int PLAYER_PURPLE_GRAPHIC_ID = 1602;

    private final Set<WorldPoint> activeWebTiles;
    private final Set<WorldPoint> webTilesToRemove;

    private final Set<String> playerNamesPurpled;
    private NPC verzikP3NPC;

    @Inject
    public VerzikP3MistakeDetector() {
        activeWebTiles = new HashSet<>();
        webTilesToRemove = new HashSet<>();
        playerNamesPurpled = new HashSet<>();
    }

    @Override
    public void shutdown() {
        super.shutdown();
        activeWebTiles.clear();
        webTilesToRemove.clear();
        playerNamesPurpled.clear();
        verzikMeleeChancedTracker.dispose();

        verzikP3NPC = null;
    }

    @Override
    protected void computeDetectingMistakes() {
        if (!detectingMistakes && isAlreadySpawned()) {
            enableDetectingMistakes();
        }
    }

    @Override
    public List<TobMistake> detectMistakes(@NonNull TobRaider raider) {
        List<TobMistake> mistakes = new ArrayList<>();

        if (raider.isDead()) {
            return mistakes;
        }

        // If this web just despawned, we can track this mistake
        // Note: We are intentionally delaying detecting this, so as to avoid cheating
        if (webTilesToRemove.contains(raider.getPreviousWorldLocation())) {
            mistakes.add(TobMistake.VERZIK_P3_WEB);
        }

        if (playerNamesPurpled.contains(raider.getName())) {
            mistakes.add(TobMistake.VERZIK_P3_PURPLE);
        }

        MeleeChanceData chancedMeleePlayer = verzikMeleeChancedTracker.getMeleeChanceData();
        if (chancedMeleePlayer != null && chancedMeleePlayer.getPlayerName().equals(raider.getName())) {
            TobMistake mistake = chancedMeleePlayer.isWasMelee() ?
                    TobMistake.VERZIK_P3_MELEE_TANKED :
                    TobMistake.VERZIK_P3_MELEE_CHANCED;
            mistakes.add(mistake);
        }

        return mistakes;
    }

    @Override
    public void afterDetect() {
        activeWebTiles.removeAll(webTilesToRemove);
        webTilesToRemove.clear();
        playerNamesPurpled.clear();
        verzikMeleeChancedTracker.setMeleeChanceData(null);
    }

    @Subscribe
    public void onGraphicChanged(GraphicChanged event) {
        if (event.getActor() instanceof Player && event.getActor().getGraphic() == PLAYER_PURPLE_GRAPHIC_ID) {
            playerNamesPurpled.add(event.getActor().getName());
        }
    }

    @Subscribe
    public void onGameTick(GameTick tick) {
        if (verzikP3NPC != null) {
            verzikMeleeChancedTracker.setVerzikAttackInfo(verzikP3NPC);
        }
    }

    @Subscribe
    public void onAnimationChanged(AnimationChanged event) {
        if (event.getActor() instanceof NPC) {
            verzikMeleeChancedTracker.checkPlayerWronglyTanked(event);
        }
    }

    @Subscribe
    public void onNpcSpawned(NpcSpawned event) {
        if (!detectingMistakes && isVerzikP3(event.getNpc())) {
            enableDetectingMistakes();
        }
    }

    @Subscribe
    public void onGameObjectSpawned(GameObjectSpawned event) {
        if (event.getGameObject().getId() == VERZIK_WEB_GAME_OBJECT_ID) {
            activeWebTiles.add(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onGameObjectDespawned(GameObjectDespawned event) {
        if (event.getGameObject().getId() == VERZIK_WEB_GAME_OBJECT_ID) {
            // Remove these *after* detecting this tick, since they were still present in the previous player location.
            webTilesToRemove.add(event.getGameObject().getWorldLocation());
        }
    }

    @Subscribe
    public void onNpcChanged(NpcChanged event) {
        if (!detectingMistakes && isVerzikP3(event.getNpc())) {
            enableDetectingMistakes();
        }
    }

    @Subscribe
    public void onActorDeath(ActorDeath event) {
        if (event.getActor() instanceof NPC && isVerzikP3((NPC) event.getActor())) {
            shutdown();
        }
    }

    private boolean isAlreadySpawned() {
        return client.getNpcs().stream().anyMatch(VerzikP3MistakeDetector::isVerzikP3);
    }

    private static boolean isVerzikP3(NPC npc) {
        return TobBossNames.VERZIK.equals(npc.getName()) && VERZIK_P3_IDS.contains(npc.getId());
    }

    private void enableDetectingMistakes() {
        detectingMistakes = true;

        verzikP3NPC = client.getNpcs().stream()
                .filter(VerzikP3MistakeDetector::isVerzikP3)
                .findFirst()
                .orElse(null);
    }

    @VisibleForTesting
    public Set<WorldPoint> getVerzikP3WebTiles() {
        return Collections.unmodifiableSet(activeWebTiles);
    }
}
