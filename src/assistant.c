#include "pingmote/assistant.h"

#include "pingmote/microphone.h"
#include "pingmote/network.h"
#include "pingmote/reply.h"
#include "pingmote/sha256.h"
#include "pingmote/speech.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    COMMAND_CAPACITY = 8,
    EVENT_CAPACITY = 16
};

#define WHISPER_MODEL_SIZE UINT64_C(77704715)

static const char WHISPER_MODEL_URL[] =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin";
static const char WHISPER_MODEL_SHA256[] =
    "921e4cf8686fdd993dcd081a5da5b6c365bfde1162e72b08d75ac75289920b1f";

typedef enum AssistantCommandType {
    COMMAND_CHAT = 0,
    COMMAND_SAVE_SETTINGS,
    COMMAND_START_LISTENING,
    COMMAND_STOP_LISTENING
} AssistantCommandType;

typedef struct AssistantCommand {
    AssistantCommandType type;
    char message[PINGMOTE_MESSAGE_CAPACITY];
    AssistantSettingsUpdate settings;
} AssistantCommand;

typedef struct AssistantImplementation {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    AssistantCommand commands[COMMAND_CAPACITY];
    AssistantEvent events[EVENT_CAPACITY];
    size_t command_read;
    size_t command_count;
    size_t event_read;
    size_t event_count;
    atomic_bool busy;
    atomic_bool stopping;
    atomic_bool network_started;
    bool thread_started;
    SecureSettings settings;
    NetworkClient network;
    CacheStore cache;
    MicrophoneRecorder microphone;
    SpeechRecognizer recognizer;
    bool network_ready;
    bool cache_ready;
} AssistantImplementation;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static void wipe_memory(void *memory, size_t size)
{
    volatile unsigned char *cursor = memory;
    for (size_t index = 0U; index < size; ++index) {
        cursor[index] = 0U;
    }
}

static bool starts_with_https(const char *value)
{
    static const char prefix[] = "https://";
    return value[0] == '\0' || strncmp(value, prefix, sizeof(prefix) - 1U) == 0;
}

static void push_event(AssistantImplementation *implementation, const AssistantEvent *event)
{
    (void)pthread_mutex_lock(&implementation->mutex);
    if (implementation->event_count == EVENT_CAPACITY) {
        implementation->event_read = (implementation->event_read + 1U) % EVENT_CAPACITY;
        implementation->event_count -= 1U;
    }
    const size_t write_index = (implementation->event_read + implementation->event_count)
        % EVENT_CAPACITY;
    implementation->events[write_index] = *event;
    implementation->event_count += 1U;
    (void)pthread_mutex_unlock(&implementation->mutex);
}

static void push_error(AssistantImplementation *implementation, const char *message)
{
    AssistantEvent event = {.type = ASSISTANT_EVENT_ERROR};
    (void)snprintf(event.detail, sizeof(event.detail), "%s", message);
    push_event(implementation, &event);
}

static void push_speech_status(AssistantImplementation *implementation, const char *message)
{
    AssistantEvent event = {.type = ASSISTANT_EVENT_SPEECH_PREPARING};
    (void)snprintf(event.detail, sizeof(event.detail), "%s", message);
    push_event(implementation, &event);
}

static void push_ready(AssistantImplementation *implementation)
{
    const AssistantEvent event = {
        .type = ASSISTANT_EVENT_READY,
        .has_groq_key = implementation->settings.groq_api_key[0] != '\0',
        .has_supabase_url = implementation->settings.supabase_url[0] != '\0',
        .has_supabase_key = implementation->settings.supabase_key[0] != '\0',
        .secure_storage_available = secure_store_is_available()
    };
    push_event(implementation, &event);
}

