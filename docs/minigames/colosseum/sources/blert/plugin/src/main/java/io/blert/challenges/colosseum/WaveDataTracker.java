/*
 * Copyright (c) 2024 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the “Software”), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.challenges.colosseum;

import io.blert.core.*;
import io.blert.events.NpcAttackEvent;
import io.blert.events.colosseum.*;
import io.blert.util.ChatText;
import io.blert.util.Tick;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldArea;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class WaveDataTracker extends DataTracker {
    private static final String bossStartMessage = "Sol Heredit jumps down from his seat...";
    private final String waveStartMessage;
    private final Pattern waveEndRegex;

    private final int ticksOnEntry;

    @Setter
    private Handicap handicap;

    @Setter
    private Handicap[] handicapOptions;

    private static final int REENTRY_PRIMARY_GAME_OBJECT_ID = 50743;
    private static final int REENTRY_SECONDARY_GROUND_OBJECT_ID = 50744;
    private static final int HEALING_TOTEM_PROJECTILE_ID = 2687;
    private static final Set<Integer> SOL_DUST_GRAPHIC_OBJECT_IDS = Set.of(2669, 2670, 2671);
    private static final Set<Integer> SOL_LASER_SCAN_GRAPHIC_OBJECT_IDS = Set.of(2689, 2690, 2691);
    private static final Set<Integer> SOL_LASER_SHOT_GRAPHIC_OBJECT_IDS = Set.of(2693, 2694, 2695);
    private static final int SOL_POOL_GRAPHIC_OBJECT_ID = 2698;

    private final Map<WorldPoint, NPC> healingTotems = new HashMap<>();
    private final Map<NPC, Pair<NPC, Integer>> activeHeals = new HashMap<>();

    private final List<GameObject> reentryPrimaryPoolsSpawned = new ArrayList<>();
    private final List<GroundObject> reentrySecondaryPoolsSpawned = new ArrayList<>();
    private final List<GameObject> reentryPrimaryPoolsDespawned = new ArrayList<>();
    private final List<GroundObject> reentrySecondaryPoolsDespawned = new ArrayList<>();
    private final Set<WorldPoint> solDustGraphics = new HashSet<>();
    private final List<WorldPoint> solPools = new ArrayList<>();
    private GraphicsObject solLaserGraphic;

    private static final Map<String, EquipmentSlot> GRAPPLE_MESSAGES = Map.of(
            "Sol Heredit: I'LL CRUSH YOUR BODY!", EquipmentSlot.TORSO,
            "Sol Heredit: I'LL BREAK YOUR BACK!", EquipmentSlot.CAPE,
            "Sol Heredit: I'LL TWIST YOUR HANDS OFF!", EquipmentSlot.GLOVES,
            "Sol Heredit: I'LL BREAK YOUR LEGS!", EquipmentSlot.LEGS,
            "Sol Heredit: I'LL CUT YOUR FEET OFF!", EquipmentSlot.BOOTS);
    private static final String GRAPPLE_DEFEND_MESSAGE = "You successfully defend from Sol Heredit's grapple!";
    private static final String GRAPPLE_PARRY_MESSAGE = "You perfectly parry Sol Heredit's grapple!";

    private @Nullable EquipmentSlot pendingGrappleTarget = null;
    private int pendingGrappleTick = -1;

    public static Stage waveToStage(int wave) {
        return Stage.values()[Stage.COLOSSEUM_WAVE_1.ordinal() + wave - 1];
    }

    public WaveDataTracker(RecordableChallenge challenge, Client client, int wave, int ticksOnEntry) {
        super(challenge, client, waveToStage(wave));

        this.waveStartMessage = "Wave: " + wave;
        this.waveEndRegex =
                Pattern.compile("Wave " + wave + " completed! " + "Wave duration: (" + Tick.TIME_STRING_REGEX + ")");
        this.ticksOnEntry = ticksOnEntry;
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        return ColosseumNpc.withId(event.getNpc().getId()).map(colosseumNpc -> {
            NPC npc = event.getNpc();
            if (colosseumNpc.isHealingTotem()) {
                healingTotems.put(npc.getWorldLocation(), npc);
            }
            if (colosseumNpc.isManticore()) {
                return new Manticore(npc, generateRoomId(npc), new Hitpoints(colosseumNpc.getHitpoints()));
            }
            return new BasicTrackedNpc(npc, generateRoomId(npc), new Hitpoints(colosseumNpc.getHitpoints()));
        });
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, @Nullable TrackedNpc trackedNpc) {
        if (trackedNpc != null) {
            if (ColosseumNpc.withId(trackedNpc.getNpc().getId())
                    .map(ColosseumNpc::isHealingTotem)
                    .orElse(false)) {
                healingTotems.remove(trackedNpc.getNpc().getWorldLocation());
            }
            activeHeals.remove(trackedNpc.getNpc());
        }
        return ColosseumNpc.withId(event.getNpc().getId()).isPresent();
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        if (!(event.getActor() instanceof NPC)) {
            return;
        }

        NPC npc = (NPC) event.getActor();
        Optional<ColosseumNpc> maybeNpc = ColosseumNpc.withId(npc.getId());
        Optional<TrackedNpc> maybeTrackedNpc = getTrackedNpcs().getByNpc(npc);
        if (maybeNpc.isEmpty() || maybeTrackedNpc.isEmpty() || npc.getAnimation() == -1) {
            return;
        }

        ColosseumNpc colosseumNpc = maybeNpc.get();
        TrackedNpc trackedNpc = maybeTrackedNpc.get();

        if (colosseumNpc.isManticore()) {
            Manticore manticore = (Manticore) trackedNpc;
            if (npc.getAnimation() == Manticore.ATTACK_ANIMATION) {
                manticore.startAttack();
            }
            return;
        }

        colosseumNpc.getAttack(npc.getAnimation()).ifPresent(attack -> {
            WorldPoint location = getWorldLocation(npc);
            dispatchEvent(new NpcAttackEvent(getStage(), getTick(), location, attack, trackedNpc));
        });
    }

    @Override
    protected void onTick() {
        if (notStarted()) {
            return;
        }

        getTrackedNpcs().stream()
                .filter(trackedNpc -> trackedNpc instanceof Manticore)
                .forEach(trackedNpc -> {
                    Manticore manticore = (Manticore) trackedNpc;
                    // Check for an attack using the previous tick's style before updating.
                    NpcAttack attack = manticore.continueAttack();
                    if (attack != null) {
                        WorldPoint location = getWorldLocation(manticore.getNpc());
                        dispatchEvent(new NpcAttackEvent(getStage(), getTick(), location, attack, manticore));
                    }
                    manticore.updateStyle();
                });

        sendReentryPoolsEvent();

        if (getStage() == Stage.COLOSSEUM_WAVE_12) {
            handleSolEvents();
        }

        reentryPrimaryPoolsSpawned.clear();
        reentrySecondaryPoolsSpawned.clear();
        reentryPrimaryPoolsDespawned.clear();
        reentrySecondaryPoolsDespawned.clear();
        solDustGraphics.clear();
        solPools.clear();
        solLaserGraphic = null;
    }

    @Override
    protected void onHitsplat(HitsplatApplied event) {
        Actor actor = event.getActor();
        Hitsplat hitsplat = event.getHitsplat();
        if (actor == client.getLocalPlayer()) {
            if (hitsplat.getHitsplatType() == HitsplatID.DOOM) {
                dispatchEvent(new DoomAppliedEvent(getStage(), getTick(), getWorldLocation(actor)));
            }
            return;
        }

        if (actor instanceof NPC && hitsplat.getHitsplatType() == HitsplatID.HEAL) {
            var activeHeal = activeHeals.get(actor);
            if (activeHeal == null) {
                return;
            }

            Optional<TrackedNpc> totem = getTrackedNpcs().getByNpc(activeHeal.getLeft());
            Optional<TrackedNpc> target = getTrackedNpcs().getByNpc((NPC) actor);
            if (totem.isPresent() && target.isPresent()) {
                dispatchEvent(new TotemHealEvent(
                        getStage(),
                        getTick(),
                        getWorldLocation(actor),
                        totem.get(),
                        target.get(),
                        activeHeal.getRight(),
                        hitsplat.getAmount()));
            }
        }
    }

    @Override
    protected void onProjectile(ProjectileMoved event) {
        Projectile projectile = event.getProjectile();
        if (projectile.getId() != HEALING_TOTEM_PROJECTILE_ID) {
            return;
        }

        NPC totem = healingTotems.get(projectile.getSourcePoint());
        if (totem == null) {
            return;
        }

        Actor target = projectile.getTargetActor();
        if (!(target instanceof NPC)) {
            return;
        }

        int cycle = client.getGameCycle() - projectile.getStartCycle();
        if (cycle == 0) {
            getTrackedNpcs().getByNpc((NPC) target).ifPresent(npc -> {
                activeHeals.put(npc.getNpc(), Pair.of(totem, getTick()));
            });
        }
    }

    @Override
    protected void onGameObjectSpawn(GameObjectSpawned event) {
        GameObject object = event.getGameObject();
        if (object.getId() == REENTRY_PRIMARY_GAME_OBJECT_ID) {
            reentryPrimaryPoolsSpawned.add(object);
        }
    }

    @Override
    protected void onGameObjectDespawn(GameObjectDespawned event) {
        GameObject object = event.getGameObject();
        if (object.getId() == REENTRY_PRIMARY_GAME_OBJECT_ID) {
            reentryPrimaryPoolsDespawned.add(object);
        }
    }

    @Override
    protected void onGroundObjectSpawn(GroundObjectSpawned event) {
        GroundObject object = event.getGroundObject();
        if (object.getId() == REENTRY_SECONDARY_GROUND_OBJECT_ID) {
            reentrySecondaryPoolsSpawned.add(object);
        }
    }

    @Override
    protected void onGroundObjectDespawn(GroundObjectDespawned event) {
        GroundObject object = event.getGroundObject();
        if (object.getId() == REENTRY_SECONDARY_GROUND_OBJECT_ID) {
            reentrySecondaryPoolsDespawned.add(object);
        }
    }

    @Override
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {
        GraphicsObject object = event.getGraphicsObject();
        if (SOL_DUST_GRAPHIC_OBJECT_IDS.contains(object.getId())) {
            solDustGraphics.add(getWorldLocation(object));
        } else if (object.getId() == SOL_POOL_GRAPHIC_OBJECT_ID) {
            solPools.add(getWorldLocation(object));
        } else if (SOL_LASER_SCAN_GRAPHIC_OBJECT_IDS.contains(object.getId())
                || SOL_LASER_SHOT_GRAPHIC_OBJECT_IDS.contains(object.getId())) {
            // Scans and shots should never happen on the same tick but if it somehow occurs,
            // prioritize a shot, which has a higher ID.
            if (solLaserGraphic == null || object.getId() > solLaserGraphic.getId()) {
                solLaserGraphic = object;
            }
        }
    }

    @Override
    protected void onMessage(ChatMessage event) {
        String stripped = ChatText.stripFormatting(event.getMessage());

        if (stripped.equals(waveStartMessage)) {
            startWave(0);
            return;
        }
        if (stripped.equals(bossStartMessage)) {
            startWave(-1);
            return;
        }

        if (getStage() == Stage.COLOSSEUM_WAVE_12) {
            EquipmentSlot grappleSlot = GRAPPLE_MESSAGES.get(stripped);
            if (grappleSlot != null) {
                pendingGrappleTarget = grappleSlot;
                pendingGrappleTick = getTick();
                return;
            }

            if (pendingGrappleTarget != null) {
                if (stripped.equals(GRAPPLE_DEFEND_MESSAGE)) {
                    dispatchGrapple(SolGrappleEvent.Outcome.DEFEND);
                    return;
                }
                if (stripped.equals(GRAPPLE_PARRY_MESSAGE)) {
                    dispatchGrapple(SolGrappleEvent.Outcome.PARRY);
                    return;
                }
            }

            Matcher matcher = ColosseumChallenge.COLOSSEUM_END_REGEX.matcher(stripped);
            if (matcher.find()) {
                try {
                    var ticks = Tick.fromTimeString(matcher.group(1));
                    if (ticks.isPresent()) {
                        int challengeTicks = ticks.get().getLeft();
                        int bossTicks = challengeTicks - ticksOnEntry;
                        finish(true, bossTicks, ticks.get().getRight());
                    } else {
                        finish(true);
                    }
                } catch (Exception e) {
                    log.warn("Could not parse timestamp from colosseum end message: {}", stripped);
                    finish(true);
                }
            }
        } else {
            Matcher matcher = waveEndRegex.matcher(stripped);
            if (matcher.find()) {
                try {
                    String inGameTime = matcher.group(1);
                    finish(inGameTime);
                } catch (Exception e) {
                    log.warn("Could not parse timestamp from wave end message: {}", stripped);
                    finish(true);
                }
            }
        }
    }

    private void dispatchGrapple(SolGrappleEvent.Outcome outcome) {
        if (pendingGrappleTarget == null) {
            return;
        }

        Player player = client.getLocalPlayer();
        WorldPoint coords = player != null ? getWorldLocation(player) : null;
        dispatchEvent(new SolGrappleEvent(getTick(), coords, pendingGrappleTick, pendingGrappleTarget, outcome));
        pendingGrappleTarget = null;
        pendingGrappleTick = -1;
    }

    private void startWave(int tickOffset) {
        if (getState() == State.NOT_STARTED) {
            super.start(tickOffset);
            dispatchEvent(new HandicapChoiceEvent(
                    getStage(), handicap, Arrays.copyOf(handicapOptions, handicapOptions.length)));
            collectExistingReentryPools();
        }
    }

    private void collectExistingReentryPools() {
        Tile[][][] tiles = client.getTopLevelWorldView().getScene().getTiles();
        if (tiles.length == 0 || tiles[0].length == 0) {
            return;
        }

        try {
            for (Tile[] tileRow : tiles[0]) {
                for (Tile tile : tileRow) {
                    if (tile == null) {
                        continue;
                    }
                    if (tile.getGameObjects() != null) {
                        for (GameObject gameObject : tile.getGameObjects()) {
                            if (gameObject != null && gameObject.getId() == REENTRY_PRIMARY_GAME_OBJECT_ID) {
                                reentryPrimaryPoolsSpawned.add(gameObject);
                            }
                        }
                    }
                    GroundObject groundObject = tile.getGroundObject();
                    if (groundObject != null && groundObject.getId() == REENTRY_SECONDARY_GROUND_OBJECT_ID) {
                        reentrySecondaryPoolsSpawned.add(groundObject);
                    }
                }
            }
        } catch (Exception e) {
            log.warn("Error collecting existing reentry pools on wave start", e);
        }
    }

    private void sendReentryPoolsEvent() {
        if (reentryPrimaryPoolsSpawned.isEmpty()
                && reentrySecondaryPoolsSpawned.isEmpty()
                && reentryPrimaryPoolsDespawned.isEmpty()
                && reentrySecondaryPoolsDespawned.isEmpty()) {
            return;
        }

        List<WorldPoint> primarySpawned =
                reentryPrimaryPoolsSpawned.stream().map(this::getWorldLocation).collect(Collectors.toList());
        List<WorldPoint> secondarySpawned = reentrySecondaryPoolsSpawned.stream()
                .map(this::getWorldLocation)
                .collect(Collectors.toList());
        List<WorldPoint> primaryDespawned = reentryPrimaryPoolsDespawned.stream()
                .map(this::getWorldLocation)
                .collect(Collectors.toList());
        List<WorldPoint> secondaryDespawned = reentrySecondaryPoolsDespawned.stream()
                .map(this::getWorldLocation)
                .collect(Collectors.toList());

        dispatchEvent(new ReentryPoolsEvent(
                getStage(), getTick(), primarySpawned, secondarySpawned, primaryDespawned, secondaryDespawned));
    }

    private void handleSolEvents() {
        // If a grapple was announced and no defend/parry message arrived within 4 ticks, it was a hit.
        if (pendingGrappleTarget != null && getTick() - pendingGrappleTick > 4) {
            dispatchGrapple(SolGrappleEvent.Outcome.HIT);
        }

        NPC sol = getTrackedNpcs().stream()
                .filter(trackedNpc ->
                        ColosseumNpc.isSolHeredit(trackedNpc.getNpc().getId()))
                .findFirst()
                .map(TrackedNpc::getNpc)
                .orElse(null);
        if (sol == null) {
            return;
        }

        if (!solDustGraphics.isEmpty()) {
            SolDustEvent.Pattern pattern;
            SolDustEvent.Direction direction = null;

            WorldPoint solLoc = getWorldLocation(sol);
            int solSize = sol.getWorldArea().getWidth();
            WorldArea solArea = new WorldArea(solLoc.getX(), solLoc.getY(), solSize, solSize, solLoc.getPlane());

            // Pattern can be identified by counting how many dusts are 2 tiles away from Sol.
            List<WorldPoint> twoAway = solDustGraphics.stream()
                    .filter(point -> solArea.distanceTo2D(point) == 2)
                    .collect(Collectors.toList());

            switch (twoAway.size()) {
                case 0:
                    // Safe ring 2 tiles away => shield 1.
                    pattern = SolDustEvent.Pattern.SHIELD_1;
                    break;
                case 2:
                    // Two trident prongs => trident 1.
                    pattern = SolDustEvent.Pattern.TRIDENT_1;
                    break;
                case 3:
                    // Three trident prongs => trident 2.
                    pattern = SolDustEvent.Pattern.TRIDENT_2;
                    break;
                default:
                    // Safe ring 3 tiles away, everything else is dust => shield 2.
                    pattern = SolDustEvent.Pattern.SHIELD_2;
                    break;
            }

            if (pattern == SolDustEvent.Pattern.TRIDENT_1 || pattern == SolDustEvent.Pattern.TRIDENT_2) {
                WorldPoint dust = twoAway.get(0);

                if (dust.getX() < solLoc.getX()) {
                    direction = SolDustEvent.Direction.WEST;
                } else if (dust.getX() >= solLoc.getX() + solSize) {
                    direction = SolDustEvent.Direction.EAST;
                } else if (dust.getY() >= solLoc.getY() + solSize) {
                    direction = SolDustEvent.Direction.NORTH;
                } else {
                    direction = SolDustEvent.Direction.SOUTH;
                }
            }

            dispatchEvent(new SolDustEvent(getTick(), getWorldLocation(sol), pattern, direction));
        }

        if (!solPools.isEmpty()) {
            dispatchEvent(new SolPoolsEvent(getTick(), solPools));
        }

        if (solLaserGraphic != null) {
            SolLasersEvent.Phase phase = SOL_LASER_SCAN_GRAPHIC_OBJECT_IDS.contains(solLaserGraphic.getId())
                    ? SolLasersEvent.Phase.SCAN
                    : SolLasersEvent.Phase.SHOT;
            dispatchEvent(new SolLasersEvent(getTick(), phase));
        }
    }
}
