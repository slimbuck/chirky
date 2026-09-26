#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "../src/asset_store.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#include <stdatomic.h>
#endif

/* cc -std=c11 -Wall -Wextra -Wpedantic -Iinclude -pthread
      tests/asset_store.c src/asset_store.c -o /tmp/asset-store-test */
static char root[256];

static double now_ms(void)
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static void pause_poll(void)
{
    struct timespec t = {.tv_nsec = 1000000};
    nanosleep(&t, NULL);
}

static void path_for(char path[512], const char *name)
{
    int n = snprintf(path, 512, "%s/%s", root, name);
    assert(n > 0 && n < 512);
}

static void write_bytes(const char *name, const void *data, size_t size)
{
    char path[512];
    path_for(path, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, size, f) == size);
    assert(!fclose(f));
}

static void sparse_file(const char *name, size_t size)
{
    char path[512];
    path_for(path, name);
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    assert(fd >= 0 && !ftruncate(fd, (off_t)size));
    assert(!close(fd));
}

static chirky_asset request(struct asset_store *s, const char *name, enum chirky_asset_type type)
{
    char path[512];
    path_for(path, name);
    chirky_asset h = asset_store_request(s, path, type);
    assert(h);
    return h;
}

static enum chirky_asset_state wait_asset(struct asset_store *s, chirky_asset h)
{
    double deadline = now_ms() + 15000;
    enum chirky_asset_state state;
    while ((state = asset_store_state(s, h)) == CHIRKY_ASSET_LOADING) {
        assert(now_ms() < deadline);
        pause_poll();
    }
    return state;
}

static enum chirky_asset_state wait_prefetch(struct asset_store *s)
{
    double deadline = now_ms() + 15000;
    enum chirky_asset_state state;
    while ((state = asset_store_prefetch_state(s)) == CHIRKY_ASSET_LOADING) {
        assert(now_ms() < deadline);
        pause_poll();
    }
    return state;
}

static struct chirky_asset_view ready(struct asset_store *s, chirky_asset h)
{
    assert(wait_asset(s, h) == CHIRKY_ASSET_READY);
    struct chirky_asset_view view = asset_store_view(s, h);
    assert(view.data);
    return view;
}

static void expect_failure(struct asset_store *s, const char *name, enum chirky_asset_type type)
{
    chirky_asset h = request(s, name, type);
    assert(wait_asset(s, h) == CHIRKY_ASSET_FAILED);
    assert(!asset_store_view(s, h).data);
    asset_store_release(s, h);
}

static void test_blob(struct asset_store *s)
{
    const unsigned char bytes[] = {'a', 0, 'b', 255};
    write_bytes("blob.bin", bytes, sizeof(bytes));
    write_bytes("empty.bin", "", 0);
    chirky_asset a = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
    chirky_asset b = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
    assert(a == b);
    struct chirky_asset_view v = ready(s, a);
    assert(v.size == sizeof(bytes) && !memcmp(v.data, bytes, v.size));
    assert(((const unsigned char *)v.data)[v.size] == 0);
    assert(!v.width && !v.height && !v.rate && !v.channels);
    asset_store_release(s, a);
    assert(asset_store_view(s, b).data == v.data);
    asset_store_release(s, b);
    assert(asset_store_state(s, a) == CHIRKY_ASSET_FAILED);
    assert(!asset_store_view(s, a).data);
    assert(!asset_store_retain(s, a));
    b = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
    assert(b != a);
    assert(!asset_store_retain(s, a));
    asset_store_release(s, a);
    assert(ready(s, b).size == sizeof(bytes));
    asset_store_release(s, b);
    b = request(s, "empty.bin", CHIRKY_ASSET_BLOB);
    v = ready(s, b);
    assert(v.size == 0 && *(const char *)v.data == 0);
    asset_store_release(s, b);
    expect_failure(s, "absent-optional.txt", CHIRKY_ASSET_BLOB);
    assert(asset_store_prefetch_state(s) == CHIRKY_ASSET_READY);
    struct asset_store_metrics metrics = asset_store_get_metrics(s);
    assert(metrics.bytes_read == 2 * sizeof(bytes) && metrics.bytes_resident == 0);
    assert(metrics.wall_ms >= 0 && metrics.worker_cpu_ms >= 0);
    asset_store_clear(s);
    metrics = asset_store_get_metrics(s);
    assert(!metrics.bytes_read && !metrics.bytes_resident && !metrics.wall_ms && !metrics.worker_cpu_ms);
}

