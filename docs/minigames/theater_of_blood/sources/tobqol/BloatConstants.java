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

import com.google.common.collect.ImmutableList;

import java.util.regex.Pattern;

public interface BloatConstants
{
	String BOSS_NAME = "Pestilent Bloat";
	String BOSS_NAME_SIMPLE = "Bloat";

	Pattern BLOAT_WAVE = Pattern.compile("Wave 'The Pestilent Bloat' \\(.*\\) complete!Duration: (\\d+):(\\d+)\\.?(\\d+)");

	int BOSS_IMAGE = 25749;

	int DOWN_ANIM = 8082;

	ImmutableList<Integer> TANK = ImmutableList.of(32957, 32955, 32959, 32960, 32964, 33084);                   // GameObjects
	ImmutableList<Integer> TOP_OF_TANK = ImmutableList.of(32958, 32962, 32964, 32965, 33062);                   // GameObjects
	ImmutableList<Integer> CEILING_CHAINS = ImmutableList.of(32949, 32950, 32951, 32952, 32953, 32954, 32970);  // GameObjects
	ImmutableList<Integer> BLOAT_FLOOR = ImmutableList.of(32941, 32942, 32944, 32946, 32948);                   // GroundObjects
}
