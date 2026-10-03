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

import javax.annotation.Nullable;
import lombok.Getter;

public enum Challenge {
    TOB("Theatre of Blood", 1),
    COX("Chambers of Xeric", 2),
    TOA("Tombs of Amascut", 3),
    COLOSSEUM("Colosseum", 4),
    INFERNO("Inferno", 5),
    MOKHAIOTL("Mokhaiotl", 6),
    ;

    @Getter
    private final String name;

    @Getter
    private final int id;

    Challenge(String name, int id) {
        this.name = name;
        this.id = id;
    }

    @Nullable
    public static Challenge fromId(int id) {
        for (Challenge challenge : values()) {
            if (challenge.getId() == id) {
                return challenge;
            }
        }
        return null;
    }
}
