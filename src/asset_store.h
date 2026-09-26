#ifndef CHIRKY_ASSET_STORE_H
#define CHIRKY_ASSET_STORE_H

#include <stdint.h>
#include "chirky.h"

#define ASSET_STORE_SLOTS 1024u
#define ASSET_STORE_MAX_BYTES (64u * 1024u * 1024u)

struct asset_store;
struct asset_store_metrics {
    double wall_ms, worker_cpu_ms;
    uint64_t bytes_read;
    size_t bytes_resident;
};

/* Native calls are thread-safe; one pthread does all filesystem I/O/decoding.
   Emscripten needs no pthread: request/prefetch run synchronously on files that
   the host has already asynchronously populated in its virtual filesystem.
   No game callbacks, game state, audio devices or GPU objects are accessed. */
struct asset_store *asset_store_create(void);
void asset_store_destroy(struct asset_store *store);

/* Cache keys are exact path strings plus type; relative paths use the process
   working directory, which must remain fixed while work is outstanding. Paths
   may be outside the prefetch directory. Zero means invalid input/exhaustion.
   Failed reads/decodes keep a valid FAILED handle until released or cleared. */
chirky_asset asset_store_request(struct asset_store *store, const char *path,
                                 enum chirky_asset_type type);
enum chirky_asset_state asset_store_state(struct asset_store *store, chirky_asset asset);
/* Empty unless READY. BLOB size excludes its extra NUL; IMAGE is top-down RGBA8;
   SOUND is native-endian signed PCM16, interleaved, with size measured in bytes.
   A view is borrowed until the last caller reference is released or clear.
   Callers must coordinate clear/destroy with readers of borrowed views, and
   must not call any store function concurrently with destroy. */
struct chirky_asset_view asset_store_view(struct asset_store *store, chirky_asset asset);
/* Acquire another caller reference, e.g. to pin sound data during playback.
   Returns false for invalid/stale handles or reference-count exhaustion. */
bool asset_store_retain(struct asset_store *store, chirky_asset asset);
void asset_store_release(struct asset_store *store, chirky_asset asset);

/* Invalidates every handle, releases caller/prefetch refs, cancels old jobs and
   resets metrics. Does not wait for old I/O. Destroy stops and joins the worker.
   Generation exhaustion retires a slot rather than reviving a stale handle.
   The 64 MiB limit covers resident and in-flight payload allocations, including
   BLOB terminators. Fixed metadata and bounded path/directory storage are extra. */
void asset_store_clear(struct asset_store *store);

/* One directory per clear; repeating the same directory is idempotent. Native
   traversal is asynchronous; browser traversal is synchronous. Regular files
   ending in .conf/.txt/.sprite/.robot/.ppm/.pam/.wav are retained as BLOB, plus
   IMAGE for .ppm/.pam and SOUND for .wav. Extensions are case-sensitive. Symlinks are not
   traversed; directory nesting is limited to 64 and paths to 4095 bytes.
   Returns false for invalid input or an already selected different directory.
   Prefetch owner refs survive caller releases until clear/destroy. State is
   READY before any prefetch, LOADING until traversal and owned loads finish,
   then FAILED for traversal/scheduling/BLOB failures. Decoder-only failures and
   unrelated optional requests do not fail the bundle. */
bool asset_store_prefetch(struct asset_store *store, const char *directory);
enum chirky_asset_state asset_store_prefetch_state(struct asset_store *store);
/* Current clear-to-clear session: wall time from first work until last idle,
   worker thread CPU time (calling-thread CPU on browser), actual bytes read,
   and currently resident payload bytes including BLOB terminators. */
struct asset_store_metrics asset_store_get_metrics(struct asset_store *store);

#endif
