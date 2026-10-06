package com.toasupplies;

import com.google.common.collect.ImmutableMap;
import lombok.AllArgsConstructor;
import lombok.Getter;
import net.runelite.client.plugins.itemstats.potions.Ambrosia;
import net.runelite.client.plugins.itemstats.potions.AncientBrew;

import javax.annotation.Nullable;
import java.util.Map;

import static net.runelite.api.ItemID.*;

@AllArgsConstructor
@Getter
public enum RaidsPotions
{
    NECT1(NECTAR_4, NECTAR_1, 1),
    NECT2(NECTAR_4, NECTAR_2, 2),
    NECT3(NECTAR_4, NECTAR_3, 3),
    NECT4(NECTAR_4, NECTAR_4, 4),
    TEARS1(TEARS_OF_ELIDINIS_4,TEARS_OF_ELIDINIS_1,1),
    TEARS2(TEARS_OF_ELIDINIS_4,TEARS_OF_ELIDINIS_2,2),
    TEARS3(TEARS_OF_ELIDINIS_4,TEARS_OF_ELIDINIS_3,3),
    TEARS4(TEARS_OF_ELIDINIS_4,TEARS_OF_ELIDINIS_4,4),
    AMBRO1(AMBROSIA_2,AMBROSIA_1,1),
    AMBRO2(AMBROSIA_2,AMBROSIA_2,2),
    ;

    private final int potionType;
    private final int id;
    private final int charges;

    private static final Map<Integer, RaidsPotions> ID_MAP;

    static
    {
        ImmutableMap.Builder<Integer, RaidsPotions> builder = new ImmutableMap.Builder<>();

        for (RaidsPotions itemCharge : values())
        {
            builder.put(itemCharge.getId(), itemCharge);
        }

        ID_MAP = builder.build();
    }

    @Nullable
    static RaidsPotions findItem(int itemId)
    {
        return ID_MAP.get(itemId);
    }
}