static void generate_speech(AssistantImplementation *implementation, const char *reply)
{
    if (!implementation->network_ready || !implementation->cache_ready) {
        return;
    }

    AssistantEvent event = {.type = ASSISTANT_EVENT_AUDIO_READY};
    if (cache_get_tts(
            &implementation->cache,
            reply,
            event.audio_path,
            sizeof(event.audio_path))) {
        push_event(implementation, &event);
        return;
    }

    char temporary_path[PINGMOTE_PATH_CAPACITY];
    char final_path[PINGMOTE_PATH_CAPACITY];
    if (!cache_get_tts_paths(
            &implementation->cache,
            reply,
            temporary_path,
            sizeof(temporary_path),
            final_path,
            sizeof(final_path))) {
        return;
    }

    char error[PINGMOTE_ERROR_CAPACITY];
    if (download_audio(
            &implementation->network,
            reply,
            temporary_path,
            error,
            sizeof(error)) != NETWORK_OK) {
        return;
    }
    if (!cache_commit_tts(
            temporary_path,
            final_path,
            error,
            sizeof(error))) {
        return;
    }

    (void)snprintf(event.audio_path, sizeof(event.audio_path), "%s", final_path);
    push_event(implementation, &event);
}

static void handle_chat(AssistantImplementation *implementation, const char *message)
{
    char reply[PINGMOTE_REPLY_CAPACITY];
    bool should_cache = false;
    if (implementation->cache_ready
        && cache_get_reply(&implementation->cache, message, reply, sizeof(reply))) {
        AssistantEvent event = {.type = ASSISTANT_EVENT_REPLY};
        (void)snprintf(event.text, sizeof(event.text), "%s", reply);
        push_event(implementation, &event);
        generate_speech(implementation, reply);
        return;
    }

    if (implementation->settings.groq_api_key[0] == '\0') {
        (void)snprintf(reply, sizeof(reply), "%s", "gimme a groq key first");
    } else if (!implementation->network_ready) {
        const uint64_t seed = (uint64_t)time(NULL);
        (void)snprintf(reply, sizeof(reply), "%s", reply_offline_fallback(seed));
    } else {
        char error[PINGMOTE_ERROR_CAPACITY];
        const NetworkStatus status = send_chat(
            &implementation->network,
            implementation->settings.groq_api_key,
            message,
            reply,
            sizeof(reply),
            error,
            sizeof(error)
        );
        if (status == NETWORK_OK) {
            should_cache = true;
        } else if (status == NETWORK_UNAUTHORIZED) {
            (void)snprintf(reply, sizeof(reply), "%s", "that groq key got rejected");
        } else if (status == NETWORK_RATE_LIMITED) {
            (void)snprintf(reply, sizeof(reply), "%s", "brain needs a tiny break");
        } else {
            const uint64_t seed = (uint64_t)time(NULL);
            (void)snprintf(reply, sizeof(reply), "%s", reply_offline_fallback(seed));
        }
    }

    if (should_cache && implementation->cache_ready) {
        char cache_error[PINGMOTE_ERROR_CAPACITY];
        (void)cache_put_reply(
            &implementation->cache,
            message,
            reply,
            cache_error,
            sizeof(cache_error)
        );
    }

    AssistantEvent event = {.type = ASSISTANT_EVENT_REPLY};
    (void)snprintf(event.text, sizeof(event.text), "%s", reply);
    push_event(implementation, &event);
    generate_speech(implementation, reply);
}

static void handle_settings(
    AssistantImplementation *implementation,
    const AssistantSettingsUpdate *update
)
{
    if (update->replace_supabase_url && !starts_with_https(update->supabase_url)) {
        push_error(implementation, "Supabase URL must use HTTPS");
        return;
    }

    SecureSettings candidate = implementation->settings;
    if (update->replace_groq_api_key) {
        (void)snprintf(candidate.groq_api_key, sizeof(candidate.groq_api_key), "%s", update->groq_api_key);
    }
    if (update->replace_supabase_url) {
        (void)snprintf(candidate.supabase_url, sizeof(candidate.supabase_url), "%s", update->supabase_url);
    }
    if (update->replace_supabase_key) {
        (void)snprintf(candidate.supabase_key, sizeof(candidate.supabase_key), "%s", update->supabase_key);
    }

    char error[PINGMOTE_ERROR_CAPACITY];
    const SecureStoreStatus status = secure_store_save(&candidate, error, sizeof(error));
    if (status != SECURE_STORE_OK) {
        secure_settings_clear(&candidate);
        push_error(implementation, error);
        return;
    }

    secure_settings_clear(&implementation->settings);
    implementation->settings = candidate;
    secure_settings_clear(&candidate);
    AssistantEvent event = {
        .type = ASSISTANT_EVENT_SETTINGS_SAVED,
        .has_groq_key = implementation->settings.groq_api_key[0] != '\0',
        .has_supabase_url = implementation->settings.supabase_url[0] != '\0',
        .has_supabase_key = implementation->settings.supabase_key[0] != '\0',
        .secure_storage_available = true
    };
    push_event(implementation, &event);
}

