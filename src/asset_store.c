#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "asset_store.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#endif

#define ASSET_PATH_MAX 4096u
#define ASSET_GENERATION_MAX (UINT32_MAX >> 10)
#define ASSET_CHUNK 65536u

struct asset_slot {
    char *path;
    uint32_t generation;
    unsigned refs;
    enum chirky_asset_type type;
    enum chirky_asset_state state;
    bool pending, prefetched;
    struct chirky_asset_view view;
    size_t allocation;
};

struct asset_store {
#ifndef __EMSCRIPTEN__
    pthread_t worker;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
#endif
    struct asset_slot slots[ASSET_STORE_SLOTS];
    uint64_t epoch;
    bool stop, scan_pending, scanning, prefetch_failed, started;
    char *directory;
    size_t allocated, resident;
    double began, ended, cpu;
    uint64_t bytes_read;
};

struct asset_job {
    uint64_t epoch;
    chirky_asset handle;
    enum chirky_asset_type type;
    char path[ASSET_PATH_MAX];
};

struct asset_result {
    struct chirky_asset_view view;
    size_t allocation;
};

static void lock_store(struct asset_store *s)
{
#ifndef __EMSCRIPTEN__
    pthread_mutex_lock(&s->mutex);
#else
    (void)s;
#endif
}

static void unlock_store(struct asset_store *s)
{
#ifndef __EMSCRIPTEN__
    pthread_mutex_unlock(&s->mutex);
#else
    (void)s;
#endif
}

static void wake_store(struct asset_store *s)
{
#ifndef __EMSCRIPTEN__
    pthread_cond_signal(&s->wake);
#else
    (void)s;
#endif
}

static double milliseconds(bool cpu)
{
    struct timespec t;
    if (clock_gettime(cpu ? CLOCK_THREAD_CPUTIME_ID : CLOCK_MONOTONIC, &t)) return 0;
    return t.tv_sec * 1000.0 + t.tv_nsec / 1000000.0;
}

static chirky_asset slot_handle(struct asset_store *s, struct asset_slot *slot)
{
    return (slot->generation << 10) | (uint32_t)(slot - s->slots);
}

static struct asset_slot *find_slot(struct asset_store *s, chirky_asset handle)
{
    struct asset_slot *slot = &s->slots[handle & (ASSET_STORE_SLOTS - 1)];
    return handle && slot->path && slot->generation == (handle >> 10) ? slot : NULL;
}

static bool current_locked(struct asset_store *s, const struct asset_job *job)
{
    return !s->stop && s->epoch == job->epoch &&
        (!job->handle || find_slot(s, job->handle));
}

static bool current(struct asset_store *s, const struct asset_job *job)
{
    lock_store(s);
    bool valid = current_locked(s, job);
    unlock_store(s);
    return valid;
}

static bool busy_locked(struct asset_store *s)
{
    if (s->scan_pending || s->scanning) return true;
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++)
        if (s->slots[i].path && s->slots[i].state == CHIRKY_ASSET_LOADING) return true;
    return false;
}

static void begin_locked(struct asset_store *s)
{
    if (!s->started) { s->began = milliseconds(false); s->started = true; }
}

static void idle_locked(struct asset_store *s)
{
    if (!busy_locked(s)) s->ended = milliseconds(false);
}

static void dispose_locked(struct asset_store *s, struct asset_slot *slot)
{
    free((void *)slot->view.data);
    free(slot->path);
    s->allocated -= slot->allocation;
    s->resident -= slot->allocation;
    uint32_t generation = slot->generation;
    *slot = (struct asset_slot){.generation = generation};
}

static void *allocate_payload(struct asset_store *s, const struct asset_job *job, size_t size)
{
    lock_store(s);
    bool allowed = current_locked(s, job) && size <= ASSET_STORE_MAX_BYTES - s->allocated;
    if (allowed) s->allocated += size;
    unlock_store(s);
    if (!allowed) return NULL;
    void *data = malloc(size);
    if (!data) {
        lock_store(s);
        s->allocated -= size;
        unlock_store(s);
    }
    return data;
}

static void free_payload(struct asset_store *s, const void *data, size_t size)
{
    if (!data) return;
    free((void *)data);
    lock_store(s);
    s->allocated -= size;
    unlock_store(s);
}

