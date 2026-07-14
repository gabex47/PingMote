#ifndef PINGMOTE_AUDIO_H
#define PINGMOTE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

typedef void (*AudioPlaybackCallback)(
    void *user_data,
    const char *path,
    bool completed
);

typedef struct AudioManager {
    void *implementation;
    bool initialized;
    bool playing;
} AudioManager;

bool audio_init(AudioManager *manager, char *error, size_t error_capacity);
bool play_audio(AudioManager *manager, const char *path, char *error, size_t error_capacity);
void audio_set_playback_callback(
    AudioManager *manager,
    AudioPlaybackCallback callback,
    void *user_data
);
void audio_update(AudioManager *manager);
bool audio_is_playing(const AudioManager *manager);
void stop_audio(AudioManager *manager);
void cleanup_audio(AudioManager *manager);

#endif