static void test_images(struct asset_store *s)
{
    const unsigned char first_bytes[] = {' ', '\t', '\r', '\n', '\v', '\f', '#', 0, 255};
    for (size_t i = 0; i < sizeof(first_bytes); i++) {
        unsigned char ppm[] = "P6\n# dimensions\n1 1\n255\nXYZ";
        size_t size = sizeof(ppm) - 1;
        ppm[size - 3] = first_bytes[i];
        write_bytes("pixel.ppm", ppm, size);
        chirky_asset h = request(s, "pixel.ppm", CHIRKY_ASSET_IMAGE);
        struct chirky_asset_view v = ready(s, h);
        const unsigned char *p = v.data;
        assert(v.width == 1 && v.height == 1 && v.size == 4);
        assert(p[0] == first_bytes[i] && p[1] == 'Y' && p[2] == 'Z' && p[3] == 255);
        asset_store_release(s, h);
    }
    const unsigned char ppm[] = "P6\r\n2 2\r\n255\r\n\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c";
    write_bytes("grid.ppm", ppm, sizeof(ppm) - 1);
    chirky_asset h = request(s, "grid.ppm", CHIRKY_ASSET_IMAGE);
    chirky_asset blob = request(s, "grid.ppm", CHIRKY_ASSET_BLOB);
    assert(h != blob);
    struct chirky_asset_view v = ready(s, h);
    assert(v.width == 2 && v.height == 2 && v.size == 16);
    const unsigned char *p = v.data;
    for (unsigned i = 0; i < 4; i++) {
        for (unsigned c = 0; c < 3; c++) assert(p[i * 4 + c] == i * 3 + c + 1);
        assert(p[i * 4 + 3] == 255);
    }
    assert(ready(s, blob).size == sizeof(ppm) - 1);
    asset_store_release(s, h);
    asset_store_release(s, blob);
    const unsigned char pam[] = "P7\n# alpha fixture\nWIDTH 2\nHEIGHT 1\nDEPTH 4\n"
        "MAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n"
        "\x01\x02\x03\x04\x80\x81\x82\xff";
    write_bytes("alpha.pam", pam, sizeof(pam) - 1);
    h = request(s, "alpha.pam", CHIRKY_ASSET_IMAGE);
    v = ready(s, h); p = v.data;
    assert(v.width == 2 && v.height == 1 && v.size == 8);
    assert(!memcmp(p, "\x01\x02\x03\x04\x80\x81\x82\xff", 8));
    asset_store_release(s, h);
    const char *bad_pam[] = {
        "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\n",
        "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\nabc",
        "P7\nWIDTH 1\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB\nENDHDR\nabcd"
    };
    for (size_t i = 0; i < sizeof(bad_pam) / sizeof(*bad_pam); i++) {
        write_bytes("bad.pam", bad_pam[i], strlen(bad_pam[i]));
        expect_failure(s, "bad.pam", CHIRKY_ASSET_IMAGE);
    }
    const char *bad[] = {"P3\n1 1\n255\nabc", "P6\n0 1\n255\n", "P6\n-1 1\n255\n",
        "P6\n1 1\n65535\nabcdef", "P6\n1 1\n255\nab", "P6\n1 1\n255",
        "P6\n4294967296 2\n255\nabc", "P6\n4294967295 4294967295\n255\nabc",
        "P6\n1 1\n255xabc", "P6\n# no newline"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        write_bytes("bad.ppm", bad[i], strlen(bad[i]));
        expect_failure(s, "bad.ppm", CHIRKY_ASSET_IMAGE);
    }
    asset_store_clear(s);
}

