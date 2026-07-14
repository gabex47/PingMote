#ifndef PINGMOTE_SPEECH_H
#define PINGMOTE_SPEECH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

typedef struct SpeechRecognizer {
    void *context;
    bool initialized;
} SpeechRecognizer;

bool speech_recognizer_init(
    SpeechRecognizer *recognizer,
    const char *model_path,
    char *error,
    size_t error_capacity
);
bool speech_transcribe(
    SpeechRecognizer *recognizer,
    const float *samples,
    size_t sample_count,
    char *transcription,
    size_t transcription_capacity,
    const atomic_bool *cancel_requested,
    char *error,
    size_t error_capacity
);
void speech_recognizer_cleanup(SpeechRecognizer *recognizer);

#endif
