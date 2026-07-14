#ifndef PINGMOTE_ASSISTANT_H
#define PINGMOTE_ASSISTANT_H

#include "pingmote/cache.h"
#include "pingmote/secure_store.h"

#include <stdbool.h>
#include <stddef.h>

#define PINGMOTE_MESSAGE_CAPACITY 2001U
#define PINGMOTE_REPLY_CAPACITY 512U
#define PINGMOTE_ERROR_CAPACITY 192U

typedef enum AssistantEventType {
    ASSISTANT_EVENT_READY = 0,
    ASSISTANT_EVENT_REPLY,
    ASSISTANT_EVENT_AUDIO_READY,
    ASSISTANT_EVENT_LISTENING_STARTED,
    ASSISTANT_EVENT_SPEECH_PREPARING,
    ASSISTANT_EVENT_TRANSCRIPTION,
    ASSISTANT_EVENT_SETTINGS_SAVED,
    ASSISTANT_EVENT_ERROR
} AssistantEventType;

typedef struct AssistantEvent {
    AssistantEventType type;
    char text[PINGMOTE_REPLY_CAPACITY];
    char detail[PINGMOTE_ERROR_CAPACITY];
    char audio_path[PINGMOTE_PATH_CAPACITY];
    bool has_groq_key;
    bool has_supabase_url;
    bool has_supabase_key;
    bool secure_storage_available;
} AssistantEvent;

typedef struct AssistantSettingsUpdate {
    char groq_api_key[PINGMOTE_API_KEY_CAPACITY];
    char supabase_url[PINGMOTE_SUPABASE_URL_CAPACITY];
    char supabase_key[PINGMOTE_API_KEY_CAPACITY];
    bool replace_groq_api_key;
    bool replace_supabase_url;
    bool replace_supabase_key;
} AssistantSettingsUpdate;

typedef struct AssistantService {
    void *implementation;
    bool initialized;
} AssistantService;

bool assistant_init(AssistantService *service, char *error, size_t error_capacity);
bool assistant_submit_chat(AssistantService *service, const char *message);
bool assistant_start_listening(AssistantService *service);
bool assistant_stop_listening(AssistantService *service);
bool assistant_save_settings(
    AssistantService *service,
    const AssistantSettingsUpdate *update
);
bool assistant_poll_event(AssistantService *service, AssistantEvent *event);
bool assistant_is_busy(const AssistantService *service);
void assistant_shutdown(AssistantService *service);

#endif
