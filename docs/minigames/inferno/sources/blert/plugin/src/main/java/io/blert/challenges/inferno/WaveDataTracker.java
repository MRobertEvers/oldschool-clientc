/*
 * Copyright (c) 2025 Alexei Frolov
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

package io.blert.challenges.inferno;

import io.blert.core.*;
import io.blert.events.NpcAttackEvent;
import io.blert.events.inferno.InfernoWaveStartEvent;
import io.blert.util.ChatText;
import java.util.Optional;
import java.util.regex.Matcher;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.events.AnimationChanged;
import net.runelite.api.events.ChatMessage;
import net.runelite.api.events.NpcDespawned;
import net.runelite.api.events.NpcSpawned;

@Slf4j
public class WaveDataTracker extends DataTracker {
    private static final String waveEndMessage = "Wave completed!";
    private final int wave;
    private final String waveStartMessage;

    public static Stage waveToStage(int wave) {
        if (wave < 1 || wave > 69) {
            throw new IllegalArgumentException("Wave must be between 1 and 69");
        }
        return Stage.values()[Stage.INFERNO_WAVE_1.ordinal() + (wave - 1)];
    }

    public WaveDataTracker(InfernoChallenge challenge, Client client, int wave) {
        super(challenge, client, waveToStage(wave));
        this.wave = wave;
        this.waveStartMessage = String.format("Wave: %d", wave);
    }

    @Override
    protected void start() {
        super.start();

        InfernoChallenge challenge = (InfernoChallenge) getChallenge();
        if (!challenge.hasLogged()) {
            int startTick = challenge.recordedDurationTicks();
            dispatchEvent(new InfernoWaveStartEvent(getStage(), wave, startTick));
        }

        // NPC spawn events are typically received before the wave start message,
        // so capture any NPCs that are already present, ignoring those already
        // captured during NOT_STARTED.
        client.getTopLevelWorldView().npcs().stream()
                .filter(npc -> getTrackedNpcs().getByNpc(npc).isEmpty())
                .forEach(npc -> {
                    InfernoNpc.withId(npc.getId())
                            .filter(inf -> !inf.isPillar())
                            .ifPresent(infernoNpc -> addTrackedNpc(new BasicTrackedNpc(
                                    npc, generateRoomId(npc), new Hitpoints(infernoNpc.getHitpoints()))));
                });
    }

    @Override
    protected void onTick() {}

    @Override
    protected Optional<? extends TrackedNpc> onNpcSpawn(NpcSpawned event) {
        return InfernoNpc.withId(event.getNpc().getId()).flatMap(npc -> {
            if (npc.isPillar()) {
                // Pillars are managed by the challenge itself.
                return Optional.empty();
            }

            Hitpoints hitpoints = getTick() > 0 && wave <= 66 && npc.isResurrectable()
                    ? npc.resurrectedHitpoints()
                    : new Hitpoints(npc.getHitpoints());

            return Optional.of(new BasicTrackedNpc(event.getNpc(), generateRoomId(event.getNpc()), hitpoints));
        });
    }

    @Override
    protected boolean onNpcDespawn(NpcDespawned event, @Nullable TrackedNpc trackedNpc) {
        if (trackedNpc instanceof Pillar) {
            ((InfernoChallenge) getChallenge()).removePillar(trackedNpc);
            return true;
        }
        return trackedNpc != null;
    }

    @Override
    protected void onAnimation(AnimationChanged event) {
        Actor actor = event.getActor();
        if (!(actor instanceof NPC)) {
            return;
        }

        Optional<InfernoNpc> maybeInfernoNpc = InfernoNpc.withId(((NPC) actor).getId());
        if (maybeInfernoNpc.isEmpty()) {
            return;
        }

        Optional<TrackedNpc> maybeNpc = getTrackedNpcs().getByNpc((NPC) actor);
        if (maybeNpc.isEmpty()) {
            return;
        }

        InfernoNpc infernoNpc = maybeInfernoNpc.get();
        TrackedNpc trackedNpc = maybeNpc.get();
        int animation = actor.getAnimation();

        infernoNpc
                .getAttack(animation)
                .ifPresent(npcAttack -> dispatchEvent(new NpcAttackEvent(
                        getStage(), getTick(), getWorldLocation(trackedNpc), npcAttack, trackedNpc)));
    }

    @Override
    protected void onMessage(ChatMessage event) {
        String stripped = ChatText.stripFormatting(event.getMessage());
        if (stripped.equals(waveStartMessage)) {
            start();
        } else if (stripped.equals(waveEndMessage)) {
            finish(true);
        } else {
            Matcher matcher = InfernoChallenge.INFERNO_END_REGEX.matcher(stripped);
            if (matcher.find()) {
                finish(true);
            }
        }
    }
}
