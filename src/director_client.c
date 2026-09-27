#define _POSIX_C_SOURCE 200809L
#include "director_client.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DIRECTOR_EVENTS 32
#define DIRECTOR_EVENT_BYTES 512
#define DIRECTOR_STATE_BYTES 4096
#define DIRECTOR_RESPONSE_BYTES 8192
#define DIRECTOR_REQUEST_BYTES (DIRECTOR_EVENTS * (DIRECTOR_EVENT_BYTES + 48) + 2048)

struct queued_event {
    uint32_t sequence;
    size_t size;
    char json[DIRECTOR_EVENT_BYTES];
};

struct director_client {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t worker;
    bool worker_started;
    bool stop;
    bool configured;
    char host[256];
    char port[8];
    char target[384];
    char game[64];
    char world[64];
    char session[96];
    struct queued_event events[DIRECTOR_EVENTS];
    size_t event_count;
    uint32_t next_sequence;
    uint32_t revision;
    size_t state_size;
    char state[DIRECTOR_STATE_BYTES];
};

struct sync_result {
    bool ok;
    uint32_t acknowledged;
    uint32_t revision;
    size_t state_size;
    char state[DIRECTOR_STATE_BYTES];
};

static bool identifier(const char *value, size_t capacity)
{
    size_t length = value ? strlen(value) : 0;
    if (!length || length >= capacity) return false;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

static bool endpoint(const char *url, char *host, size_t host_capacity,
                     char *port, size_t port_capacity,
                     char *target, size_t target_capacity)
{
    static const char prefix[] = "http://";
    if (!url || strncmp(url, prefix, sizeof(prefix) - 1)) return false;
    const char *authority = url + sizeof(prefix) - 1;
    const char *slash = strchr(authority, '/');
    size_t authority_length = slash ? (size_t)(slash - authority) : strlen(authority);
    if (!authority_length || authority_length >= 300) return false;
    char copy[300];
    memcpy(copy, authority, authority_length); copy[authority_length] = 0;
    char *colon = strrchr(copy, ':');
    const char *port_value = "80";
    if (colon) {
        *colon++ = 0;
        if (!*colon) return false;
        for (const char *c = colon; *c; c++) if (!isdigit((unsigned char)*c)) return false;
        long number = strtol(colon, NULL, 10);
        if (number < 1 || number > 65535) return false;
        port_value = colon;
    }
    if (!*copy || strlen(copy) >= host_capacity || strlen(port_value) >= port_capacity) return false;
    const char *base = slash ? slash : "";
    size_t base_length = strlen(base);
    while (base_length && base[base_length - 1] == '/') base_length--;
    int written = snprintf(target, target_capacity, "%.*s/v1/sync", (int)base_length, base);
    if (written < 0 || (size_t)written >= target_capacity) return false;
    snprintf(host, host_capacity, "%s", copy);
    snprintf(port, port_capacity, "%s", port_value);
    return true;
}

static bool send_all(int socket_fd, const char *data, size_t size)
{
    while (size) {
        ssize_t sent = send(socket_fd, data, size, 0);
        if (sent <= 0) return false;
        data += sent; size -= (size_t)sent;
    }
    return true;
}

static bool read_header_uint(const char *headers, const char *name, uint32_t *value)
{
    size_t name_length = strlen(name);
    for (const char *line = strstr(headers, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        line += 2;
        if (!*line || (line[0] == '\r' && line[1] == '\n')) break;
        if (strncasecmp(line, name, name_length) || line[name_length] != ':') continue;
        const char *number = line + name_length + 1;
        while (*number == ' ' || *number == '\t') number++;
        char *end = NULL;
        unsigned long parsed = strtoul(number, &end, 10);
        if (end == number || parsed > UINT32_MAX) return false;
        *value = (uint32_t)parsed;
        return true;
    }
    return false;
}

static struct sync_result request_state(const char *host, const char *port,
                                        const char *target, const char *body,
                                        size_t body_size)
{
    struct sync_result result = {0};
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, port, &hints, &addresses)) return result;
    int socket_fd = -1;
    for (struct addrinfo *address = addresses; address; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) continue;
        struct timeval timeout = {.tv_sec = 2};
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (!connect(socket_fd, address->ai_addr, address->ai_addrlen)) break;
        close(socket_fd); socket_fd = -1;
    }
    freeaddrinfo(addresses);
    if (socket_fd < 0) return result;

    char header[1024];
    int header_size = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\nHost: %s:%s\r\nContent-Type: application/json\r\n"
        "Accept: text/plain\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        target, host, port, body_size);
    if (header_size <= 0 || (size_t)header_size >= sizeof(header) ||
        !send_all(socket_fd, header, (size_t)header_size) || !send_all(socket_fd, body, body_size)) {
        close(socket_fd); return result;
    }
    char response[DIRECTOR_RESPONSE_BYTES + 1];
    size_t used = 0;
    while (used < DIRECTOR_RESPONSE_BYTES) {
        ssize_t received = recv(socket_fd, response + used, DIRECTOR_RESPONSE_BYTES - used, 0);
        if (received == 0) break;
        if (received < 0) { used = 0; break; }
        used += (size_t)received;
    }
    close(socket_fd);
    if (!used) return result;
    response[used] = 0;
    if (strncmp(response, "HTTP/1.1 200 ", 13) && strncmp(response, "HTTP/1.0 200 ", 13)) return result;
    char *body_start = strstr(response, "\r\n\r\n");
    if (!body_start) return result;
    body_start += 4;
    size_t available = used - (size_t)(body_start - response);
    uint32_t content_length = 0;
    if (!read_header_uint(response, "Content-Length", &content_length) ||
        content_length > available || content_length >= sizeof(result.state) ||
        !read_header_uint(response, "X-Chirky-Ack", &result.acknowledged) ||
        !read_header_uint(response, "X-Chirky-Revision", &result.revision)) return result;
    memcpy(result.state, body_start, content_length);
    result.state[content_length] = 0;
    result.state_size = content_length;
    result.ok = true;
    return result;
}

