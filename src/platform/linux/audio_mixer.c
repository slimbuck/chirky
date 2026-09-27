#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "audio_mixer.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUDIO_RATE 48000u
#define AUDIO_VOICES 8u
#define AUDIO_COMMANDS 32u
#define AUDIO_BLOCK 240u
#define AUDIO_STALL_MS 500u

/* ALSA's stable runtime ABI; no development headers or link-time dependency.
   snd_pcm_sframes_t / snd_pcm_uframes_t are long / unsigned long on Pi too. */
typedef struct _snd_pcm snd_pcm_t;
enum { PCM_PLAYBACK = 0, PCM_NONBLOCK = 1, PCM_S16_LE = 2,
       PCM_RW_INTERLEAVED = 3 };
struct audio_alsa {
    void *library;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*close)(snd_pcm_t *);
    int (*set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
    int (*get_params)(snd_pcm_t *, unsigned long *, unsigned long *);
    long (*writei)(snd_pcm_t *, const void *, unsigned long);
    int (*prepare)(snd_pcm_t *);
    int (*drop)(snd_pcm_t *);
};

struct audio_command {
    const unsigned char *data;
    size_t frames;
    unsigned rate, channels;
};

struct audio_voice {
    struct audio_command sample;
    size_t frame;
    unsigned fraction;
};

struct audio_mixer {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    char *device;
    bool started, alive, stopping, resetting;
    uint64_t startup_wall_us, startup_cpu_us;
    struct audio_command commands[AUDIO_COMMANDS];
    unsigned head, count;
};

static bool audio_alsa_load(struct audio_alsa *a)
{
    a->library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!a->library) return false;
#define AUDIO_SYMBOL(member, name) do { \
    void *symbol = dlsym(a->library, name); \
    _Static_assert(sizeof(a->member) == sizeof(symbol), "POSIX function pointer"); \
    if (!symbol) return false; \
    memcpy(&a->member, &symbol, sizeof(symbol)); \
} while (0)
    AUDIO_SYMBOL(open, "snd_pcm_open");
    AUDIO_SYMBOL(close, "snd_pcm_close");
    AUDIO_SYMBOL(set_params, "snd_pcm_set_params");
    AUDIO_SYMBOL(get_params, "snd_pcm_get_params");
    AUDIO_SYMBOL(writei, "snd_pcm_writei");
    AUDIO_SYMBOL(prepare, "snd_pcm_prepare");
    AUDIO_SYMBOL(drop, "snd_pcm_drop");
#undef AUDIO_SYMBOL
    return true;
}

static int audio_sample(const struct audio_command *s, size_t frame, unsigned channel)
{
    const unsigned char *p = s->data + (frame * s->channels + channel) * 2;
    int16_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void audio_add_voice(struct audio_voice voices[AUDIO_VOICES], unsigned *count,
                            struct audio_command sample)
{
    /* Rapid repeats must not stack gain or restart a sound's attack. */
    for(unsigned i=0;i<*count;i++) {
        const struct audio_command *active=&voices[i].sample;
        if(active->data==sample.data && active->frames==sample.frames &&
            active->rate==sample.rate && active->channels==sample.channels)return;
    }
    if (*count == AUDIO_VOICES) {
        memmove(voices, voices + 1, (AUDIO_VOICES - 1) * sizeof(*voices));
        --*count;
    }
    voices[(*count)++] = (struct audio_voice){.sample = sample};
}

static void audio_mix(struct audio_voice voices[AUDIO_VOICES], unsigned *count,
                      unsigned char *output, unsigned frames)
{
    for (unsigned f = 0; f < frames; ++f) {
        int mixed[2] = {0, 0};
        for (unsigned v = 0; v < *count; ++v) {
            struct audio_voice *voice = &voices[v];
            const struct audio_command *s = &voice->sample;
            if (voice->frame >= s->frames) continue;
            size_t next = voice->frame + (voice->frame < s->frames - 1);
            for (unsigned ch = 0; ch < 2; ++ch) {
                unsigned source = s->channels == 1 ? 0 : ch;
                int first = audio_sample(s, voice->frame, source);
                int second = audio_sample(s, next, source);
                mixed[ch] += first + (int)((int64_t)(second - first) *
                                           voice->fraction / AUDIO_RATE);
            }
            /* Rational phase keeps rates exact without float drift or a
               whole-clip phase accumulator that can overflow on long sounds. */
            uint64_t phase = (uint64_t)voice->fraction + s->rate;
            size_t step = (size_t)(phase / AUDIO_RATE);
            voice->fraction = (unsigned)(phase % AUDIO_RATE);
            size_t remaining = s->frames - voice->frame;
            voice->frame += step < remaining ? step : remaining;
        }
        for (unsigned ch = 0; ch < 2; ++ch) {
            int value = mixed[ch];
            if (value > 32767) value = 32767;
            if (value < -32768) value = -32768;
            unsigned packed = (uint16_t)value;
            output[f * 4 + ch * 2] = (unsigned char)packed;
            output[f * 4 + ch * 2 + 1] = (unsigned char)(packed >> 8);
        }
    }
    unsigned kept = 0;
    for (unsigned v = 0; v < *count; ++v)
        if (voices[v].frame < voices[v].sample.frames) voices[kept++] = voices[v];
    memset(voices + kept, 0, (AUDIO_VOICES - kept) * sizeof(*voices));
    *count = kept;
}

static uint64_t audio_milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (unsigned long)now.tv_nsec / 1000000;
}

