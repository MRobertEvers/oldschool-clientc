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

package io.blert.challenges.tob.rooms;

import io.blert.core.Stage;

/**
 * Rooms in the Theatre of Blood.
 */
public enum Room {
    MAIDEN,
    BLOAT,
    NYLOCAS,
    SOTETSEG,
    XARPUS,
    VERZIK;

    /**
     * Returns the name of the room as written in the in-game "wave" message.
     */
    public String waveName() {
        switch (this) {
            case MAIDEN:
                return "The Maiden of Sugadinti";
            case BLOAT:
                return "The Pestilent Bloat";
            case NYLOCAS:
                return "The Nylocas";
            case SOTETSEG:
                return "Sotetseg";
            case XARPUS:
                return "Xarpus";
            case VERZIK:
                return "The Final Challenge";
            default:
                return "";
        }
    }

    public Stage toStage() {
        switch (this) {
            case MAIDEN:
                return Stage.TOB_MAIDEN;
            case BLOAT:
                return Stage.TOB_BLOAT;
            case NYLOCAS:
                return Stage.TOB_NYLOCAS;
            case SOTETSEG:
                return Stage.TOB_SOTETSEG;
            case XARPUS:
                return Stage.TOB_XARPUS;
            case VERZIK:
                return Stage.TOB_VERZIK;
        }

        return null;
    }
}
