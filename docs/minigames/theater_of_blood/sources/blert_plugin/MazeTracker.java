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

package io.blert.challenges.tob.rooms.sotetseg;

import com.google.common.collect.ImmutableList;
import java.util.*;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import net.runelite.api.coords.WorldPoint;
import org.apache.commons.lang3.tuple.Pair;

@Getter
@Slf4j
public class MazeTracker {
    /**
     * An infill path is an assumed path between two points in a Sotetseg maze.
     * <p>
     * Players can move diagonally and up to two tiles at a time, but the maze only runs in cardinal directions and
     * includes every tile. Therefore, the infill path is a series of steps that a player would take to move between
     * two points if they were constrained to the maze's grid.
     * <p>
     * An infill path is defined by the following three properties:
     * <ul>
     *     <li>{@code dx}: The horizontal distance between the start and end point, in the range {@code [-2, 2]}.</li>
     *     <li>{@code dy}: The vertical distance between the start and end point, in the range {@code [0, 2]}.
     *     (The maze only moves upwards.)</li>
     *     <li>{@code path}: A list of coordinates defining the path between the start and end points. The first
     *     coordinate is always {@code (0, 0)}, and the last coordinate is always {@code (dx, dy)}.</li>
     * </ul>
     */
    private static class InfillPath {
        private class PathIterator implements Iterator<WorldPoint> {
            private final WorldPoint origin;
            private int index = 0;

            PathIterator(WorldPoint origin) {
                this.origin = origin;
            }

            public boolean hasNext() {
                return index < path.length;
            }

            public WorldPoint next() {
                if (!hasNext()) {
                    throw new NoSuchElementException();
                }

                final Pair<Integer, Integer> step = path[index++];
                return new WorldPoint(
                        origin.getX() + step.getLeft(), origin.getY() + step.getRight(), origin.getPlane());
            }
        }

        private final int dx;
        private final int dy;
        private final Pair<Integer, Integer>[] path;

        @SafeVarargs
        InfillPath(int dx, int dy, Pair<Integer, Integer>... path) {
            this.dx = dx;
            this.dy = dy;
            this.path = path;
        }

        public Iterator<WorldPoint> pathFrom(WorldPoint origin) {
            return new PathIterator(origin);
        }
    }

    // Infill paths starting from points on non-pivot rows.
    private static final ImmutableList<InfillPath> NON_PIVOT_INFILLS = ImmutableList.of(
            // Two steps left on a non-pivot row.
            //   o o o
            //   o o o
            //   E x S
            new InfillPath(-2, 0, Pair.of(0, 0), Pair.of(-1, 0), Pair.of(-2, 0)),
            // Single step left on a non-pivot row.
            //   o o o
            //   o o o
            //   o E S
            new InfillPath(-1, 0, Pair.of(0, 0), Pair.of(-1, 0)),
            // Single step right on a non-pivot row.
            //   o o o
            //   o o o
            //   S E o
            new InfillPath(1, 0, Pair.of(0, 0), Pair.of(1, 0)),
            // Two steps right on a non-pivot row.
            //   o o o
            //   o o o
            //   S x E
            new InfillPath(2, 0, Pair.of(0, 0), Pair.of(1, 0), Pair.of(2, 0)),
            // Two steps left and one step up on a non-pivot row (L-shape).
            //   o o o
            //   E o o
            //   x x S
            new InfillPath(-2, 1, Pair.of(0, 0), Pair.of(-1, 0), Pair.of(-2, 0), Pair.of(-2, 1)),
            // One step left and one step up on a non-pivot row (diagonal).
            //   o o o
            //   o E o
            //   o x S
            new InfillPath(-1, 1, Pair.of(0, 0), Pair.of(-1, 0), Pair.of(-1, 1)),
            // One step up on a non-pivot row.
            //   o o o
            //   o E o
            //   o S o
            new InfillPath(0, 1, Pair.of(0, 0), Pair.of(0, 1)),
            // One step right and one step up on a non-pivot row (diagonal).
            //   o o o
            //   o E o
            //   S x o
            new InfillPath(1, 1, Pair.of(0, 0), Pair.of(1, 0), Pair.of(1, 1)),
            // Two steps right and one step up on a non-pivot row (L-shape).
            //   o o o
            //   o o E
            //   S x x
            new InfillPath(2, 1, Pair.of(0, 0), Pair.of(1, 0), Pair.of(2, 0), Pair.of(2, 1)),
            // Two steps left and two steps up on a non-pivot row (diagonal).
            //   E x o
            //   o x o
            //   o x S
            new InfillPath(-2, 2, Pair.of(0, 0), Pair.of(-1, 0), Pair.of(-1, 1), Pair.of(-1, 2), Pair.of(-2, 2)),
            // One step left and two steps up on a non-pivot row (L-shape).
            //   E x o
            //   o x o
            //   o S o
            new InfillPath(-1, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(0, 2), Pair.of(-1, 2)),
            // Two steps up on a non-pivot row.
            //   o E o
            //   o x o
            //   o S o
            new InfillPath(0, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(0, 2)),
            // One step right and two steps up on a non-pivot row (L-shape).
            //   o x E
            //   o x o
            //   o S o
            new InfillPath(1, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(0, 2), Pair.of(1, 2)),
            // Two steps right and two steps up on a non-pivot row (diagonal).
            //   o x E
            //   o x o
            //   S x o
            new InfillPath(2, 2, Pair.of(0, 0), Pair.of(1, 0), Pair.of(1, 1), Pair.of(1, 2), Pair.of(2, 2)));

