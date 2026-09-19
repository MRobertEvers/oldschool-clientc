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

import com.google.common.collect.ImmutableMap;
import io.blert.core.ChallengeMode;
import java.util.Arrays;
import java.util.Optional;
import lombok.Getter;
import net.runelite.api.gameval.NpcID;

/**
 * All known NPCs within the Theatre of Blood.
 */
public enum TobNpc {
    // Maiden boss.
    MAIDEN_ENTRY(10814, 6, ChallengeMode.TOB_ENTRY, 500),
    MAIDEN_REGULAR(8360, 6, ChallengeMode.TOB_REGULAR, new int[] {2625, 3062, 3500}),
    MAIDEN_HARD(10822, 6, ChallengeMode.TOB_HARD, new int[] {2625, 3062, 3500}),

    // Maiden red crab.
    MAIDEN_MATOMENOS_ENTRY(10820, 1, ChallengeMode.TOB_ENTRY, 16),
    MAIDEN_MATOMENOS_REGULAR(8366, 1, ChallengeMode.TOB_REGULAR, new int[] {75, 87, 100}),
    MAIDEN_MATOMENOS_HARD(10828, 1, ChallengeMode.TOB_HARD, new int[] {75, 87, 100}),

    // Maiden heat-seeking blood spawn.
    MAIDEN_BLOOD_SPAWN_ENTRY(10821, 1, ChallengeMode.TOB_ENTRY, 0),
    MAIDEN_BLOOD_SPAWN_REGULAR(8367, 1, ChallengeMode.TOB_REGULAR, new int[] {90, 105, 120}),
    MAIDEN_BLOOD_SPAWN_HARD(10829, 1, ChallengeMode.TOB_HARD, new int[] {90, 105, 120}),

    // Bloat.
    BLOAT_ENTRY(10812, 1, ChallengeMode.TOB_ENTRY, 320),
    BLOAT_REGULAR(8359, 1, ChallengeMode.TOB_REGULAR, new int[] {1500, 1750, 2000}),
    BLOAT_HARD(10813, 1, ChallengeMode.TOB_HARD, new int[] {1800, 2100, 2400}),