static size_t build_body(char *body, size_t capacity, const char *game,
                         const char *world, const char *session,
                         const struct queued_event *events, size_t event_count,
                         uint32_t revision)
{
    int written = snprintf(body, capacity,
        "{\"protocol\":1,\"game\":\"%s\",\"world\":\"%s\",\"session\":\"%s\","
        "\"last_revision\":%u,\"events\":[",
        game, world, session, revision);
    if (written < 0 || (size_t)written >= capacity) return 0;
    size_t used = (size_t)written;
    for (size_t i = 0; i < event_count; i++) {
        written = snprintf(body + used, capacity - used,
                           "%s{\"sequence\":%u,\"event\":",
                           i ? "," : "", events[i].sequence);
        if (written < 0 || (size_t)written >= capacity - used) return 0;
        used += (size_t)written;
        if (events[i].size >= capacity - used) return 0;
        memcpy(body + used, events[i].json, events[i].size); used += events[i].size;
        if (used + 2 >= capacity) return 0;
        body[used++] = '}';
    }
    if (used + 3 > capacity) return 0;
    body[used++] = ']'; body[used++] = '}'; body[used] = 0;
    return used;
}

static void deadline_after(struct timespec *deadline, long milliseconds)
{
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_nsec += milliseconds * 1000000L;
    deadline->tv_sec += deadline->tv_nsec / 1000000000L;
    deadline->tv_nsec %= 1000000000L;
}

static void *worker_main(void *opaque)
{
    struct director_client *client = opaque;
    pthread_mutex_lock(&client->mutex);
    while (!client->stop) {
        while (!client->stop && !client->configured)
            pthread_cond_wait(&client->changed, &client->mutex);
        if (client->stop) break;
        char host[sizeof(client->host)], port[sizeof(client->port)], target[sizeof(client->target)];
        char game[sizeof(client->game)], world[sizeof(client->world)], session[sizeof(client->session)];
        snprintf(host, sizeof(host), "%s", client->host);
        snprintf(port, sizeof(port), "%s", client->port);
        snprintf(target, sizeof(target), "%s", client->target);
        snprintf(game, sizeof(game), "%s", client->game);
        snprintf(world, sizeof(world), "%s", client->world);
        snprintf(session, sizeof(session), "%s", client->session);
        struct queued_event events[DIRECTOR_EVENTS];
        size_t event_count = client->event_count;
        memcpy(events, client->events, event_count * sizeof(*events));
        uint32_t revision = client->revision;
        char body[DIRECTOR_REQUEST_BYTES];
        size_t body_size = build_body(body, sizeof(body), game, world, session,
                                      events, event_count, revision);
        pthread_mutex_unlock(&client->mutex);
        struct sync_result result = body_size ? request_state(host, port, target, body, body_size) : (struct sync_result){0};
        pthread_mutex_lock(&client->mutex);
        if (result.ok && client->configured && !strcmp(host, client->host) &&
            !strcmp(port, client->port) && !strcmp(session, client->session)) {
            size_t remove = 0;
            while (remove < client->event_count &&
                   client->events[remove].sequence <= result.acknowledged) remove++;
            if (remove) {
                memmove(client->events, client->events + remove,
                        (client->event_count - remove) * sizeof(*client->events));
                client->event_count -= remove;
            }
            if (result.revision > client->revision && result.state_size) {
                memcpy(client->state, result.state, result.state_size + 1);
                client->state_size = result.state_size;
                client->revision = result.revision;
            }
        }
        struct timespec deadline;
        deadline_after(&deadline, 1000);
        if (!client->stop) pthread_cond_timedwait(&client->changed, &client->mutex, &deadline);
    }
    pthread_mutex_unlock(&client->mutex);
    return NULL;
}