static void put16(unsigned char *p, unsigned n)
{ p[0] = (unsigned char)n; p[1] = (unsigned char)(n >> 8); }

static void put32(unsigned char *p, uint32_t n)
{ put16(p, n & 65535); put16(p + 2, n >> 16); }

static size_t wave(unsigned char out[128], unsigned bits, unsigned channels,
                   const unsigned char *samples, size_t length, bool data_first)
{
    memset(out, 0, 128);
    memcpy(out, "RIFF", 4);
    memcpy(out + 8, "WAVEJUNK", 8);
    put32(out + 16, 1);
    out[20] = 123; /* Odd unknown chunk, with its required pad byte. */
    size_t data_offset = data_first ? 22 : 46;
    size_t fmt_offset = data_first ? 30 + length + (length & 1) : 22;
    memcpy(out + fmt_offset, "fmt ", 4);
    put32(out + fmt_offset + 4, 16);
    unsigned char *fmt = out + fmt_offset + 8;
    put16(fmt, 1);
    put16(fmt + 2, channels);
    put32(fmt + 4, 22050);
    put32(fmt + 8, 22050 * channels * bits / 8);
    put16(fmt + 12, channels * bits / 8);
    put16(fmt + 14, bits);
    memcpy(out + data_offset, "data", 4);
    put32(out + data_offset + 4, (uint32_t)length);
    memcpy(out + data_offset + 8, samples, length);
    size_t size = 54 + length + (length & 1);
    put32(out + 4, (uint32_t)size - 8);
    return size;
}

static void test_sounds(struct asset_store *s)
{
    unsigned char wav[128];
    const unsigned char pcm16[] = {0, 128, 255, 127, 0, 0, 255, 255};
    size_t size = wave(wav, 16, 2, pcm16, sizeof(pcm16), false);
    write_bytes("stereo.wav", wav, size);
    chirky_asset h = request(s, "stereo.wav", CHIRKY_ASSET_SOUND);
    struct chirky_asset_view v = ready(s, h);
    const int16_t *p = v.data;
    assert(v.channels == 2 && v.rate == 22050 && v.size == 8 && !v.width && !v.height);
    assert(p[0] == -32768 && p[1] == 32767 && p[2] == 0 && p[3] == -1);
    assert(asset_store_retain(s, h)); /* Audio pins dynamically requested sound. */
    asset_store_release(s, h); /* Game drops its original reference. */
    assert(asset_store_state(s, h) == CHIRKY_ASSET_READY);
    assert(asset_store_view(s, h).data == p && p[0] == -32768 && p[3] == -1);
    asset_store_release(s, h);
    assert(!asset_store_retain(s, h) && !asset_store_view(s, h).data);
    const unsigned char pcm8[] = {0, 128, 255};
    size = wave(wav, 8, 1, pcm8, sizeof(pcm8), true);
    write_bytes("mono.wav", wav, size);
    h = request(s, "mono.wav", CHIRKY_ASSET_SOUND);
    v = ready(s, h); p = v.data;
    assert(v.channels == 1 && v.rate == 22050 && v.size == 6);
    assert(p[0] == -32768 && p[1] == 0 && p[2] == 32512);
    asset_store_release(s, h);
    for (unsigned mutation = 0; mutation < 13; mutation++) {
        size = wave(wav, 16, 2, pcm16, sizeof(pcm16), false);
        switch (mutation) {
        case 0: wav[0] = 'X'; break;
        case 1: put32(wav + 4, UINT32_MAX); break;
        case 2: put32(wav + 16, UINT32_MAX); break;
        case 3: put16(wav + 30, 3); break; /* float */
        case 4: put16(wav + 32, 0); break;
        case 5: put32(wav + 34, 0); break;
        case 6: put32(wav + 38, 1); break;
        case 7: put16(wav + 42, 1); break;
        case 8: put16(wav + 44, 24); break;
        case 9: put32(wav + 50, 7); break; /* incomplete frame */
        case 10: size--; break;
        case 11: memcpy(wav + 46, "LIST", 4); break;
        case 12: put32(wav + 26, 15); break;
        }
        write_bytes("invalid.wav", wav, size);
        expect_failure(s, "invalid.wav", CHIRKY_ASSET_SOUND);
    }
    /* Every truncated prefix must fail without overreading. */
    size = wave(wav, 8, 1, pcm8, sizeof(pcm8), false);
    for (size_t cut = 0; cut < size; cut++) {
        write_bytes("invalid.wav", wav, cut);
        expect_failure(s, "invalid.wav", CHIRKY_ASSET_SOUND);
    }
    asset_store_clear(s);
}

