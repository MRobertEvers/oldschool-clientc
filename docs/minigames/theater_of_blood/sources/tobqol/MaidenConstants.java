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

import net.runelite.api.NullObjectID;

import java.util.regex.Pattern;

public interface MaidenConstants
{
	String BOSS_NAME = "The Maiden of Sugadinti";
	String BOSS_NAME_SIMPLE = "Maiden";

	int BOSS_IMAGE = 25748;

	Pattern MAIDEN_WAVE = Pattern.compile("Wave 'The Maiden of Sugadinti' \\(.*\\) complete!");

	String RED_CRAB_NAME = "Nylocas Matomenos";
	String BLOOD_SPAWN_NAME = "Blood spawn";

	int MAIDEN_BLOOD_TOSS_ANIM = 8091;
	int MAIDEN_ATTACK_ANIM = 8092;
	int MAIDEN_DEATH_ANIM = 8093;

	int RED_CRAB_DEATH_ANIM = 8097;

	int BLOOD_TOSS_PROJ = 1578;

	int BLOOD_SPLAT_ID = 1579;                      // GraphicObject
	int BLOOD_TRAIL_ID = NullObjectID.NULL_32984;   // GameObject
}
