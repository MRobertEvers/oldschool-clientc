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

package io.blert.core;

import io.blert.events.*;
import io.blert.util.Location;
import io.blert.util.Tick;
import java.util.List;
import java.util.Optional;
import javax.annotation.Nullable;
import lombok.*;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import net.runelite.client.callback.ClientThread;

@Slf4j
public abstract class DataTracker implements RuneliteEventHandler {
    protected enum State {
        NOT_STARTED,
        IN_PROGRESS,
        COMPLETED,
        TERMINATING,
    }

    @AllArgsConstructor
    private static class PendingCompletion {
        final boolean completion;
        final int inGameStageTicks;
        final boolean gameTicksPrecise;
    }

    @Getter(AccessLevel.PROTECTED)
    private final RecordableChallenge challenge;

    protected final Client client;
    protected final ClientThread clientThread;

    @Getter
    private final Stage stage;

    @Getter(AccessLevel.PROTECTED)
    @Setter(AccessLevel.PROTECTED)
    private State state;

    @Nullable
    private PendingCompletion pendingCompletion;

    private int startClientTick;

    @Getter
    private int totalTicks;

    @Getter(AccessLevel.PROTECTED)
    private final TrackedNpcCollection trackedNpcs = new TrackedNpcCollection();

    public DataTracker(RecordableChallenge challenge, Client client, Stage stage) {
        this.challenge = challenge;
        this.client = client;
        this.clientThread = challenge.getClientThread();
        this.state = State.NOT_STARTED;
        this.stage = stage;
    }

    public boolean notStarted() {
        return state == State.NOT_STARTED;
    }

    public boolean inProgress() {
        return state == State.IN_PROGRESS;
    }

    public boolean completed() {
        return state == State.COMPLETED;
    }

    public boolean terminating() {
        return state == State.TERMINATING;
    }

    /**
     * Prepares the tracker for cleanup, preventing any further events from being processed.
     */
    public void terminate() {
        if (getState() == State.IN_PROGRESS && pendingCompletion != null) {
            completeStage(false);
        }

        if (getState() == State.IN_PROGRESS) {
            state = State.TERMINATING;
            finish(false);
        } else {
            state = State.TERMINATING;
        }
    }

    public void tick() {
        if (state != State.IN_PROGRESS) {
            return;
        }

        updatePlayers();

        challenge.getParty().forEach(this::checkForPlayerActions);

        // Run implementation-specific behavior.
        try {
            onTick();
        } catch (Exception e) {
            log.error("Error during onTick for stage {}", stage, e);
        }

        // Send out an update for every tracked NPC and commit player death events.
        // This must be done after `onTick` to ensure any implementation-specific changes are complete.
        for (Raider raider : challenge.getParty()) {
            if (raider.getDeathTick() == getTick()) {
                Player player = raider.getPlayer();
                WorldPoint location = player != null ? getWorldLocation(player) : null;
                dispatchEvent(new PlayerDeathEvent(getStage(), getTick(), location, raider.getUsername()));
            }
        }
        trackedNpcs.forEach(this::sendNpcUpdate);

        if (pendingCompletion != null) {
            completeStage(true);
        }
    }

    /**
     * Returns number of ticks the tracker has been active.
     *
     * @return The current tick.
     */
    public int getTick() {
        if (notStarted()) {
            return 0;
        }
        return client.getTickCount() - this.startClientTick;
    }

    /**
     * Gathers information about the stage and dispatches appropriate events. Invoked every tick while the challenge
     * is in progress.
     */
    protected abstract void onTick();