static bool model_file_is_valid(const char *path, char *error, size_t error_capacity)
{
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0
        || (uint64_t)info.st_size != WHISPER_MODEL_SIZE) {
        set_error(error, error_capacity, "speech model has an invalid size");
        return false;
    }
    return sha256_file_matches(path, WHISPER_MODEL_SHA256, error, error_capacity);
}

static bool ensure_speech_recognizer(AssistantImplementation *implementation)
{
    if (implementation->recognizer.initialized) {
        return true;
    }
    if (!implementation->cache_ready || !implementation->network_ready) {
        push_error(implementation, "speech model is unavailable offline on first use");
        return false;
    }

    char model_path[PINGMOTE_PATH_CAPACITY];
    char temporary_path[PINGMOTE_PATH_CAPACITY];
    const int model_length = snprintf(
        model_path,
        sizeof(model_path),
        "%s/ggml-tiny.en.bin",
        implementation->cache.root
    );
    const int temporary_length = snprintf(
        temporary_path,
        sizeof(temporary_path),
        "%s/ggml-tiny.en.part",
        implementation->cache.root
    );
    if (model_length < 0 || (size_t)model_length >= sizeof(model_path)
        || temporary_length < 0 || (size_t)temporary_length >= sizeof(temporary_path)) {
        push_error(implementation, "speech model path is too long");
        return false;
    }

    char error[PINGMOTE_ERROR_CAPACITY];
    bool valid_model = model_file_is_valid(model_path, error, sizeof(error));
    if (!valid_model) {
        (void)remove(model_path);
        push_speech_status(implementation, "downloading speech brain once (74 mb)");
        const NetworkStatus status = network_download_file(
            &implementation->network,
            WHISPER_MODEL_URL,
            temporary_path,
            (size_t)WHISPER_MODEL_SIZE,
            error,
            sizeof(error)
        );
        if (status != NETWORK_OK
            || !model_file_is_valid(temporary_path, error, sizeof(error))) {
            (void)remove(temporary_path);
            if (!atomic_load_explicit(&implementation->stopping, memory_order_acquire)) {
                push_error(implementation, error);
            }
            return false;
        }
        if (rename(temporary_path, model_path) != 0) {
            (void)remove(temporary_path);
            push_error(implementation, "failed to save speech model");
            return false;
        }
        (void)chmod(model_path, 0600);
        valid_model = true;
    }

    if (!valid_model || atomic_load_explicit(&implementation->stopping, memory_order_acquire)) {
        return false;
    }
    push_speech_status(implementation, "loading tiny speech brain...");
    if (!speech_recognizer_init(
            &implementation->recognizer,
            model_path,
            error,
            sizeof(error))) {
        push_error(implementation, error);
        return false;
    }
    return true;
}

static void handle_start_listening(AssistantImplementation *implementation)
{
    char error[PINGMOTE_ERROR_CAPACITY];
    if (!implementation->microphone.initialized
        && !microphone_init(&implementation->microphone, error, sizeof(error))) {
        push_error(implementation, error);
        return;
    }
    if (!microphone_start(&implementation->microphone, error, sizeof(error))) {
        push_error(implementation, error);
        return;
    }
    const AssistantEvent event = {.type = ASSISTANT_EVENT_LISTENING_STARTED};
    push_event(implementation, &event);
}

static void handle_stop_listening(AssistantImplementation *implementation)
{
    const float *samples = NULL;
    size_t sample_count = 0U;
    bool truncated = false;
    char error[PINGMOTE_ERROR_CAPACITY];
    if (!microphone_stop(
            &implementation->microphone,
            &samples,
            &sample_count,
            &truncated,
            error,
            sizeof(error))) {
        push_error(implementation, error);
        return;
    }
    if (sample_count < 4800U) {
        push_error(implementation, "hold the mic a little longer");
        return;
    }
    push_speech_status(
        implementation,
        truncated ? "transcribing first 30 seconds..." : "transcribing..."
    );
    if (!ensure_speech_recognizer(implementation)) {
        return;
    }

    char transcription[PINGMOTE_MESSAGE_CAPACITY];
    if (!speech_transcribe(
            &implementation->recognizer,
            samples,
            sample_count,
            transcription,
            sizeof(transcription),
            &implementation->stopping,
            error,
            sizeof(error))) {
        if (!atomic_load_explicit(&implementation->stopping, memory_order_acquire)) {
            push_error(implementation, error);
        }
        return;
    }

    AssistantEvent event = {.type = ASSISTANT_EVENT_TRANSCRIPTION};
    (void)snprintf(event.text, sizeof(event.text), "%s", transcription);
    push_event(implementation, &event);
    handle_chat(implementation, transcription);
}

