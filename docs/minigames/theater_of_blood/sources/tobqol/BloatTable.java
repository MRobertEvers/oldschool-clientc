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
package com.tobqol.rooms.bloat.commons;

import com.google.common.collect.ImmutableTable;
import com.google.common.collect.Table;
import com.tobqol.api.game.Instance;
import lombok.RequiredArgsConstructor;
import net.runelite.api.NpcID;

import static com.google.common.collect.Tables.immutableCell;

@RequiredArgsConstructor
public enum BloatTable implements BloatConstants
{
	BLOAT(NpcID.PESTILENT_BLOAT_10812, NpcID.PESTILENT_BLOAT, NpcID.PESTILENT_BLOAT_10813);

	private final int sm, rg, hm;

	private static final Table<Instance.Mode, Integer, BloatTable> LOOKUP_TABLE;

	static
	{
		ImmutableTable.Builder<Instance.Mode, Integer, BloatTable> l_builder = ImmutableTable.builder();

		for (BloatTable def : values())
		{
			l_builder.put(immutableCell(Instance.Mode.STORY, def.sm, def));
			l_builder.put(immutableCell(Instance.Mode.REGULAR, def.rg, def));
			l_builder.put(immutableCell(Instance.Mode.HARD, def.hm, def));
		}

		LOOKUP_TABLE = l_builder.build();
	}

	public static Instance.Mode findMode(int npcId)
	{
		return Instance.findFirstMode(mode -> LOOKUP_TABLE.get(mode, npcId) != null);
	}
}