static chirky_asset request_locked(struct asset_store *s, const char *path,
                                   enum chirky_asset_type type, bool owner)
{
    struct asset_slot *available = NULL;
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) {
        struct asset_slot *slot = &s->slots[i];
        if (!slot->path) {
            if (!available && slot->generation < ASSET_GENERATION_MAX) available = slot;
        } else if (slot->type == type && !strcmp(slot->path, path)) {
            if (owner) slot->prefetched = true;
            else {
                if (slot->refs == UINT_MAX) return 0;
                slot->refs++;
            }
            return slot_handle(s, slot);
        }
    }
    if (!available) return 0;
    char *copy = strdup(path);
    if (!copy) return 0;
    *available = (struct asset_slot){.path = copy, .generation = available->generation + 1,
        .refs = owner ? 0 : 1, .type = type, .state = CHIRKY_ASSET_LOADING,
        .pending = true, .prefetched = owner};
    begin_locked(s);
    wake_store(s);
    return slot_handle(s, available);
}

static bool read_file(struct asset_store *s, const struct asset_job *job, struct asset_result *out)
{
    /* Reject pipes/devices before reading; O_NONBLOCK also avoids a FIFO open
       blocking cancellation or shutdown. Regular file reads are chunked. */
    int fd = open(job->path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uint64_t)st.st_size >= ASSET_STORE_MAX_BYTES) { close(fd); return false; }
    size_t size = (size_t)st.st_size;
    unsigned char *data = allocate_payload(s, job, size + 1);
    if (!data) { close(fd); return false; }
    size_t offset = 0;
    while (offset < size && current(s, job)) {
        size_t chunk = size - offset;
        if (chunk > ASSET_CHUNK) chunk = ASSET_CHUNK;
        ssize_t got = read(fd, data + offset, chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        offset += (size_t)got;
        lock_store(s);
        if (s->epoch == job->epoch) s->bytes_read += (uint64_t)got;
        unlock_store(s);
    }
    close(fd);
    if (offset != size || !current(s, job)) { free_payload(s, data, size + 1); return false; }
    data[size] = 0;
    *out = (struct asset_result){.view = {.data = data, .size = size}, .allocation = size + 1};
    return true;
}

static bool ppm_number(const unsigned char *data, size_t size, size_t *pos, unsigned *value)
{
    for (;;) {
        while (*pos < size && isspace(data[*pos])) (*pos)++;
        if (*pos == size) return false;
        if (data[*pos] != '#') break;
        while (*pos < size && data[*pos] != '\n' && data[*pos] != '\r') (*pos)++;
    }
    if (!isdigit(data[*pos])) return false;
    unsigned n = 0;
    while (*pos < size && isdigit(data[*pos])) {
        unsigned digit = data[(*pos)++] - '0';
        if (n > (UINT_MAX - digit) / 10) return false;
        n = n * 10 + digit;
    }
    if (*pos == size || !isspace(data[*pos])) return false;
    *value = n;
    return true;
}

static bool decode_image(struct asset_store *s, const struct asset_job *job,
                         const struct asset_result *raw, struct asset_result *out)
{
    const unsigned char *data = raw->view.data;
    size_t size = raw->view.size, pos = 2;
    unsigned width, height, maximum;
    if (size < 3 || memcmp(data, "P6", 2) || !isspace(data[2]) ||
        !ppm_number(data, size, &pos, &width) || !ppm_number(data, size, &pos, &height) ||
        !ppm_number(data, size, &pos, &maximum) || !width || !height || maximum != 255)
        return false;
    /* Consume only the raster separator (or CRLF), never binary whitespace. */
    unsigned char separator = data[pos++];
    if (separator == '\r' && pos < size && data[pos] == '\n') pos++;
    uint64_t pixels64 = (uint64_t)width * height;
    if (pixels64 > ASSET_STORE_MAX_BYTES / 4 || pixels64 * 3 > size - pos) return false;
    size_t pixels = (size_t)pixels64, bytes = pixels * 4;
    unsigned char *rgba = allocate_payload(s, job, bytes);
    if (!rgba) return false;
    for (size_t i = 0; i < pixels; i++) {
        if (!(i % 16384) && !current(s, job)) { free_payload(s, rgba, bytes); return false; }
        memcpy(rgba + i * 4, data + pos + i * 3, 3);
        rgba[i * 4 + 3] = 255;
    }
    *out = (struct asset_result){.view = {.data = rgba, .size = bytes,
        .width = width, .height = height}, .allocation = bytes};
    return true;
}

