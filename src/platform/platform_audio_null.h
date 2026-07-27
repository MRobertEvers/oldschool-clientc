#ifndef SRC_PLATFORM_PLATFORM_AUDIO_NULL_H
#define SRC_PLATFORM_PLATFORM_AUDIO_NULL_H

#include "platform/platform_audio.h"

/*
 * Inspection hooks for the silent backend, for tests and harnesses.
 *
 * Kept out of platform_audio.h on purpose: "what did the last clip look like" is
 * something only a backend that never plays anything can answer, and the game
 * must not be able to ask a backend what it just heard.
 */

/** Effect id of the last accepted clip, or -1. */
int
PlatformAudioNull_LastSoundId(struct PlatformAudio* audio);

int
PlatformAudioNull_LastSampleCount(struct PlatformAudio* audio);

/** Samples in the last clip that were not silence (0x80). Zero means the game
 *  submitted a buffer that would have been inaudible. */
int
PlatformAudioNull_LastNonSilentSamples(struct PlatformAudio* audio);

#endif
