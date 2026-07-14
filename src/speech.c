#include "pingmote/speech.h"

#include <whisper.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static int worker_count(void)
{
    const long processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (processors <= 1L) {
        return 1;
    }
    return processors < 4L ? (int)processors : 4;
}

bool speech_recognizer_init(
    SpeechRecognizer *recognizer,
    const char *model_path,
    char *error,
    size_t error_capacity
)
{
    if (recognizer == NULL || model_path == NULL || model_path[0] == '\0') {
        set_error(error, error_capacity, "speech recognizer and model path are required");
        return false;
    }
    *recognizer = (SpeechRecognizer){0};
    struct whisper_context_params params = whisper_context_default_params();
    params.use_gpu = true;
    params.flash_attn = true;
    struct whisper_context *const context = whisper_init_from_file_with_params(model_path, params);
    if (context == NULL) {
        set_error(error, error_capacity, "failed to load speech model");
        return false;
    }
    recognizer->context = context;
    recognizer->initialized = true;
    set_error(error, error_capacity, "");
    return true;
}

static bool append_segment(
    char *output,
    size_t capacity,
    const char *segment,
    bool add_space
)
{
    while (*segment != '\0' && isspace((unsigned char)*segment) != 0) {
        segment += 1;
    }
    const size_t segment_length = strlen(segment);
    const size_t current = strlen(output);
    const size_t separator = add_space && current > 0U ? 1U : 0U;
    if (segment_length == 0U || current + separator + segment_length >= capacity) {
        return segment_length == 0U;
    }
    if (separator > 0U) {
        output[current] = ' ';
    }
    (void)memcpy(output + current + separator, segment, segment_length + 1U);
    return true;
}

static bool abort_transcription(void *user_data)
{
    const atomic_bool *const cancel_requested = user_data;
    return cancel_requested != NULL
        && atomic_load_explicit(cancel_requested, memory_order_acquire);
}

bool speech_transcribe(
    SpeechRecognizer *recognizer,
    const float *samples,
    size_t sample_count,
    char *transcription,
    size_t transcription_capacity,
    const atomic_bool *cancel_requested,
    char *error,
    size_t error_capacity
)
{
    if (recognizer == NULL || !recognizer->initialized || recognizer->context == NULL
        || samples == NULL || sample_count < 4800U || sample_count > (size_t)INT_MAX
        || transcription == NULL || transcription_capacity == 0U) {
        set_error(error, error_capacity, "at least 0.3 seconds of recorded audio is required");
        return false;
    }
    transcription[0] = '\0';

    struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = worker_count();
    params.translate = false;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.token_timestamps = false;
    params.language = "en";
    params.detect_language = false;
    params.suppress_blank = true;
    params.suppress_nst = true;
    params.max_tokens = 64;
    params.abort_callback = abort_transcription;
    params.abort_callback_user_data = (void *)cancel_requested;
    if (whisper_full(
            recognizer->context,
            params,
            samples,
            (int)sample_count) != 0) {
        set_error(error, error_capacity, "speech transcription failed");
        return false;
    }

    const int segment_count = whisper_full_n_segments(recognizer->context);
    for (int index = 0; index < segment_count; ++index) {
        const char *const segment = whisper_full_get_segment_text(recognizer->context, index);
        if (segment == NULL || !append_segment(
                transcription,
                transcription_capacity,
                segment,
                index > 0)) {
            set_error(error, error_capacity, "speech transcription was too long");
            transcription[0] = '\0';
            return false;
        }
    }

    size_t length = strlen(transcription);
    while (length > 0U && isspace((unsigned char)transcription[length - 1U]) != 0) {
        transcription[--length] = '\0';
    }
    if (length == 0U || (transcription[0] == '[' && transcription[length - 1U] == ']')) {
        transcription[0] = '\0';
        set_error(error, error_capacity, "no speech was detected");
        return false;
    }

    set_error(error, error_capacity, "");
    return true;
}

void speech_recognizer_cleanup(SpeechRecognizer *recognizer)
{
    if (recognizer == NULL) {
        return;
    }
    if (recognizer->context != NULL) {
        whisper_free(recognizer->context);
    }
    *recognizer = (SpeechRecognizer){0};
}