static uint16_t little16(const unsigned char *p)
{ return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }

static uint32_t little32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool decode_sound(struct asset_store *s, const struct asset_job *job,
                         const struct asset_result *raw, struct asset_result *out)
{
    const unsigned char *data = raw->view.data, *fmt = NULL, *pcm = NULL;
    size_t size = raw->view.size, pcm_size = 0;
    if (size < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) return false;
    uint32_t riff = little32(data + 4);
    if (riff < 4 || (uint64_t)riff + 8 > size) return false;
    size_t end = (size_t)riff + 8;
    for (size_t pos = 12; pos < end;) {
        if (!current(s, job) || end - pos < 8) return false;
        const unsigned char *chunk = data + pos;
        size_t length = little32(chunk + 4);
        pos += 8;
        if (length > end - pos) return false;
        if (!memcmp(chunk, "fmt ", 4)) {
            if (fmt || length < 16) return false;
            fmt = data + pos;
        } else if (!memcmp(chunk, "data", 4)) {
            if (pcm) return false;
            pcm = data + pos;
            pcm_size = length;
        }
        pos += length;
        if (length & 1) { if (pos == end) return false; pos++; }
    }
    if (!fmt || !pcm || little16(fmt) != 1) return false;
    unsigned channels = little16(fmt + 2), rate = little32(fmt + 4);
    unsigned align = little16(fmt + 12), bits = little16(fmt + 14);
    if (!channels || !rate || (bits != 8 && bits != 16) || align != channels * (bits / 8) ||
        (uint64_t)rate * align != little32(fmt + 8) || pcm_size % align) return false;
    size_t samples = pcm_size / (bits / 8);
    if (samples > ASSET_STORE_MAX_BYTES / sizeof(int16_t)) return false;
    size_t bytes = samples * sizeof(int16_t), allocation = bytes ? bytes : 1;
    int16_t *decoded = allocate_payload(s, job, allocation);
    if (!decoded) return false;
    for (size_t i = 0; i < samples; i++) {
        if (!(i % 32768) && !current(s, job)) { free_payload(s, decoded, allocation); return false; }
        int value = bits == 8 ? ((int)pcm[i] - 128) * 256 : (int)little16(pcm + i * 2);
        if (bits == 16 && value >= 32768) value -= 65536;
        decoded[i] = (int16_t)value;
    }
    *out = (struct asset_result){.view = {.data = decoded, .size = bytes,
        .rate = rate, .channels = channels}, .allocation = allocation};
    return true;
}

static void load_job(struct asset_store *s, const struct asset_job *job)
{
    double cpu = milliseconds(true);
    struct asset_result raw = {0}, result = {0};
    bool ok = current(s, job) && read_file(s, job, &raw);
    if (ok) {
        if (job->type == CHIRKY_ASSET_BLOB) { result = raw; raw = (struct asset_result){0}; }
        else if (job->type == CHIRKY_ASSET_IMAGE) ok = decode_image(s, job, &raw, &result);
        else ok = decode_sound(s, job, &raw, &result);
    }
    free_payload(s, raw.view.data, raw.allocation);
    lock_store(s);
    if (s->epoch == job->epoch) s->cpu += milliseconds(true) - cpu;
    if (current_locked(s, job)) {
        struct asset_slot *slot = find_slot(s, job->handle);
        slot->state = ok ? CHIRKY_ASSET_READY : CHIRKY_ASSET_FAILED;
        if (ok) {
            slot->view = result.view;
            slot->allocation = result.allocation;
            s->resident += result.allocation;
            result = (struct asset_result){0};
        }
        idle_locked(s);
    }
    unlock_store(s);
    free_payload(s, result.view.data, result.allocation);
}

static bool asset_extension(const char *name, enum chirky_asset_type *type)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    *type = CHIRKY_ASSET_BLOB;
    if (!strcmp(ext, ".ppm")) *type = CHIRKY_ASSET_IMAGE;
    else if (!strcmp(ext, ".wav")) *type = CHIRKY_ASSET_SOUND;
    else if (strcmp(ext, ".conf") && strcmp(ext, ".txt") &&
             strcmp(ext, ".sprite") && strcmp(ext, ".robot")) return false;
    return true;
}

static void scan_failed(struct asset_store *s, const struct asset_job *job)
{
    lock_store(s);
    if (current_locked(s, job)) s->prefetch_failed = true;
    unlock_store(s);
}

