#ifndef PINGMOTE_AUDIO_H
#define PINGMOTE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct AudioManager {
    bool initialized;
    bool playing;
} AudioManager;

bool audio_init(AudioManager *manager, char *error, size_t error_capacity);
bool play_audio(AudioManager *manager, const char *path, char *error, size_t error_capacity);
void stop_audio(AudioManager *manager);
void cleanup_audio(AudioManager *manager);

#endif
