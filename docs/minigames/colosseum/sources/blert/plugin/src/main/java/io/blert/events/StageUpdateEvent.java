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

import io.blert.core.Stage;
import java.util.Optional;
import lombok.Getter;

@Getter
public class StageUpdateEvent extends Event {
    public enum Status {
        ENTERED,
        STARTED,
        COMPLETED,
        WIPED,
    }

    private final Status status;
    private final boolean accurate;
    private final Optional<Integer> inGameTicks;
    private final boolean gameTicksPrecise;

    public StageUpdateEvent(Stage stage, int tick, Status status) {
        this(stage, tick, status, false, Optional.empty(), false);
    }

    public StageUpdateEvent(
            Stage stage,
            int tick,
            Status status,
            boolean accurate,
            Optional<Integer> inGameTicks,
            boolean gameTicksPrecise) {
        super(EventType.STAGE_UPDATE, stage, tick, null);
        this.status = status;
        this.accurate = accurate;
        this.inGameTicks = inGameTicks;
        this.gameTicksPrecise = gameTicksPrecise;
    }

    @Override
    protected String eventDataString() {
        return "status=" + status;
    }
}
