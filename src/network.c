#include "pingmote/network.h"

#include <cJSON.h>
#include <curl/curl.h>

#include <stdio.h>
#include <string.h>

#define PINGMOTE_HTTP_BUFFER_CAPACITY 8192U
#define PINGMOTE_URL_CAPACITY 512U
#define PINGMOTE_HEADER_CAPACITY 4096U

typedef struct WriteBuffer {
    char *data;
    size_t length;
    size_t capacity;
} WriteBuffer;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error == NULL || capacity == 0U) {
        return;
    }

    (void)snprintf(error, capacity, "%s", message);
}

static bool is_https_url(const char *url)
{
    static const char prefix[] = "https://";
    return url != NULL && strncmp(url, prefix, sizeof(prefix) - 1U) == 0;
}

static size_t write_to_buffer(void *contents, size_t size, size_t count, void *user_data)
{
    WriteBuffer *const buffer = user_data;
    const size_t byte_count = size * count;

    if (buffer == NULL || contents == NULL || (size != 0U && byte_count / size != count)) {
        return 0U;
    }

    if (byte_count >= buffer->capacity - buffer->length) {
        return 0U;
    }

    (void)memcpy(buffer->data + buffer->length, contents, byte_count);
    buffer->length += byte_count;
    buffer->data[buffer->length] = '\0';
    return byte_count;
}

static size_t write_to_file(void *contents, size_t size, size_t count, void *user_data)
{
    FILE *const file = user_data;
    return file == NULL ? 0U : fwrite(contents, size, count, file) * size;
}

static bool append_header(struct curl_slist **headers, const char *value)
{
    struct curl_slist *const next = curl_slist_append(*headers, value);
    if (next == NULL) {
        return false;
    }

    *headers = next;
    return true;
}

bool network_init(NetworkClient *client, const NetworkConfig *config, char *error, size_t error_capacity)
{
    if (client == NULL || config == NULL) {
        set_error(error, error_capacity, "network client and configuration are required");
        return false;
    }

    if (!is_https_url(config->supabase_url)) {
        set_error(error, error_capacity, "Supabase URL must use HTTPS");
        return false;
    }

    if (config->publishable_key == NULL || config->publishable_key[0] == '\0'
        || config->access_token == NULL || config->access_token[0] == '\0') {
        set_error(error, error_capacity, "Supabase key and access token are required");
        return false;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        set_error(error, error_capacity, "failed to initialize libcurl");
        return false;
    }

    client->config = *config;
    client->initialized = true;
    set_error(error, error_capacity, "");
    return true;
}

