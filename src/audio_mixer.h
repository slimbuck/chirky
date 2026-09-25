#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct audio_mixer;
enum audio_mixer_state { AUDIO_STARTING, AUDIO_READY, AUDIO_FAILED };
struct audio_mixer_info {
    enum audio_mixer_state state;
    uint64_t startup_wall_us, startup_cpu_us;
};

/* Native Linux only; link with -pthread -ldl. Loads libasound.so.2 at runtime.
   One worker per mixer, 48 kHz stereo output, at most eight voices.
   NULL/empty device selects plughw:0,0. Returns immediately after creating the
   worker, or NULL on allocation/thread failure. Device setup is asynchronous;
   poll info for READY/FAILED. No ALSA setup or device wait on the caller. */
struct audio_mixer *audio_mixer_start(const char *device);
struct audio_mixer_info audio_mixer_get_info(struct audio_mixer *m);

/* Enqueue immutable interleaved native-endian signed PCM16 (no WAV header),
   mono or stereo, at a nonzero source rate. Unaligned data is supported.
   Copies only the pointer and metadata: keep ALL accepted samples alive and
   unchanged until reset/stop returns, even if a sound should have finished.
   No allocation, I/O, or waiting: false means invalid, busy, full, or unavailable.
   Requests can queue during startup. True means queued, not guaranteed audible;
   repeats of an active sample are ignored (not restarted), while distinct
   sounds can mix. Overload steals the oldest voice. Failed startup discards queued requests.
   NULL mixer is a harmless no-op returning false. Queue capacity is 32. */
bool audio_mixer_play(struct audio_mixer *m, const void *data, size_t size,
                      unsigned rate, unsigned channels);

/* Synchronous unload barrier: discard queued/active/buffered audio and release
   every sample reference before returning. The worker remains reusable unless
   the device has failed. Call BEFORE clearing the asset store.
   Serialize reset/stop on the host thread and quiesce producers before freeing
   their samples. Plays during reset are rejected. NULL is harmless. */
void audio_mixer_reset(struct audio_mixer *m);

/* Join the worker and free the mixer. No calls may overlap stop, or use m after
   it returns. All sample storage can then be freed. NULL is harmless. */
void audio_mixer_stop(struct audio_mixer *m);

#endif