struct director_client *director_client_create(void)
{
    struct director_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    if (pthread_mutex_init(&client->mutex, NULL)) { free(client); return NULL; }
    if (pthread_cond_init(&client->changed, NULL)) {
        pthread_mutex_destroy(&client->mutex); free(client); return NULL;
    }
    client->next_sequence = 1;
    if (pthread_create(&client->worker, NULL, worker_main, client)) {
        pthread_cond_destroy(&client->changed); pthread_mutex_destroy(&client->mutex);
        free(client); return NULL;
    }
    client->worker_started = true;
    return client;
}

void director_client_destroy(struct director_client *client)
{
    if (!client) return;
    pthread_mutex_lock(&client->mutex);
    client->stop = true;
    pthread_cond_signal(&client->changed);
    pthread_mutex_unlock(&client->mutex);
    if (client->worker_started) pthread_join(client->worker, NULL);
    pthread_cond_destroy(&client->changed);
    pthread_mutex_destroy(&client->mutex);
    free(client);
}

bool director_client_connect(struct director_client *client, const char *url,
                             const char *game, const char *world)
{
    if (!client || !identifier(game, sizeof(client->game)) ||
        !identifier(world, sizeof(client->world))) return false;
    char host[sizeof(client->host)], port[sizeof(client->port)], target[sizeof(client->target)];
    if (!endpoint(url, host, sizeof(host), port, sizeof(port), target, sizeof(target))) return false;
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    pthread_mutex_lock(&client->mutex);
    snprintf(client->host, sizeof(client->host), "%s", host);
    snprintf(client->port, sizeof(client->port), "%s", port);
    snprintf(client->target, sizeof(client->target), "%s", target);
    snprintf(client->game, sizeof(client->game), "%s", game);
    snprintf(client->world, sizeof(client->world), "%s", world);
    snprintf(client->session, sizeof(client->session), "%s-%ld-%ld", game,
             (long)getpid(), (long)now.tv_nsec);
    client->event_count = client->state_size = 0;
    client->next_sequence = 1;
    client->revision = 0;
    client->configured = true;
    pthread_cond_signal(&client->changed);
    pthread_mutex_unlock(&client->mutex);
    return true;
}

void director_client_disconnect(struct director_client *client)
{
    if (!client) return;
    pthread_mutex_lock(&client->mutex);
    client->configured = false;
    client->event_count = client->state_size = 0;
    client->revision = 0;
    pthread_mutex_unlock(&client->mutex);
}

bool director_client_emit(struct director_client *client, const char *json, size_t size)
{
    if (!client || !json || size < 2 || size >= DIRECTOR_EVENT_BYTES ||
        json[0] != '{' || json[size - 1] != '}') return false;
    pthread_mutex_lock(&client->mutex);
    bool accepted = client->configured && client->event_count < DIRECTOR_EVENTS;
    if (accepted) {
        struct queued_event *event = &client->events[client->event_count++];
        event->sequence = client->next_sequence++;
        event->size = size;
        memcpy(event->json, json, size); event->json[size] = 0;
        pthread_cond_signal(&client->changed);
    }
    pthread_mutex_unlock(&client->mutex);
    return accepted;
}

size_t director_client_state(struct director_client *client,
                             uint32_t after_revision, char *text,
                             size_t capacity, uint32_t *revision)
{
    if (!client || !text || !capacity || !revision) return 0;
    pthread_mutex_lock(&client->mutex);
    size_t size = 0;
    if (client->configured && client->revision > after_revision &&
        client->state_size + 1 <= capacity) {
        size = client->state_size;
        memcpy(text, client->state, size + 1);
        *revision = client->revision;
    }
    pthread_mutex_unlock(&client->mutex);
    return size;
}
