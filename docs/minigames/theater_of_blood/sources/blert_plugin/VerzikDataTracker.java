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

package io.blert.challenges.tob.rooms.verzik;

import com.google.common.collect.ImmutableSet;
import io.blert.challenges.tob.HpVarbitTrackedNpc;
import io.blert.challenges.tob.TheatreChallenge;
import io.blert.challenges.tob.TobNpc;
import io.blert.challenges.tob.rooms.Room;
import io.blert.challenges.tob.rooms.RoomDataTracker;
import io.blert.core.*;
import io.blert.events.Event;
import io.blert.events.EventType;
import io.blert.events.NpcAttackEvent;
import io.blert.events.PlayerAttackEvent;
import io.blert.events.tob.*;
import java.util.*;
import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.*;
import net.runelite.api.coords.WorldArea;
import net.runelite.api.coords.WorldPoint;
import net.runelite.api.events.*;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class VerzikDataTracker extends RoomDataTracker {
    private static final int P1_AUTO_ANIMATION = 8109;
    private static final int P2_AUTO_ANIMATION = 8114;
    private static final int P2_BOUNCE_ANIMATION = 8116;
    private static final int P3_TRANSITION_ANIMATION = 8118;

    private static final int P2_BOUNCE_GRAPHIC = 245;
    private static final int P3_TORNADO_HEAL_GRAPHIC = 1602;

    private static final int P2_CABBAGE_PROJECTILE = 1583;
    private static final int P2_ZAP_PROJECTILE = 1585;
    private static final int P2_PURPLE_PROJECTILE = 1586;
    private static final int P2_MAGE_PROJECTILE = 1591;
    private static final int P3_RANGE_PROJECTILE = 1593;
    private static final int P3_MAGE_PROJECTILE = 1594;

    private static final int P1_ATTACK_SPEED = 14;
    private static final int P1_MIN_DAWN_DAMAGE = 75;
    private static final int P2_ATTACK_SPEED = 4;
    private static final int P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN = 3;
    private static final int P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_REDS = 12;
    private static final int P2_ATTACKS_PER_REDS = 7;
    private static final int P3_ATTACK_SPEED = 7;
    private static final int P3_ENRAGED_ATTACK_SPEED = 5;
    private static final int P3_TICKS_BEFORE_FIRST_ATTACK = 12;
    private static final int P3_GREEN_BALL_TICK_DELAY = 12;
    private static final int P3_ATTACKS_BEFORE_SPECIAL = 4;

    private static final ImmutableSet<Integer> VERZIK_WEB_IDS = ImmutableSet.of(8376, 10837, 10854);
    private static final int VERZIK_YELLOW_OBJECT_ID = 1595;

    private static final int DAWNBRINGER_ITEM_ID = 22516;

    private HpVarbitTrackedNpc verzik;

    private VerzikPhase phase;
    int phaseStartTick;

    private int unidentifiedVerzikAttackTick;
    private int nextVerzikAttackTick;
    private final Set<Number> p3MeleeChanceTicks = new HashSet<>();
    private int firstP3AttackTick;
    private @Nullable NpcAttack nextVerzikAttack;
    private int verzikAttacksUntilSpecial;
    private final P2AttackTracker p2AttackTracker = new P2AttackTracker();
    private @Nullable VerzikSpecial activeSpecial;
    private VerzikSpecial nextSpecial;
    boolean enraged;

    private int redCrabsTick;
    private int redCrabSpawnCount;
    private final List<Pair<Number, String>> dawnSpecs = new ArrayList<>();
    private final List<BasicTrackedNpc> pillars = new ArrayList<>();
    private final Set<VerzikCrab> explodingCrabs = new HashSet<>();
    private final Set<BasicTrackedNpc> specialCrabs = new HashSet<>();
    private final List<WorldPoint> yellowPools = new ArrayList<>();
    // Map of tornado NPC index to the tornado, as indexes are stable across spawns.
    private final Map<Integer, BasicTrackedNpc> tornadoes = new HashMap<>();
    private final Map<Player, Number> tornadoHealTicks = new HashMap<>();
    private final Map<Actor, List<Hitsplat>> hitsplatsThisTick = new HashMap<>();
    private final List<Player> p2BounceChances = new ArrayList<>();
    private int p2LastBounce;

    private static class P2AttackTracker {
        private static final int MIN_ATTACKS_BEFORE_ZAP = 4;
        // This isn't the actual minimum, it's just large enough to avoid false
        // positives, even when accounting for client lag.
        private static final int MIN_ATTACKS_BEFORE_PURPLE = 16;

        private int untilZap;
        private int untilPurple;
        private boolean foundAttack;

        @Getter
        private Player target;

        P2AttackTracker() {
            this.untilZap = 0;
            this.untilPurple = 0;
            this.foundAttack = false;
        }

        NpcAttack checkProjectile(Projectile projectile) {
            if (foundAttack) {
                return null;
            }

            NpcAttack attack = null;

            switch (projectile.getId()) {
                case P2_CABBAGE_PROJECTILE:
                    attack = NpcAttack.TOB_VERZIK_P2_CABBAGE;
                    break;
                case P2_ZAP_PROJECTILE:
                    if (untilZap <= 0) {
                        attack = NpcAttack.TOB_VERZIK_P2_ZAP;
                    }
                    break;
                case P2_PURPLE_PROJECTILE:
                    if (untilPurple <= 0) {
                        attack = NpcAttack.TOB_VERZIK_P2_PURPLE;
                    }
                    break;
                case P2_MAGE_PROJECTILE:
                    attack = NpcAttack.TOB_VERZIK_P2_MAGE;
                    break;
            }

            if (attack != null) {
                foundAttack = true;
                this.target =
                        projectile.getTargetActor() instanceof Player ? (Player) projectile.getTargetActor() : null;
            }

            return attack;
        }

        void trackAttack(@Nonnull NpcAttack attack) {
            foundAttack = false;
            this.target = null;

            if (attack == NpcAttack.TOB_VERZIK_P2_PURPLE) {
                untilPurple = MIN_ATTACKS_BEFORE_PURPLE;
            } else {
                untilPurple--;
            }

            if (attack == NpcAttack.TOB_VERZIK_P2_ZAP) {
                untilZap = MIN_ATTACKS_BEFORE_ZAP;
            } else {
                untilZap--;
            }
        }

        void reset() {
            untilZap = 0;
            untilPurple = 0;
            foundAttack = false;
        }
    }

    public VerzikDataTracker(TheatreChallenge manager, Client client) {
        super(manager, client, Room.VERZIK);
        this.phase = VerzikPhase.IDLE;
        this.unidentifiedVerzikAttackTick = -1;
        this.nextVerzikAttackTick = -1;
        this.firstP3AttackTick = -1;
        this.nextVerzikAttack = null;
        this.verzikAttacksUntilSpecial = -1;
        this.redCrabsTick = -1;
        this.redCrabSpawnCount = 0;
        this.p2LastBounce = -1;
        this.enraged = false;
        this.activeSpecial = null;
    }

    @Override
    protected void onRoomStart() {
        client.getTopLevelWorldView().npcs().stream()
                .filter(npc -> TobNpc.isAnyVerzik(npc.getId()))
                .findFirst()
                .ifPresent(npc -> {
                    TobNpc tobNpc = TobNpc.withId(npc.getId()).orElseThrow();

                    if (verzik == null) {
                        verzik = new HpVarbitTrackedNpc(
                                npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
                        addTrackedNpc(verzik);
                    }

                    if (tobNpc.isVerzikP1()) {
                        startVerzikPhase(VerzikPhase.P1, getTick(), true);
                    } else if (tobNpc.isVerzikP2()) {
                        startVerzikPhase(VerzikPhase.P2, getTick(), true);
                    } else if (tobNpc.isVerzikP3()) {
                        startVerzikPhase(VerzikPhase.P3, getTick(), true);
                    }
                });
        phaseStartTick = 0;
    }

    private static final int PLAYER_ATTACK_DAWN_SPEC = 22;

    @Override
    protected void onBlertEvent(Event event) {
        if (phase == VerzikPhase.P1 && event.getType() == EventType.PLAYER_ATTACK) {
            PlayerAttackEvent attackEvent = (PlayerAttackEvent) event;
            if (attackEvent.getAttack().is(PLAYER_ATTACK_DAWN_SPEC)) {
                dawnSpecs.add(Pair.of(attackEvent.getTick(), attackEvent.getUsername()));
            }
        }
    }

    @Override
    protected void onTick() {
        super.onTick();
        final int tick = getTick();

        if (tick == redCrabsTick) {
            if (redCrabSpawnCount == 1) {
                log.debug("Reds: {} ({})", tick, formattedRoomTime());
            }

            // TODO(frolv): Remove this in favor of the generic NPC spawn event.
            dispatchEvent(new VerzikRedsSpawnEvent(tick));
        }

        if (verzik == null) {
            return;
        }

        if (tick == nextVerzikAttackTick - 1) {
            if (phase == VerzikPhase.P2) {
                checkForBounceChances();
            } else if (phase == VerzikPhase.P3) {
                checkForMeleeChance();
            }
        }

        if (tick == nextVerzikAttackTick) {
            handleVerzikAttack(tick);
        }

        if (phase == VerzikPhase.P3) {
            checkForEnrage(tick);
        }

        if (activeSpecial != null && verzik.getNpc().getInteracting() != null) {
            // Once Verzik targets a player, her special attack has ended.
            if (activeSpecial == VerzikSpecial.YELLOWS) {
                nextVerzikAttackTick = tick + 7;
            } else {
                nextVerzikAttackTick = tick + 10;
            }

            activeSpecial = null;
            yellowPools.clear();
        } else if (activeSpecial == VerzikSpecial.YELLOWS) {
            HashSet<WorldPoint> currentYellows = new HashSet<>(yellowPools.size());
            client.getTopLevelWorldView().getGraphicsObjects().forEach(g -> {
                if (g.getId() == VERZIK_YELLOW_OBJECT_ID) {
                    currentYellows.add(WorldPoint.fromLocalInstance(client, g.getLocation()));
                }
            });
            dispatchEvent(new VerzikYellowsEvent(tick, new ArrayList<>(currentYellows)));
        }

        if (tick > phaseStartTick + 5) {
            // After the phase has been active for several ticks, re-enable varbit updates.
            verzik.setDisableVarbitUpdates(false);
        }

        tornadoHealTicks.forEach((player, healTick) -> {
            if (healTick.equals(tick)) {
                sendHealEvent(tick, player);
            }
        });

        hitsplatsThisTick.clear();
    }

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        NPC npc = event.getNpc();
        final int tick = getTick();

        if (activeSpecial == null && VERZIK_WEB_IDS.contains(npc.getId())) {
            activeSpecial = VerzikSpecial.WEBS;
            return Optional.empty();
        }

        if (npc.getId() == NpcID.SUPPORTING_PILLAR) {
            BasicTrackedNpc pillar =
                    new BasicTrackedNpc(npc, TobNpc.VERZIK_PILLAR, generateRoomId(npc), new Hitpoints(0));
            pillars.add(pillar);
            return Optional.of(pillar);
        }

        if (npc.getId() == NpcID.COLLAPSING_PILLAR) {
            // Find the pillar that collapsed and despawn it.
            pillars.stream()
                    .filter(pillar ->
                            npc.getWorldArea().contains2D(pillar.getNpc().getWorldLocation()))
                    .findFirst()
                    .ifPresent(collapsed -> {
                        pillars.remove(collapsed);
                        despawnTrackedNpc(collapsed);
                    });
            return Optional.empty();
        }

        var maybeNpc = TobNpc.withId(npc.getId());
        if (maybeNpc.isEmpty()) {
            return Optional.empty();
        }
        TobNpc tobNpc = maybeNpc.get();

        if (tobNpc.isAnyVerzik()) {
            long roomId = verzik != null ? verzik.getRoomId() : generateRoomId(npc);
            verzik = new HpVarbitTrackedNpc(npc, tobNpc, roomId, new Hitpoints(tobNpc, theatreChallenge.getScale()));
            return Optional.of(verzik);
        }

        if (tobNpc.isVerzikCrab()) {
            WorldPoint point = getWorldLocation(npc);
            long roomId = generateRoomId(npc);
            VerzikCrab crab = VerzikCrab.fromSpawnedNpc(npc, tobNpc, roomId, point, theatreChallenge.getScale(), phase);

            explodingCrabs.add(crab);
            return Optional.of(crab);
        }

        if (tobNpc.isVerzikAthanatos()) {
            BasicTrackedNpc purpleCrab = new BasicTrackedNpc(
                    npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
            specialCrabs.add(purpleCrab);
            return Optional.of(purpleCrab);
        }

        if (tobNpc.isVerzikMatomenos()) {
            if (tick != redCrabsTick) {
                startNewRedsPhase(tick);
            }

            BasicTrackedNpc crab = new BasicTrackedNpc(
                    npc, tobNpc, generateRoomId(npc), new Hitpoints(tobNpc, theatreChallenge.getScale()));
            specialCrabs.add(crab);
            return Optional.of(crab);
        }

        if (tobNpc.isVerzikTornado()) {
            BasicTrackedNpc existing = tornadoes.get(npc.getIndex());
            long roomId = existing != null ? existing.getRoomId() : generateRoomId(npc);
            BasicTrackedNpc tornado =
                    new BasicTrackedNpc(npc, tobNpc, roomId, new Hitpoints(tobNpc, theatreChallenge.getScale()));
            tornadoes.put(npc.getIndex(), tornado);
            return Optional.of(tornado);
        }

        return Optional.empty();
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, TrackedNpc trackedNpc) {
        NPC npc = event.getNpc();

        if (TobNpc.isVerzikMatomenos(npc.getId()) || TobNpc.isVerzikAthanatos(npc.getId())) {
            return trackedNpc instanceof BasicTrackedNpc && specialCrabs.remove(trackedNpc);
        }

        if (TobNpc.isVerzikCrab(npc.getId())) {
            return trackedNpc instanceof VerzikCrab && explodingCrabs.remove(trackedNpc);
        }

        if (TobNpc.isVerzikTornado(npc.getId())) {
            // A despawn event following hitting a player should not remove the
            // tracked NPC as the same logical tornado (with the same NPC index)
            // will reappear. Only fully despawn tornadoes at the end.
            return false;
        }

        if (TobNpc.isVerzikP1(npc.getId())) {
            final int tick = getTick();
            log.debug("P1: {} ({})", tick, formattedRoomTime());
            dispatchEvent(new VerzikPhaseEvent(tick, VerzikPhase.P2));
        }

        // Verzik despawns between phases, but it should not be counted.
        // Her final despawn is sent when she changes to her death ID.
        return false;
    }

    @Override
    protected void onNpcChange(NpcChanged changed) {
        NPC npc = changed.getNpc();
        int beforeId = changed.getOld().getId();

        final int tick = getTick();

        var maybeVerzik = TobNpc.withId(npc.getId());
        if (maybeVerzik.isEmpty()) {
            return;
        }
        TobNpc tobNpc = maybeVerzik.get();

        if (TobNpc.isVerzikIdle(beforeId) && tobNpc.isVerzikP1()) {
            startRoom();
            return;
        }

        if (TobNpc.isVerzikP1Transition(beforeId) && tobNpc.isVerzikP2()) {
            startVerzikPhase(VerzikPhase.P2, tick, false);

            // A transition from P1 to P2 does not spawn a new NPC. Simply reset Verzik's HP to its P2 value.
            verzik.setHitpoints(new Hitpoints(tobNpc, theatreChallenge.getScale()));
        }

        if (TobNpc.isVerzikP3(beforeId) && tobNpc.isVerzikDeath() && verzik != null) {
            despawnTrackedNpc(verzik);
            verzik = null;
            phase = VerzikPhase.IDLE;

            for (TrackedNpc tornado : tornadoes.values()) {
                despawnTrackedNpc(tornado);
            }
            tornadoes.clear();
        }
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        if (!(actor instanceof NPC)) {
            return;
        }

        final int npcId = ((NPC) actor).getId();
        final int animationId = actor.getAnimation();
        final int tick = getTick();

        if (phase == VerzikPhase.P2 && TobNpc.isVerzikP2(npcId) && animationId == P3_TRANSITION_ANIMATION) {
            startVerzikPhase(VerzikPhase.P3, tick, true);
            firstP3AttackTick = nextVerzikAttackTick;
            log.debug("P2: {} ({})", tick, formattedRoomTime());
            return;
        }

        if (verzik != null && actor == verzik.getNpc()) {
            if (animationId == P1_AUTO_ANIMATION && phase == VerzikPhase.P1) {
                nextVerzikAttackTick = tick + 1;
                nextVerzikAttack = NpcAttack.TOB_VERZIK_P1_AUTO;
                return;
            }

            if (animationId == P2_AUTO_ANIMATION && phase == VerzikPhase.P2) {
                nextVerzikAttackTick = tick;
                return;
            }

            if (animationId == P2_BOUNCE_ANIMATION && phase == VerzikPhase.P2) {
                nextVerzikAttack = NpcAttack.TOB_VERZIK_P2_BOUNCE;
                p2LastBounce = tick;
            }
        }
    }

    @Override
    protected void onProjectile(ProjectileMoved event) {
        final int tick = getTick();
        Projectile projectile = event.getProjectile();

        if (phase == VerzikPhase.P2 && tick == nextVerzikAttackTick && nextVerzikAttack == null) {
            if (tick != redCrabsTick) {
                nextVerzikAttack = p2AttackTracker.checkProjectile(projectile);
            }
        }

        if (phase == VerzikPhase.P3) {
            int totalCycles = projectile.getEndCycle() - projectile.getStartCycle();
            if (projectile.getRemainingCycles() != totalCycles) {
                return;
            }

            VerzikAttackStyleEvent.Style style;
            switch (event.getProjectile().getId()) {
                case P3_RANGE_PROJECTILE:
                    style = VerzikAttackStyleEvent.Style.RANGE;
                    break;
                case P3_MAGE_PROJECTILE:
                    style = VerzikAttackStyleEvent.Style.MAGE;
                    break;
                default:
                    return;
            }

            if (unidentifiedVerzikAttackTick != -1) {
                dispatchEvent(new VerzikAttackStyleEvent(tick, style, unidentifiedVerzikAttackTick));
                unidentifiedVerzikAttackTick = -1;
            }
        }
    }

    @Override
    protected void onGraphicsObjectCreation(GraphicsObjectCreated event) {
        GraphicsObject obj = event.getGraphicsObject();
        if (obj.getId() != VERZIK_YELLOW_OBJECT_ID) {
            return;
        }

        int totalYellows = theatreChallenge.getLivingRaiderCount();
        if (theatreChallenge.getChallengeMode() == ChallengeMode.TOB_HARD) {
            totalYellows *= 3;
        }

        if (yellowPools.isEmpty()) {
            activeSpecial = VerzikSpecial.YELLOWS;
        }

        if (yellowPools.size() <= totalYellows) {
            yellowPools.add(WorldPoint.fromLocalInstance(client, obj.getLocation()));
        }
    }

    @Override
    protected void onGraphicChange(GraphicChanged event) {
        if (!(event.getActor() instanceof Player)) {
            return;
        }

        Player player = (Player) event.getActor();
        final int tick = getTick();

        if (player.hasSpotAnim(P3_TORNADO_HEAL_GRAPHIC)) {
            int lastHeal = tornadoHealTicks.getOrDefault(player, -1).intValue();
            if (lastHeal < tick - 10) {
                tornadoHealTicks.put(player, tick);
            }
        }

        if (player.hasSpotAnim(P2_BOUNCE_GRAPHIC)) {
            if (p2LastBounce >= tick - 3) {
                int chances = p2BounceChances.size();
                dispatchEvent(new VerzikBounceEvent(
                        tick, p2LastBounce, chances, theatreChallenge.getLivingRaiderCount() - chances, player));
            }
            p2LastBounce = -1;
            p2BounceChances.clear();
        }
    }

    @Override
    protected void onHitsplat(HitsplatApplied event) {
        if (phase == VerzikPhase.P1) {
            Hitsplat hitsplat = event.getHitsplat();
            if (event.getActor() == verzik.getNpc() && hitsplat.getAmount() >= P1_MIN_DAWN_DAMAGE) {
                var iter = dawnSpecs.iterator();
                while (iter.hasNext()) {
                    var pair = iter.next();
                    int attackTick = pair.getLeft().intValue();

                    if (attackTick >= getTick() - 4) {
                        dispatchEvent(new VerzikDawnEvent(
                                getTick(),
                                getWorldLocation(verzik),
                                attackTick,
                                hitsplat.getAmount(),
                                pair.getRight()));
                        iter.remove();
                        break;
                    }
                }
            }
        } else if (phase == VerzikPhase.P3) {
            // Hitsplats are tracked for P3 healing.
            hitsplatsThisTick
                    .computeIfAbsent(event.getActor(), k -> new ArrayList<>())
                    .add(event.getHitsplat());
        }
    }

    @Override
    protected void onItemSpawn(ItemSpawned event) {
        if (event.getItem().getId() == DAWNBRINGER_ITEM_ID
                && verzik != null
                && TobNpc.isVerzikP1(verzik.getNpc().getId())) {
            dispatchEvent(new VerzikDawnDropEvent(getTick(), getWorldLocation(event.getTile()), true));
        }
    }

    @Override
    protected void onItemDespawn(ItemDespawned event) {
        if (event.getItem().getId() == DAWNBRINGER_ITEM_ID
                && verzik != null
                && TobNpc.isVerzikP1(verzik.getNpc().getId())) {
            dispatchEvent(new VerzikDawnDropEvent(getTick(), getWorldLocation(event.getTile()), false));
        }
    }

    private void checkForBounceChances() {
        p2BounceChances.clear();
        WorldArea verzikArea = verzik.getNpc().getWorldArea();

        theatreChallenge.getParty().forEach(r -> {
            Player player = r.getPlayer();
            if (r.isDead() || player == null) {
                return;
            }

            WorldPoint point = player.getWorldLocation();
            if (verzikArea.isInMeleeDistance(point) || verzikArea.contains(point)) {
                p2BounceChances.add(player);
            }
        });
    }

    private void checkForMeleeChance() {
        if (nextVerzikAttackTick == firstP3AttackTick) {
            // First P3 attack can't be melee.
            return;
        }

        Actor tank = verzik.getNpc().getInteracting();
        if (!(tank instanceof Player)) {
            return;
        }

        WorldArea verzikArea = verzik.getNpc().getWorldArea();
        boolean isMeleeDistance = verzikArea.isInMeleeDistance(tank.getWorldLocation());
        boolean isUnderVerzik = verzikArea.contains(tank.getWorldLocation());
        if (isMeleeDistance && !isUnderVerzik) {
            log.debug("Player {} chanced a melee on tick {} ({})", tank.getName(), getTick(), formattedRoomTime());
            p3MeleeChanceTicks.add(nextVerzikAttackTick);
        }
    }

    private void sendMeleeIfChanced(int tick) {
        if (unidentifiedVerzikAttackTick != -1) {
            // No projectiles were recorded since the last Verzik
            // attack. Check if it could have been a melee attack.
            if (p3MeleeChanceTicks.contains(unidentifiedVerzikAttackTick)) {
                dispatchEvent(new VerzikAttackStyleEvent(
                        tick, VerzikAttackStyleEvent.Style.MELEE, unidentifiedVerzikAttackTick));
            }
            unidentifiedVerzikAttackTick = -1;
        }
    }

    private void handleVerzikAttack(int tick) {
        var party = getChallenge().getParty();
        if (!party.isEmpty() && party.stream().allMatch(Raider::isDead)) {
            // Verzik attacks are inferred purely from tick cadence rather than
            // animations. Stop sending them if every player has died.
            if (phase == VerzikPhase.P3) {
                sendMeleeIfChanced(tick);
            }
            return;
        }

        Player target = null;

        switch (phase) {
            case P1:
                // No special handling required.
                break;

            case P2:
                if (nextVerzikAttack == null && tick != redCrabsTick) {
                    for (Projectile projectile : client.getProjectiles()) {
                        NpcAttack maybeAttack = p2AttackTracker.checkProjectile(projectile);
                        if (maybeAttack != null) {
                            nextVerzikAttack = maybeAttack;
                            break;
                        }
                    }
                }

                if (nextVerzikAttack != null) {
                    target = p2AttackTracker.getTarget();
                    p2AttackTracker.trackAttack(nextVerzikAttack);
                    if (nextVerzikAttack != NpcAttack.TOB_VERZIK_P2_BOUNCE) {
                        int chances = p2BounceChances.size();
                        dispatchEvent(new VerzikBounceEvent(
                                tick, chances, theatreChallenge.getLivingRaiderCount() - chances));
                        p2BounceChances.clear();
                        p2LastBounce = -1;
                    }
                }

                if (verzikAttacksUntilSpecial > 0) {
                    verzikAttacksUntilSpecial--;
                }
                if (redCrabSpawnCount > 0 && verzikAttacksUntilSpecial == 0) {
                    // Last auto before the next reds phase.
                    nextVerzikAttackTick = -1;
                }
                break;

            case P3:
                sendMeleeIfChanced(tick);

                if (verzikAttacksUntilSpecial == 0) {
                    verzikAttacksUntilSpecial = P3_ATTACKS_BEFORE_SPECIAL;
                    WorldPoint point = getWorldLocation(verzik);

                    switch (nextSpecial) {
                        case BALL:
                            // Green ball occurs alongside a regular Verzik attack, and delays her next attack by 12
                            // ticks total.
                            // TODO(frolv): `unidentifiedVerzikAttackTick` is deliberately not set here, as the green
                            // ball projectile seems to hide the regular attack projectiles. Investigate this further.
                            dispatchEvent(
                                    new NpcAttackEvent(getStage(), tick, point, NpcAttack.VERZIK_P3_BALL, verzik));
                            nextSpecial = nextSpecial.next();
                            verzikAttacksUntilSpecial += 1;
                            nextVerzikAttackTick += P3_GREEN_BALL_TICK_DELAY - attackSpeed();
                            break;

                        case CRABS:
                            // Crabs occur alongside a regular Verzik attack whose projectile is identifiable.
                            nextSpecial = nextSpecial.next();
                            verzikAttacksUntilSpecial += 1;
                            unidentifiedVerzikAttackTick = tick;
                            break;

                        case WEBS:
                        case YELLOWS:
                            NpcAttack attack = nextSpecial == VerzikSpecial.WEBS
                                    ? NpcAttack.TOB_VERZIK_P3_WEBS
                                    : NpcAttack.TOB_VERZIK_P3_YELLOWS;
                            dispatchEvent(new NpcAttackEvent(getStage(), tick, point, attack, verzik));

                            // Other specials pause the attack cycle until they are completed.
                            p3MeleeChanceTicks.remove(tick);
                            nextSpecial = nextSpecial.next();
                            nextVerzikAttackTick = -1;
                            nextVerzikAttack = null;
                            return;
                    }
                } else {
                    // Verzik is performing a regular attack on this tick, without a stacked special.
                    // Mark the attack as unidentified.
                    unidentifiedVerzikAttackTick = tick;
                }

                nextVerzikAttack = NpcAttack.TOB_VERZIK_P3_AUTO;
                verzikAttacksUntilSpecial--;
                break;

            default:
                nextVerzikAttack = null;
                nextVerzikAttackTick = -1;
                return;
        }

        if (nextVerzikAttack != null && verzik != null) {
            WorldPoint location = getWorldLocation(verzik);
            if (target != null) {
                dispatchEvent(
                        new NpcAttackEvent(getStage(), tick, location, nextVerzikAttack, verzik, target.getName()));
            } else {
                dispatchEvent(new NpcAttackEvent(getStage(), tick, location, nextVerzikAttack, verzik));
            }
        }

        nextVerzikAttackTick += attackSpeed();
        nextVerzikAttack = null;
    }

    private void checkForEnrage(int tick) {
        if (enraged) {
            return;
        }

        if (verzik.getHitpoints().percentage() < 25.0 && verzik.getNpc().getOverheadText() != null) {
            enraged = true;
            if (activeSpecial != VerzikSpecial.WEBS) {
                nextVerzikAttackTick = tick + P3_ENRAGED_ATTACK_SPEED;
            }
        }
    }

    private void startVerzikPhase(VerzikPhase phase, int tick, boolean dispatchPhaseEvent) {
        this.phase = phase;
        phaseStartTick = tick;
        nextVerzikAttack = null;
        unidentifiedVerzikAttackTick = -1;

        if (phase == VerzikPhase.P2) {
            nextVerzikAttackTick = tick + P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_SPAWN;
            p2AttackTracker.reset();
        } else if (phase == VerzikPhase.P3) {
            nextVerzikAttackTick = tick + P3_TICKS_BEFORE_FIRST_ATTACK;
            p3MeleeChanceTicks.clear();
            verzikAttacksUntilSpecial = P3_ATTACKS_BEFORE_SPECIAL;
            nextSpecial = VerzikSpecial.CRABS;
            activeSpecial = null;
            enraged = false;
        } else {
            dawnSpecs.clear();
            nextVerzikAttackTick = -1;
        }

        if (verzik != null) {
            // Temporarily disable varbit updates at the start of a phase as the varbit lags behind, so it may reset
            // the HP to 0 from the previous phase.
            verzik.setDisableVarbitUpdates(true);
        }

        if (dispatchPhaseEvent && phase != VerzikPhase.P1) {
            dispatchEvent(new VerzikPhaseEvent(tick, phase));
        }
    }

    private void startNewRedsPhase(int tick) {
        redCrabsTick = tick;
        redCrabSpawnCount++;
        verzikAttacksUntilSpecial = P2_ATTACKS_PER_REDS;
        nextVerzikAttack = null;
        nextVerzikAttackTick = tick + P2_TICKS_BEFORE_FIRST_ATTACK_AFTER_REDS;
    }

    private int attackSpeed() {
        switch (phase) {
            case P1:
                return P1_ATTACK_SPEED;
            case P2:
                return P2_ATTACK_SPEED;
            case P3:
                return enraged ? P3_ENRAGED_ATTACK_SPEED : P3_ATTACK_SPEED;
            default:
                return -1;
        }
    }

    private void sendHealEvent(int tick, Player player) {
        int healAmount = -1;

        List<Hitsplat> playerSplats = hitsplatsThisTick.get(player);
        List<Hitsplat> verzikSplats = hitsplatsThisTick.get(verzik.getNpc());
        if (playerSplats != null && verzikSplats != null) {
            for (Hitsplat splat : playerSplats) {
                if (splat.getHitsplatType() != HitsplatID.DAMAGE_OTHER) {
                    continue;
                }

                int expectedHeal = splat.getAmount() * 3;

                Iterator<Hitsplat> iter = verzikSplats.iterator();
                while (iter.hasNext()) {
                    Hitsplat verzikSplat = iter.next();
                    if (verzikSplat.getHitsplatType() != HitsplatID.HEAL) {
                        continue;
                    }

                    if (verzikSplat.getAmount() == expectedHeal) {
                        healAmount = expectedHeal;
                        iter.remove();
                        break;
                    }
                }
            }
        }

        dispatchEvent(new VerzikHealEvent(tick, getWorldLocation(player), player, healAmount));
    }
}
