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

import lombok.NonNull;

/**
 * Delays execution of a task by a specified number of ticks.
 * <p>
 * Progression towards execution is made by calling the {@link DeferredTask#tick()} method,
 * typically from some {@code onTick} handler.
 */
public class DeferredTask {
    private final @NonNull Runnable task;
    private int ticks;

    public DeferredTask(@NonNull Runnable task, int ticks) {
        this.task = task;
        this.ticks = ticks;
    }

    /**
     * Decrements the number of ticks remaining before the task is executed.
     */
    public void tick() {
        ticks = Math.max(-1, ticks - 1);

        if (ticks == 0) {
            task.run();
            ticks = -1;
        }
    }

    /**
     * Cancels the task, preventing it from being executed.
     */
    public void cancel() {
        ticks = -1;
    }

    /**
     * Resets the number of ticks remaining before the task is executed.
     *
     * @param ticks Updated number of ticks.
     */
    public void reset(int ticks) {
        this.ticks = Math.max(-1, ticks);
    }
}
