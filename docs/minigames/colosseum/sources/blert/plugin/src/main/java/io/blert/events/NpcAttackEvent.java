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

package io.blert.events;

import io.blert.core.NpcAttack;
import io.blert.core.Stage;
import io.blert.core.TrackedNpc;
import javax.annotation.Nullable;
import lombok.Getter;
import net.runelite.api.Player;
import net.runelite.api.coords.WorldPoint;

@Getter
public class NpcAttackEvent extends Event {
    private final NpcAttack attack;
    private final int npcId;
    private final long roomId;
    private final @Nullable String target;

    public NpcAttackEvent(
            Stage stage,
            int tick,
            @Nullable WorldPoint point,
            NpcAttack attack,
            TrackedNpc npc,
            @Nullable String target) {
        super(EventType.NPC_ATTACK, stage, tick, point);
        this.attack = attack;
        this.npcId = npc.getNpcId();
        this.roomId = npc.getRoomId();
        this.target = target;
    }

    public NpcAttackEvent(Stage stage, int tick, @Nullable WorldPoint point, NpcAttack attack, TrackedNpc npc) {
        this(
                stage,
                tick,
                point,
                attack,
                npc,
                npc.getNpc().getInteracting() instanceof Player
                        ? npc.getNpc().getInteracting().getName()
                        : null);
    }

    @Override
    protected String eventDataString() {
        return "npc_attack=(attack=" + attack + ", target=" + (target != null ? target : "none") + ")";
    }
}
