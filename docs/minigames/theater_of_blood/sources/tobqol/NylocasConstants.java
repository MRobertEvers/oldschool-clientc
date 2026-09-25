/*
 * Copyright (c) 2022, Damen <gh: damencs>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:

 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.

 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.

 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package com.tobqol.rooms.nylocas.commons;

import com.google.common.collect.*;
import com.tobqol.api.game.Instance;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.experimental.Accessors;
import net.runelite.api.NpcID;
import net.runelite.api.NullNpcID;
import net.runelite.api.Point;

import javax.annotation.Nullable;
import java.awt.*;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;

import static com.google.common.collect.Tables.immutableCell;

@RequiredArgsConstructor
@Getter
@Accessors(fluent = true)
public enum NylocasConstants
{
	BOSS_DROPPING_MELEE(NpcID.NYLOCAS_VASILIAS_10786, NpcID.NYLOCAS_VASILIAS, NpcID.NYLOCAS_VASILIAS_10807),
	BOSS_MELEE(NpcID.NYLOCAS_VASILIAS_10787, NpcID.NYLOCAS_VASILIAS_8355, NpcID.NYLOCAS_VASILIAS_10808),
	BOSS_MAGIC(NpcID.NYLOCAS_VASILIAS_10788, NpcID.NYLOCAS_VASILIAS_8356, NpcID.NYLOCAS_VASILIAS_10809),
	BOSS_RANGE(NpcID.NYLOCAS_VASILIAS_10789, NpcID.NYLOCAS_VASILIAS_8357, NpcID.NYLOCAS_VASILIAS_10810),
	DEMI_BOSS_DROPPING_MELEE(NpcID.NYLOCAS_PRINKIPAS),
	DEMI_BOSS_MELEE(NpcID.NYLOCAS_PRINKIPAS_10804),
	DEMI_BOSS_MAGIC(NpcID.NYLOCAS_PRINKIPAS_10805),
	DEMI_BOSS_RANGE(NpcID.NYLOCAS_PRINKIPAS_10806),
	MELEE_SMALL(
			NpcID.NYLOCAS_ISCHYROS_10791,
			NpcID.NYLOCAS_ISCHYROS_8342,
			NpcID.NYLOCAS_ISCHYROS_10791,
			NpcID.NYLOCAS_ISCHYROS_10780,
			NpcID.NYLOCAS_ISCHYROS_8348,
			NpcID.NYLOCAS_ISCHYROS_10797
	),
	RANGE_SMALL(
			NpcID.NYLOCAS_TOXOBOLOS_10775,
			NpcID.NYLOCAS_TOXOBOLOS_8343,
			NpcID.NYLOCAS_TOXOBOLOS_10792,
			NpcID.NYLOCAS_TOXOBOLOS_10781,
			NpcID.NYLOCAS_TOXOBOLOS_8349,
			NpcID.NYLOCAS_TOXOBOLOS_10798
	),
	MAGIC_SMALL(
			NpcID.NYLOCAS_HAGIOS_10776,
			NpcID.NYLOCAS_HAGIOS,
			NpcID.NYLOCAS_HAGIOS_10793,
			NpcID.NYLOCAS_HAGIOS_10782,
			NpcID.NYLOCAS_HAGIOS_8350,
			NpcID.NYLOCAS_HAGIOS_10799
	),
	MELEE_BIG(
			NpcID.NYLOCAS_ISCHYROS_10777,
			NpcID.NYLOCAS_ISCHYROS_8345,
			NpcID.NYLOCAS_ISCHYROS_10794,
			NpcID.NYLOCAS_ISCHYROS_10783,
			NpcID.NYLOCAS_ISCHYROS_8351,
			NpcID.NYLOCAS_ISCHYROS_10800
	),
	RANGE_BIG(
			NpcID.NYLOCAS_TOXOBOLOS_10778,
			NpcID.NYLOCAS_TOXOBOLOS_8346,
			NpcID.NYLOCAS_TOXOBOLOS_10795,
			NpcID.NYLOCAS_TOXOBOLOS_10784,
			NpcID.NYLOCAS_TOXOBOLOS_8352,
			NpcID.NYLOCAS_TOXOBOLOS_10801
	),
	MAGIC_BIG(
			NpcID.NYLOCAS_HAGIOS_10779,
			NpcID.NYLOCAS_HAGIOS_8347,
			NpcID.NYLOCAS_HAGIOS_10796,
			NpcID.NYLOCAS_HAGIOS_10785,
			NpcID.NYLOCAS_HAGIOS_8353,
			NpcID.NYLOCAS_HAGIOS_10802
	),
	PILLAR(NullNpcID.NULL_10790, NullNpcID.NULL_8358, NullNpcID.NULL_10811);

	private final int sm, rg, hm, aggro_sm, aggro_rg, aggro_hm;

	public static int BOSS_IMAGE = 25750;
	public static int NYLOCAS_WAVES_TOTAL = 31;

	public static Pattern NYLOCAS_WAVE = Pattern.compile("Wave 'The Nylocas' \\(.*\\) complete!");

	public static final String BOSS_NAME = "Nylocas Vasilias";
	public static final String DEMI_BOSS_NAME = "Nylocas Prinkipas";
	public static final String MELEE_NAME = "Nylocas Ischyros";
	public static final String RANGE_NAME = "Nylocas Toxobolos";
	public static final String MAGIC_NAME = "Nylocas Hagios";

	public static final int BOSS_MAGIC_ANIM = 7989;
	public static final int BOSS_RANGE_ANIM = 7999;
	public static final int BOSS_MELEE_ANIM = 8004;

	public static final int PILLAR_GO_ID = 32862;
	public static final int PILLAR_COLLAPSED_GO_ID = 32864;
	public static final int SPECTATOR_WEB_1 = 32939;
	public static final int SPECTATOR_WEB_2 = 32865;
	public static final int SPECTATOR_WEB_3 = 32937;
	public static final int WALL_1 = 32876;
	public static final int WALL_2 = 32899;

	public static final int IMMUNE_GRAPHIC = 1558; // Spawns on the SW tile of the nylo
	public static final int MELEE_SMALL_DESPAWN_GRAPHIC = 1562;
	public static final int RANGE_SMALL_DESPAWN_GRAPHIC = 1563;
	public static final int MAGIC_SMALL_DESPAWN_GRAPHIC = 1564;
	public static final int UNK_DESPAWN_GRAPHIC_1 = 1561;
	public static final int UNK_DESPAWN_GRAPHIC_2 = 1891;
	public static final int UNK_DESPAWN_GRAPHIC_3 = 1892;
	public static final int UNK_DESPAWN_GRAPHIC_4 = 1893;
	public static final int UNK_DESPAWN_GRAPHIC_5 = 1894;

	private static final ImmutableMultimap<NylocasConstants, Integer> ENUM_MULTIMAP;
	private static final Table<Instance.Mode, Integer, NylocasConstants> LOOKUP_TABLE;
	private static final ImmutableMultimap<NylocasType, Integer> TYPE_IDS;

	public static final Set<Point> NYLOCAS_VALID_SPAWNS = ImmutableSet.of(
			new Point(17, 24), new Point(17, 25), new Point(18, 24), new Point(18, 25),
			new Point(31, 9), new Point(31, 10), new Point(32, 9), new Point(32, 10),
			new Point(46, 24), new Point(46, 25), new Point(47, 24), new Point(47, 25)
	);

	static
	{
		ImmutableMultimap.Builder<NylocasConstants, Integer> enum_builder = ImmutableListMultimap.builder();
		ImmutableTable.Builder<Instance.Mode, Integer, NylocasConstants> t_builder = ImmutableTable.builder();

		for (NylocasConstants def : values())
		{
			if (def.sm == -1 && def.rg == -1)
			{
				enum_builder.put(def, def.hm);
				t_builder.put(immutableCell(Instance.Mode.HARD, def.hm, def));
				continue;
			}

			enum_builder.putAll(def, def.sm, def.rg, def.hm);
			t_builder.put(immutableCell(Instance.Mode.STORY, def.sm, def));
			t_builder.put(immutableCell(Instance.Mode.REGULAR, def.rg, def));
			t_builder.put(immutableCell(Instance.Mode.HARD, def.hm, def));

			if (def.aggro_sm == -1 && def.aggro_rg == -1 && def.aggro_hm == -1)
			{
				continue;
			}

			enum_builder.putAll(def, def.aggro_sm, def.aggro_rg, def.aggro_hm);
			t_builder.put(immutableCell(Instance.Mode.STORY, def.aggro_sm, def));
			t_builder.put(immutableCell(Instance.Mode.REGULAR, def.aggro_rg, def));
			t_builder.put(immutableCell(Instance.Mode.HARD, def.aggro_hm, def));
		}

		ENUM_MULTIMAP = enum_builder.build();
		LOOKUP_TABLE = t_builder.build();

		ImmutableMultimap.Builder<NylocasType, Integer> type_ids = ImmutableListMultimap.builder();

		List<NylocasConstants> melees = ImmutableList.of(
				BOSS_DROPPING_MELEE,
				BOSS_MELEE,
				BOSS_MAGIC,
				BOSS_RANGE
		);

		melees.forEach(def -> type_ids.putAll(NylocasType.BOSS, def.sm, def.rg, def.hm));

		List<NylocasConstants> demis = ImmutableList.of(
				DEMI_BOSS_DROPPING_MELEE,
				DEMI_BOSS_MELEE,
				DEMI_BOSS_MAGIC,
				DEMI_BOSS_RANGE
		);

		demis.forEach(def -> type_ids.putAll(NylocasType.DEMI, def.hm));

		List<NylocasConstants> bigs = ImmutableList.of(
				MELEE_BIG,
				RANGE_BIG,
				MAGIC_BIG
		);

		bigs.forEach(def -> type_ids.putAll(NylocasType.BIG, def.sm, def.rg, def.hm, def.aggro_sm, def.aggro_rg, def.aggro_hm));

		List<NylocasConstants> smalls = ImmutableList.of(
				MELEE_SMALL,
				RANGE_SMALL,
				MAGIC_SMALL
		);

		smalls.forEach(def -> type_ids.putAll(NylocasType.SMALL, def.sm, def.rg, def.hm, def.aggro_sm, def.aggro_rg, def.aggro_hm));

		TYPE_IDS = type_ids.build();
	}

	public static boolean matchesAnyMode(NylocasConstants def, int npcId)
	{
		if (def == null)
		{
			return false;
		}

		return ENUM_MULTIMAP.get(def).contains(npcId);
	}

	@Nullable
	public static NylocasConstants queryTable(Instance.Mode mode, int npcId)
	{
		if (mode == null)
		{
			return null;
		}

		return LOOKUP_TABLE.get(mode, npcId);
	}

	@Nullable
	public static Instance.Mode findMode(int npcId)
	{
		return Instance.findFirstMode(mode -> queryTable(mode, npcId) != null);
	}

	public static boolean isBoss(int npcId)
	{
		return TYPE_IDS.get(NylocasType.BOSS).contains(npcId);
	}

	public static boolean isDemiBoss(int npcId)
	{
		return TYPE_IDS.get(NylocasType.DEMI).contains(npcId);
	}

	public static boolean isBigNylo(int npcId)
	{
		return TYPE_IDS.get(NylocasType.BIG).contains(npcId);
	}

	public static boolean isSmallNylo(int npcId)
	{
		return TYPE_IDS.get(NylocasType.SMALL).contains(npcId);
	}

	public static boolean isWavesNylo(int npcId)
	{
		return isBigNylo(npcId) || isSmallNylo(npcId);
	}

	NylocasConstants(int sm, int rg, int hm)
	{
		this(sm, rg, hm, -1, -1, -1);
	}

	NylocasConstants(int hm)
	{
		this(-1, -1, hm);
	}
}