bool send_chat(
    NetworkClient *client,
    const char *message,
    char *reply,
    size_t reply_capacity,
    char *error,
    size_t error_capacity
)
{
    char endpoint[PINGMOTE_URL_CAPACITY];
    char api_key_header[PINGMOTE_HEADER_CAPACITY];
    char authorization_header[PINGMOTE_HEADER_CAPACITY];
    char response_data[PINGMOTE_HTTP_BUFFER_CAPACITY] = {0};
    WriteBuffer response = {response_data, 0U, sizeof(response_data)};
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    cJSON *request_json = NULL;
    cJSON *response_json = NULL;
    char *request_body = NULL;
    bool succeeded = false;
    long status_code = 0L;

    if (client == NULL || !client->initialized || message == NULL || message[0] == '\0'
        || reply == NULL || reply_capacity == 0U) {
        set_error(error, error_capacity, "valid client, message, and reply buffer are required");
        return false;
    }

    reply[0] = '\0';
    if (strlen(message) > 2000U) {
        set_error(error, error_capacity, "message exceeds 2000 bytes");
        return false;
    }

    const int endpoint_length = snprintf(
        endpoint,
        sizeof(endpoint),
        "%s/functions/v1/chat",
        client->config.supabase_url
    );
    const int key_length = snprintf(
        api_key_header,
        sizeof(api_key_header),
        "apikey: %s",
        client->config.publishable_key
    );
    const int auth_length = snprintf(
        authorization_header,
        sizeof(authorization_header),
        "Authorization: Bearer %s",
        client->config.access_token
    );

    if (endpoint_length < 0 || (size_t)endpoint_length >= sizeof(endpoint)
        || key_length < 0 || (size_t)key_length >= sizeof(api_key_header)
        || auth_length < 0 || (size_t)auth_length >= sizeof(authorization_header)) {
        set_error(error, error_capacity, "network configuration is too long");
        goto cleanup;
    }

    request_json = cJSON_CreateObject();
    if (request_json == NULL || cJSON_AddStringToObject(request_json, "message", message) == NULL) {
        set_error(error, error_capacity, "failed to create chat request");
        goto cleanup;
    }

    request_body = cJSON_PrintUnformatted(request_json);
    curl = curl_easy_init();
    if (request_body == NULL || curl == NULL) {
        set_error(error, error_capacity, "failed to allocate network request");
        goto cleanup;
    }

    if (!append_header(&headers, "Content-Type: application/json")
        || !append_header(&headers, api_key_header)
        || !append_header(&headers, authorization_header)) {
        set_error(error, error_capacity, "failed to create request headers");
        goto cleanup;
    }

    (void)curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    (void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request_body));
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    (void)curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);
    (void)curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    (void)curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    (void)curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    (void)curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    (void)curl_easy_setopt(curl, CURLOPT_USERAGENT, "PingMote/0.1");

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        set_error(error, error_capacity, "chat request failed");
        goto cleanup;
    }

    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (status_code < 200L || status_code >= 300L) {
        set_error(error, error_capacity, status_code == 401L ? "authentication required" : "chat service unavailable");
        goto cleanup;
    }

    response_json = cJSON_ParseWithLength(response.data, response.length);
    const cJSON *const reply_json = response_json == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(response_json, "reply");
    if (!cJSON_IsString(reply_json) || reply_json->valuestring == NULL) {
        set_error(error, error_capacity, "chat service returned an invalid response");
        goto cleanup;
    }

    const int copied = snprintf(reply, reply_capacity, "%s", reply_json->valuestring);
    if (copied < 0 || (size_t)copied >= reply_capacity) {
        reply[0] = '\0';
        set_error(error, error_capacity, "reply buffer is too small");
        goto cleanup;
    }

    set_error(error, error_capacity, "");
    succeeded = true;

cleanup:
    cJSON_Delete(response_json);
    cJSON_free(request_body);
    cJSON_Delete(request_json);
    curl_slist_free_all(headers);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    return succeeded;
}

bool download_audio(
    NetworkClient *client,
    const char *url,
    const char *output_path,
    char *error,
    size_t error_capacity
)
{
    if (client == NULL || !client->initialized || !is_https_url(url)
        || output_path == NULL || output_path[0] == '\0') {
        set_error(error, error_capacity, "valid client, HTTPS URL, and output path are required");
        return false;
    }

    FILE *const file = fopen(output_path, "wb");
    if (file == NULL) {
        set_error(error, error_capacity, "failed to open audio output file");
        return false;
    }

    CURL *const curl = curl_easy_init();
    if (curl == NULL) {
        (void)fclose(file);
        (void)remove(output_path);
        set_error(error, error_capacity, "failed to allocate audio request");
        return false;
    }

    (void)curl_easy_setopt(curl, CURLOPT_URL, url);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    (void)curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);
    (void)curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    (void)curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    (void)curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    (void)curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    (void)curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    (void)curl_easy_setopt(curl, CURLOPT_USERAGENT, "PingMote/0.1");

    const CURLcode result = curl_easy_perform(curl);
    long status_code = 0L;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);
    const int close_result = fclose(file);

    if (result != CURLE_OK || status_code < 200L || status_code >= 300L || close_result != 0) {
        (void)remove(output_path);
        set_error(error, error_capacity, "audio download failed");
        return false;
    }

    set_error(error, error_capacity, "");
    return true;
}

void network_cleanup(NetworkClient *client)
{
    if (client == NULL || !client->initialized) {
        return;
    }

    curl_global_cleanup();
    client->initialized = false;
}