static void scan_directory(struct asset_store *s, struct asset_job *job, unsigned depth)
{
    if (!current(s, job)) return;
    DIR *dir = opendir(job->path);
    if (!dir) { scan_failed(s, job); return; }
    size_t prefix = strlen(job->path);
    while (current(s, job)) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) scan_failed(s, job); break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        size_t length = strlen(entry->d_name);
        size_t separator = prefix && job->path[prefix - 1] == '/' ? 0 : 1;
        if (prefix + separator + length >= sizeof(job->path)) { scan_failed(s, job); continue; }
        if (separator) job->path[prefix] = '/';
        memcpy(job->path + prefix + separator, entry->d_name, length + 1);
        struct stat st;
        if (lstat(job->path, &st)) scan_failed(s, job);
        else if (S_ISDIR(st.st_mode)) {
            if (depth >= 64) scan_failed(s, job);
            else scan_directory(s, job, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            enum chirky_asset_type type;
            if (asset_extension(entry->d_name, &type)) {
                lock_store(s);
                if (current_locked(s, job)) {
                    if (!request_locked(s, job->path, CHIRKY_ASSET_BLOB, true)) s->prefetch_failed = true;
                    if (type != CHIRKY_ASSET_BLOB && !request_locked(s, job->path, type, true))
                        s->prefetch_failed = true;
                }
                unlock_store(s);
            }
        }
        job->path[prefix] = '\0';
    }
    closedir(dir);
}

/* Called with the lock held; jobs copy paths so clear can free slots at once. */
static bool take_job(struct asset_store *s, struct asset_job *job)
{
    if (s->stop) return false;
    *job = (struct asset_job){.epoch = s->epoch};
    if (s->scan_pending) {
        s->scan_pending = false;
        s->scanning = true;
        strcpy(job->path, s->directory);
        return true;
    }
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) {
        struct asset_slot *slot = &s->slots[i];
        if (slot->path && slot->pending) {
            slot->pending = false;
            job->handle = slot_handle(s, slot);
            job->type = slot->type;
            strcpy(job->path, slot->path);
            return true;
        }
    }
    return false;
}

static void run_job(struct asset_store *s, struct asset_job *job)
{
    if (job->handle) { load_job(s, job); return; }
    double cpu = milliseconds(true);
    scan_directory(s, job, 0);
    lock_store(s);
    if (s->epoch == job->epoch) {
        s->scanning = false;
        s->cpu += milliseconds(true) - cpu;
        idle_locked(s);
    }
    unlock_store(s);
}

#ifndef __EMSCRIPTEN__
static void *asset_worker(void *arg)
{
    struct asset_store *s = arg;
    lock_store(s);
    while (!s->stop) {
        struct asset_job job;
        if (!take_job(s, &job)) { pthread_cond_wait(&s->wake, &s->mutex); continue; }
        unlock_store(s);
        run_job(s, &job);
        lock_store(s);
    }
    unlock_store(s);
    return NULL;
}
#else
static void drain_jobs(struct asset_store *s)
{
    struct asset_job job;
    while (take_job(s, &job)) run_job(s, &job);
}
#endif

struct asset_store *asset_store_create(void)
{
    struct asset_store *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
#ifndef __EMSCRIPTEN__
    if (pthread_mutex_init(&s->mutex, NULL)) { free(s); return NULL; }
    if (pthread_cond_init(&s->wake, NULL)) { pthread_mutex_destroy(&s->mutex); free(s); return NULL; }
    if (pthread_create(&s->worker, NULL, asset_worker, s)) {
        pthread_cond_destroy(&s->wake); pthread_mutex_destroy(&s->mutex); free(s); return NULL;
    }
#endif
    return s;
}

static void clear_locked(struct asset_store *s)
{
    s->epoch++;
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) dispose_locked(s, &s->slots[i]);
    free(s->directory);
    s->directory = NULL;
    s->scan_pending = s->scanning = s->prefetch_failed = s->started = false;
    s->began = s->ended = s->cpu = 0;
    s->bytes_read = 0;
}

void asset_store_clear(struct asset_store *s)
{
    if (!s) return;
    lock_store(s);
    clear_locked(s);
    wake_store(s);
    unlock_store(s);
}

