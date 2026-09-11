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

package io.blert.challenges.tob;

import io.blert.core.BasicTrackedNpc;
import io.blert.core.Hitpoints;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import net.runelite.api.NPC;

/**
 * A tracked NPC whose hitpoints are periodically updated from a varbit.
 */
@Getter
@Setter
public class HpVarbitTrackedNpc extends BasicTrackedNpc {
    private final TobNpc tobNpc;
    boolean disableVarbitUpdates;

    public HpVarbitTrackedNpc(@NonNull NPC npc, TobNpc tobNpc, long roomId, Hitpoints hitpoints) {
        super(npc, tobNpc, roomId, hitpoints);
        this.disableVarbitUpdates = false;
        this.tobNpc = tobNpc;
    }

    public void updateHitpointsFromVarbit(int varbitValue) {
        if (disableVarbitUpdates) {
            return;
        }

        // The varbit stores a value from 0 to 1000, representing the percentage of hitpoints remaining
        // to a tenth of a percent.
        double ratio = varbitValue / 1000.0;
        int updatedHitpoints = (int) (getHitpoints().getBase() * ratio);
        int currentHitpoints = getHitpoints().getCurrent();

        // As bosses can have several thousands of hitpoints, the calculated value is not entirely precise.
        // If the current value is within a few hitpoints of the calculated value, assume that it is correct.
        if (Math.abs(currentHitpoints - updatedHitpoints) > 5) {
            Hitpoints newHitpoints = getHitpoints().update(updatedHitpoints);
            setHitpoints(newHitpoints);
        }
    }
}
