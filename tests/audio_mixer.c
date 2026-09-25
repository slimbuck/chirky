/* Standalone, no ALSA headers or hardware needed:
   cc -std=c11 -O2 -Wall -Wextra -Wpedantic -pthread tests/audio_mixer.c -ldl -o /tmp/audio-mixer-test
   /tmp/audio-mixer-test
   /tmp/audio-mixer-test --null   (also exercise installed libasound.so.2)
   Includes implementation to check mix arithmetic and inject the runtime ABI. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdio.h>

static void *test_dlopen(const char *, int);
static void *test_dlsym(void *, const char *);
static int test_dlclose(void *);
#define dlopen test_dlopen
#define dlsym test_dlsym
#define dlclose test_dlclose
#include "../src/audio_mixer.c"
#undef dlopen
#undef dlsym
#undef dlclose

enum fake_mode { NORMAL, PARTIAL, AGAIN, FATAL, XRUN, SUSPENDED, INTERRUPTED,
                 ZERO, REPEATED_XRUN };
static bool real_alsa, missing_library, missing_symbol, open_failure, setup_failure;
static bool params_failure, drop_failure, prepare_failure;
static atomic_int mode, writes, prepared, dropped, closed, loaded, unloaded, captured;
static atomic_int hold_drop, inside_drop, reset_returned;
static atomic_int hold_open, inside_open;
static pthread_t owner;
static unsigned char capture[8192 * 4];
static char opened_device[128];
static int fake_handle;

static int fake_open(snd_pcm_t **pcm, const char *device, int stream, int flags)
{
    owner = pthread_self();
    assert(stream == PCM_PLAYBACK && flags == PCM_NONBLOCK);
    snprintf(opened_device, sizeof(opened_device), "%s", device);
    atomic_store(&inside_open,1);
    uint64_t began=audio_milliseconds();
    while(atomic_load(&hold_open)) {
        assert(audio_milliseconds()-began<3000);
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
    }
    if (open_failure) return -ENODEV;
    *pcm = (snd_pcm_t *)&fake_handle;
    return 0;
}

static void check_owner(snd_pcm_t *pcm)
{
    assert(pcm == (snd_pcm_t *)&fake_handle);
    assert(pthread_equal(owner, pthread_self()));
}

static int fake_close(snd_pcm_t *pcm)
{
    check_owner(pcm);
    atomic_fetch_add(&closed, 1);
    return 0;
}

static int fake_set_params(snd_pcm_t *pcm, int format, int access, unsigned channels,
                            unsigned rate, int resample, unsigned latency)
{
    check_owner(pcm);
    assert(format == 2 && access == 3 && channels == 2 && rate == 48000);
    assert(resample == 1 && latency == 20000);
    return setup_failure ? -EINVAL : 0;
}

static int fake_get_params(snd_pcm_t *pcm, unsigned long *buffer, unsigned long *period)
{
    check_owner(pcm);
    *buffer = 960;
    *period = 240;
    return params_failure ? -EINVAL : 0;
}

static long fake_writei(snd_pcm_t *pcm, const void *data, unsigned long frames)
{
    check_owner(pcm);
    assert(frames && frames <= AUDIO_BLOCK);
    atomic_fetch_add(&writes, 1);
    int how = atomic_load(&mode);
    if (how == AGAIN) return -EAGAIN;
    if (how == FATAL) return -ENODEV;
    if (how == ZERO) return 0;
    if (how == REPEATED_XRUN) return -EPIPE;
    if (how == XRUN || how == SUSPENDED || how == INTERRUPTED) {
        atomic_store(&mode, NORMAL);
        return how == XRUN ? -EPIPE : how == SUSPENDED ? -ESTRPIPE : -EINTR;
    }
    if (how == PARTIAL && frames > 7) frames = 7;
    unsigned count = (unsigned)atomic_load(&captured);
    unsigned copy = count < 8192 ? 8192 - count : 0;
    if (copy > frames) copy = (unsigned)frames;
    memcpy(capture + count * 4, data, copy * 4);
    atomic_store(&captured, (int)(count + copy));
    return (long)frames;
}

static int fake_prepare(snd_pcm_t *pcm)
{
    check_owner(pcm);
    atomic_fetch_add(&prepared, 1);
    return prepare_failure ? -ENODEV : 0;
}

static int fake_drop(snd_pcm_t *pcm)
{
    check_owner(pcm);
    atomic_fetch_add(&dropped, 1);
    if (atomic_load(&hold_drop)) {
        atomic_store(&inside_drop, 1);
        uint64_t start = audio_milliseconds();
        while (atomic_load(&hold_drop)) {
            assert(audio_milliseconds() - start < 3000);
            struct timespec delay = {0, 1000000};
            nanosleep(&delay, NULL);
        }
    }
    return drop_failure ? -ENODEV : 0;
}

static void *test_dlopen(const char *path, int flags)
{
    if (real_alsa) return dlopen(path, flags);
    assert(!strcmp(path, "libasound.so.2"));
    if (missing_library) return NULL;
    atomic_fetch_add(&loaded, 1);
    return &fake_handle;
}

static void *test_dlsym(void *library, const char *name)
{
    if (real_alsa) return dlsym(library, name);
    assert(library == &fake_handle);
    if (missing_symbol) return NULL;
    struct audio_alsa a = {.open = fake_open, .close = fake_close,
        .set_params = fake_set_params, .get_params = fake_get_params,
        .writei = fake_writei, .prepare = fake_prepare, .drop = fake_drop};
    void *symbol = NULL;
#define SYMBOL(member) if (!strcmp(name, "snd_pcm_" #member)) \
    memcpy(&symbol, &a.member, sizeof(symbol))
    SYMBOL(open);
    SYMBOL(close);
    SYMBOL(set_params);
    SYMBOL(get_params);
    SYMBOL(writei);
    SYMBOL(prepare);
    SYMBOL(drop);
#undef SYMBOL
    assert(symbol);
    return symbol;
}

static int test_dlclose(void *library)
{
    if (real_alsa) return dlclose(library);
    assert(library == &fake_handle);
    atomic_fetch_add(&unloaded, 1);
    return 0;
}

static void put_sample(unsigned char *data, size_t index, int value)
{
    int16_t sample = (int16_t)value;
    memcpy(data + index * 2, &sample, sizeof(sample));
}

static int get_sample(const unsigned char *data, size_t index)
{
    unsigned value = (unsigned)data[index * 2] | ((unsigned)data[index * 2 + 1] << 8);
    return value < 32768u ? (int)value : (int)value - 65536;
}

static void test_mix(void)
{
    unsigned char storage[17] = {0}, *pcm = storage + 1, output[40];
    struct audio_voice voices[AUDIO_VOICES] = {0};
    unsigned count = 0;
    const int samples[] = {-32768, 32767, 1200, -2400, 0, 0, -1, 1};
    for (unsigned i = 0; i < 8; ++i) put_sample(pcm, i, samples[i]);
    audio_add_voice(voices, &count, (struct audio_command){pcm, 4, 48000, 2});
    audio_mix(voices, &count, output, 5);
    for (unsigned i = 0; i < 8; ++i) assert(get_sample(output, i) == samples[i]);
    assert(get_sample(output, 8) == 0 && get_sample(output, 9) == 0 && count == 0);

    put_sample(pcm, 0, 0);
    put_sample(pcm, 1, 12000);
    audio_add_voice(voices, &count, (struct audio_command){pcm, 2, 24000, 1});
    audio_mix(voices, &count, output, 5);
    const int interpolated[] = {0, 6000, 12000, 12000, 0};
    for (unsigned i = 0; i < 5; ++i)
        assert(get_sample(output, i * 2) == interpolated[i] &&
               get_sample(output, i * 2 + 1) == interpolated[i]);
    assert(count == 0);

    put_sample(pcm, 0, 30000);
    put_sample(pcm, 1, -30000);
    unsigned char distinct[8][4];
    for (unsigned i = 0; i < 8; ++i) {
        memcpy(distinct[i],pcm,4);
        audio_add_voice(voices, &count, (struct audio_command){distinct[i], 1, 48000, 2});
    }
    audio_mix(voices, &count, output, 1);
    assert(get_sample(output, 0) == 32767 && get_sample(output, 1) == -32768);

    for (unsigned i = 0; i < 4; ++i) put_sample(pcm, i, (int)i * 1000);
    audio_add_voice(voices, &count, (struct audio_command){pcm, 4, 96000, 1});
    audio_mix(voices, &count, output, 3);
    assert(get_sample(output, 0) == 0 && get_sample(output, 2) == 2000);
    assert(get_sample(output, 4) == 0 && count == 0);
}

static void test_repeated_sound(void)
{
    unsigned char pcm[16],other[4],output[16];
    for(unsigned i=0;i<8;i++)put_sample(pcm,i,1000+(int)i*100);
    put_sample(other,0,200);put_sample(other,1,-200);
    struct audio_command sample={pcm,4,48000,2};
    struct audio_voice voices[AUDIO_VOICES]={0};unsigned count=0;
    for(unsigned i=0;i<32;i++)audio_add_voice(voices,&count,sample);
    assert(count==1);
    audio_mix(voices,&count,output,1);
    assert(get_sample(output,0)==1000 && get_sample(output,1)==1100);
    for(unsigned i=0;i<32;i++)audio_add_voice(voices,&count,sample);
    assert(count==1 && voices[0].frame==1);
    audio_add_voice(voices,&count,(struct audio_command){other,1,48000,2});
    assert(count==2);
    audio_mix(voices,&count,output,3);
    assert(get_sample(output,0)==1400 && get_sample(output,1)==1100);
    assert(get_sample(output,2)==1400 && get_sample(output,3)==1500);
    assert(get_sample(output,4)==1600 && get_sample(output,5)==1700 && !count);
    audio_add_voice(voices,&count,sample);
    audio_mix(voices,&count,output,4);
    assert(!count && !memcmp(output,pcm,sizeof(pcm)));
}

static void test_rates(void)
{
    const unsigned rates[] = {1, 22050, 44100, 48000, 96000, 192000, UINT32_MAX};
    unsigned char pcm[2000], output[137 * 4];
    for (unsigned i = 0; i < 1000; ++i) put_sample(pcm, i, (int)i * 20 - 10000);
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        struct audio_voice voices[AUDIO_VOICES] = {0};
        unsigned count = 0, rendered = 0;
        audio_add_voice(voices, &count, (struct audio_command){pcm, 1000, rates[r], 1});
        for (unsigned block = 0; block < 16; ++block) {
            audio_mix(voices, &count, output, 137);
            for (unsigned f = 0; f < 137; ++f, ++rendered) {
                uint64_t phase = (uint64_t)rendered * rates[r];
                uint64_t frame = phase / AUDIO_RATE;
                int expected = 0;
                if (frame < 1000) {
                    int first = (int)frame * 20 - 10000;
                    expected = first + (frame < 999 ? (int)(20 * (phase % AUDIO_RATE) / AUDIO_RATE) : 0);
                }
                assert(get_sample(output, f * 2) == expected);
                assert(get_sample(output, f * 2 + 1) == expected);
            }
        }
        assert(count == (rates[r] == 1 ? 1u : 0u));
    }
}

static void test_voice_bounds(void)
{
    unsigned char pcm[10][2], output[4];
    struct audio_voice voices[AUDIO_VOICES] = {0};
    unsigned count = 0;
    for (unsigned i = 0; i < 10; ++i) {
        put_sample(pcm[i], 0, (int)i + 1);
        audio_add_voice(voices, &count, (struct audio_command){pcm[i], 1, 48000, 1});
        assert(count <= AUDIO_VOICES);
    }
    assert(count == 8 && voices[0].sample.data == pcm[2]);
    audio_mix(voices, &count, output, 1);
    assert(get_sample(output, 0) == 52 && get_sample(output, 1) == 52 && count == 0);
    for (unsigned i = 0; i < 8; ++i) assert(!voices[i].sample.data);
}

static void test_queue(void)
{
    struct audio_mixer m = {.started = true, .alive = true, .head = AUDIO_COMMANDS - 3};
    unsigned char pcm[4] = {0};
    assert(!pthread_mutex_init(&m.mutex, NULL));
    assert(!pthread_cond_init(&m.wake, NULL));
    assert(!audio_mixer_play(NULL, pcm, 4, 48000, 2));
    assert(!audio_mixer_play(&m, NULL, 4, 48000, 2));
    assert(!audio_mixer_play(&m, pcm, 0, 48000, 2));
    assert(!audio_mixer_play(&m, pcm, 3, 48000, 2));
    assert(!audio_mixer_play(&m, pcm, 4, 0, 2));
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 0));
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 3));
    pthread_mutex_lock(&m.mutex);
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 2));
    pthread_mutex_unlock(&m.mutex);
    for (unsigned i = 0; i < AUDIO_COMMANDS; ++i)
        assert(audio_mixer_play(&m, pcm, 4, 48000, 2));
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 2) && m.count == AUDIO_COMMANDS);
    audio_clear_commands(&m);
    for (unsigned i = 0; i < AUDIO_COMMANDS; ++i) assert(!m.commands[i].data);
    m.resetting = true;
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 2));
    m.resetting = false;
    m.alive = false;
    assert(!audio_mixer_play(&m, pcm, 4, 48000, 2));
    pthread_cond_destroy(&m.wake);
    pthread_mutex_destroy(&m.mutex);
    audio_mixer_reset(NULL);
    audio_mixer_stop(NULL);
}

static void pause_test(void)
{
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
}

static void wait_for(atomic_int *value, int target)
{
    uint64_t start = audio_milliseconds();
    while (atomic_load(value) < target) {
        assert(audio_milliseconds() - start < 3000);
        pause_test();
    }
}

static void play_until_accepted(struct audio_mixer *m, const void *pcm, size_t size)
{
    uint64_t start = audio_milliseconds();
    while (!audio_mixer_play(m, pcm, size, 48000, 2)) {
        assert(audio_milliseconds() - start < 3000);
        pause_test();
    }
}

static struct audio_mixer *start_ready(const char *device)
{
    struct audio_mixer *m=audio_mixer_start(device);
    assert(m);
    uint64_t began=audio_milliseconds();
    while(audio_mixer_get_info(m).state==AUDIO_STARTING) {
        assert(audio_milliseconds()-began<3000);pause_test();
    }
    if(audio_mixer_get_info(m).state==AUDIO_FAILED){audio_mixer_stop(m);return NULL;}
    return m;
}

static void clear_fake(void)
{
    atomic_store(&mode, NORMAL);
    atomic_store(&writes, 0);
    atomic_store(&prepared, 0);
    atomic_store(&dropped, 0);
    atomic_store(&closed, 0);
    atomic_store(&loaded, 0);
    atomic_store(&unloaded, 0);
    atomic_store(&captured, 0);
    atomic_store(&hold_drop, 0);
    atomic_store(&inside_drop, 0);
    atomic_store(&reset_returned, 0);
    atomic_store(&hold_open,0);
    atomic_store(&inside_open,0);
    memset(capture, 0, sizeof(capture));
    missing_library = missing_symbol = open_failure = setup_failure = false;
    params_failure = drop_failure = prepare_failure = false;
}

static void test_async_start(void)
{
    clear_fake();atomic_store(&hold_open,1);
    struct audio_mixer *m=audio_mixer_start("fake");
    assert(m);wait_for(&inside_open,1);
    assert(audio_mixer_get_info(m).state==AUDIO_STARTING);
    unsigned char *old=malloc(400);assert(old);memset(old,127,400);
    play_until_accepted(m,old,400);
    audio_mixer_reset(m);free(old);
    assert(!atomic_load(&writes));
    unsigned char fresh[4]={0};put_sample(fresh,0,1234);put_sample(fresh,1,-2345);
    for(unsigned i=0;i<AUDIO_COMMANDS;i++)play_until_accepted(m,fresh,sizeof(fresh));
    atomic_store(&hold_open,0);wait_for(&captured,1200);
    audio_mixer_reset(m);
    assert(get_sample(capture,0)==1234 && get_sample(capture,1)==-2345);
    for(unsigned i=4;i<1200*4;i++)assert(!capture[i]);
    struct audio_mixer_info info=audio_mixer_get_info(m);
    assert(info.state==AUDIO_READY && info.startup_wall_us);
    audio_mixer_stop(m);
    assert(audio_mixer_get_info(NULL).state==AUDIO_FAILED);
}

static void test_start_failures(void)
{
    bool *failures[] = {&missing_library, &missing_symbol, &open_failure,
                       &setup_failure, &params_failure};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        clear_fake();
        *failures[i] = true;
        assert(!start_ready("unavailable"));
        assert(atomic_load(&loaded) == atomic_load(&unloaded));
        assert(atomic_load(&closed) == (i >= 3 ? 1 : 0));
    }
    clear_fake();
    struct audio_mixer *m = start_ready(NULL);
    assert(m && !strcmp(opened_device, "plughw:0,0"));
    audio_mixer_stop(m);
    assert(atomic_load(&closed) == 1);
}

static void test_worker_output(void)
{
    const int modes[] = {NORMAL, PARTIAL, XRUN, SUSPENDED, INTERRUPTED};
    unsigned char pcm[300 * 4];
    for (unsigned i = 0; i < 600; ++i) put_sample(pcm, i, (int)i - 300);
    for (unsigned k = 0; k < sizeof(modes) / sizeof(modes[0]); ++k) {
        clear_fake();
        atomic_store(&mode, modes[k]);
        struct audio_mixer *m = start_ready("fake");
        assert(m);
        play_until_accepted(m, pcm, sizeof(pcm));
        wait_for(&captured, 1440); /* Complete sound plus startup padding. */
        audio_mixer_reset(m);
        for (unsigned i = 0; i < 600; ++i) assert(get_sample(capture, i) == (int)i - 300);
        for (unsigned i = sizeof(pcm); i < 1440 * 4; ++i) assert(!capture[i]);
        assert(atomic_load(&dropped) == 1);
        audio_mixer_stop(m);
        assert(atomic_load(&closed) == 1 && atomic_load(&unloaded) == 1);
    }
    /* A one-frame sound must still be padded past the start threshold. */
    clear_fake();
    struct audio_mixer *m = start_ready("fake");
    assert(m);
    play_until_accepted(m, pcm, 4);
    wait_for(&captured, 1200);
    audio_mixer_reset(m);
    assert(get_sample(capture, 0) == -300 && get_sample(capture, 1) == -299);
    audio_mixer_stop(m);
}

