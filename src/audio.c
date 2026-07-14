#include "pingmote/audio.h"

#include <miniaudio.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PINGMOTE_AUDIO_PATH_CAPACITY 1024U

typedef struct AudioImplementation {
    ma_engine engine;
    ma_sound sound;
    AudioPlaybackCallback callback;
    void *callback_user_data;
    atomic_bool playback_ended;
    bool engine_initialized;
    bool sound_initialized;
    char current_path[PINGMOTE_AUDIO_PATH_CAPACITY];
} AudioImplementation;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static void set_miniaudio_error(
    char *error,
    size_t capacity,
    const char *operation,
    ma_result result
)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(
            error,
            capacity,
            "%s: %s",
            operation,
            ma_result_description(result)
        );
    }
}

static void playback_end_callback(void *user_data, ma_sound *sound)
{
    (void)sound;
    AudioImplementation *const implementation = user_data;
    if (implementation != NULL) {
        atomic_store_explicit(&implementation->playback_ended, true, memory_order_release);
    }
}

static void finish_playback(AudioManager *manager, bool completed)
{
    AudioImplementation *const implementation = manager->implementation;
    if (implementation == NULL || !implementation->sound_initialized) {
        manager->playing = false;
        return;
    }

    char finished_path[PINGMOTE_AUDIO_PATH_CAPACITY];
    (void)snprintf(
        finished_path,
        sizeof(finished_path),
        "%s",
        implementation->current_path
    );

    ma_sound_uninit(&implementation->sound);
    implementation->sound_initialized = false;
    implementation->current_path[0] = '\0';
    atomic_store_explicit(&implementation->playback_ended, false, memory_order_release);
    manager->playing = false;

    if (implementation->callback != NULL) {
        implementation->callback(
            implementation->callback_user_data,
            finished_path,
            completed
        );
    }
}

bool audio_init(AudioManager *manager, char *error, size_t error_capacity)
{
    if (manager == NULL) {
        set_error(error, error_capacity, "audio manager is required");
        return false;
    }

    *manager = (AudioManager){0};
    AudioImplementation *const implementation = calloc(1U, sizeof(*implementation));
    if (implementation == NULL) {
        set_error(error, error_capacity, "failed to allocate audio manager");
        return false;
    }

    atomic_init(&implementation->playback_ended, false);
    const ma_result result = ma_engine_init(NULL, &implementation->engine);
    if (result != MA_SUCCESS) {
        set_miniaudio_error(error, error_capacity, "failed to initialize audio", result);
        free(implementation);
        return false;
    }

    implementation->engine_initialized = true;
    manager->implementation = implementation;
    manager->initialized = true;
    manager->playing = false;
    set_error(error, error_capacity, "");
    return true;
}

bool play_audio(AudioManager *manager, const char *path, char *error, size_t error_capacity)
{
    if (manager == NULL || !manager->initialized || manager->implementation == NULL) {
        set_error(error, error_capacity, "audio manager is not initialized");
        return false;
    }

    if (path == NULL || path[0] == '\0') {
        set_error(error, error_capacity, "audio path is required");
        return false;
    }

    if (strlen(path) >= PINGMOTE_AUDIO_PATH_CAPACITY) {
        set_error(error, error_capacity, "audio path is too long");
        return false;
    }

    if (manager->playing) {
        stop_audio(manager);
    }

    AudioImplementation *const implementation = manager->implementation;
    const ma_uint32 flags = MA_SOUND_FLAG_ASYNC
        | MA_SOUND_FLAG_DECODE
        | MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_result result = ma_sound_init_from_file(
        &implementation->engine,
        path,
        flags,
        NULL,
        NULL,
        &implementation->sound
    );
    if (result != MA_SUCCESS) {
        set_miniaudio_error(error, error_capacity, "failed to load audio", result);
        return false;
    }

    implementation->sound_initialized = true;
    (void)snprintf(
        implementation->current_path,
        sizeof(implementation->current_path),
        "%s",
        path
    );
    atomic_store_explicit(&implementation->playback_ended, false, memory_order_release);

    result = ma_sound_set_end_callback(
        &implementation->sound,
        playback_end_callback,
        implementation
    );
    if (result == MA_SUCCESS) {
        result = ma_sound_start(&implementation->sound);
    }
    if (result != MA_SUCCESS) {
        set_miniaudio_error(error, error_capacity, "failed to start audio", result);
        ma_sound_uninit(&implementation->sound);
        implementation->sound_initialized = false;
        implementation->current_path[0] = '\0';
        return false;
    }

    manager->playing = true;
    set_error(error, error_capacity, "");
    return true;
}

void audio_set_playback_callback(
    AudioManager *manager,
    AudioPlaybackCallback callback,
    void *user_data
)
{
    if (manager == NULL || manager->implementation == NULL) {
        return;
    }

    AudioImplementation *const implementation = manager->implementation;
    implementation->callback = callback;
    implementation->callback_user_data = user_data;
}

void audio_update(AudioManager *manager)
{
    if (manager == NULL || !manager->initialized || manager->implementation == NULL) {
        return;
    }

    AudioImplementation *const implementation = manager->implementation;
    if (implementation->sound_initialized
        && atomic_exchange_explicit(
            &implementation->playback_ended,
            false,
            memory_order_acq_rel)) {
        finish_playback(manager, true);
    }
}

bool audio_is_playing(const AudioManager *manager)
{
    return manager != NULL && manager->initialized && manager->playing;
}

void stop_audio(AudioManager *manager)
{
    if (manager == NULL || !manager->initialized || manager->implementation == NULL) {
        return;
    }

    AudioImplementation *const implementation = manager->implementation;
    if (!implementation->sound_initialized) {
        manager->playing = false;
        return;
    }

    (void)ma_sound_stop(&implementation->sound);
    finish_playback(manager, false);
}

void cleanup_audio(AudioManager *manager)
{
    if (manager == NULL || manager->implementation == NULL) {
        return;
    }

    AudioImplementation *const implementation = manager->implementation;
    if (implementation->sound_initialized) {
        (void)ma_sound_stop(&implementation->sound);
        ma_sound_uninit(&implementation->sound);
        implementation->sound_initialized = false;
    }
    if (implementation->engine_initialized) {
        ma_engine_uninit(&implementation->engine);
        implementation->engine_initialized = false;
    }

    free(implementation);
    *manager = (AudioManager){0};
}
