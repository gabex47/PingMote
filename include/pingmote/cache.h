#ifndef PINGMOTE_CACHE_H
#define PINGMOTE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

#define PINGMOTE_PATH_CAPACITY 1024U

typedef struct CacheStore {
    char root[PINGMOTE_PATH_CAPACITY];
    bool initialized;
} CacheStore;

bool cache_init(CacheStore *cache, char *error, size_t error_capacity);
bool cache_init_at(
    CacheStore *cache,
    const char *root,
    char *error,
    size_t error_capacity
);
bool cache_get_reply(
    const CacheStore *cache,
    const char *prompt,
    char *reply,
    size_t reply_capacity
);
bool cache_put_reply(
    const CacheStore *cache,
    const char *prompt,
    const char *reply,
    char *error,
    size_t error_capacity
);
bool cache_get_tts(
    const CacheStore *cache,
    const char *reply,
    char *path,
    size_t path_capacity
);
bool cache_get_tts_paths(
    const CacheStore *cache,
    const char *reply,
    char *temporary_path,
    size_t temporary_capacity,
    char *final_path,
    size_t final_capacity
);
bool cache_commit_tts(
    const char *temporary_path,
    const char *final_path,
    char *error,
    size_t error_capacity
);
void cache_cleanup(CacheStore *cache);

#endif