static void test_prefetch(struct asset_store *s)
{
    char dir[512], nested[512], path[512];
    path_for(dir, "bundle");
    path_for(nested, "bundle/nested");
    assert(!mkdir(dir, 0700) && !mkdir(nested, 0700));
    write_bytes("bundle/game.conf", "config", 6);
    write_bytes("bundle/nested/dialog.txt", "text", 4);
    write_bytes("bundle/nested/player.sprite", "sprite", 6);
    write_bytes("bundle/nested/player.robot", "robot", 5);
    write_bytes("bundle/nested/bad.ppm", "bad", 3);
    write_bytes("bundle/nested/bad.wav", "bad", 3);
    write_bytes("bundle/nested/ignored.bin", "ignored", 7);
    unsigned char wav[128];
    const unsigned char pcm[] = {0, 0, 1, 0};
    size_t size = wave(wav, 16, 1, pcm, sizeof(pcm), false);
    write_bytes("bundle/nested/ok.wav", wav, size);
    const char ppm[] = "P6\n1 1\n255\nabc";
    write_bytes("bundle/nested/ok.ppm", ppm, sizeof(ppm) - 1);
#ifndef __EMSCRIPTEN__
    path_for(path, "bundle/nested/loop");
    assert(!symlink(dir, path));
#endif
    /* Host order: request external launcher, append game prefetch. */
    chirky_asset external = request(s, "grid.ppm", CHIRKY_ASSET_IMAGE);
    assert(asset_store_prefetch(s, dir));
    assert(asset_store_prefetch(s, dir));
    assert(!asset_store_prefetch(s, root));
    assert(wait_prefetch(s) == CHIRKY_ASSET_READY);
    assert(ready(s, external).width == 2);
    const char *blobs[] = {"bundle/game.conf", "bundle/nested/dialog.txt", "bundle/nested/player.sprite",
        "bundle/nested/player.robot", "bundle/nested/ok.ppm", "bundle/nested/ok.wav",
        "bundle/nested/bad.ppm", "bundle/nested/bad.wav"};
    for (size_t i = 0; i < sizeof(blobs) / sizeof(*blobs); i++) {
        path_for(path, blobs[i]);
        assert(!unlink(path)); /* Proves subsequent request uses the prefetched cache. */
        chirky_asset a = request(s, blobs[i], CHIRKY_ASSET_BLOB);
        const void *data = ready(s, a).data;
        asset_store_release(s, a);
        chirky_asset b = request(s, blobs[i], CHIRKY_ASSET_BLOB);
        assert(a == b && asset_store_view(s, b).data == data);
        asset_store_release(s, b);
    }
    chirky_asset sound = request(s, "bundle/nested/ok.wav", CHIRKY_ASSET_SOUND);
    const int16_t *data = ready(s, sound).data;
    asset_store_release(s, sound);
    assert(data[0] == 0 && data[1] == 1);
    assert(asset_store_view(s, sound).data == data);
    chirky_asset image = request(s, "bundle/nested/ok.ppm", CHIRKY_ASSET_IMAGE);
    assert(ready(s, image).width == 1);
    asset_store_release(s, image);
    expect_failure(s, "bundle/nested/bad.ppm", CHIRKY_ASSET_IMAGE);
    expect_failure(s, "bundle/nested/bad.wav", CHIRKY_ASSET_SOUND);
    expect_failure(s, "bundle/missing-optional.txt", CHIRKY_ASSET_BLOB);
    assert(asset_store_prefetch_state(s) == CHIRKY_ASSET_READY);
    path_for(path, "bundle/nested/ignored.bin");
    assert(!unlink(path));
    expect_failure(s, "bundle/nested/ignored.bin", CHIRKY_ASSET_BLOB);
    asset_store_clear(s);
    assert(asset_store_state(s, sound) == CHIRKY_ASSET_FAILED);
    assert(!asset_store_retain(s, sound));
    assert(asset_store_state(s, external) == CHIRKY_ASSET_FAILED);
    assert(!asset_store_get_metrics(s).bytes_resident);
    path_for(path, "absent-directory");
    assert(asset_store_prefetch(s, path));
    assert(wait_prefetch(s) == CHIRKY_ASSET_FAILED);
    asset_store_clear(s);
    write_bytes("bundle/trailing.txt", "slash", 5);
    path_for(path, "bundle/");
    assert(asset_store_prefetch(s, path));
    assert(wait_prefetch(s) == CHIRKY_ASSET_READY);
    path_for(path, "bundle/trailing.txt");
    assert(!unlink(path));
    chirky_asset trailing = request(s, "bundle/trailing.txt", CHIRKY_ASSET_BLOB);
    assert(ready(s, trailing).size == 5);
    asset_store_clear(s);
}

