#ifndef SRC_PLATFORM_PLATFORM_AUDIO_NULL_H
#define SRC_PLATFORM_PLATFORM_AUDIO_NULL_H

#include "platform/platform_audio.h"

/*
 * Inspection hooks for the silent backend, for tests and harnesses.
 *
 * Kept out of platform_audio.h on purpose: "what would that block have sounded
 * like" is something only a backend that never plays anything can answer, and
 * the game must not be able to ask a backend what it just heard.
 */

/** `source_id` of the last VOICE_START submitted — the cache effect id. */
int
PlatformAudioNull_LastVoiceSource(struct PlatformAudio* audio);

/** Peak magnitude of the last mixed block. Zero means the game queued something
 *  that would have been inaudible. */
int
PlatformAudioNull_LastPeak(struct PlatformAudio* audio);

/** Sum of |sample| over the last mixed block. */
long
PlatformAudioNull_LastEnergy(struct PlatformAudio* audio);

#endif
