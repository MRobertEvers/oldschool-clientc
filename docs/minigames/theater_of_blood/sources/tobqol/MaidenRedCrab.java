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

import com.tobqol.api.game.Health;
import com.tobqol.api.game.Hitpoints;
import com.tobqol.api.game.Instance;
import lombok.AccessLevel;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.experimental.Accessors;
import lombok.experimental.FieldDefaults;
import net.runelite.api.Actor;
import net.runelite.api.Client;
import net.runelite.api.NPC;
import org.apache.commons.lang3.tuple.Pair;

import java.util.function.Predicate;

@FieldDefaults(level = AccessLevel.PRIVATE, makeFinal = true)
@Getter
@Accessors(fluent = true)
@EqualsAndHashCode(of = "npc", doNotUseGetters = true)
public class MaidenRedCrab implements Predicate<NPC>
{
	NPC npc;
	Health health;
	String phaseKey;
	String spawnKey;
	boolean scuffed;

	public MaidenRedCrab(Client client, Instance instance, NPC npc, MaidenPhase phase)
	{
		this.npc = npc;
		this.health = new Health(Hitpoints.MAIDEN_MATOMENOS.getBaseHP(instance));
		this.phaseKey = phase.key();

		Pair<String, Boolean> id = MaidenTable.lookupMatomenosSpawn(client, npc);
		this.spawnKey = id == null ? "Unknown" : id.getLeft();
		this.scuffed = id != null && id.getRight();
	}

	public int distance(Actor actor)
	{
		if (actor == null)
		{
			return -1;
		}

		return npc.getWorldArea().distanceTo2D(actor.getWorldArea()) - 1;
	}

	@Override
	public boolean test(NPC npc)
	{
		return npc != null && (npc == this.npc || npc.getIndex() == this.npc.getIndex());
	}
}