    /**
     * Implementation-specific equivalent of the {@code onGameStateChanged} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGameState(GameStateChanged event) {}

    /**
     * Event handler invoked when a new NPC spawns. If the spawned NPC should be tracked and have its data reported,
     * returns a {@link TrackedNpc} describing it. Otherwise, returns {@link Optional#empty()}, performing any desired
     * actions to handle the spawn.
     *
     * @param event The Runelite NPC spawn event.
     * @return The tracked NPC to track if desired.
     */
    protected abstract Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event);

    /**
     * Event handler invoked when an NPC in the room despawns. If the NPC was being tracked, and is now completely
     * dead or otherwise inactive, returns {@code true} to indicate that it should be removed from tracking.
     *
     * @param event      The event.
     * @param trackedNpc The tracked NPC corresponding to the despawned NPC, if it is tracked.
     * @return {@code true} if the NPC should no longer be tracked, {@code false} if it should not (e.g. this despawn
     * indicates a phase change).
     */
    protected abstract boolean onNpcDespawn(NpcDespawned event, @Nullable TrackedNpc trackedNpc);

    /**
     * Implementation-specific equivalent of the {@code onNpcChanged} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onNpcChange(NpcChanged event) {}

    /**
     * Implementation-specific equivalent of the {@code onPlayerChanged} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onPlayerChange(PlayerChanged event) {}

    /**
     * Implementation-specific equivalent of the {@code onAnimationChanged} Runelite event handler.
     * Should be overriden by implementations which require special animation tracking.
     *
     * @param event The animation event.
     */
    protected void onAnimation(AnimationChanged event) {}

    /**
     * Implementation-specific equivalent of the {@code onProjectileMoved} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onProjectile(ProjectileMoved event) {}

    /**
     * Implementation-specific equivalent of the {@code onChatMessage} Runelite event handler.
     * Should be overriden by implementations which require special animation tracking.
     *
     * @param event The event.
     */
    protected void onMessage(ChatMessage event) {}

    /**
     * Implementation-specific equivalent of the {@code onHitsplatApplied} Runelite event handler.
     * Should be overriden by implementations which require special hitsplat tracking.
     *
     * @param hitsplatApplied The hitsplat event.
     */
    protected void onHitsplat(HitsplatApplied hitsplatApplied) {}

    /**
     * Implementation-specific equivalent of the {@code onGameObjectSpawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGameObjectSpawn(GameObjectSpawned event) {}

    /**
     * Implementation-specific equivalent of the {@code onGameObjectDespawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGameObjectDespawn(GameObjectDespawned event) {}

    /**
     * Implementation-specific equivalent of the {@code onGroundObjectSpawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGroundObjectSpawn(GroundObjectSpawned event) {}

    /**
     * Implementation-specific equivalent of the {@code onGroundObjectDespawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGroundObjectDespawn(GroundObjectDespawned event) {}

    /**
     * Implementation-specific equivalent of the {@code onGraphicChanged}
     * Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGraphicChange(GraphicChanged event) {}

    /**
     * Implementation-specific equivalent of the {@code onGraphicsObjectCreated} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {}

    /**
     * Implementation-specific equivalent of the {@code onActorDeath} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onDeath(ActorDeath event) {}

    /**
     * Implementation-specific equivalent of the {@code onItemSpawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onItemSpawn(ItemSpawned event) {}

    /**
     * Implementation-specific equivalent of the {@code onItemDespawned} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onItemDespawn(ItemDespawned event) {}

    /**
     * Returns whether attacking the NPC with the given ID ignores the player's
     * attack cooldown.
     * Should be overriden by implementations which require special handling.
     *
     * @param npc The NPC.
     * @return {@code true} if the NPC ignores cooldown, {@code false} otherwise.
     */
    protected boolean npcIgnoresCooldown(NPC npc) {
        return false;
    }

    /**
     * Implementation-specific equivalent of the {@code onVarbitChanged} Runelite event handler.
     * Should be overriden by implementations which require special handling.
     *
     * @param event The event.
     */
    protected void onVarbit(VarbitChanged event) {}

    protected WorldPoint getWorldLocation(@NonNull Actor actor) {
        return Location.getWorldLocation(client, actor.getWorldLocation());
    }

    protected WorldPoint getWorldLocation(@NonNull TrackedNpc trackedNpc) {
        return getWorldLocation(trackedNpc.getNpc());
    }

    protected WorldPoint getWorldLocation(@NonNull Tile tile) {
        return Location.getWorldLocation(client, tile.getWorldLocation());
    }

    protected WorldPoint getWorldLocation(@NonNull GameObject object) {
        return Location.getWorldLocation(client, object.getWorldLocation());
    }

    protected WorldPoint getWorldLocation(@NonNull GroundObject object) {
        return Location.getWorldLocation(client, object.getWorldLocation());
    }

    protected WorldPoint getWorldLocation(@NonNull GraphicsObject object) {
        WorldPoint wp = WorldPoint.fromLocal(client, object.getLocation());
        return Location.getWorldLocation(client, wp);
    }

    /**
     * Sends an event to the registered event handler, if any.
     */
    protected void dispatchEvent(Event event) {
        challenge.dispatchEvent(event);
    }

    protected void start(int tickOffset) {
        this.startClientTick = client.getTickCount() + tickOffset;
        setState(State.IN_PROGRESS);

        client.getTopLevelWorldView().players().forEach(player -> {
            Raider raider = challenge.getRaider(player.getName());
            if (raider != null) {
                raider.setPlayer(player);
                raider.resetForNewRoom();
            }
        });

        dispatchEvent(new StageUpdateEvent(getStage(), 0, StageUpdateEvent.Status.STARTED));
    }

    /**
     * Starts the tracker.
     */
    protected void start() {
        start(0);
    }

    protected void finish(boolean completion) {
        finish(completion, -1, false);
    }

    protected void finish(String inGameTime) {
        var ticks = Tick.fromTimeString(inGameTime);
        if (ticks.isPresent()) {
            finish(true, ticks.get().getLeft(), ticks.get().getRight());
        } else {
            finish(true);
        }
    }

    /**
     * Marks the stage as complete, queueing cleanup to run at the end of the tick.
     *
     * @param completion       Whether the stage was completed successfully.
     * @param inGameStageTicks The number of in-game ticks the stage took to complete, or -1 if the in-game timer is not
     *                         available. If provided, it is used to verify the accuracy of the recorded stage time.
     * @param gameTicksPrecise Whether the in-game tick timer is precise or rounded.
     */
    protected void finish(boolean completion, int inGameStageTicks, boolean gameTicksPrecise) {
        switch (state) {
            case TERMINATING:
                log.debug("Forcefully terminating stage {}", stage);
                pendingCompletion = new PendingCompletion(completion, inGameStageTicks, gameTicksPrecise);
                completeStage(false);
                break;
            case IN_PROGRESS:
                if (pendingCompletion == null) {
                    pendingCompletion = new PendingCompletion(completion, inGameStageTicks, gameTicksPrecise);
                }
                break;
            default:
                break;
        }
    }

    private void completeStage(boolean waitToDispatch) {
        final PendingCompletion pending = pendingCompletion;
        if (pending == null) {
            return;
        }
        pendingCompletion = null;

        final boolean completion = pending.completion;
        final int inGameStageTicks = pending.inGameStageTicks;
        final boolean gameTicksPrecise = pending.gameTicksPrecise;

        if (state == State.IN_PROGRESS) {
            setState(State.COMPLETED);
        }

        final int lastRecordedRoomTick = getTick();
        boolean accurate;

        totalTicks = lastRecordedRoomTick;

        if (inGameStageTicks != -1) {
            if (inGameStageTicks != lastRecordedRoomTick) {
                log.warn(
                        "Stage {} completion time mismatch: in-game room ticks = {}, recorded ticks = {}",
                        stage,
                        inGameStageTicks,
                        lastRecordedRoomTick);
                accurate = false;
                totalTicks = inGameStageTicks;
            } else {
                accurate = true;
            }
        } else {
            log.debug(
                    "Stage {} finished in {} unconfirmed ticks ({})",
                    stage,
                    lastRecordedRoomTick,
                    Tick.asTimeString(lastRecordedRoomTick));
            accurate = false;
        }

        boolean spectator =
                !challenge.playerIsInChallenge(client.getLocalPlayer().getName());
        boolean isWipe = challenge.getParty().stream().allMatch(Raider::isDead);

        if (spectator && !completion && !isWipe) {
            log.info("Spectator left challenge at stage {}", stage);
        } else {
            var status = completion ? StageUpdateEvent.Status.COMPLETED : StageUpdateEvent.Status.WIPED;
            log.info("Stage {} finished, status: {}", stage, status);

            Optional<Integer> gameTicks = inGameStageTicks == -1 ? Optional.empty() : Optional.of(inGameStageTicks);
            Runnable dispatch = () -> challenge.dispatchEvent(new StageUpdateEvent(
                    getStage(), lastRecordedRoomTick, status, accurate, gameTicks, gameTicksPrecise));

            if (waitToDispatch) {
                // Don't send the final room status immediately; allow other pending subscribers to run and dispatch
                // their
                // own events first.
                clientThread.invokeLater(dispatch);
            } else {
                dispatch.run();
            }
        }
    }

    private void updatePlayers() {
        int tick = getTick();

        client.getTopLevelWorldView().players().forEach(player -> {
            Raider raider = challenge.getRaider(player.getName());
            if (raider == null || raider.isDead() && raider.getDeathTick() < tick) {
                return;
            }

            raider.updateState(client, player, tick);

            dispatchEvent(PlayerUpdateEvent.fromRaider(getStage(), tick, getWorldLocation(player), client, raider));
        });
    }

    private void checkForPlayerActions(@NonNull Raider raider) {
        if (raider.isDead() && raider.getDeathTick() < getTick()) {
            return;
        }

        Player player = raider.getPlayer();
        if (player == null) {
            return;
        }

        checkForPlayerAttack(raider, player);
        checkForPlayerSpell(raider, player);
    }

    private void checkForPlayerSpell(@NonNull Raider raider, @NonNull Player player) {
        final int tick = getTick();
        SpellRegistry spellRegistry = challenge.getSpellRegistry();
        SpellDefinition spell = null;

        if (raider.getAnimationId() != -1) {
            SpellDefinition candidate = spellRegistry.findByAnimation(raider.getAnimationId());
            if (candidate != null) {
                spell = raider.tryRecordSpell(tick, candidate, null);
            } else {
                spell = checkForTargetedPlayerSpell(spellRegistry, raider, player);
            }
        }

        // If no animation match, try to match a non-targeted spell by graphic on the caster.
        // Targeted spells generally apply their graphics on the target.
        if (spell == null) {
            for (int graphicId : raider.getGraphicIds()) {
                SpellDefinition candidate = spellRegistry.findByGraphic(graphicId);
                if (candidate != null && !candidate.isTargeted()) {
                    spell = raider.tryRecordSpell(tick, candidate, graphicId);
                    if (spell != null) {
                        break;
                    }
                }
            }
        }

        if (spell == null) {
            return;
        }

        if (spell.isTargeted()) {
            Actor interacting = player.getInteracting();
            if (interacting == null) {
                dispatchEvent(PlayerSpellEvent.withNoTarget(getStage(), tick, getWorldLocation(player), spell, raider));
                return;
            }

            if (interacting instanceof Player) {
                Player targetPlayer = (Player) interacting;
                dispatchEvent(PlayerSpellEvent.withPlayerTarget(
                        getStage(), tick, getWorldLocation(player), spell, raider, targetPlayer.getName()));
            } else if (interacting instanceof NPC) {
                NPC targetNpc = (NPC) interacting;
                SpellDefinition finalSpell = spell;
                trackedNpcs
                        .getByNpc(targetNpc)
                        .ifPresent(trackedNpc -> dispatchEvent(PlayerSpellEvent.withNpcTarget(
                                getStage(), tick, getWorldLocation(player), finalSpell, raider, trackedNpc)));
            } else {
                dispatchEvent(PlayerSpellEvent.withNoTarget(getStage(), tick, getWorldLocation(player), spell, raider));
            }
        } else {
            dispatchEvent(PlayerSpellEvent.withNoTarget(getStage(), tick, getWorldLocation(player), spell, raider));
        }
    }

    private SpellDefinition checkForTargetedPlayerSpell(
            SpellRegistry spellRegistry, @NonNull Raider raider, @NonNull Player player) {
        Actor interacting = player.getInteracting();
        if (interacting == null) {
            return null;
        }

        List<SpellDefinition> candidates = spellRegistry.getTargetedSpellsByAnimation(raider.getAnimationId());
        for (SpellDefinition targetedCandidate : candidates) {
            boolean interactingHasGraphic = targetedCandidate.getTargetGraphics().stream()
                    .anyMatch(g -> {
                        for (var spotAnim : interacting.getSpotAnims()) {
                            if (spotAnim.getId() == g.getId() && spotAnim.getFrame() <= g.getMaxFrame()) {
                                return true;
                            }
                        }
                        return false;
                    });
            if (interactingHasGraphic) {
                SpellDefinition spell = raider.tryRecordSpell(getTick(), targetedCandidate, null);
                if (spell != null) {
                    return spell;
                }
            }
        }

        return null;
    }

    private void checkForPlayerAttack(@NonNull Raider raider, @NonNull Player player) {
        int animationId = raider.getAnimationId();
        if (animationId == -1) {
            return;
        }

        final int tick = getTick();
        Optional<NPC> target = raider.getTarget();
        boolean isNewAnimation = raider.getAnimationTick() == tick;

        boolean ignoreCooldown = target.map(this::npcIgnoresCooldown).orElse(false);

        boolean mayHaveAttacked = (ignoreCooldown || raider.isOffCooldownOn(tick))
                && (isNewAnimation || raider.isBlowpiping() || raider.stoppedBlowpiping());
        if (!mayHaveAttacked) {
            return;
        }

        WorldPoint point = getWorldLocation(player);

        AttackRegistry registry = challenge.getAttackRegistry();
        Optional<AttackDefinition> maybeAttack;
        Optional<Item> weapon = raider.getEquippedItem(EquipmentSlot.WEAPON);
        int weaponId = weapon.map(Item::getId).orElse(-1);

        if (raider.stoppedBlowpiping()) {
            // In some instances, a continuous animation ("blowpiping") overrides
            // another weapon's attack animation when the player attacks right
            // off cooldown. If the player is still using the same animation but
            // has just stopped blowpiping and targeted another NPC, assume that
            // they attacked it with the weapon they're holding.
            if (!registry.isContinuousAnimation(animationId) || target.isEmpty()) {
                return;
            }
            maybeAttack = registry.findSuppressedAttack(weaponId);
        } else {
            maybeAttack = registry.find(weaponId, animationId, player::hasSpotAnim);
        }

        maybeAttack.ifPresent(attack -> {
            try {
                if (!isNewAnimation && !attack.isFrameAllowed(player.getAnimationFrame())) {
                    return;
                }

                AttackDefinition finalAttack = attack;
                if (attack.hasProjectile()) {
                    finalAttack = adjustForProjectile(registry, attack, player, weaponId);
                }

                raider.recordAttack(tick, finalAttack, ignoreCooldown);

                TrackedNpc roomTarget = target.flatMap(trackedNpcs::getByNpc).orElse(null);
                int distanceToNpc = target.map(npc -> npc.getWorldArea().distanceTo2D(player.getWorldArea()))
                        .orElse(-1);
                dispatchEvent(new PlayerAttackEvent(
                        getStage(), tick, point, finalAttack, weapon.orElse(null), raider, roomTarget, distanceToNpc));
            } catch (Exception e) {
                log.error("Error processing attack {} for {} on tick {}", attack, raider.getUsername(), tick, e);
            }
        });
    }

    /**
     * Adjusts the given attack definition based on any projectiles currently
     * in-flight from the player.
     *
     * @param registry The attack registry.
     * @param attack   Initial attack determined by weapon and animation.
     * @param player   The player performing the attack.
     * @param weaponId The ID of the weapon the player is holding.
     * @return A modified attack definition if a distinguishing projectile is
     * found, or the original attack definition otherwise.
     */
    private AttackDefinition adjustForProjectile(
            AttackRegistry registry, AttackDefinition attack, Player player, int weaponId) {
        List<AttackDefinition> possibleAttacks = registry.allWithAnimations(attack.getAnimationIds());

        for (Projectile p : client.getProjectiles()) {
            for (AttackDefinition candidate : possibleAttacks) {
                AttackDefinition.Projectile expectedProjectile = candidate.getProjectileForWeapon(weaponId);
                if (expectedProjectile != null && projectileMatches(p, expectedProjectile, player)) {
                    return candidate;
                }
            }
        }

        return attack;
    }

    private boolean projectileMatches(Projectile p, AttackDefinition.Projectile projectile, Player player) {
        if (p.getId() != projectile.getId()) {
            return false;
        }

        WorldPoint origin = Location.getWorldLocation(client, p.getSourcePoint());
        boolean originatesFromPlayer = origin == null || origin.distanceTo2D(getWorldLocation(player)) == 0;
        boolean startCycleMatches = p.getStartCycle() - client.getGameCycle() == projectile.getStartCycleOffset();

        return originatesFromPlayer && startCycleMatches;
    }

    /**
     * Sends an {@link NpcEvent} about an NPC in the room.
     */
    protected void sendNpcUpdate(TrackedNpc trackedNpc) {
        if (trackedNpc.getNpc().getId() == -1) {
            return;
        }

        final int tick = getTick();
        WorldPoint point = getWorldLocation(trackedNpc);

        if (trackedNpc.getSpawnTick() == tick) {
            dispatchEvent(NpcEvent.spawn(getStage(), tick, point, trackedNpc));
        } else {
            dispatchEvent(NpcEvent.update(getStage(), tick, point, trackedNpc));
        }

        trackedNpc.setUpdatedProperties(false);
    }

    public void addTrackedNpc(TrackedNpc trackedNpc) {
        boolean existing = trackedNpcs.remove(trackedNpc);
        if (!existing) {
            trackedNpc.setSpawnTick(getTick());
        }
        trackedNpcs.add(trackedNpc);

        challenge.updateMode(trackedNpc.getMode());
    }

    protected void despawnTrackedNpc(TrackedNpc trackedNpc) {
        if (trackedNpcs.remove(trackedNpc)) {
            dispatchEvent(NpcEvent.death(getStage(), getTick(), getWorldLocation(trackedNpc), trackedNpc));
        }
    }

    protected long generateRoomId(@NonNull NPC npc) {
        return npc.getIndex();
    }

    public final void onGameStateChanged(GameStateChanged event) {
        if (terminating()) {
            return;
        }

        onGameState(event);
    }

    public final void onNpcSpawned(NpcSpawned event) {
        if (terminating()) {
            return;
        }

        onNpcSpawn(event).ifPresent(this::addTrackedNpc);
    }

    @Override
    public final void onNpcDespawned(NpcDespawned event) {
        if (terminating()) {
            return;
        }

        Optional<TrackedNpc> maybeTrackedNpc = trackedNpcs.getByNpc(event.getNpc());
        if (onNpcDespawn(event, maybeTrackedNpc.orElse(null))) {
            maybeTrackedNpc.ifPresent(this::despawnTrackedNpc);
        }
    }

    @Override
    public final void onNpcChanged(NpcChanged event) {
        if (!terminating()) {
            onNpcChange(event);
        }
    }

    @Override
    public final void onPlayerChanged(PlayerChanged event) {
        if (!terminating()) {
            onPlayerChange(event);
        }
    }

    @Override
    public final void onAnimationChanged(AnimationChanged event) {
        if (getState() != State.IN_PROGRESS) {
            return;
        }

        Actor actor = event.getActor();
        if (actor instanceof Player) {
            Raider raider = challenge.getRaider(actor.getName());
            if (raider != null) {
                int animationId = actor.getAnimation();
                boolean isContinuous = challenge.getAttackRegistry().isContinuousAnimation(animationId);
                raider.setAnimation(getTick(), animationId, isContinuous);
            }
        }

        onAnimation(event);
    }

    @Override
    public final void onProjectileMoved(ProjectileMoved event) {
        if (!terminating()) {
            onProjectile(event);
        }
    }

    @Override
    public final void onChatMessage(ChatMessage event) {
        if (event.getType() == ChatMessageType.GAMEMESSAGE) {
            onMessage(event);
        }
    }

    @Override
    public final void onHitsplatApplied(HitsplatApplied hitsplatApplied) {
        if (getState() != State.IN_PROGRESS) {
            return;
        }

        Actor target = hitsplatApplied.getActor();
        Hitsplat hitsplat = hitsplatApplied.getHitsplat();

        if (target instanceof NPC) {
            trackedNpcs.getByNpc((NPC) target).ifPresent(trackedNpc -> {
                if (hitsplat.getHitsplatType() == HitsplatID.HEAL) {
                    trackedNpc.getHitpoints().boost(hitsplat.getAmount());
                } else {
                    trackedNpc.getHitpoints().drain(hitsplat.getAmount());
                }
            });
        }

        onHitsplat(hitsplatApplied);
    }

    @Override
    public final void onGameObjectSpawned(GameObjectSpawned event) {
        if (!terminating()) {
            onGameObjectSpawn(event);
        }
    }

    @Override
    public final void onGameObjectDespawned(GameObjectDespawned event) {
        if (!terminating()) {
            onGameObjectDespawn(event);
        }
    }

    @Override
    public final void onGroundObjectSpawned(GroundObjectSpawned event) {
        if (!terminating()) {
            onGroundObjectSpawn(event);
        }
    }

    @Override
    public final void onGroundObjectDespawned(GroundObjectDespawned event) {
        if (!terminating()) {
            onGroundObjectDespawn(event);
        }
    }

    @Override
    public final void onGraphicChanged(GraphicChanged event) {
        if (!terminating()) {
            onGraphicChange(event);
        }
    }

    @Override
    public final void onGraphicsObjectCreated(GraphicsObjectCreated event) {
        if (!terminating()) {
            onGraphicsObjectCreation(event);
        }
    }

    @Override
    public final void onActorDeath(ActorDeath event) {
        if (terminating()) {
            return;
        }

        if (event.getActor() instanceof Player) {
            Raider raider = challenge.getRaider(event.getActor().getName());
            if (raider != null) {
                raider.setDead(getTick());
            }
        }

        onDeath(event);
    }

    @Override
    public final void onItemSpawned(ItemSpawned event) {
        if (!terminating()) {
            onItemSpawn(event);
        }
    }

    @Override
    public final void onItemDespawned(ItemDespawned event) {
        if (!terminating()) {
            onItemDespawn(event);
        }
    }

    @Override
    public final void onVarbitChanged(VarbitChanged varbitChanged) {
        if (getState() != State.IN_PROGRESS) {
            return;
        }

        onVarbit(varbitChanged);
    }
}