static uint64_t audio_microseconds(clockid_t clock)
{
    struct timespec now;
    clock_gettime(clock,&now);
    return (uint64_t)now.tv_sec*1000000+(unsigned long)now.tv_nsec/1000;
}

static void audio_retry_wait(struct audio_mixer *m)
{
    struct timespec until;
    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_nsec += 2000000; /* Bounded, interruptible 2 ms backoff. */
    if (until.tv_nsec >= 1000000000) {
        ++until.tv_sec;
        until.tv_nsec -= 1000000000;
    }
    pthread_mutex_lock(&m->mutex);
    if (!m->stopping && !m->resetting)
        pthread_cond_timedwait(&m->wake, &m->mutex, &until);
    pthread_mutex_unlock(&m->mutex);
}

static void audio_clear_commands(struct audio_mixer *m)
{
    memset(m->commands, 0, sizeof(m->commands));
    m->head = m->count = 0;
}

static void *audio_worker(void *context)
{
    struct audio_mixer *m = context;
    struct audio_alsa a = {0};
    snd_pcm_t *pcm = NULL;
    struct audio_voice voices[AUDIO_VOICES] = {0};
    unsigned voice_count = 0, pending = 0, offset = 0;
    unsigned char output[AUDIO_BLOCK * 4];
    unsigned long buffer_frames = 0, period_frames = 0, tail = 0;
    uint64_t progress = audio_milliseconds();
    uint64_t startup_wall=audio_microseconds(CLOCK_MONOTONIC);
    uint64_t startup_cpu=audio_microseconds(CLOCK_THREAD_CPUTIME_ID);

    if (!audio_alsa_load(&a) ||
        a.open(&pcm, m->device, PCM_PLAYBACK, PCM_NONBLOCK) < 0 ||
        a.set_params(pcm, PCM_S16_LE, PCM_RW_INTERLEAVED, 2, AUDIO_RATE, 1, 20000) < 0 ||
        a.get_params(pcm, &buffer_frames, &period_frames) < 0 || !buffer_frames) {
        fprintf(stderr, "Audio unavailable on %s\n", m->device);
        goto done;
    }
    pthread_mutex_lock(&m->mutex);
    m->startup_wall_us=audio_microseconds(CLOCK_MONOTONIC)-startup_wall;
    m->startup_cpu_us=audio_microseconds(CLOCK_THREAD_CPUTIME_ID)-startup_cpu;
    m->started = m->alive = true;
    pthread_cond_broadcast(&m->wake);
    pthread_mutex_unlock(&m->mutex);

    for (;;) {
        pthread_mutex_lock(&m->mutex);
        while (!m->stopping && !m->resetting && !m->count &&
               !voice_count && !pending && !tail)
            pthread_cond_wait(&m->wake, &m->mutex);
        if (m->stopping) {
            pthread_mutex_unlock(&m->mutex);
            break;
        }
        if (m->resetting) {
            audio_clear_commands(m);
            memset(voices, 0, sizeof(voices));
            voice_count = pending = offset = 0;
            tail = 0;
            pthread_mutex_unlock(&m->mutex);
            /* Never drain: reset must discard buffered output and must not
               wait for playback. Acknowledge only after ALSA releases it. */
            if (a.drop(pcm) < 0 || a.prepare(pcm) < 0) break;
            pthread_mutex_lock(&m->mutex);
            m->resetting = false;
            pthread_cond_broadcast(&m->wake);
            pthread_mutex_unlock(&m->mutex);
            continue;
        }
        while (m->count) {
            audio_add_voice(voices, &voice_count, m->commands[m->head]);
            memset(&m->commands[m->head], 0, sizeof(m->commands[m->head]));
            m->head = (m->head + 1) % AUDIO_COMMANDS;
            --m->count;
        }
        pthread_mutex_unlock(&m->mutex);

        if (!pending) {
            if (voice_count) tail = buffer_frames;
            else tail = tail > AUDIO_BLOCK ? tail - AUDIO_BLOCK : 0;
            /* A buffer of trailing silence also starts sounds shorter than
               ALSA's start threshold, without spinning on silence at idle. */
            audio_mix(voices, &voice_count, output, AUDIO_BLOCK);
            pending = AUDIO_BLOCK;
            offset = 0;
            progress = audio_milliseconds();
        }
        long written = a.writei(pcm, output + offset * 4, pending);
        if (written > 0 && (unsigned long)written <= pending) {
            offset += (unsigned)written;
            pending -= (unsigned)written;
            progress = audio_milliseconds();
            continue;
        }
        if (written == -EPIPE || written == -ESTRPIPE) {
            /* prepare discards a suspended buffer; unlike snd_pcm_recover's
               resume loop it never sleeps waiting for a device to resume. */
            if (a.prepare(pcm) < 0) break;
        } else if (written != 0 && written != -EAGAIN && written != -EINTR) {
            break;
        }
        if (audio_milliseconds() - progress >= AUDIO_STALL_MS) break;
        audio_retry_wait(m);
    }

done:
    memset(voices, 0, sizeof(voices));
    if (pcm) {
        a.drop(pcm);
        a.close(pcm);
    }
    if (a.library) dlclose(a.library);
    pthread_mutex_lock(&m->mutex);
    if(!m->started) {
        m->startup_wall_us=audio_microseconds(CLOCK_MONOTONIC)-startup_wall;
        m->startup_cpu_us=audio_microseconds(CLOCK_THREAD_CPUTIME_ID)-startup_cpu;
    }
    audio_clear_commands(m);
    m->alive = m->resetting = false;
    m->started = true;
    pthread_cond_broadcast(&m->wake);
    pthread_mutex_unlock(&m->mutex);
    return NULL;
}