static void *reset_thread(void *m)
{
    audio_mixer_reset(m);
    atomic_store(&reset_returned, 1);
    return NULL;
}

static void test_reset_acknowledgement(void)
{
    clear_fake();
    atomic_store(&mode, AGAIN);
    struct audio_mixer *m = start_ready("fake");
    assert(m);
    unsigned char *old = malloc(48000 * 4);
    assert(old);
    memset(old, 0x7f, 48000 * 4);
    play_until_accepted(m, old, 48000 * 4);
    wait_for(&writes, 1);
    atomic_store(&hold_drop, 1);
    pthread_t resetting;
    assert(!pthread_create(&resetting, NULL, reset_thread, m));
    wait_for(&inside_drop, 1);
    for (unsigned i = 0; i < 32; ++i)
        assert(!audio_mixer_play(m, old, 48000 * 4, 48000, 2));
    assert(!atomic_load(&reset_returned));
    atomic_store(&hold_drop, 0);
    pthread_join(resetting, NULL);
    assert(atomic_load(&reset_returned));
    free(old);

    unsigned char fresh[4];
    put_sample(fresh, 0, 1000);
    put_sample(fresh, 1, -2000);
    atomic_store(&mode, NORMAL);
    play_until_accepted(m, fresh, sizeof(fresh));
    wait_for(&captured, 1200);
    audio_mixer_reset(m);
    assert(get_sample(capture, 0) == 1000 && get_sample(capture, 1) == -2000);
    for (unsigned i = 4; i < 1200 * 4; ++i) assert(!capture[i]);
    audio_mixer_stop(m);
}

