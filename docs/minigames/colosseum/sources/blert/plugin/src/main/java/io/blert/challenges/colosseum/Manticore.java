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

import io.blert.core.BasicTrackedNpc;
import io.blert.core.Hitpoints;
import io.blert.core.NpcAttack;
import javax.annotation.Nullable;
import lombok.Getter;
import lombok.NonNull;
import net.runelite.api.NPC;

public class Manticore extends BasicTrackedNpc {
    public static final int LOADING_ANIMATION = 10868;
    public static final int ATTACK_ANIMATION = 10869;

    private static final int MAGE_PROJECTILE = 2681;
    private static final int RANGE_PROJECTILE = 2683;
    private static final int MELEE_PROJECTILE = 2685;

    enum Style {
        MAGE,
        RANGE,
        MELEE,
    }

    @Getter
    private @Nullable Style style = null;

    private int attacksRemaining = 0;

    public Manticore(@NonNull NPC npc, long roomId, Hitpoints hitpoints) {
        super(npc, roomId, hitpoints);
    }

    public void updateStyle() {
        if (style != null) {
            return;
        }

        NPC npc = getNpc();
        if (npc.getAnimation() == LOADING_ANIMATION || npc.getAnimation() == ATTACK_ANIMATION) {
            // Manticores can have multiple style graphics; the first one is its next attack.
            for (var spotAnim : npc.getSpotAnims()) {
                if (spotAnim.getId() == MAGE_PROJECTILE) {
                    style = Style.MAGE;
                    break;
                }
                if (spotAnim.getId() == RANGE_PROJECTILE) {
                    style = Style.RANGE;
                    break;
                }
                if (spotAnim.getId() == MELEE_PROJECTILE) {
                    style = Style.MELEE;
                    break;
                }
            }
        } else {
            style = null;
        }
    }

    public void startAttack() {
        attacksRemaining = 3;
    }

    public @Nullable NpcAttack continueAttack() {
        if (attacksRemaining == 0 || getNpc().isDead()) {
            return null;
        }

        attacksRemaining--;
        NpcAttack attack = attackForStyle();
        style = null;
        return attack;
    }

    private @Nullable NpcAttack attackForStyle() {
        if (style == null) {
            return null;
        }
        switch (style) {
            case MAGE:
                return NpcAttack.COLOSSEUM_MANTICORE_MAGE;
            case RANGE:
                return NpcAttack.COLOSSEUM_MANTICORE_RANGE;
            default:
                return NpcAttack.COLOSSEUM_MANTICORE_MELEE;
        }
    }
}
