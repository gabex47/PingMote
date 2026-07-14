#include "pingmote/microphone.h"

#include <miniaudio.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MICROPHONE_SAMPLE_RATE 16000U
#define MICROPHONE_MAXIMUM_SECONDS 30U
#define MICROPHONE_MAXIMUM_SAMPLES (MICROPHONE_SAMPLE_RATE * MICROPHONE_MAXIMUM_SECONDS)

typedef struct MicrophoneImplementation {
    ma_device device;
    float *samples;
    atomic_size_t sample_count;
    atomic_bool accepting_samples;
    atomic_bool truncated;
    bool device_initialized;
    bool device_started;
} MicrophoneImplementation;

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
        (void)snprintf(error, capacity, "%s: %s", operation, ma_result_description(result));
    }
}

static void capture_callback(
    ma_device *device,
    void *output,
    const void *input,
    ma_uint32 frame_count
)
{
    (void)output;
    MicrophoneImplementation *const implementation = device == NULL
        ? NULL
        : device->pUserData;
    if (implementation == NULL || input == NULL
        || !atomic_load_explicit(&implementation->accepting_samples, memory_order_acquire)) {
        return;
    }

    const size_t current = atomic_load_explicit(
        &implementation->sample_count,
        memory_order_relaxed
    );
    const size_t requested = (size_t)frame_count;
    const size_t available = current < MICROPHONE_MAXIMUM_SAMPLES
        ? MICROPHONE_MAXIMUM_SAMPLES - current
        : 0U;
    const size_t copy_count = requested < available ? requested : available;
    if (copy_count > 0U) {
        (void)memcpy(
            implementation->samples + current,
            input,
            copy_count * sizeof(*implementation->samples)
        );
        atomic_store_explicit(
            &implementation->sample_count,
            current + copy_count,
            memory_order_release
        );
    }
    if (copy_count < requested) {
        atomic_store_explicit(&implementation->truncated, true, memory_order_release);
        atomic_store_explicit(
            &implementation->accepting_samples,
            false,
            memory_order_release
        );
    }
}

bool microphone_init(
    MicrophoneRecorder *recorder,
    char *error,
    size_t error_capacity
)
{
    if (recorder == NULL) {
        set_error(error, error_capacity, "microphone recorder is required");
        return false;
    }
    *recorder = (MicrophoneRecorder){0};

    MicrophoneImplementation *const implementation = calloc(1U, sizeof(*implementation));
    if (implementation == NULL) {
        set_error(error, error_capacity, "failed to allocate microphone recorder");
        return false;
    }
    implementation->samples = calloc(
        MICROPHONE_MAXIMUM_SAMPLES,
        sizeof(*implementation->samples)
    );
    if (implementation->samples == NULL) {
        free(implementation);
        set_error(error, error_capacity, "failed to allocate microphone buffer");
        return false;
    }
    atomic_init(&implementation->sample_count, 0U);
    atomic_init(&implementation->accepting_samples, false);
    atomic_init(&implementation->truncated, false);

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1U;
    config.sampleRate = MICROPHONE_SAMPLE_RATE;
    config.dataCallback = capture_callback;
    config.pUserData = implementation;
    const ma_result result = ma_device_init(NULL, &config, &implementation->device);
    if (result != MA_SUCCESS) {
        free(implementation->samples);
        free(implementation);
        set_miniaudio_error(error, error_capacity, "failed to open microphone", result);
        return false;
    }

    implementation->device_initialized = true;
    recorder->implementation = implementation;
    recorder->initialized = true;
    set_error(error, error_capacity, "");
    return true;
}

bool microphone_start(
    MicrophoneRecorder *recorder,
    char *error,
    size_t error_capacity
)
{
    if (recorder == NULL || !recorder->initialized || recorder->implementation == NULL) {
        set_error(error, error_capacity, "microphone is not initialized");
        return false;
    }
    if (recorder->recording) {
        set_error(error, error_capacity, "microphone is already recording");
        return false;
    }

    MicrophoneImplementation *const implementation = recorder->implementation;
    atomic_store_explicit(&implementation->sample_count, 0U, memory_order_release);
    atomic_store_explicit(&implementation->truncated, false, memory_order_release);
    atomic_store_explicit(&implementation->accepting_samples, true, memory_order_release);
    const ma_result result = ma_device_start(&implementation->device);
    if (result != MA_SUCCESS) {
        atomic_store_explicit(&implementation->accepting_samples, false, memory_order_release);
        set_miniaudio_error(error, error_capacity, "failed to start microphone", result);
        return false;
    }

    implementation->device_started = true;
    recorder->recording = true;
    set_error(error, error_capacity, "");
    return true;
}

bool microphone_stop(
    MicrophoneRecorder *recorder,
    const float **samples,
    size_t *sample_count,
    bool *was_truncated,
    char *error,
    size_t error_capacity
)
{
    if (recorder == NULL || !recorder->initialized || recorder->implementation == NULL
        || samples == NULL || sample_count == NULL || was_truncated == NULL) {
        set_error(error, error_capacity, "active microphone and output buffers are required");
        return false;
    }
    if (!recorder->recording) {
        set_error(error, error_capacity, "microphone is not recording");
        return false;
    }

    MicrophoneImplementation *const implementation = recorder->implementation;
    atomic_store_explicit(&implementation->accepting_samples, false, memory_order_release);
    const ma_result result = ma_device_stop(&implementation->device);
    implementation->device_started = false;
    recorder->recording = false;
    if (result != MA_SUCCESS) {
        set_miniaudio_error(error, error_capacity, "failed to stop microphone", result);
        return false;
    }

    *samples = implementation->samples;
    *sample_count = atomic_load_explicit(&implementation->sample_count, memory_order_acquire);
    *was_truncated = atomic_load_explicit(&implementation->truncated, memory_order_acquire);
    set_error(error, error_capacity, "");
    return true;
}

void microphone_cleanup(MicrophoneRecorder *recorder)
{
    if (recorder == NULL || recorder->implementation == NULL) {
        return;
    }
    MicrophoneImplementation *const implementation = recorder->implementation;
    atomic_store_explicit(&implementation->accepting_samples, false, memory_order_release);
    if (implementation->device_started) {
        (void)ma_device_stop(&implementation->device);
        implementation->device_started = false;
    }
    if (implementation->device_initialized) {
        ma_device_uninit(&implementation->device);
        implementation->device_initialized = false;
    }
    free(implementation->samples);
    free(implementation);
    *recorder = (MicrophoneRecorder){0};
}
