#include "pingmote/cache.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#define pingmote_mkdir(path) _mkdir(path)
#else
#include <unistd.h>
#define pingmote_mkdir(path) mkdir((path), 0700)
#endif

#define CACHE_LIFETIME_SECONDS (7LL * 24LL * 60LL * 60LL)
#define CACHE_MAGIC "PMCACHE1"

typedef struct CacheHeader {
    char magic[9];
    int64_t created_at;
    uint32_t prompt_length;
    uint32_t reply_length;
} CacheHeader;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static uint64_t hash_text(const char *text)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        hash ^= (uint64_t)(unsigned char)text[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool build_path(
    const CacheStore *cache,
    const char *prefix,
    const char *text,
    const char *suffix,
    char *path,
    size_t capacity
)
{
    if (cache == NULL || !cache->initialized || prefix == NULL || text == NULL
        || suffix == NULL || path == NULL || capacity == 0U) {
        return false;
    }

    const int length = snprintf(
        path,
        capacity,
        "%s/%s_%016" PRIx64 "%s",
        cache->root,
        prefix,
        hash_text(text),
        suffix
    );
    return length >= 0 && (size_t)length < capacity;
}

static bool file_is_fresh(const char *path)
{
    struct stat info;
    if (path == NULL || stat(path, &info) != 0) {
        return false;
    }

    const time_t now = time(NULL);
    return now != (time_t)-1
        && info.st_size > 0
        && difftime(now, info.st_mtime) <= (double)CACHE_LIFETIME_SECONDS;
}

static bool is_cache_file(const char *name)
{
    return strncmp(name, "reply_", 6U) == 0
        || strncmp(name, "tts_", 4U) == 0;
}

static void prune_expired(const CacheStore *cache)
{
    DIR *const directory = opendir(cache->root);
    if (directory == NULL) {
        return;
    }

    const time_t now = time(NULL);
    struct dirent *entry = NULL;
    while ((entry = readdir(directory)) != NULL) {
        if (!is_cache_file(entry->d_name)) {
            continue;
        }

        char path[PINGMOTE_PATH_CAPACITY];
        const int length = snprintf(path, sizeof(path), "%s/%s", cache->root, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            continue;
        }

        struct stat info;
        if (stat(path, &info) == 0
            && (strstr(entry->d_name, ".part") != NULL
                || now == (time_t)-1
                || difftime(now, info.st_mtime) > (double)CACHE_LIFETIME_SECONDS)) {
            (void)remove(path);
        }
    }
    (void)closedir(directory);
}

bool cache_init_at(
    CacheStore *cache,
    const char *root,
    char *error,
    size_t error_capacity
)
{
    if (cache == NULL || root == NULL || root[0] == '\0') {
        set_error(error, error_capacity, "cache path is required");
        return false;
    }

    *cache = (CacheStore){0};
    if (strlen(root) >= sizeof(cache->root)) {
        set_error(error, error_capacity, "cache path is too long");
        return false;
    }

    if (pingmote_mkdir(root) != 0 && errno != EEXIST) {
        set_error(error, error_capacity, "failed to create cache directory");
        return false;
    }

    struct stat info;
    if (stat(root, &info) != 0 || !S_ISDIR(info.st_mode)) {
        set_error(error, error_capacity, "cache path is not a directory");
        return false;
    }

    (void)snprintf(cache->root, sizeof(cache->root), "%s", root);
    cache->initialized = true;
#if !defined(_WIN32)
    (void)chmod(cache->root, 0700);
#endif
    prune_expired(cache);
    set_error(error, error_capacity, "");
    return true;
}

bool cache_init(CacheStore *cache, char *error, size_t error_capacity)
{
    const char *const home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        set_error(error, error_capacity, "home directory is unavailable");
        return false;
    }

    char root[PINGMOTE_PATH_CAPACITY];
#if defined(__APPLE__)
    const int length = snprintf(
        root,
        sizeof(root),
        "%s/Library/Caches/com.pingmote.desktop",
        home
    );
#else
    const int length = snprintf(root, sizeof(root), "%s/.cache/pingmote", home);
#endif
    if (length < 0 || (size_t)length >= sizeof(root)) {
        set_error(error, error_capacity, "cache path is too long");
        return false;
    }
    return cache_init_at(cache, root, error, error_capacity);
}

