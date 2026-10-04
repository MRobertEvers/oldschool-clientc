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

package io.blert.util;

import java.util.Optional;
import org.apache.commons.lang3.tuple.Pair;

public class Tick {
    public static final int MILLISECONDS_PER_TICK = 600;
    public static final String TIME_STRING_REGEX = "(\\d+:)?\\d{1,2}:\\d{2}(\\.\\d{2})?";

    /**
     * Convert a number of ticks to a human-readable time string.
     *
     * @param ticks The number of ticks.
     * @return Formatted time string.
     */
    public static String asTimeString(int ticks) {
        if (ticks < 0) {
            throw new IllegalArgumentException("ticks must be non-negative");
        }

        int hours = 0;
        if (ticks >= 6000) {
            hours = ticks / 6000;
            ticks = ticks % 6000;
        }

        int milliseconds = ticks * MILLISECONDS_PER_TICK;
        int seconds = (milliseconds / 1000) % 60;
        int minutes = milliseconds / 1000 / 60;
        int centiseconds = (milliseconds % 1000) / 10;

        if (hours > 0) {
            return String.format("%d:%02d:%02d.%02d", hours, minutes, seconds, centiseconds);
        }

        return String.format("%d:%02d.%02d", minutes, seconds, centiseconds);
    }

    /**
     * Convert a human-readable time string to a number of ticks. The string may either be a "precise" in-game time
     * string (e.g. "2:19.20") or an imprecise time string (e.g. "2:19"). In the latter case, the time will be rounded
     * up to the nearest tick.
     *
     * @param timeString The time string.
     * @return The number of ticks.
     */
    public static Optional<Pair<Integer, Boolean>> fromTimeString(String timeString) {
        if (timeString.isEmpty()) {
            return Optional.empty();
        }

        if (!timeString.matches(TIME_STRING_REGEX)) {
            throw new IllegalArgumentException("Invalid time string: " + timeString);
        }

        String[] parts = timeString.split(":");

        int hours = 0;
        int minutes;
        String[] secondsAndCentiseconds;
        if (parts.length == 3) {
            hours = Integer.parseInt(parts[0]);
            minutes = Integer.parseInt(parts[1]);
            secondsAndCentiseconds = parts[2].split("\\.");
        } else {
            minutes = Integer.parseInt(parts[0]);
            secondsAndCentiseconds = parts[1].split("\\.");
        }

        int seconds = Integer.parseInt(secondsAndCentiseconds[0]);

        boolean precise = true;

        int milliseconds = hours * 60 * 60 * 1000 + minutes * 60 * 1000 + seconds * 1000;
        if (secondsAndCentiseconds.length == 2) {
            int centiseconds = Integer.parseInt(secondsAndCentiseconds[1]);
            milliseconds += centiseconds * 10;
        } else {
            precise = false;
            int remainder = milliseconds % MILLISECONDS_PER_TICK;
            if (remainder != 0) {
                // Round up to the next tick.
                milliseconds += MILLISECONDS_PER_TICK - remainder;
            }
        }

        return Optional.of(Pair.of(milliseconds / MILLISECONDS_PER_TICK, precise));
    }
}
