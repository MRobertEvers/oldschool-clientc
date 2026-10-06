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

import lombok.Getter;

@Getter
public enum Handicap {
    MANTIMAYHEM(0),
    REENTRY(1),
    BEES(2),
    VOLATILITY(3),
    BLASPHEMY(4),
    RELENTLESS(5),
    QUARTET(6),
    TOTEMIC(7),
    DOOM(8),
    DYNAMIC_DUO(9),
    SOLARFLARE(10),
    MYOPIA(11),
    FRAILTY(12),
    RED_FLAG(13),
    ;

    private final int id;

    Handicap(int id) {
        this.id = id;
    }

    public static Handicap withId(int id) {
        for (Handicap handicap : values()) {
            if (handicap.id == id) {
                return handicap;
            }
        }
        return null;
    }
}