static void test_bounds(struct asset_store *s)
{
    assert(!asset_store_request(s, NULL, CHIRKY_ASSET_BLOB));
    assert(!asset_store_request(s, "", CHIRKY_ASSET_BLOB));
    assert(!asset_store_request(s, "anything", (enum chirky_asset_type)99));
    char long_path[4097];
    memset(long_path, 'a', sizeof(long_path) - 1); long_path[sizeof(long_path) - 1] = 0;
    assert(!asset_store_request(s, long_path, CHIRKY_ASSET_BLOB));
    assert(!asset_store_prefetch(s, long_path));
    chirky_asset handles[ASSET_STORE_SLOTS];
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) {
        char name[64];
        snprintf(name, sizeof(name), "absent-%u.txt", i);
        handles[i] = request(s, name, CHIRKY_ASSET_BLOB);
    }
    assert(!asset_store_request(s, "overflow.txt", CHIRKY_ASSET_BLOB));
    write_bytes("bundle/capacity.txt", "capacity", 8);
    char directory[512];
    path_for(directory, "bundle");
    assert(asset_store_prefetch(s, directory));
    assert(wait_prefetch(s) == CHIRKY_ASSET_FAILED);
    asset_store_release(s, handles[0]);
    chirky_asset replacement = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
    assert(replacement != handles[0]);
    asset_store_release(s, handles[0]);
    assert(ready(s, replacement).size == 4);
    asset_store_clear(s);
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) assert(!asset_store_view(s, handles[i]).data);
    sparse_file("limit.bin", ASSET_STORE_MAX_BYTES);
    expect_failure(s, "limit.bin", CHIRKY_ASSET_BLOB);
    sparse_file("bundle/too-large.txt", ASSET_STORE_MAX_BYTES);
    assert(asset_store_prefetch(s, directory));
    assert(wait_prefetch(s) == CHIRKY_ASSET_FAILED);
    asset_store_clear(s);
    char oversized[512];
    path_for(oversized, "bundle/too-large.txt");
    assert(!unlink(oversized));
    sparse_file("limit.bin", ASSET_STORE_MAX_BYTES - 1);
    chirky_asset full = request(s, "limit.bin", CHIRKY_ASSET_BLOB);
    assert(ready(s, full).size == ASSET_STORE_MAX_BYTES - 1);
    assert(asset_store_get_metrics(s).bytes_resident == ASSET_STORE_MAX_BYTES);
    expect_failure(s, "empty.bin", CHIRKY_ASSET_BLOB);
    asset_store_release(s, full);
    chirky_asset empty = request(s, "empty.bin", CHIRKY_ASSET_BLOB);
    assert(!ready(s, empty).size);
    asset_store_release(s, empty);
    /* The decoder's temporary source and destination share the same budget. */
    char path[512];
    path_for(path, "large.ppm");
    FILE *f = fopen(path, "wb");
    assert(f && fprintf(f, "P6\n4096 4096\n255\n") > 0);
    long header = ftell(f);
    assert(header > 0 && !ftruncate(fileno(f), header + 4096 * 4096 * 3));
    assert(!fclose(f));
    expect_failure(s, "large.ppm", CHIRKY_ASSET_IMAGE);
    assert(!asset_store_get_metrics(s).bytes_resident);
    asset_store_clear(s);
}