struct audio_mixer *audio_mixer_start(const char *device)
{
    struct audio_mixer *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->device = strdup(device && *device ? device : "plughw:0,0");
    if (!m->device) { free(m); return NULL; }
    if (pthread_mutex_init(&m->mutex, NULL)) goto fail;
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr)) goto fail_mutex;
    int error = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (!error) error = pthread_cond_init(&m->wake, &attr);
    pthread_condattr_destroy(&attr);
    if (error) goto fail_mutex;
    if (pthread_create(&m->thread, NULL, audio_worker, m)) goto fail_cond;
    return m;

fail_cond:
    pthread_cond_destroy(&m->wake);
fail_mutex:
    pthread_mutex_destroy(&m->mutex);
fail:
    free(m->device);
    free(m);
    return NULL;
}

struct audio_mixer_info audio_mixer_get_info(struct audio_mixer *m)
{
    if(!m)return (struct audio_mixer_info){.state=AUDIO_FAILED};
    pthread_mutex_lock(&m->mutex);
    struct audio_mixer_info info={.state=!m->started?AUDIO_STARTING:m->alive?AUDIO_READY:AUDIO_FAILED,
        .startup_wall_us=m->startup_wall_us,.startup_cpu_us=m->startup_cpu_us};
    pthread_mutex_unlock(&m->mutex);
    return info;
}

bool audio_mixer_play(struct audio_mixer *m, const void *data, size_t size,
                      unsigned rate, unsigned channels)
{
    if (!m || !data || !size || !rate || (channels != 1 && channels != 2) ||
        size % (channels * 2)) return false;
    if (pthread_mutex_trylock(&m->mutex)) return false;
    bool accepted = (!m->started || m->alive) && !m->stopping && !m->resetting && m->count < AUDIO_COMMANDS;
    if (accepted) {
        unsigned slot = (m->head + m->count) % AUDIO_COMMANDS;
        m->commands[slot] = (struct audio_command){data, size / (channels * 2), rate, channels};
        ++m->count;
        pthread_cond_signal(&m->wake);
    }
    pthread_mutex_unlock(&m->mutex);
    return accepted;
}

void audio_mixer_reset(struct audio_mixer *m)
{
    if (!m) return;
    pthread_mutex_lock(&m->mutex);
    /* During setup the worker has not consumed any commands or PCM pointers. */
    if(!m->started)audio_clear_commands(m);
    if (m->alive) {
        m->resetting = true;
        pthread_cond_signal(&m->wake);
        while (m->resetting && m->alive) pthread_cond_wait(&m->wake, &m->mutex);
    }
    pthread_mutex_unlock(&m->mutex);
}

void audio_mixer_stop(struct audio_mixer *m)
{
    if (!m) return;
    pthread_mutex_lock(&m->mutex);
    m->stopping = true;
    pthread_cond_signal(&m->wake);
    pthread_mutex_unlock(&m->mutex);
    pthread_join(m->thread, NULL);
    pthread_cond_destroy(&m->wake);
    pthread_mutex_destroy(&m->mutex);
    free(m->device);
    free(m);
}
