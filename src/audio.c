#include "pingmote/audio.h"

#include <stdio.h>

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error == NULL || capacity == 0U) {
        return;
    }

    (void)snprintf(error, capacity, "%s", message);
}

bool audio_init(AudioManager *manager, char *error, size_t error_capacity)
{
    if (manager == NULL) {
        set_error(error, error_capacity, "audio manager is required");
        return false;
    }

    manager->initialized = true;
    manager->playing = false;
    set_error(error, error_capacity, "");
    return true;
}

bool play_audio(AudioManager *manager, const char *path, char *error, size_t error_capacity)
{
    if (manager == NULL || !manager->initialized) {
        set_error(error, error_capacity, "audio manager is not initialized");
        return false;
    }

    if (path == NULL || path[0] == '\0') {
        set_error(error, error_capacity, "audio path is required");
        return false;
    }

    /* Playback is intentionally silent until miniaudio is wired into this boundary. */
    manager->playing = true;
    set_error(error, error_capacity, "");
    return true;
}

void stop_audio(AudioManager *manager)
{
    if (manager != NULL) {
        manager->playing = false;
    }
}

void cleanup_audio(AudioManager *manager)
{
    if (manager == NULL) {
        return;
    }

    manager->playing = false;
    manager->initialized = false;
}