void asset_store_destroy(struct asset_store *s)
{
    if (!s) return;
    lock_store(s);
    s->stop = true;
    clear_locked(s);
    wake_store(s);
    unlock_store(s);
#ifndef __EMSCRIPTEN__
    pthread_join(s->worker, NULL);
    pthread_cond_destroy(&s->wake);
    pthread_mutex_destroy(&s->mutex);
#endif
    free(s);
}

chirky_asset asset_store_request(struct asset_store *s, const char *path, enum chirky_asset_type type)
{
    if (!s || !path || !*path || strnlen(path, ASSET_PATH_MAX) >= ASSET_PATH_MAX ||
        (type != CHIRKY_ASSET_BLOB && type != CHIRKY_ASSET_IMAGE && type != CHIRKY_ASSET_SOUND)) return 0;
    lock_store(s);
    chirky_asset handle = request_locked(s, path, type, false);
    unlock_store(s);
#ifdef __EMSCRIPTEN__
    drain_jobs(s);
#endif
    return handle;
}

enum chirky_asset_state asset_store_state(struct asset_store *s, chirky_asset handle)
{
    if (!s) return CHIRKY_ASSET_FAILED;
    lock_store(s);
    struct asset_slot *slot = find_slot(s, handle);
    enum chirky_asset_state state = slot ? slot->state : CHIRKY_ASSET_FAILED;
    unlock_store(s);
    return state;
}

struct chirky_asset_view asset_store_view(struct asset_store *s, chirky_asset handle)
{
    struct chirky_asset_view view = {0};
    if (!s) return view;
    lock_store(s);
    struct asset_slot *slot = find_slot(s, handle);
    if (slot && slot->state == CHIRKY_ASSET_READY) view = slot->view;
    unlock_store(s);
    return view;
}

bool asset_store_retain(struct asset_store *s, chirky_asset handle)
{
    if (!s) return false;
    lock_store(s);
    struct asset_slot *slot = find_slot(s, handle);
    bool ok = slot && slot->refs < UINT_MAX;
    if (ok) slot->refs++;
    unlock_store(s);
    return ok;
}

void asset_store_release(struct asset_store *s, chirky_asset handle)
{
    if (!s) return;
    lock_store(s);
    struct asset_slot *slot = find_slot(s, handle);
    if (slot && slot->refs) {
        slot->refs--;
        if (!slot->refs && !slot->prefetched) dispose_locked(s, slot);
        idle_locked(s);
    }
    unlock_store(s);
}

bool asset_store_prefetch(struct asset_store *s, const char *directory)
{
    if (!s || !directory || !*directory || strnlen(directory, ASSET_PATH_MAX) >= ASSET_PATH_MAX) return false;
    lock_store(s);
    bool ok;
    if (s->directory) ok = !strcmp(s->directory, directory);
    else {
        s->directory = strdup(directory);
        ok = s->directory != NULL;
        if (ok) { s->scan_pending = true; begin_locked(s); wake_store(s); }
        else s->prefetch_failed = true;
    }
    unlock_store(s);
#ifdef __EMSCRIPTEN__
    drain_jobs(s);
#endif
    return ok;
}

enum chirky_asset_state asset_store_prefetch_state(struct asset_store *s)
{
    if (!s) return CHIRKY_ASSET_FAILED;
    lock_store(s);
    bool loading = s->scan_pending || s->scanning, failed = s->prefetch_failed;
    for (unsigned i = 0; i < ASSET_STORE_SLOTS; i++) {
        struct asset_slot *slot = &s->slots[i];
        if (!slot->path || !slot->prefetched) continue;
        if (slot->state == CHIRKY_ASSET_LOADING) loading = true;
        if (slot->type == CHIRKY_ASSET_BLOB && slot->state == CHIRKY_ASSET_FAILED) failed = true;
    }
    unlock_store(s);
    return loading ? CHIRKY_ASSET_LOADING : failed ? CHIRKY_ASSET_FAILED : CHIRKY_ASSET_READY;
}

struct asset_store_metrics asset_store_get_metrics(struct asset_store *s)
{
    if (!s) return (struct asset_store_metrics){0};
    lock_store(s);
    double end = busy_locked(s) ? milliseconds(false) : s->ended;
    struct asset_store_metrics metrics = {.wall_ms = s->started ? end - s->began : 0,
        .worker_cpu_ms = s->cpu, .bytes_read = s->bytes_read, .bytes_resident = s->resident};
    unlock_store(s);
    return metrics;
}