static void test_reset_lifetime(void)
{
    clear_fake();
    atomic_store(&mode, AGAIN);
    struct audio_mixer *m = start_ready("fake");
    assert(m);
    for (unsigned iteration = 0; iteration < 100; ++iteration) {
        unsigned char *pcm = calloc(48000, 4);
        assert(pcm);
        int before = atomic_load(&writes);
        play_until_accepted(m, pcm, 48000 * 4);
        wait_for(&writes, before + 1);
        for (unsigned i = 0; i < 64; ++i) audio_mixer_play(m, pcm, 48000 * 4, 48000, 2);
        uint64_t start = audio_milliseconds();
        audio_mixer_reset(m);
        assert(audio_milliseconds() - start < 1000);
        free(pcm); /* ASan detects retained queued/active pointers on reuse. */
        pthread_mutex_lock(&m->mutex);
        assert(!m->count && !m->resetting && m->alive);
        for (unsigned i = 0; i < AUDIO_COMMANDS; ++i) assert(!m->commands[i].data);
        pthread_mutex_unlock(&m->mutex);
    }
    unsigned char *pcm = calloc(48000, 4);
    assert(pcm);
    int before = atomic_load(&writes);
    play_until_accepted(m, pcm, 48000 * 4);
    wait_for(&writes, before + 1);
    uint64_t start = audio_milliseconds();
    audio_mixer_stop(m);
    assert(audio_milliseconds() - start < 1000);
    free(pcm);
    assert(atomic_load(&closed) == 1);
}

