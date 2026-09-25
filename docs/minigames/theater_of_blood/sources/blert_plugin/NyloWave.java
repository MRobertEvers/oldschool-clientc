/*
 * Copyright (c) 2026 Alexei Frolov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

package io.blert.challenges.tob.rooms.nylocas;

import com.google.common.collect.ImmutableMap;
import io.blert.challenges.tob.TobNpc;
import java.util.Arrays;
import java.util.List;
import java.util.OptionalInt;
import net.runelite.api.coords.WorldPoint;

/**
 * Identifies Nylocas waves by their unique spawn composition.
 * <p>
 * Each wave's lane spawns are encoded as a 6-character string with positions:
 * east0, east1, south0, south1, west0, west1.
 * <p>
 * Characters represent NPC type:
 * <pre>
 *   i/I = Ischyros small/big
 *   t/T = Toxobolos small/big
 *   h/H = Hagios small/big
 *   _   = empty slot
 * </pre>
 * <p>
 * Three wave pairs are ambiguous (identical composition at spawn time because aggro
 * status is not yet assigned): 17/19, 26/27, 28/31. These are disambiguated using
 * sequential context when available, or skipped if joining mid-fight.
 */
class NyloWave {
    private static final char EMPTY = '_';

    private static final ImmutableMap<String, int[]> WAVE_CANDIDATES = new ImmutableMap.Builder<String, int[]>()
            .put("_i_ht_", new int[] {1})
            .put("t_i__h", new int[] {2})
            .put("_h_ti_", new int[] {3})
            .put("i_H__t", new int[] {4})
            .put("_h_iT_", new int[] {5})
            .put("I_t__h", new int[] {6})
            .put("_iTh__", new int[] {7})
            .put("t_i_H_", new int[] {8})
            .put("_h__T_", new int[] {9})
            .put("Tttttt", new int[] {10})
            .put("hhhhH_", new int[] {11})
            .put("iiI_ii", new int[] {12})
            .put("I_itht", new int[] {13})
            .put("T_thih", new int[] {14})
            .put("thH_ti", new int[] {15})
            .put("h_i__t", new int[] {16})
            .put("H_H_H_", new int[] {17, 19})
            .put("T_T_T_", new int[] {18})
            .put("I_H_I_", new int[] {20})
            .put("ttiihh", new int[] {21})
            .put("H_htI_", new int[] {22})
            .put("H_T_th", new int[] {23})
            .put("I_H_T_", new int[] {24})
            .put("H_T_I_", new int[] {25})
            .put("H_I_H_", new int[] {26, 27})
            .put("thhiti", new int[] {28, 31})
            .put("thI_ti", new int[] {29})
            .put("H_hiT_", new int[] {30})
            .build();

    /**
     * Identifies a wave by its lane spawn composition.
     *
     * @param spawns      All lane spawns for a single tick.
     * @param currentWave The last identified wave number (0 if none yet).
     * @return The wave number, or empty if unknown or ambiguous without context.
     */
    static OptionalInt identifyWave(List<Nylo> spawns, int currentWave) {
        String key = encodeSpawn(spawns);
        int[] candidates = WAVE_CANDIDATES.get(key);

        if (candidates == null) {
            return OptionalInt.empty();
        }
        if (candidates.length == 1) {
            return OptionalInt.of(candidates[0]);
        }

        // Ambiguous: use sequential context if available, otherwise skip.
        if (currentWave == 0) {
            return OptionalInt.empty();
        }
        return Arrays.stream(candidates).filter(wave -> wave > currentWave).findFirst();
    }

    /**
     * Encodes a Nylo NPC to a character.
     *
     * @param npcId NPC ID at spawn time.
     * @return Character encoding the NPC's style and size.
     */
    static char encodeNylo(int npcId) {
        if (TobNpc.isNylocasIschyrosSmall(npcId)) return 'i';
        if (TobNpc.isNylocasIschyrosBig(npcId)) return 'I';
        if (TobNpc.isNylocasToxobolosSmall(npcId)) return 't';
        if (TobNpc.isNylocasToxobolosBig(npcId)) return 'T';
        if (TobNpc.isNylocasHagiosSmall(npcId)) return 'h';
        if (TobNpc.isNylocasHagiosBig(npcId)) return 'H';
        return EMPTY;
    }

    /**
     * Encodes a wave spawn from a list of lane-spawned nylos into a key string.
     *
     * @param spawns All lane spawns for a single tick.
     * @return 6-character wave key.
     */
    static String encodeSpawn(List<Nylo> spawns) {
        char[] key = {EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY};

        for (Nylo nylo : spawns) {
            int index = laneIndex(nylo);
            if (index >= 0 && index < key.length) {
                key[index] = encodeNylo(nylo.getNpc().getId());
            }
        }

        return new String(key);
    }

    private static int laneIndex(Nylo nylo) {
        WorldPoint spawn = nylo.getSpawnPoint();
        switch (nylo.getSpawnType()) {
            case EAST:
                return spawn.equals(SpawnType.EAST_LANE_SOUTH) || spawn.equals(SpawnType.EAST_LANE_SOUTHWEST) ? 0 : 1;
            case SOUTH:
                return spawn.equals(SpawnType.SOUTH_LANE_WEST) ? 2 : 3;
            case WEST:
                return spawn.equals(SpawnType.WEST_LANE_SOUTH) ? 4 : 5;
            default:
                return -1;
        }
    }
}
