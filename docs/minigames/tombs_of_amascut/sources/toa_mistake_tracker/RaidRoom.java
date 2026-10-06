package com.toamistaketracker;

import lombok.Getter;
import lombok.RequiredArgsConstructor;

@RequiredArgsConstructor
public enum RaidRoom {

    // limbo 13392
    RAID_LOBBY_OUTSIDE(13454),
    RAID_LOBBY_INSIDE(14160),
    HET_PUZZLE(14674),
    CRONDIS_PUZZLE(15698),
    SCABARAS_PUZZLE(14162),
    APMEKEN_PUZZLE(15186),
    AKKHA(14676),
    ZEBAK(15700),
    KEPHRI(14164),
    BABA(15188),
    WARDENS_P1_P2(15184),
    WARDENS_P3(15696),
    ;

    @Getter
    private final int regionId;

    public static RaidRoom forRegionId(int region) {
        for (RaidRoom r : RaidRoom.values()) {
            if (r.getRegionId() == region) {
                return r;
            }
        }

        return null;
    }
}