static void test_device_failure(void)
{
    const int modes[] = {FATAL, AGAIN, ZERO, REPEATED_XRUN};
    unsigned char pcm[4] = {0};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        clear_fake();
        atomic_store(&mode, modes[i]);
        struct audio_mixer *m = start_ready("fake");
        assert(m);
        play_until_accepted(m, pcm, sizeof(pcm));
        wait_for(&closed, 1);
        audio_mixer_reset(m);
        assert(!audio_mixer_play(m, pcm, sizeof(pcm), 48000, 2));
        audio_mixer_stop(m);
        assert(atomic_load(&closed) == 1);
    }
    for (unsigned i = 0; i < 2; ++i) {
        clear_fake();
        drop_failure = i == 0;
        prepare_failure = i == 1;
        struct audio_mixer *m = start_ready("fake");
        assert(m);
        audio_mixer_reset(m);
        assert(!audio_mixer_play(m, pcm, sizeof(pcm), 48000, 2));
        audio_mixer_stop(m);
        assert(atomic_load(&closed) == 1);
    }
}

static void test_null_device(void)
{
    real_alsa = true;
    struct audio_mixer *m = start_ready("null");
    assert(m);
    for (unsigned i = 0; i < 20; ++i) {
        unsigned char *pcm = calloc(48000, 4);
        assert(pcm);
        play_until_accepted(m, pcm, 48000 * 4);
        pause_test();
        audio_mixer_reset(m);
        free(pcm);
    }
    audio_mixer_stop(m);
    assert(!start_ready("chirky-nonexistent-device"));
    real_alsa = false;
}

int main(int argc, char **argv)
{
    test_mix();
    test_repeated_sound();
    test_rates();
    test_voice_bounds();
    test_queue();
    test_async_start();
    test_start_failures();
    test_worker_output();
    test_reset_acknowledgement();
    test_reset_lifetime();
    test_device_failure();
    if (argc == 2 && !strcmp(argv[1], "--null")) test_null_device();
    puts("audio_mixer: all tests passed");
    return 0;
}