static void *assistant_thread_main(void *user_data)
{
    AssistantImplementation *const implementation = user_data;
    char error[PINGMOTE_ERROR_CAPACITY];
    implementation->network_ready = network_init(
        &implementation->network,
        error,
        sizeof(error)
    );
    atomic_store_explicit(
        &implementation->network_started,
        implementation->network_ready,
        memory_order_release
    );
    implementation->cache_ready = cache_init(&implementation->cache, error, sizeof(error));
    const SecureStoreStatus load_status = secure_store_load(
        &implementation->settings,
        error,
        sizeof(error)
    );
    if (load_status != SECURE_STORE_OK && load_status != SECURE_STORE_NOT_FOUND) {
        push_error(implementation, error);
    }
    push_ready(implementation);

    for (;;) {
        (void)pthread_mutex_lock(&implementation->mutex);
        while (implementation->command_count == 0U
            && !atomic_load_explicit(&implementation->stopping, memory_order_acquire)) {
            (void)pthread_cond_wait(&implementation->condition, &implementation->mutex);
        }
        if (atomic_load_explicit(&implementation->stopping, memory_order_acquire)) {
            (void)pthread_mutex_unlock(&implementation->mutex);
            break;
        }
        const size_t command_index = implementation->command_read;
        AssistantCommand command = implementation->commands[command_index];
        wipe_memory(&implementation->commands[command_index], sizeof(AssistantCommand));
        implementation->command_read = (implementation->command_read + 1U) % COMMAND_CAPACITY;
        implementation->command_count -= 1U;
        (void)pthread_mutex_unlock(&implementation->mutex);

        atomic_store_explicit(&implementation->busy, true, memory_order_release);
        if (command.type == COMMAND_CHAT) {
            handle_chat(implementation, command.message);
        } else if (command.type == COMMAND_SAVE_SETTINGS) {
            handle_settings(implementation, &command.settings);
        } else if (command.type == COMMAND_START_LISTENING) {
            handle_start_listening(implementation);
        } else if (command.type == COMMAND_STOP_LISTENING) {
            handle_stop_listening(implementation);
        }
        atomic_store_explicit(&implementation->busy, false, memory_order_release);
        wipe_memory(&command, sizeof(command));
    }

    microphone_cleanup(&implementation->microphone);
    speech_recognizer_cleanup(&implementation->recognizer);
    if (implementation->network_ready) {
        network_cleanup(&implementation->network);
    }
    cache_cleanup(&implementation->cache);
    secure_settings_clear(&implementation->settings);
    return NULL;
}

static bool push_command(AssistantImplementation *implementation, const AssistantCommand *command)
{
    (void)pthread_mutex_lock(&implementation->mutex);
    if (implementation->command_count == COMMAND_CAPACITY) {
        (void)pthread_mutex_unlock(&implementation->mutex);
        return false;
    }
    const size_t write_index = (implementation->command_read + implementation->command_count)
        % COMMAND_CAPACITY;
    implementation->commands[write_index] = *command;
    implementation->command_count += 1U;
    (void)pthread_cond_signal(&implementation->condition);
    (void)pthread_mutex_unlock(&implementation->mutex);
    return true;
}

