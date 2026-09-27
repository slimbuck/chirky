#define _POSIX_C_SOURCE 200809L
#include "../src/director_client.h"
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct fixture {
    int listener;
    volatile bool stop;
    volatile bool saw_event;
};

static void reply(int client, uint32_t acknowledged)
{
    const char state[] = "version=1\nrevision=7\nweather=rain\ngrowth_boost=2\n";
    char header[512];
    int size = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
        "X-Chirky-Ack: %u\r\nX-Chirky-Revision: 7\r\nConnection: close\r\n\r\n",
        strlen(state), acknowledged);
    assert(size > 0 && (size_t)size < sizeof(header));
    assert(send(client, header, (size_t)size, 0) == size);
    assert(send(client, state, strlen(state), 0) == (ssize_t)strlen(state));
}

static ssize_t read_request(int client, char *request, size_t capacity)
{
    size_t used = 0, expected = 0;
    while (used + 1 < capacity) {
        ssize_t received = recv(client, request + used, capacity - used - 1, 0);
        if (received <= 0) return received;
        used += (size_t)received; request[used] = 0;
        char *body = strstr(request, "\r\n\r\n");
        if (!body) continue;
        if (!expected) {
            char *length = strstr(request, "Content-Length:");
            if (!length) return -1;
            expected = (size_t)strtoul(length + strlen("Content-Length:"), NULL, 10);
        }
        size_t header_size = (size_t)(body + 4 - request);
        if (used >= header_size + expected) return (ssize_t)used;
    }
    return -1;
}

static void *serve(void *opaque)
{
    struct fixture *fixture = opaque;
    while (!fixture->stop) {
        int client = accept(fixture->listener, NULL, NULL);
        if (client < 0) break;
        char request[8193];
        ssize_t size = read_request(client, request, sizeof(request));
        if (size > 0) {
            request[size] = 0;
            if (strstr(request, "\"kind\":\"talk\"")) fixture->saw_event = true;
            reply(client, fixture->saw_event ? 1 : 0);
        }
        close(client);
    }
    return NULL;
}

int main(void)
{
    int listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    int reuse = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)));
    assert(!listen(listener, 4));
    socklen_t address_size = sizeof(address);
    assert(!getsockname(listener, (struct sockaddr *)&address, &address_size));
    struct fixture fixture = {.listener = listener};
    pthread_t server; assert(!pthread_create(&server, NULL, serve, &fixture));

    struct director_client *client = director_client_create(); assert(client);
    assert(!director_client_connect(client, "https://127.0.0.1", "game", "world"));
    char url[64]; snprintf(url, sizeof(url), "http://127.0.0.1:%u", ntohs(address.sin_port));
    assert(director_client_connect(client, url, "bramble-hollow", "test-world"));
    const char event[] = "{\"tick\":1,\"day\":1,\"kind\":\"talk\",\"detail\":\"Maple\"}";
    assert(director_client_emit(client, event, strlen(event)));

    char state[256]; uint32_t revision = 0; size_t state_size = 0;
    for (int tries = 0; tries < 300 && (!fixture.saw_event || !state_size); tries++) {
        struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL);
        state_size = director_client_state(client, 0, state, sizeof(state), &revision);
    }
    assert(fixture.saw_event && state_size && revision == 7);
    assert(strstr(state, "weather=rain") && !director_client_state(client, 7, state, sizeof(state), &revision));
    director_client_destroy(client);
    fixture.stop = true; shutdown(listener, SHUT_RDWR); close(listener);
    pthread_join(server, NULL);
    puts("Director client: background HTTP, event acknowledgement and revisioned state passed.");
    return 0;
}
