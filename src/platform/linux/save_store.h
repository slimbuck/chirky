#ifndef CHIRKY_SAVE_STORE_H
#define CHIRKY_SAVE_STORE_H
#include <stdbool.h>
#include <stddef.h>
size_t save_store_read(void *unused,const char *game,const char *key,void *data,size_t capacity);
bool save_store_write(void *unused,const char *game,const char *key,const void *data,size_t size);
#endif