    // Melee small nylos.
    NYLOCAS_ISCHYROS_SMALL_ENTRY(10774, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_ISCHYROS_SMALL_REGULAR(8342, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_ISCHYROS_SMALL_HARD(10791, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),
    NYLOCAS_ISCHYROS_SMALL_AGGRO_ENTRY(10780, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_ISCHYROS_SMALL_AGRRO_REGULAR(8348, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_ISCHYROS_SMALL_AGGRO_HARD(10797, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),

    // Melee big nylos.
    NYLOCAS_ISCHYROS_BIG_ENTRY(10777, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_ISCHYROS_BIG_REGULAR(8345, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_ISCHYROS_BIG_HARD(10794, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),
    NYLOCAS_ISCHYROS_BIG_AGGRO_ENTRY(10783, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_ISCHYROS_BIG_AGGRO_REGULAR(8351, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_ISCHYROS_BIG_AGGRO_HARD(10800, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),

    // Range small nylos.
    NYLOCAS_TOXOBOLOS_SMALL_ENTRY(10775, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_TOXOBOLOS_SMALL_REGULAR(8343, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_TOXOBOLOS_SMALL_HARD(10792, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),
    NYLOCAS_TOXOBOLOS_SMALL_AGGRO_ENTRY(10781, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_TOXOBOLOS_SMALL_AGRRO_REGULAR(8349, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_TOXOBOLOS_SMALL_AGGRO_HARD(10798, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),

    // Range big nylos.
    NYLOCAS_TOXOBOLOS_BIG_ENTRY(10778, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_TOXOBOLOS_BIG_REGULAR(8346, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_TOXOBOLOS_BIG_HARD(10795, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),
    NYLOCAS_TOXOBOLOS_BIG_AGGRO_ENTRY(10784, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_TOXOBOLOS_BIG_AGGRO_REGULAR(8352, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_TOXOBOLOS_BIG_AGGRO_HARD(10801, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),

    // Mage small nylos.
    NYLOCAS_HAGIOS_SMALL_ENTRY(10776, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_HAGIOS_SMALL_REGULAR(8344, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_HAGIOS_SMALL_HARD(10793, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),
    NYLOCAS_HAGIOS_SMALL_AGGRO_ENTRY(10782, 1, ChallengeMode.TOB_ENTRY, 2),
    NYLOCAS_HAGIOS_SMALL_AGRRO_REGULAR(8350, 1, ChallengeMode.TOB_REGULAR, new int[] {8, 9, 11}),
    NYLOCAS_HAGIOS_SMALL_AGGRO_HARD(10799, 1, ChallengeMode.TOB_HARD, new int[] {8, 9, 11}),

    // Mage big nylos.
    NYLOCAS_HAGIOS_BIG_ENTRY(10779, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_HAGIOS_BIG_REGULAR(8347, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_HAGIOS_BIG_HARD(10796, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),
    NYLOCAS_HAGIOS_BIG_AGGRO_ENTRY(10785, 1, ChallengeMode.TOB_ENTRY, 3),
    NYLOCAS_HAGIOS_BIG_AGGRO_REGULAR(8353, 1, ChallengeMode.TOB_REGULAR, new int[] {16, 19, 22}),
    NYLOCAS_HAGIOS_BIG_AGGRO_HARD(10802, 1, ChallengeMode.TOB_HARD, new int[] {16, 19, 22}),

    // HMT nylo prince.
    NYLOCAS_PRINKIPAS(10803, 4, ChallengeMode.TOB_HARD, new int[] {300, 350, 400}),

    // Nylo king.
    NYLOCAS_VASILIAS_ENTRY(10787, 4, ChallengeMode.TOB_ENTRY, 360),
    NYLOCAS_VASILIAS_REGULAR(8354, 4, ChallengeMode.TOB_REGULAR, new int[] {1875, 2187, 2500}),
    NYLOCAS_VASILIAS_HARD(10807, 4, ChallengeMode.TOB_HARD, new int[] {1875, 2187, 2500}),

    // Inactive Sotetseg (prior to room entry or during maze).
    SOTETSEG_IDLE_ENTRY(10864, 1, ChallengeMode.TOB_ENTRY, 560),
    SOTETSEG_IDLE_REGULAR(8387, 1, ChallengeMode.TOB_REGULAR, new int[] {3000, 3500, 4000}),
    SOTETSEG_IDLE_HARD(10867, 1, ChallengeMode.TOB_HARD, new int[] {3000, 3500, 4000}),

    // Main Sotetseg boss.
    SOTETSEG_ENTRY(10865, 1, ChallengeMode.TOB_ENTRY, 560),
    SOTETSEG_REGULAR(8388, 1, ChallengeMode.TOB_REGULAR, new int[] {3000, 3500, 4000}),
    SOTETSEG_HARD(10868, 1, ChallengeMode.TOB_HARD, new int[] {3000, 3500, 4000}),

    // Inactive Xarpus prior to room entry.
    XARPUS_IDLE_ENTRY(10766, 1, ChallengeMode.TOB_ENTRY, 680),
    XARPUS_IDLE_REGULAR(8338, 1, ChallengeMode.TOB_REGULAR, new int[] {3810, 4445, 5080}),
    XARPUS_IDLE_HARD(10770, 1, ChallengeMode.TOB_HARD, new int[] {4500, 5250, 6000}),

    // Xarpus healing during P1 exhumes.
    XARPUS_P1_ENTRY(10767, 1, ChallengeMode.TOB_ENTRY, 680),
    XARPUS_P1_REGULAR(8339, 1, ChallengeMode.TOB_REGULAR, new int[] {3810, 4445, 5080}),
    XARPUS_P1_HARD(10771, 1, ChallengeMode.TOB_HARD, new int[] {4500, 5250, 6000}),

    // Xarpus main phase and post-screech.
    XARPUS_ENTRY(10768, 1, ChallengeMode.TOB_ENTRY, 680),
    XARPUS_REGULAR(8340, 1, ChallengeMode.TOB_REGULAR, new int[] {3810, 4445, 5080}),
    XARPUS_HARD(10772, 1, ChallengeMode.TOB_HARD, new int[] {4500, 5250, 6000}),

    // Verzik sitting at her throne prior to combat.
    VERZIK_IDLE_ENTRY(NpcID.VERZIK_INITIAL_STORY, 1, ChallengeMode.TOB_ENTRY, 240),
    VERZIK_IDLE_REGULAR(NpcID.VERZIK_INITIAL, 1, ChallengeMode.TOB_REGULAR, new int[] {1500, 1750, 2000}),
    VERZIK_IDLE_REGULAR_QS(NpcID.VERZIK_INITIAL_QUICKSTART, 1, ChallengeMode.TOB_REGULAR, new int[] {1500, 1750, 2000}),
    VERZIK_IDLE_HARD(NpcID.VERZIK_INITIAL_HARD, 1, ChallengeMode.TOB_HARD, new int[] {1500, 1750, 2000}),
    VERZIK_IDLE_HARD_QS(NpcID.VERZIK_INITIAL_HARD_QUICKSTART, 1, ChallengeMode.TOB_HARD, new int[] {1500, 1750, 2000}),

    // Verzik P1.
    VERZIK_P1_ENTRY(NpcID.VERZIK_PHASE1_STORY, 1, ChallengeMode.TOB_ENTRY, 240),
    VERZIK_P1_REGULAR(NpcID.VERZIK_PHASE1, 1, ChallengeMode.TOB_REGULAR, new int[] {1500, 1750, 2000}),
    VERZIK_P1_HARD(NpcID.VERZIK_PHASE1_HARD, 1, ChallengeMode.TOB_HARD, new int[] {1500, 1750, 2000}),

    VERZIK_P1_TRANSITION_ENTRY(NpcID.VERZIK_PHASE1_TO2_TRANSITION_STORY, 1, ChallengeMode.TOB_ENTRY, 0),
    VERZIK_P1_TRANSITION_REGULAR(NpcID.VERZIK_PHASE1_TO2_TRANSITION, 1, ChallengeMode.TOB_REGULAR, 0),
    VERZIK_P1_TRANSITION_HARD(NpcID.VERZIK_PHASE1_TO2_TRANSITION_HARD, 1, ChallengeMode.TOB_HARD, 0),

    // Verzik P2, including the transition to P3.
    VERZIK_P2_ENTRY(NpcID.VERZIK_PHASE2_STORY, 2, ChallengeMode.TOB_ENTRY, 320),
    VERZIK_P2_REGULAR(NpcID.VERZIK_PHASE2, 2, ChallengeMode.TOB_REGULAR, new int[] {2437, 2843, 3250}),
    VERZIK_P2_HARD(NpcID.VERZIK_PHASE2_HARD, 2, ChallengeMode.TOB_HARD, new int[] {2437, 2843, 3250}),

    // Verzik P3.
    VERZIK_P3_ENTRY(NpcID.VERZIK_PHASE3_STORY, 1, ChallengeMode.TOB_ENTRY, 320),
    VERZIK_P3_REGULAR(NpcID.VERZIK_PHASE3, 1, ChallengeMode.TOB_REGULAR, new int[] {2437, 2843, 3250}),
    VERZIK_P3_HARD(NpcID.VERZIK_PHASE3_HARD, 1, ChallengeMode.TOB_HARD, new int[] {2437, 2843, 3250}),

    // Verzik death animation.
    VERZIK_DEATH_ENTRY(NpcID.VERZIK_DEATH_BAT_STORY, 1, ChallengeMode.TOB_ENTRY, 0),
    VERZIK_DEATH_REGULAR(NpcID.VERZIK_DEATH_BAT, 1, ChallengeMode.TOB_REGULAR, 0),
    VERZIK_DEATH_HARD(NpcID.VERZIK_DEATH_BAT_HARD, 1, ChallengeMode.TOB_HARD, 0),

    // Pillars at Verzik.
    VERZIK_PILLAR(NpcID.VERZIK_PILLAR_NPC, 1, ChallengeMode.NO_MODE, 0),

    // Nylos at Verzik.
    // TODO(frolv): Correct HP values.
    VERZIK_NYLOCAS_ISCHYROS_ENTRY(10841, 1, ChallengeMode.TOB_ENTRY, 11),
    VERZIK_NYLOCAS_ISCHYROS_REGULAR(8381, 1, ChallengeMode.TOB_REGULAR, 11),
    VERZIK_NYLOCAS_ISCHYROS_HARD(10858, 1, ChallengeMode.TOB_HARD, 11),
    VERZIK_NYLOCAS_TOXOBOLOS_ENTRY(10842, 1, ChallengeMode.TOB_ENTRY, 11),
    VERZIK_NYLOCAS_TOXOBOLOS_REGULAR(8382, 1, ChallengeMode.TOB_REGULAR, 11),
    VERZIK_NYLOCAS_TOXOBOLOS_HARD(10859, 1, ChallengeMode.TOB_HARD, 11),
    VERZIK_NYLOCAS_HAGIOS_ENTRY(10843, 1, ChallengeMode.TOB_ENTRY, 11),
    VERZIK_NYLOCAS_HAGIOS_REGULAR(8383, 1, ChallengeMode.TOB_REGULAR, 11),
    VERZIK_NYLOCAS_HAGIOS_HARD(10860, 1, ChallengeMode.TOB_HARD, 11),

    // Purple crab at Verzik.
    VERZIK_ATHANATOS_ENTRY(10844, 1, ChallengeMode.TOB_ENTRY, 0),
    VERZIK_ATHANATOS_REGULAR(8384, 1, ChallengeMode.TOB_REGULAR, 0),
    VERZIK_ATHANATOS_HARD(10861, 1, ChallengeMode.TOB_HARD, 0),

    // Red crabs at Verzik.
    VERZIK_MATOMENOS_ENTRY(10845, 1, ChallengeMode.TOB_ENTRY, 0),
    VERZIK_MATOMENOS_REGULAR(8385, 1, ChallengeMode.TOB_REGULAR, new int[] {150, 175, 200}),
    VERZIK_MATOMENOS_HARD(10862, 1, ChallengeMode.TOB_HARD, new int[] {150, 175, 200}),

    // Tornadoes at Verzik.
    VERZIK_TORNADO_ENTRY(10846, 1, ChallengeMode.TOB_ENTRY, 0),
    VERZIK_TORNADO_REGULAR(8386, 1, ChallengeMode.TOB_REGULAR, 0),
    VERZIK_TORNADO_HARD(10863, 1, ChallengeMode.TOB_HARD, 0),
    ;

    @Getter
    private final int id;

    private final int idRange;

    @Getter
    private final ChallengeMode mode;

    private final int[] hitpointsByScale;

    private static final ImmutableMap<Integer, TobNpc> npcsById;

    static {
        ImmutableMap.Builder<Integer, TobNpc> builder = ImmutableMap.builder();

        for (TobNpc npc : TobNpc.values()) {
            for (int i = 0; i < npc.idRange; i++) {
                builder.put(npc.id + i, npc);
            }
        }

        npcsById = builder.build();
    }

    public static Optional<TobNpc> withId(int id) {
        return Optional.ofNullable(npcsById.get(id));
    }

    TobNpc(int id, int idRange, ChallengeMode mode, int[] hitpoints) {
        this.id = id;
        this.idRange = idRange;
        this.mode = mode;
        this.hitpointsByScale = hitpoints;
    }

    TobNpc(int id, int idRange, ChallengeMode mode, int hitpoints) {
        this.id = id;
        this.idRange = idRange;
        this.mode = mode;
        this.hitpointsByScale = new int[] {hitpoints, hitpoints, hitpoints};
    }

    public boolean hasId(int id) {
        return id >= this.id && id < this.id + this.idRange;
    }

    private static boolean idMatches(int id, TobNpc... npcs) {
        return Arrays.stream(npcs).anyMatch(npc -> npc.hasId(id));
    }

    public static boolean isMaiden(int id) {
        return idMatches(id, MAIDEN_ENTRY, MAIDEN_REGULAR, MAIDEN_HARD);
    }

    public static boolean isMaidenMatomenos(int id) {
        return idMatches(id, MAIDEN_MATOMENOS_ENTRY, MAIDEN_MATOMENOS_REGULAR, MAIDEN_MATOMENOS_HARD);
    }

    public static boolean isMaidenBloodSpawn(int id) {
        return idMatches(id, MAIDEN_BLOOD_SPAWN_ENTRY, MAIDEN_BLOOD_SPAWN_REGULAR, MAIDEN_BLOOD_SPAWN_HARD);
    }

    public static boolean isBloat(int id) {
        return idMatches(id, BLOAT_ENTRY, BLOAT_REGULAR, BLOAT_HARD);
    }

    public static boolean isNylocasIschyrosSmall(int id) {
        return idMatches(id, NYLOCAS_ISCHYROS_SMALL_ENTRY, NYLOCAS_ISCHYROS_SMALL_REGULAR, NYLOCAS_ISCHYROS_SMALL_HARD)
                || idMatches(
                        id,
                        NYLOCAS_ISCHYROS_SMALL_AGGRO_ENTRY,
                        NYLOCAS_ISCHYROS_SMALL_AGRRO_REGULAR,
                        NYLOCAS_ISCHYROS_SMALL_AGGRO_HARD);
    }

    public static boolean isNylocasIschyrosBig(int id) {
        return idMatches(id, NYLOCAS_ISCHYROS_BIG_ENTRY, NYLOCAS_ISCHYROS_BIG_REGULAR, NYLOCAS_ISCHYROS_BIG_HARD)
                || idMatches(
                        id,
                        NYLOCAS_ISCHYROS_BIG_AGGRO_ENTRY,
                        NYLOCAS_ISCHYROS_BIG_AGGRO_REGULAR,
                        NYLOCAS_ISCHYROS_BIG_AGGRO_HARD);
    }

    public static boolean isNylocasIschyros(int id) {
        return isNylocasIschyrosSmall(id) || isNylocasIschyrosBig(id);
    }

    public static boolean isNylocasToxobolosSmall(int id) {
        return idMatches(
                        id,
                        NYLOCAS_TOXOBOLOS_SMALL_ENTRY,
                        NYLOCAS_TOXOBOLOS_SMALL_REGULAR,
                        NYLOCAS_TOXOBOLOS_SMALL_HARD)
                || idMatches(
                        id,
                        NYLOCAS_TOXOBOLOS_SMALL_AGGRO_ENTRY,
                        NYLOCAS_TOXOBOLOS_SMALL_AGRRO_REGULAR,
                        NYLOCAS_TOXOBOLOS_SMALL_AGGRO_HARD);
    }

    public static boolean isNylocasToxobolosBig(int id) {
        return idMatches(id, NYLOCAS_TOXOBOLOS_BIG_ENTRY, NYLOCAS_TOXOBOLOS_BIG_REGULAR, NYLOCAS_TOXOBOLOS_BIG_HARD)
                || idMatches(
                        id,
                        NYLOCAS_TOXOBOLOS_BIG_AGGRO_ENTRY,
                        NYLOCAS_TOXOBOLOS_BIG_AGGRO_REGULAR,
                        NYLOCAS_TOXOBOLOS_BIG_AGGRO_HARD);
    }

    public static boolean isNylocasToxobolos(int id) {
        return isNylocasToxobolosSmall(id) || isNylocasToxobolosBig(id);
    }

    public static boolean isNylocasHagiosSmall(int id) {
        return idMatches(id, NYLOCAS_HAGIOS_SMALL_ENTRY, NYLOCAS_HAGIOS_SMALL_REGULAR, NYLOCAS_HAGIOS_SMALL_HARD)
                || idMatches(
                        id,
                        NYLOCAS_HAGIOS_SMALL_AGGRO_ENTRY,
                        NYLOCAS_HAGIOS_SMALL_AGRRO_REGULAR,
                        NYLOCAS_HAGIOS_SMALL_AGGRO_HARD);
    }

    public static boolean isNylocasHagiosBig(int id) {
        return idMatches(id, NYLOCAS_HAGIOS_BIG_ENTRY, NYLOCAS_HAGIOS_BIG_REGULAR, NYLOCAS_HAGIOS_BIG_HARD)
                || idMatches(
                        id,
                        NYLOCAS_HAGIOS_BIG_AGGRO_ENTRY,
                        NYLOCAS_HAGIOS_BIG_AGGRO_REGULAR,
                        NYLOCAS_HAGIOS_BIG_AGGRO_HARD);
    }

    public static boolean isNylocasHagios(int id) {
        return isNylocasHagiosSmall(id) || isNylocasHagiosBig(id);
    }

    public static boolean isNylocas(int id) {
        return isNylocasIschyros(id) || isNylocasToxobolos(id) || isNylocasHagios(id);
    }

    public static boolean isNylocasPrinkipas(int id) {
        return NYLOCAS_PRINKIPAS.hasId(id);
    }

    public static boolean isNylocasVasilias(int id) {
        return idMatches(id, NYLOCAS_VASILIAS_ENTRY, NYLOCAS_VASILIAS_REGULAR, NYLOCAS_VASILIAS_HARD);
    }

    public static boolean isDroppingNyloBoss(int id) {
        // The base ID of each Nylo boss is the NPC ID of it dropping from the ceiling, so do a strict check agsinst
        // those, ignoring the ID range.
        return id == NYLOCAS_PRINKIPAS.id
                || id == NYLOCAS_VASILIAS_ENTRY.id
                || id == NYLOCAS_VASILIAS_REGULAR.id
                || id == NYLOCAS_VASILIAS_HARD.id;
    }

    public static boolean isSotetsegIdle(int id) {
        return idMatches(id, SOTETSEG_IDLE_ENTRY, SOTETSEG_IDLE_REGULAR, SOTETSEG_IDLE_HARD);
    }

    public static boolean isSotetseg(int id) {
        return idMatches(id, SOTETSEG_ENTRY, SOTETSEG_REGULAR, SOTETSEG_HARD);
    }

    public static boolean isAnySotetseg(int id) {
        return isSotetsegIdle(id) || isSotetseg(id);
    }

    public static boolean isXarpusIdle(int id) {
        return idMatches(id, XARPUS_IDLE_ENTRY, XARPUS_IDLE_REGULAR, XARPUS_IDLE_HARD);
    }

    public boolean isXarpusIdle() {
        return isXarpusIdle(this.id);
    }

    public static boolean isXarpusP1(int id) {
        return idMatches(id, XARPUS_P1_ENTRY, XARPUS_P1_REGULAR, XARPUS_P1_HARD);
    }

    public boolean isXarpusP1() {
        return isXarpusP1(this.id);
    }

    public static boolean isXarpus(int id) {
        return idMatches(id, XARPUS_ENTRY, XARPUS_REGULAR, XARPUS_HARD);
    }

    public boolean isXarpus() {
        return isXarpus(this.id);
    }

    public static boolean isAnyXarpus(int id) {
        return isXarpusIdle(id) || isXarpusP1(id) || isXarpus(id);
    }

    public boolean isAnyXarpus() {
        return isXarpusIdle() || isXarpusP1() || isXarpus();
    }

    public static boolean isVerzikIdle(int id) {
        return idMatches(
                id,
                VERZIK_IDLE_ENTRY,
                VERZIK_IDLE_REGULAR,
                VERZIK_IDLE_REGULAR_QS,
                VERZIK_IDLE_HARD,
                VERZIK_IDLE_HARD_QS);
    }

    public boolean isVerzikIdle() {
        return isVerzikIdle(this.id);
    }

    public static boolean isVerzikP1(int id) {
        return idMatches(id, VERZIK_P1_ENTRY, VERZIK_P1_REGULAR, VERZIK_P1_HARD);
    }

    public boolean isVerzikP1() {
        return isVerzikP1(this.id);
    }

    public static boolean isVerzikP1Transition(int id) {
        return idMatches(id, VERZIK_P1_TRANSITION_ENTRY, VERZIK_P1_TRANSITION_REGULAR, VERZIK_P1_TRANSITION_HARD);
    }

    public boolean isVerzikP1Transition() {
        return isVerzikP1Transition(this.id);
    }

    public static boolean isVerzikP2(int id) {
        return idMatches(id, VERZIK_P2_ENTRY, VERZIK_P2_REGULAR, VERZIK_P2_HARD);
    }

    public boolean isVerzikP2() {
        return isVerzikP2(this.id);
    }

    public static boolean isVerzikP3(int id) {
        return idMatches(id, VERZIK_P3_ENTRY, VERZIK_P3_REGULAR, VERZIK_P3_HARD);
    }

    public boolean isVerzikP3() {
        return isVerzikP3(this.id);
    }

    public static boolean isVerzikDeath(int id) {
        return idMatches(id, VERZIK_DEATH_ENTRY, VERZIK_DEATH_REGULAR, VERZIK_DEATH_HARD);
    }

    public boolean isVerzikDeath() {
        return isVerzikDeath(this.id);
    }

    public static boolean isAnyVerzik(int id) {
        return isVerzikIdle(id)
                || isVerzikP1(id)
                || isVerzikP1Transition(id)
                || isVerzikP2(id)
                || isVerzikP3(id)
                || isVerzikDeath(id);
    }

    public boolean isAnyVerzik() {
        return isAnyVerzik(this.id);
    }

    public static boolean isVerzikIschyros(int id) {
        return idMatches(
                id, VERZIK_NYLOCAS_ISCHYROS_ENTRY, VERZIK_NYLOCAS_ISCHYROS_REGULAR, VERZIK_NYLOCAS_ISCHYROS_HARD);
    }

    public static boolean isVerzikToxobolos(int id) {
        return idMatches(
                id, VERZIK_NYLOCAS_TOXOBOLOS_ENTRY, VERZIK_NYLOCAS_TOXOBOLOS_REGULAR, VERZIK_NYLOCAS_TOXOBOLOS_HARD);
    }

    public static boolean isVerzikHagios(int id) {
        return idMatches(id, VERZIK_NYLOCAS_HAGIOS_ENTRY, VERZIK_NYLOCAS_HAGIOS_REGULAR, VERZIK_NYLOCAS_HAGIOS_HARD);
    }

    public static boolean isVerzikCrab(int id) {
        return isVerzikIschyros(id) || isVerzikToxobolos(id) || isVerzikHagios(id);
    }

    public boolean isVerzikCrab() {
        return isVerzikCrab(this.id);
    }

    public static boolean isVerzikAthanatos(int id) {
        return idMatches(id, VERZIK_ATHANATOS_ENTRY, VERZIK_ATHANATOS_REGULAR, VERZIK_ATHANATOS_HARD);
    }

    public boolean isVerzikAthanatos() {
        return isVerzikAthanatos(this.id);
    }

    public static boolean isVerzikMatomenos(int id) {
        return idMatches(id, VERZIK_MATOMENOS_ENTRY, VERZIK_MATOMENOS_REGULAR, VERZIK_MATOMENOS_HARD);
    }

    public boolean isVerzikMatomenos() {
        return isVerzikMatomenos(this.id);
    }

    public static boolean isVerzikTornado(int id) {
        return idMatches(id, VERZIK_TORNADO_ENTRY, VERZIK_TORNADO_REGULAR, VERZIK_TORNADO_HARD);
    }

    public boolean isVerzikTornado() {
        return isVerzikTornado(this.id);
    }

    public int getBaseHitpoints(int scale) {
        if (scale < 1 || scale > 5) {
            return -1;
        }

        if (mode == ChallengeMode.TOB_ENTRY) {
            if (isNylocas(this.id)) {
                return hitpointsByScale[0];
            }
            // Entry mode scales hitpoints linearly with party size.
            return hitpointsByScale[0] * scale;
        }

        switch (scale) {
            case 5:
                return hitpointsByScale[2];
            case 4:
                return hitpointsByScale[1];
            default:
                return hitpointsByScale[0];
        }
    }
}