#ifndef __EMSCRIPTEN__
struct concurrent_args { struct asset_store *store; chirky_asset pinned; const void *data; };

static void *concurrent_requests(void *arg)
{
    struct concurrent_args *a = arg;
    for (unsigned i = 0; i < 500; i++) {
        chirky_asset h = request(a->store, "blob.bin", CHIRKY_ASSET_BLOB);
        assert(h == a->pinned && asset_store_view(a->store, h).data == a->data);
        assert(asset_store_retain(a->store, h));
        assert(!memcmp(a->data, "a\0b\xff", 4));
        asset_store_release(a->store, h);
        asset_store_release(a->store, h);
    }
    return NULL;
}

struct cancel_args { struct asset_store *store; atomic_bool stop; };

static void *requests_during_clear(void *arg)
{
    struct cancel_args *a = arg;
    while (!atomic_load(&a->stop)) {
        chirky_asset h = request(a->store, "limit.bin", CHIRKY_ASSET_BLOB);
        (void)asset_store_state(a->store, h);
        /* Borrowed data must not be dereferenced concurrently with clear. */
        (void)asset_store_view(a->store, h);
        asset_store_release(a->store, h);
    }
    return NULL;
}

static void test_concurrency(struct asset_store *s)
{
    chirky_asset pinned = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
    struct concurrent_args args = {.store = s, .pinned = pinned, .data = ready(s, pinned).data};
    pthread_t threads[8];
    for (unsigned i = 0; i < 8; i++) assert(!pthread_create(&threads[i], NULL, concurrent_requests, &args));
    for (unsigned i = 0; i < 8; i++) assert(!pthread_join(threads[i], NULL));
    assert(asset_store_view(s, pinned).data == args.data);
    asset_store_release(s, pinned);
    struct cancel_args cancel = {.store = s};
    atomic_init(&cancel.stop, false);
    for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, requests_during_clear, &cancel));
    for (unsigned i = 0; i < 200; i++) { asset_store_clear(s); pause_poll(); }
    atomic_store(&cancel.stop, true);
    for (unsigned i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    asset_store_clear(s);
    assert(ready(s, request(s, "blob.bin", CHIRKY_ASSET_BLOB)).size == 4);
    asset_store_clear(s);
    char fifo[512];
    path_for(fifo, "never-written.fifo");
    assert(!mkfifo(fifo, 0600));
    expect_failure(s, "never-written.fifo", CHIRKY_ASSET_BLOB);
    assert(!unlink(fifo));
}
#endif

