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
package com.tobqol.rooms.maiden.commons;

import com.google.common.collect.ImmutableTable;
import com.google.common.collect.Table;
import com.tobqol.api.game.Instance;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.experimental.Accessors;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import net.runelite.api.NpcID;
import net.runelite.api.coords.WorldPoint;
import org.apache.commons.lang3.tuple.Pair;

import javax.annotation.Nullable;

import static com.google.common.collect.Tables.immutableCell;

@RequiredArgsConstructor
@Getter
@Accessors(fluent = true)
public enum MaidenTable implements MaidenConstants
{
	MAIDEN_P0(NpcID.THE_MAIDEN_OF_SUGADINTI_10814, NpcID.THE_MAIDEN_OF_SUGADINTI, NpcID.THE_MAIDEN_OF_SUGADINTI_10822),         // Pre-70s
	MAIDEN_P1(NpcID.THE_MAIDEN_OF_SUGADINTI_10815, NpcID.THE_MAIDEN_OF_SUGADINTI_8361, NpcID.THE_MAIDEN_OF_SUGADINTI_10823),    // Post-70s
	MAIDEN_P2(NpcID.THE_MAIDEN_OF_SUGADINTI_10816, NpcID.THE_MAIDEN_OF_SUGADINTI_8362, NpcID.THE_MAIDEN_OF_SUGADINTI_10824),    // Post-50s
	MAIDEN_P3(NpcID.THE_MAIDEN_OF_SUGADINTI_10817, NpcID.THE_MAIDEN_OF_SUGADINTI_8363, NpcID.THE_MAIDEN_OF_SUGADINTI_10825),    // Post-30s
	MAIDEN_DESPAWN_0(NpcID.THE_MAIDEN_OF_SUGADINTI_10818, NpcID.THE_MAIDEN_OF_SUGADINTI_8364, NpcID.THE_MAIDEN_OF_SUGADINTI_10826),
	MAIDEN_DESPAWN_1(NpcID.THE_MAIDEN_OF_SUGADINTI_10819, NpcID.THE_MAIDEN_OF_SUGADINTI_8365, NpcID.THE_MAIDEN_OF_SUGADINTI_10827),
	BLOOD_SPAWN(NpcID.BLOOD_SPAWN_10821, NpcID.BLOOD_SPAWN, NpcID.BLOOD_SPAWN_10829),
	RED_CRAB(NpcID.NYLOCAS_MATOMENOS_10820, NpcID.NYLOCAS_MATOMENOS, NpcID.NYLOCAS_MATOMENOS_10828);

	private final int sm, rg, hm;

	private static final Table<Instance.Mode, Integer, MaidenTable> LOOKUP_TABLE;
	private static final Table<String, Integer, Boolean> SPAWNS_TABLE;

	static
	{
		ImmutableTable.Builder<Instance.Mode, Integer, MaidenTable> l_builder = ImmutableTable.builder();

		for (MaidenTable def : values())
		{
			l_builder.put(immutableCell(Instance.Mode.STORY, def.sm, def));
			l_builder.put(immutableCell(Instance.Mode.REGULAR, def.rg, def));
			l_builder.put(immutableCell(Instance.Mode.HARD, def.hm, def));
		}

		LOOKUP_TABLE = l_builder.build();

		ImmutableTable.Builder<String, Integer, Boolean> s_builder = ImmutableTable.builder();
		s_builder.put(immutableCell("N1", 23 << 8 | 42, true));
		s_builder.put(immutableCell("N1", 22 << 8 | 41, false));
		s_builder.put(immutableCell("N2", 27 << 8 | 42, true));
		s_builder.put(immutableCell("N2", 26 << 8 | 41, false));
		s_builder.put(immutableCell("N3", 31 << 8 | 42, true));
		s_builder.put(immutableCell("N3", 30 << 8 | 41, false));
		s_builder.put(immutableCell("N4 (1)", 35 << 8 | 42, true));
		s_builder.put(immutableCell("N4 (1)", 34 << 8 | 41, false));
		s_builder.put(immutableCell("N4 (2)", 35 << 8 | 40, true));
		s_builder.put(immutableCell("N4 (2)", 34 << 8 | 39, false));
		s_builder.put(immutableCell("S1", 23 << 8 | 20, true));
		s_builder.put(immutableCell("S1", 22 << 8 | 21, false));
		s_builder.put(immutableCell("S2", 27 << 8 | 20, true));
		s_builder.put(immutableCell("S2", 26 << 8 | 21, false));
		s_builder.put(immutableCell("S3", 31 << 8 | 20, true));
		s_builder.put(immutableCell("S3", 30 << 8 | 21, false));
		s_builder.put(immutableCell("S4 (1)", 35 << 8 | 20, true));
		s_builder.put(immutableCell("S4 (1)", 34 << 8 | 21, false));
		s_builder.put(immutableCell("S4 (2)", 35 << 8 | 22, true));
		s_builder.put(immutableCell("S4 (2)", 34 << 8 | 23, false));
		SPAWNS_TABLE = s_builder.build();
	}

	@Nullable
	public static Instance.Mode findMode(int npcId)
	{
		return Instance.findFirstMode(mode -> LOOKUP_TABLE.get(mode, npcId) != null);
	}

	@Nullable
	public static Pair<String, Boolean> lookupMatomenosSpawn(Client client, @Nullable NPC npc)
	{
		if (npc == null)
		{
			return null;
		}

		WorldPoint wp = WorldPoint.fromLocal(client, npc.getLocalLocation());
		int packed = wp.getRegionX() << 8 | wp.getRegionY();

		for (String spawnKey : SPAWNS_TABLE.rowKeySet())
		{
			Boolean scuffed = SPAWNS_TABLE.get(spawnKey, packed);

			if (scuffed == null)
			{
				continue;
			}

			return Pair.of(spawnKey, scuffed);
		}

		return null;
	}
}