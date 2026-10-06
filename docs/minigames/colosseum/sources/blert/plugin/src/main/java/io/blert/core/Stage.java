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

import java.util.HashMap;
import java.util.Map;
import javax.annotation.Nullable;
import lombok.Getter;

public enum Stage {
    TOB_MAIDEN(10),
    TOB_BLOAT(11),
    TOB_NYLOCAS(12),
    TOB_SOTETSEG(13),
    TOB_XARPUS(14),
    TOB_VERZIK(15),

    COX_TEKTON(20),
    COX_CRABS(21),
    COX_ICE_DEMON(22),
    COX_SHAMANS(23),
    COX_VANGUARDS(24),
    COX_THIEVING(25),
    COX_VESPULA(26),
    COX_TIGHTROPE(27),
    COX_GUARDIANS(28),
    COX_VASA(29),
    COX_MYSTICS(30),
    COX_MUTTADILE(31),
    COX_OLM(32),

    TOA_APMEKEN(40),
    TOA_BABA(41),
    TOA_SCABARAS(42),
    TOA_KEPHRI(43),
    TOA_HET(44),
    TOA_AKKHA(45),
    TOA_CRONDIS(46),
    TOA_ZEBAK(47),
    TOA_WARDENS(48),

    MOKHAIOTL_DELVE_1(50),
    MOKHAIOTL_DELVE_2(51),
    MOKHAIOTL_DELVE_3(52),
    MOKHAIOTL_DELVE_4(53),
    MOKHAIOTL_DELVE_5(54),
    MOKHAIOTL_DELVE_6(55),
    MOKHAIOTL_DELVE_7(56),
    MOKHAIOTL_DELVE_8(57),
    MOKHAIOTL_DELVE_8PLUS(58),

    COLOSSEUM_WAVE_1(100),
    COLOSSEUM_WAVE_2(101),
    COLOSSEUM_WAVE_3(102),
    COLOSSEUM_WAVE_4(103),
    COLOSSEUM_WAVE_5(104),
    COLOSSEUM_WAVE_6(105),
    COLOSSEUM_WAVE_7(106),
    COLOSSEUM_WAVE_8(107),
    COLOSSEUM_WAVE_9(108),
    COLOSSEUM_WAVE_10(109),
    COLOSSEUM_WAVE_11(110),
    COLOSSEUM_WAVE_12(111),

    INFERNO_WAVE_1(200),
    INFERNO_WAVE_2(201),
    INFERNO_WAVE_3(202),
    INFERNO_WAVE_4(203),
    INFERNO_WAVE_5(204),
    INFERNO_WAVE_6(205),
    INFERNO_WAVE_7(206),
    INFERNO_WAVE_8(207),
    INFERNO_WAVE_9(208),
    INFERNO_WAVE_10(209),
    INFERNO_WAVE_11(210),
    INFERNO_WAVE_12(211),
    INFERNO_WAVE_13(212),
    INFERNO_WAVE_14(213),
    INFERNO_WAVE_15(214),
    INFERNO_WAVE_16(215),
    INFERNO_WAVE_17(216),
    INFERNO_WAVE_18(217),
    INFERNO_WAVE_19(218),
    INFERNO_WAVE_20(219),
    INFERNO_WAVE_21(220),
    INFERNO_WAVE_22(221),
    INFERNO_WAVE_23(222),
    INFERNO_WAVE_24(223),
    INFERNO_WAVE_25(224),
    INFERNO_WAVE_26(225),
    INFERNO_WAVE_27(226),
    INFERNO_WAVE_28(227),
    INFERNO_WAVE_29(228),
    INFERNO_WAVE_30(229),
    INFERNO_WAVE_31(230),
    INFERNO_WAVE_32(231),
    INFERNO_WAVE_33(232),
    INFERNO_WAVE_34(233),
    INFERNO_WAVE_35(234),
    INFERNO_WAVE_36(235),
    INFERNO_WAVE_37(236),
    INFERNO_WAVE_38(237),
    INFERNO_WAVE_39(238),
    INFERNO_WAVE_40(239),
    INFERNO_WAVE_41(240),
    INFERNO_WAVE_42(241),
    INFERNO_WAVE_43(242),
    INFERNO_WAVE_44(243),
    INFERNO_WAVE_45(244),
    INFERNO_WAVE_46(245),
    INFERNO_WAVE_47(246),
    INFERNO_WAVE_48(247),
    INFERNO_WAVE_49(248),
    INFERNO_WAVE_50(249),
    INFERNO_WAVE_51(250),
    INFERNO_WAVE_52(251),
    INFERNO_WAVE_53(252),
    INFERNO_WAVE_54(253),
    INFERNO_WAVE_55(254),
    INFERNO_WAVE_56(255),
    INFERNO_WAVE_57(256),
    INFERNO_WAVE_58(257),
    INFERNO_WAVE_59(258),
    INFERNO_WAVE_60(259),
    INFERNO_WAVE_61(260),
    INFERNO_WAVE_62(261),
    INFERNO_WAVE_63(262),
    INFERNO_WAVE_64(263),
    INFERNO_WAVE_65(264),
    INFERNO_WAVE_66(265),
    INFERNO_WAVE_67(266),
    INFERNO_WAVE_68(267),
    INFERNO_WAVE_69(268),
    ;

    @Getter
    private final int id;

    private static final Map<Integer, Stage> ID_MAP = new HashMap<>();

    static {
        for (Stage stage : values()) {
            ID_MAP.put(stage.id, stage);
        }
    }

    Stage(int id) {
        this.id = id;
    }

    /**
     * Retrieves the Stage enum constant associated with a specific ID.
     * @param stageId The integer ID to look up.
     * @return The matching Stage, or null if no match is found.
     */
    @Nullable
    public static Stage fromId(int stageId) {
        return ID_MAP.get(stageId);
    }
}