bool cache_get_reply(
    const CacheStore *cache,
    const char *prompt,
    char *reply,
    size_t reply_capacity
)
{
    if (prompt == NULL || reply == NULL || reply_capacity == 0U) {
        return false;
    }
    reply[0] = '\0';

    char path[PINGMOTE_PATH_CAPACITY];
    if (!build_path(cache, "reply", prompt, ".cache", path, sizeof(path))
        || !file_is_fresh(path)) {
        return false;
    }

    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    CacheHeader header = {0};
    const size_t prompt_length = strlen(prompt);
    bool valid = fread(&header, sizeof(header), 1U, file) == 1U
        && memcmp(header.magic, CACHE_MAGIC, sizeof(header.magic)) == 0
        && header.prompt_length == prompt_length
        && header.reply_length > 0U
        && (size_t)header.reply_length < reply_capacity;

    char stored_prompt[2049];
    if (valid && prompt_length < sizeof(stored_prompt)) {
        valid = fread(stored_prompt, 1U, prompt_length, file) == prompt_length;
        stored_prompt[prompt_length] = '\0';
        valid = valid && strcmp(stored_prompt, prompt) == 0;
    } else {
        valid = false;
    }

    if (valid) {
        valid = fread(reply, 1U, header.reply_length, file) == header.reply_length;
        reply[header.reply_length] = '\0';
    }

    valid = fclose(file) == 0 && valid;
    if (!valid) {
        reply[0] = '\0';
        (void)remove(path);
    }
    return valid;
}

bool cache_put_reply(
    const CacheStore *cache,
    const char *prompt,
    const char *reply,
    char *error,
    size_t error_capacity
)
{
    if (prompt == NULL || reply == NULL || prompt[0] == '\0' || reply[0] == '\0'
        || strlen(prompt) > UINT32_MAX || strlen(reply) > UINT32_MAX) {
        set_error(error, error_capacity, "valid prompt and reply are required");
        return false;
    }

    char path[PINGMOTE_PATH_CAPACITY];
    char temporary_path[PINGMOTE_PATH_CAPACITY];
    if (!build_path(cache, "reply", prompt, ".cache", path, sizeof(path))
        || !build_path(cache, "reply", prompt, ".part", temporary_path, sizeof(temporary_path))) {
        set_error(error, error_capacity, "cache path is unavailable");
        return false;
    }

    FILE *const file = fopen(temporary_path, "wb");
    if (file == NULL) {
        set_error(error, error_capacity, "failed to create reply cache");
        return false;
    }
#if !defined(_WIN32)
    (void)chmod(temporary_path, 0600);
#endif

    const size_t prompt_length = strlen(prompt);
    const size_t reply_length = strlen(reply);
    const CacheHeader header = {
        .magic = CACHE_MAGIC,
        .created_at = (int64_t)time(NULL),
        .prompt_length = (uint32_t)prompt_length,
        .reply_length = (uint32_t)reply_length
    };
    bool success = fwrite(&header, sizeof(header), 1U, file) == 1U
        && fwrite(prompt, 1U, prompt_length, file) == prompt_length
        && fwrite(reply, 1U, reply_length, file) == reply_length
        && fflush(file) == 0;
    success = fclose(file) == 0 && success;
    if (success) {
        success = rename(temporary_path, path) == 0;
    }
    if (!success) {
        (void)remove(temporary_path);
        set_error(error, error_capacity, "failed to save reply cache");
        return false;
    }

    set_error(error, error_capacity, "");
    return true;
}

bool cache_get_tts_paths(
    const CacheStore *cache,
    const char *reply,
    char *temporary_path,
    size_t temporary_capacity,
    char *final_path,
    size_t final_capacity
)
{
    return reply != NULL && reply[0] != '\0'
        && build_path(cache, "tts", reply, ".part", temporary_path, temporary_capacity)
        && build_path(cache, "tts", reply, ".wav", final_path, final_capacity);
}

bool cache_get_tts(
    const CacheStore *cache,
    const char *reply,
    char *path,
    size_t path_capacity
)
{
    if (!build_path(cache, "tts", reply, ".wav", path, path_capacity)) {
        return false;
    }
    if (!file_is_fresh(path)) {
        (void)remove(path);
        path[0] = '\0';
        return false;
    }
    return true;
}

bool cache_commit_tts(
    const char *temporary_path,
    const char *final_path,
    char *error,
    size_t error_capacity
)
{
    if (temporary_path == NULL || final_path == NULL
        || temporary_path[0] == '\0' || final_path[0] == '\0') {
        set_error(error, error_capacity, "speech cache paths are required");
        return false;
    }
    if (rename(temporary_path, final_path) != 0) {
        (void)remove(temporary_path);
        set_error(error, error_capacity, "failed to finish speech cache");
        return false;
    }
#if !defined(_WIN32)
    (void)chmod(final_path, 0600);
#endif
    set_error(error, error_capacity, "");
    return true;
}

void cache_cleanup(CacheStore *cache)
{
    if (cache != NULL) {
        *cache = (CacheStore){0};
    }
}
