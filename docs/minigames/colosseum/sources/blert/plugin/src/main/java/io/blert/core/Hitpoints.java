/*
 * Copyright (c) 2023-2024 Alexei Frolov
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

import io.blert.challenges.tob.TobNpc;

/**
 * Represents a game entity's current and maximum hitpoints.
 */
public class Hitpoints extends SkillLevel {
    public static Hitpoints fromRatio(double ratio, int baseHp) {
        return new Hitpoints((int) (baseHp * ratio), baseHp);
    }

    public Hitpoints(int base) {
        super(base, base);
    }

    public Hitpoints(int current, int base) {
        super(current, base);
    }

    public Hitpoints(Hitpoints other) {
        super(other.current, other.base);
    }

    public Hitpoints(TobNpc tobNpc, int scale) {
        this(tobNpc.getBaseHitpoints(scale));
    }

    public Hitpoints update(int current) {
        return new Hitpoints(current, base);
    }

    public double percentage() {
        if (base == 0) {
            return Double.NaN;
        }
        return current / (double) base * 100.0;
    }
}
