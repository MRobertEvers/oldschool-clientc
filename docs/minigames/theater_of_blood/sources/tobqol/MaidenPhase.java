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

import com.google.common.collect.ImmutableMap;
import lombok.Getter;
import lombok.RequiredArgsConstructor;
import lombok.experimental.Accessors;
import net.runelite.api.NPC;

import java.util.HashMap;
import java.util.Map;

@RequiredArgsConstructor
@Getter
@Accessors(fluent = true)
public enum MaidenPhase
{
	P1("70s"),
	P2("50s"),
	P3("30s"),
	OTHER("*");

	private final String key;

	private static final Map<Integer, MaidenPhase> LOOKUP;

	static
	{
		ImmutableMap.Builder<Integer, MaidenPhase> builder = ImmutableMap.builder();

		Map<MaidenTable, MaidenPhase> phases = new HashMap<>();

		phases.put(MaidenTable.MAIDEN_P0, MaidenPhase.P1);
		phases.put(MaidenTable.MAIDEN_P1, MaidenPhase.P2);
		phases.put(MaidenTable.MAIDEN_P2, MaidenPhase.P3);


		phases.forEach((table, phase) ->
		{
			builder.put(table.sm(), phase);
			builder.put(table.rg(), phase);
			builder.put(table.hm(), phase);
		});

		LOOKUP = builder.build();
	}

	public static MaidenPhase compose(NPC npc)
	{
		if (npc == null)
		{
			return OTHER;
		}

		return LOOKUP.getOrDefault(npc.getId(), OTHER);
	}

	public boolean isPhaseOne()
	{
		return this == P1;
	}

	public boolean isPhaseTwo()
	{
		return this == P2;
	}

	public boolean isPhaseThree()
	{
		return this == P3;
	}

	public boolean isNonTrackedPhase()
	{
		return this == OTHER;
	}
}