    // Infill paths starting from points on pivot rows. Pivot rows only contain a single point, so the infill paths
    // cannot move horizontally on rows 0 or 2.
    private static final ImmutableList<InfillPath> PIVOT_INFILLS = ImmutableList.of(
            // Two steps left and one step up on a pivot row (incorrect L-shape).
            //   o o o
            //   E x x
            //   o o S
            new InfillPath(-2, 1, Pair.of(0, 0), Pair.of(0, 1), Pair.of(-1, 1), Pair.of(-2, 1)),
            // One step left and one step up on a pivot row (diagonal).
            //   o o o
            //   o E x
            //   o o S
            new InfillPath(-1, 1, Pair.of(0, 0), Pair.of(0, 1), Pair.of(-1, 1)),
            // One step up on a pivot row.
            //   o o o
            //   o E o
            //   o S o
            new InfillPath(0, 1, Pair.of(0, 0), Pair.of(0, 1)),
            // One step right and one step up on a pivot row (diagonal).
            //   o o o
            //   x E o
            //   S o o
            new InfillPath(1, 1, Pair.of(0, 0), Pair.of(0, 1), Pair.of(1, 1)),
            // Two steps right and one step up on a pivot row (incorrect L-shape).
            //   o o o
            //   x x E
            //   S o o
            new InfillPath(2, 1, Pair.of(0, 0), Pair.of(0, 1), Pair.of(1, 1), Pair.of(2, 1)),
            // Two steps left and two steps up on a pivot row (diagonal).
            //   E o o
            //   x x x
            //   o o S
            new InfillPath(-2, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(-1, 1), Pair.of(-2, 1), Pair.of(-2, 2)),
            // One step left and two steps up on a pivot row (L-shape).
            //   o E o
            //   o x x
            //   o o S
            new InfillPath(-1, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(-1, 1), Pair.of(-1, 2)),
            // Two steps up on a pivot row.
            //   o E o
            //   o x o
            //   o S o
            new InfillPath(0, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(0, 2)),
            // One step right and two steps up on a pivot row (L-shape).
            //   o E o
            //   x x o
            //   S o o
            new InfillPath(1, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(1, 1), Pair.of(1, 2)),
            // Two steps right and two steps up on a pivot row (diagonal).
            //   o o E
            //   x x x
            //   S o o
            new InfillPath(2, 2, Pair.of(0, 0), Pair.of(0, 1), Pair.of(1, 1), Pair.of(2, 1), Pair.of(2, 2)));

    private final List<WorldPoint> overworldPoints = new ArrayList<>();
    private final List<WorldPoint> underworldPivots = new ArrayList<>();
    private final Set<WorldPoint> raggedPoints = new HashSet<>();

    public MazeTracker() {}

    public void addPotentialOverworldPoint(WorldPoint point) {
        if (overworldPoints.contains(point)) {
            return;
        }

        if (overworldPoints.isEmpty()) {
            overworldPoints.add(point);
            return;
        }

        WorldPoint lastPoint = overworldPoints.get(overworldPoints.size() - 1);
        int dx = point.getX() - lastPoint.getX();
        int dy = point.getY() - lastPoint.getY();
        boolean isPivot = (lastPoint.getY() - Maze.OVERWORLD_MAZE_START.getY()) % 2 == 0;

        InfillPath infillPath = null;
        if (isPivot) {
            for (InfillPath infill : PIVOT_INFILLS) {
                if (infill.dx == dx && infill.dy == dy) {
                    infillPath = infill;
                    break;
                }
            }
        } else {
            for (InfillPath infill : NON_PIVOT_INFILLS) {
                if (infill.dx == dx && infill.dy == dy) {
                    infillPath = infill;
                    break;
                }
            }
        }

        if (infillPath == null) {
            log.error("No infill path for ({}, {}) pivot={}", dx, dy, isPivot);
            return;
        }

        for (Iterator<WorldPoint> it = infillPath.pathFrom(lastPoint); it.hasNext(); ) {
            WorldPoint nextPoint = it.next();
            if (!overworldPoints.contains(nextPoint) && !raggedPoints.contains(nextPoint)) {
                overworldPoints.add(nextPoint);
            }
        }
    }

    public void removeOverworldPoint(WorldPoint point) {
        raggedPoints.add(point);
    }

    public void finishMaze() {
        // If the last recorded point is on the penultimate row, assume the final pivot has the same x coordinate.
        if (!overworldPoints.isEmpty()) {
            final WorldPoint lastPoint = overworldPoints.get(overworldPoints.size() - 1);
            final int penultimateRow = Maze.OVERWORLD_MAZE_START.getY() + Maze.HEIGHT - 2;
            if (lastPoint.getY() == penultimateRow) {
                overworldPoints.add(new WorldPoint(lastPoint.getX(), penultimateRow + 1, lastPoint.getPlane()));
            }
        }

        overworldPoints.removeAll(raggedPoints);
        overworldPoints.removeIf(point -> (point.getY() - Maze.OVERWORLD_MAZE_START.getY()) % 2 == 1);
    }

    public void addUnderworldPoint(WorldPoint point) {
        int y = point.getY() - Maze.UNDERWORLD_MAZE_START.getY();
        if (y % 2 == 1) {
            // Only even rows are pivots.
            return;
        }

        if (!underworldPivots.contains(point)) {
            underworldPivots.add(point);
        }
    }

    public boolean hasOverworldPivots() {
        return !overworldPoints.isEmpty();
    }

    public boolean hasUnderworldPivots() {
        return !underworldPivots.isEmpty();
    }

    public int[] getPivots() {
        List<WorldPoint> pivots = hasUnderworldPivots() ? underworldPivots : overworldPoints;
        return pivots.stream().mapToInt(WorldPoint::getX).toArray();
    }

    public void reset() {
        overworldPoints.clear();
        underworldPivots.clear();
        raggedPoints.clear();
    }
}
