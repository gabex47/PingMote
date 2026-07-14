#ifndef PINGMOTE_MICROPHONE_H
#define PINGMOTE_MICROPHONE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct MicrophoneRecorder {
    void *implementation;
    bool initialized;
    bool recording;
} MicrophoneRecorder;

bool microphone_init(
    MicrophoneRecorder *recorder,
    char *error,
    size_t error_capacity
);
bool microphone_start(
    MicrophoneRecorder *recorder,
    char *error,
    size_t error_capacity
);
bool microphone_stop(
    MicrophoneRecorder *recorder,
    const float **samples,
    size_t *sample_count,
    bool *was_truncated,
    char *error,
    size_t error_capacity
);
void microphone_cleanup(MicrophoneRecorder *recorder);

#endif
