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
package com.tobqol.rooms.sotetseg.commons;

import com.google.common.collect.ImmutableList;

import java.util.regex.Pattern;

public interface SotetsegConstants
{
	String BOSS_NAME = "Sotetseg";

	int BOSS_IMAGE = 25751;

	Pattern SOTETSEG_WAVE = Pattern.compile("Wave 'Sotetseg' \\(.*\\) complete!");

	String SOTETSEG_DEATHBALL_RAW = "<col=bf0000>A large ball of energy is shot your way...</col>";
	String SOTETSEG_DEATHBALL = "A large ball of energy is shot your way...";

	int MAZE_TELE_ANIM = 1816;

	int SOTETSEG_MELEE_ANIM = 8138;
	int SOTETSEG_MAGIC_ANIM = 8139;

	int DEATH_BALL = 1604;
	int MAGIC_ORB = 1606;
	int RANGE_ORB = 1607;

	int MAZE_UNDERWORLD_PORTAL = 33037; // GameObject

	int INACTIVE_MAZE_GROUND_OBJ_0 = 33033; // Overworld
	int INACTIVE_MAZE_GROUND_OBJ_1 = 33034; // Underworld
	ImmutableList<Integer> ACTIVE_MAZE_GROUND_OBJS = ImmutableList.of(33035, 41750, 41751, 41752, 41753);

	ImmutableList<Integer> UNDERWORLD_ROCKS = ImmutableList.of(33063, 33064, 33065);

	int INFERNO_RANGE = 1378;
	int INFERNO_MAGE = 1380;
	int INFERNO_DEATH_BALL = 1375;
	int TOA_RANGE = 2241;
	int TOA_MAGE = 2224;
	int TOA_DEATH_BALL = 2176;
	int COX_MAGE = 1341;
	int COX_RANGE = 1343;
}