bool assistant_init(AssistantService *service, char *error, size_t error_capacity)
{
    if (service == NULL) {
        set_error(error, error_capacity, "assistant service is required");
        return false;
    }
    *service = (AssistantService){0};
    AssistantImplementation *const implementation = calloc(1U, sizeof(*implementation));
    if (implementation == NULL) {
        set_error(error, error_capacity, "failed to allocate assistant service");
        return false;
    }
    atomic_init(&implementation->busy, false);
    atomic_init(&implementation->stopping, false);
    atomic_init(&implementation->network_started, false);
    if (pthread_mutex_init(&implementation->mutex, NULL) != 0) {
        free(implementation);
        set_error(error, error_capacity, "failed to initialize assistant synchronization");
        return false;
    }
    if (pthread_cond_init(&implementation->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&implementation->mutex);
        free(implementation);
        set_error(error, error_capacity, "failed to initialize assistant synchronization");
        return false;
    }
    if (pthread_create(&implementation->thread, NULL, assistant_thread_main, implementation) != 0) {
        (void)pthread_cond_destroy(&implementation->condition);
        (void)pthread_mutex_destroy(&implementation->mutex);
        free(implementation);
        set_error(error, error_capacity, "failed to start assistant worker");
        return false;
    }
    implementation->thread_started = true;
    service->implementation = implementation;
    service->initialized = true;
    set_error(error, error_capacity, "");
    return true;
}

bool assistant_submit_chat(AssistantService *service, const char *message)
{
    if (service == NULL || !service->initialized || service->implementation == NULL
        || message == NULL || message[0] == '\0' || strlen(message) >= PINGMOTE_MESSAGE_CAPACITY) {
        return false;
    }
    AssistantCommand command = {.type = COMMAND_CHAT};
    (void)snprintf(command.message, sizeof(command.message), "%s", message);
    return push_command(service->implementation, &command);
}

bool assistant_start_listening(AssistantService *service)
{
    if (service == NULL || !service->initialized || service->implementation == NULL
        || assistant_is_busy(service)) {
        return false;
    }
    const AssistantCommand command = {.type = COMMAND_START_LISTENING};
    return push_command(service->implementation, &command);
}

bool assistant_stop_listening(AssistantService *service)
{
    if (service == NULL || !service->initialized || service->implementation == NULL) {
        return false;
    }
    const AssistantCommand command = {.type = COMMAND_STOP_LISTENING};
    return push_command(service->implementation, &command);
}

bool assistant_save_settings(
    AssistantService *service,
    const AssistantSettingsUpdate *update
)
{
    if (service == NULL || !service->initialized || service->implementation == NULL
        || update == NULL) {
        return false;
    }
    const AssistantCommand command = {
        .type = COMMAND_SAVE_SETTINGS,
        .settings = *update
    };
    return push_command(service->implementation, &command);
}

bool assistant_poll_event(AssistantService *service, AssistantEvent *event)
{
    if (service == NULL || !service->initialized || service->implementation == NULL
        || event == NULL) {
        return false;
    }
    AssistantImplementation *const implementation = service->implementation;
    (void)pthread_mutex_lock(&implementation->mutex);
    if (implementation->event_count == 0U) {
        (void)pthread_mutex_unlock(&implementation->mutex);
        return false;
    }
    *event = implementation->events[implementation->event_read];
    implementation->event_read = (implementation->event_read + 1U) % EVENT_CAPACITY;
    implementation->event_count -= 1U;
    (void)pthread_mutex_unlock(&implementation->mutex);
    return true;
}

bool assistant_is_busy(const AssistantService *service)
{
    if (service == NULL || !service->initialized || service->implementation == NULL) {
        return false;
    }
    const AssistantImplementation *const implementation = service->implementation;
    return atomic_load_explicit(&implementation->busy, memory_order_acquire);
}

void assistant_shutdown(AssistantService *service)
{
    if (service == NULL || service->implementation == NULL) {
        return;
    }
    AssistantImplementation *const implementation = service->implementation;
    atomic_store_explicit(&implementation->stopping, true, memory_order_release);
    if (atomic_load_explicit(&implementation->network_started, memory_order_acquire)) {
        network_cancel(&implementation->network);
    }
    (void)pthread_mutex_lock(&implementation->mutex);
    (void)pthread_cond_broadcast(&implementation->condition);
    (void)pthread_mutex_unlock(&implementation->mutex);
    if (implementation->thread_started) {
        (void)pthread_join(implementation->thread, NULL);
    }
    (void)pthread_cond_destroy(&implementation->condition);
    (void)pthread_mutex_destroy(&implementation->mutex);
    secure_settings_clear(&implementation->settings);
    wipe_memory(implementation->commands, sizeof(implementation->commands));
    free(implementation);
    *service = (AssistantService){0};
}
