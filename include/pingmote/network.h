#ifndef PINGMOTE_NETWORK_H
#define PINGMOTE_NETWORK_H

#include <stdbool.h>
#include <stddef.h>

typedef struct NetworkConfig {
    const char *supabase_url;
    const char *publishable_key;
    const char *access_token;
} NetworkConfig;

typedef struct NetworkClient {
    NetworkConfig config;
    bool initialized;
} NetworkClient;

bool network_init(NetworkClient *client, const NetworkConfig *config, char *error, size_t error_capacity);
bool send_chat(
    NetworkClient *client,
    const char *message,
    char *reply,
    size_t reply_capacity,
    char *error,
    size_t error_capacity
);
bool download_audio(
    NetworkClient *client,
    const char *url,
    const char *output_path,
    char *error,
    size_t error_capacity
);
void network_cleanup(NetworkClient *client);

#endif
