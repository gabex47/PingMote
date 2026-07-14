#define _POSIX_C_SOURCE 200809L

#include "pingmote/cache.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void write_sample(const char *path)
{
    FILE *const file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite("RIFF1234WAVE", 1U, 12U, file) == 12U);
    assert(fclose(file) == 0);
}

int main(void)
{
    char directory[PINGMOTE_PATH_CAPACITY];
    const int directory_length = snprintf(
        directory,
        sizeof(directory),
        "/tmp/pingmote-cache-test-%ld-%lld",
        (long)getpid(),
        (long long)time(NULL)
    );
    assert(directory_length > 0 && (size_t)directory_length < sizeof(directory));
    assert(mkdir(directory, 0700) == 0);

    CacheStore cache;
    char error[160];
    assert(cache_init_at(&cache, directory, error, sizeof(error)));
    assert(cache.initialized);

    assert(cache_put_reply(&cache, "hello", "yo boss", error, sizeof(error)));
    char reply[64];
    assert(cache_get_reply(&cache, "hello", reply, sizeof(reply)));
    assert(strcmp(reply, "yo boss") == 0);
    assert(!cache_get_reply(&cache, "different", reply, sizeof(reply)));

    char temporary_path[PINGMOTE_PATH_CAPACITY];
    char final_path[PINGMOTE_PATH_CAPACITY];
    assert(cache_get_tts_paths(
        &cache,
        "yo boss",
        temporary_path,
        sizeof(temporary_path),
        final_path,
        sizeof(final_path)
    ));
    write_sample(temporary_path);
    assert(cache_commit_tts(temporary_path, final_path, error, sizeof(error)));

    char cached_path[PINGMOTE_PATH_CAPACITY];
    assert(cache_get_tts(&cache, "yo boss", cached_path, sizeof(cached_path)));
    assert(strcmp(cached_path, final_path) == 0);

    const time_t expired = time(NULL) - (8 * 24 * 60 * 60);
    const struct timespec times[2] = {
        {.tv_sec = expired, .tv_nsec = 0L},
        {.tv_sec = expired, .tv_nsec = 0L}
    };
    assert(utimensat(AT_FDCWD, final_path, times, 0) == 0);
    assert(!cache_get_tts(&cache, "yo boss", cached_path, sizeof(cached_path)));

    char reply_path[PINGMOTE_PATH_CAPACITY];
    const int length = snprintf(
        reply_path,
        sizeof(reply_path),
        "%s/reply_a430d84680aabd0b.cache",
        directory
    );
    assert(length > 0 && (size_t)length < sizeof(reply_path));
    (void)remove(reply_path);
    assert(rmdir(directory) == 0);
    cache_cleanup(&cache);
    return 0;
}