static void test_cancel(struct asset_store *s)
{
    for (unsigned iteration = 0; iteration < 30; iteration++) {
        chirky_asset old = request(s, "limit.bin", CHIRKY_ASSET_BLOB);
#ifndef __EMSCRIPTEN__
        if (iteration == 0) {
            double deadline = now_ms() + 15000;
            while (!asset_store_get_metrics(s).bytes_read) {
                assert(now_ms() < deadline);
                pause_poll();
            }
        }
#endif
        asset_store_clear(s);
        chirky_asset fresh = request(s, "blob.bin", CHIRKY_ASSET_BLOB);
        assert(fresh != old);
        asset_store_release(s, old);
        assert(asset_store_state(s, old) == CHIRKY_ASSET_FAILED);
        assert(!memcmp(ready(s, fresh).data, "a\0b\xff", 4));
        assert(asset_store_get_metrics(s).bytes_resident == 5);
        asset_store_clear(s);
    }
    for (unsigned iteration = 0; iteration < 20; iteration++) {
        assert(asset_store_prefetch(s, root));
        asset_store_clear(s);
        assert(asset_store_prefetch_state(s) == CHIRKY_ASSET_READY);
    }
    for (unsigned iteration = 0; iteration < 10; iteration++) {
        struct asset_store *temporary = asset_store_create();
        assert(temporary);
        (void)request(temporary, "limit.bin", CHIRKY_ASSET_BLOB);
        asset_store_destroy(temporary);
    }
}

static void test_shipped(struct asset_store *s)
{
    if (access("games/hardware-test/assets/edge-beep.wav", R_OK)) return;
    assert(asset_store_prefetch(s, "games"));
    assert(wait_prefetch(s) == CHIRKY_ASSET_READY);
    chirky_asset h = asset_store_request(s, "games/hardware-test/assets/edge-beep.wav", CHIRKY_ASSET_SOUND);
    struct chirky_asset_view v = ready(s, h);
    assert(v.rate == 48000 && v.channels == 2 && v.size);
    h = asset_store_request(s, "games/rosey-chop/assets/chop.wav", CHIRKY_ASSET_SOUND);
    v = ready(s, h);
    assert(v.rate == 22050 && v.channels == 1 && v.size);
    h = asset_store_request(s, "games/phosphor-run/assets/artwork/splash.ppm", CHIRKY_ASSET_IMAGE);
    v = ready(s, h);
    assert(v.width && v.height && v.size == (size_t)v.width * v.height * 4);
    h = asset_store_request(s, "games/bramble-hollow/assets/player.pam", CHIRKY_ASSET_IMAGE);
    v = ready(s, h);
    assert(v.width == 160 && v.height == 160 && v.size == (size_t)v.width * v.height * 4);
    bool transparent = false;
    for (size_t i = 3; i < v.size; i += 4) transparent |= ((const unsigned char *)v.data)[i] < 255;
    assert(transparent);
    asset_store_clear(s);
}

static void remove_fixture_tree(const char *directory)
{
    DIR *dir = opendir(directory);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        assert(n > 0 && (size_t)n < sizeof(path));
        struct stat st;
        assert(!lstat(path, &st));
        if (S_ISDIR(st.st_mode)) remove_fixture_tree(path);
        else assert(!unlink(path));
    }
    assert(!closedir(dir) && !rmdir(directory));
}

int main(void)
{
    strcpy(root, "/tmp/chirky-assets-XXXXXX");
    assert(mkdtemp(root));
    struct asset_store *s = asset_store_create();
    assert(s);
    assert(!asset_store_request(NULL, "x", CHIRKY_ASSET_BLOB));
    assert(asset_store_state(NULL, 0) == CHIRKY_ASSET_FAILED);
    assert(!asset_store_view(NULL, 0).data);
    assert(!asset_store_retain(NULL, 0) && !asset_store_retain(s, 0));
    assert(!asset_store_view(s, UINT32_MAX).data);
    assert(!asset_store_prefetch(NULL, root));
    asset_store_release(NULL, 0);
    asset_store_clear(NULL);
    asset_store_destroy(NULL);
    test_blob(s);
    test_images(s);
    test_sounds(s);
    test_prefetch(s);
    test_bounds(s);
#ifndef __EMSCRIPTEN__
    test_concurrency(s);
#endif
    test_cancel(s);
    test_shipped(s);
    asset_store_destroy(s);
    remove_fixture_tree(root);
    puts("Assets: cache/ownership, PPM/PAM, WAV, prefetch, limits, cancellation and stale handles passed.");
    return 0;
}
