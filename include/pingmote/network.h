#ifndef PINGMOTE_NETWORK_H
#define PINGMOTE_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

typedef enum NetworkStatus {
    NETWORK_OK = 0,
    NETWORK_INVALID_ARGUMENT,
    NETWORK_OFFLINE,
    NETWORK_UNAUTHORIZED,
    NETWORK_RATE_LIMITED,
    NETWORK_HTTP_ERROR,
    NETWORK_INVALID_RESPONSE,
    NETWORK_IO_ERROR,
    NETWORK_INTERNAL_ERROR
} NetworkStatus;

typedef struct NetworkClient {
    atomic_bool initialized;
    atomic_bool cancel_requested;
} NetworkClient;

bool network_init(NetworkClient *client, char *error, size_t error_capacity);
NetworkStatus send_chat(
    NetworkClient *client,
    const char *groq_api_key,
    const char *message,
    char *reply,
    size_t reply_capacity,
    char *error,
    size_t error_capacity
);
NetworkStatus download_audio(
    NetworkClient *client,
    const char *text,
    const char *output_path,
    char *error,
    size_t error_capacity
);
NetworkStatus network_download_file(
    NetworkClient *client,
    const char *url,
    const char *output_path,
    size_t maximum_bytes,
    char *error,
    size_t error_capacity
);
const char *network_status_name(NetworkStatus status);
void network_cancel(NetworkClient *client);
void network_cleanup(NetworkClient *client);

#endif
