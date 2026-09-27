#ifndef CHIRKY_DIRECTOR_CLIENT_H
#define CHIRKY_DIRECTOR_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct director_client;

struct director_client *director_client_create(void);
void director_client_destroy(struct director_client *client);

bool director_client_connect(struct director_client *client, const char *url,
                             const char *game, const char *world);
void director_client_disconnect(struct director_client *client);
bool director_client_emit(struct director_client *client, const char *json,
                          size_t size);

/* Copies a newer NUL-terminated state document and returns its byte count,
   excluding the terminator. Zero means no newer complete state is available. */
size_t director_client_state(struct director_client *client,
                             uint32_t after_revision, char *text,
                             size_t capacity, uint32_t *revision);

#endif
