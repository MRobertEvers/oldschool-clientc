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

import java.util.Optional;

/**
 * State of rooms in a ToB raid.
 * The ordinal values of this enum correspond to values of the TOB_ROOM_STATUS client varbit.
 */
public enum RoomState {
    INACTIVE,
    // The actual boss fight.
    BOSS,
    // A special phase in a boss fight (Sote maze, Xarpus exhumes).
    SPECIAL,
    // P1 has its own status value for some reason (thanks Jagex).
    VERZIK_P1;

    public static Optional<RoomState> fromVarbit(int varbit) {
        switch (varbit) {
            case 0:
                return Optional.of(INACTIVE);
            case 1:
                return Optional.of(BOSS);
            case 2:
                return Optional.of(SPECIAL);
            case 3:
                return Optional.of(VERZIK_P1);
            default:
                return Optional.empty();
        }
    }

    public boolean isActive() {
        return this != INACTIVE;
    }
}
