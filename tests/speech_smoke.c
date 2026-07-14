#include "pingmote/cache.h"
#include "pingmote/network.h"
#include "pingmote/sha256.h"
#include "pingmote/speech.h"

#include <miniaudio.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MODEL_SIZE UINT64_C(77704715)

static const char MODEL_URL[] =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin";
static const char MODEL_SHA256[] =
    "921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f";

static bool prepare_model(char *model_path, size_t capacity, char *error, size_t error_capacity)
{
    CacheStore cache;
    if (!cache_init(&cache, error, error_capacity)) {
        return false;
    }
    const int model_length = snprintf(
        model_path,
        capacity,
        "%s/ggml-tiny.en.bin",
        cache.root
    );
    char temporary_path[PINGMOTE_PATH_CAPACITY];
    const int temporary_length = snprintf(
        temporary_path,
        sizeof(temporary_path),
        "%s/ggml-tiny.en.part",
        cache.root
    );
    if (model_length < 0 || (size_t)model_length >= capacity
        || temporary_length < 0 || (size_t)temporary_length >= sizeof(temporary_path)) {
        return false;
    }

    if (sha256_file_matches(model_path, MODEL_SHA256, error, error_capacity)) {
        return true;
    }
    (void)remove(model_path);

    NetworkClient network;
    if (!network_init(&network, error, error_capacity)) {
        return false;
    }
    const NetworkStatus status = network_download_file(
        &network,
        MODEL_URL,
        temporary_path,
        (size_t)MODEL_SIZE,
        error,
        error_capacity
    );
    network_cleanup(&network);
    if (status != NETWORK_OK
        || !sha256_file_matches(temporary_path, MODEL_SHA256, error, error_capacity)
        || rename(temporary_path, model_path) != 0) {
        (void)remove(temporary_path);
        return false;
    }
    (void)chmod(model_path, 0600);
    return true;
}

static bool decode_sample(float **samples, size_t *sample_count)
{
    ma_decoder decoder;
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1U, 16000U);
    if (ma_decoder_init_file(PINGMOTE_WHISPER_SAMPLE_PATH, &config, &decoder) != MA_SUCCESS) {
        return false;
    }

    ma_uint64 frame_count = 0U;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &frame_count) != MA_SUCCESS
        || frame_count == 0U || frame_count > SIZE_MAX / sizeof(float)) {
        ma_decoder_uninit(&decoder);
        return false;
    }
    float *const decoded = calloc((size_t)frame_count, sizeof(*decoded));
    if (decoded == NULL) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    ma_uint64 frames_read = 0U;
    const ma_result result = ma_decoder_read_pcm_frames(
        &decoder,
        decoded,
        frame_count,
        &frames_read
    );
    ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS || frames_read == 0U || frames_read > SIZE_MAX) {
        free(decoded);
        return false;
    }
    *samples = decoded;
    *sample_count = (size_t)frames_read;
    return true;
}

int main(void)
{
    char error[192];
    char model_path[PINGMOTE_PATH_CAPACITY];
    if (!prepare_model(model_path, sizeof(model_path), error, sizeof(error))) {
        (void)fprintf(stderr, "model preparation failed: %s\n", error);
        return 1;
    }

    float *samples = NULL;
    size_t sample_count = 0U;
    if (!decode_sample(&samples, &sample_count)) {
        (void)fprintf(stderr, "sample decoding failed\n");
        return 1;
    }

    SpeechRecognizer recognizer;
    if (!speech_recognizer_init(&recognizer, model_path, error, sizeof(error))) {
        free(samples);
        (void)fprintf(stderr, "recognizer init failed: %s\n", error);
        return 1;
    }
    atomic_bool cancelled;
    atomic_init(&cancelled, false);
    char transcription[2001];
    const bool success = speech_transcribe(
        &recognizer,
        samples,
        sample_count,
        transcription,
        sizeof(transcription),
        &cancelled,
        error,
        sizeof(error)
    );
    speech_recognizer_cleanup(&recognizer);
    free(samples);
    if (!success || strstr(transcription, "ask not") == NULL) {
        (void)fprintf(
            stderr,
            "transcription failed: %s (%s)\n",
            error,
            transcription
        );
        return 1;
    }
    (void)printf("%s\n", transcription);
    return 0;
}
