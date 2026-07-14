#include "pingmote/network.h"

#include "pingmote/reply.h"

#include <cJSON.h>
#include <curl/curl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PINGMOTE_HTTP_BUFFER_CAPACITY 16384U
#define PINGMOTE_URL_CAPACITY 4096U
#define PINGMOTE_HEADER_CAPACITY 1024U
#define PINGMOTE_TTS_MAXIMUM_BYTES (8U * 1024U * 1024U)

static const char GROQ_ENDPOINT[] = "https://api.groq.com/openai/v1/chat/completions";
static const char TETYYYS_ENDPOINT[] = "https://tetyys.com/SAPI4/SAPI4";
static const char MOTE_SYSTEM_PROMPT[] =
    "you are mote, a tiny desktop creature living inside the user's computer. "
    "reply in 3 to 15 words. never exceed 20 words. lowercase plain text only. "
    "be funny, awkward, and casual. do not sound like chatgpt. do not overexplain. "
    "do not use markdown, links, or emojis. act like you have always lived here.";

typedef struct WriteBuffer {
    char *data;
    size_t length;
    size_t capacity;
} WriteBuffer;

typedef struct FileSink {
    FILE *file;
    size_t bytes_written;
    size_t maximum_bytes;
    bool limit_exceeded;
} FileSink;

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static void wipe_bytes(void *memory, size_t size)
{
    volatile unsigned char *cursor = memory;
    for (size_t index = 0U; index < size; ++index) {
        cursor[index] = 0U;
    }
}

static void wipe_user_message(cJSON *request)
{
    cJSON *const messages = request == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(request, "messages");
    cJSON *const user = cJSON_IsArray(messages) ? cJSON_GetArrayItem(messages, 1) : NULL;
    cJSON *const content = cJSON_IsObject(user)
        ? cJSON_GetObjectItemCaseSensitive(user, "content")
        : NULL;
    if (cJSON_IsString(content) && content->valuestring != NULL) {
        wipe_bytes(content->valuestring, strlen(content->valuestring));
    }
}

static bool is_https_url(const char *url)
{
    static const char prefix[] = "https://";
    return url != NULL && strncmp(url, prefix, sizeof(prefix) - 1U) == 0;
}

static size_t write_to_buffer(void *contents, size_t size, size_t count, void *user_data)
{
    WriteBuffer *const buffer = user_data;
    if (buffer == NULL || contents == NULL || (count != 0U && size > SIZE_MAX / count)) {
        return 0U;
    }

    const size_t byte_count = size * count;
    if (buffer->length >= buffer->capacity
        || byte_count >= buffer->capacity - buffer->length) {
        return 0U;
    }

    (void)memcpy(buffer->data + buffer->length, contents, byte_count);
    buffer->length += byte_count;
    buffer->data[buffer->length] = '\0';
    return byte_count;
}

static size_t write_to_file(void *contents, size_t size, size_t count, void *user_data)
{
    FileSink *const sink = user_data;
    if (sink == NULL || sink->file == NULL || contents == NULL
        || (count != 0U && size > SIZE_MAX / count)) {
        return 0U;
    }

    const size_t byte_count = size * count;
    if (sink->bytes_written > sink->maximum_bytes
        || byte_count > sink->maximum_bytes - sink->bytes_written) {
        sink->limit_exceeded = true;
        return 0U;
    }

    const size_t written_count = fwrite(contents, size, count, sink->file);
    const size_t written_bytes = written_count * size;
    sink->bytes_written += written_bytes;
    return written_bytes;
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

static NetworkStatus status_from_curl(CURLcode result)
{
    switch (result) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
            return NETWORK_OFFLINE;
        case CURLE_OK:
            return NETWORK_OK;
        default:
            return NETWORK_HTTP_ERROR;
    }
}

static NetworkStatus status_from_http(long status_code)
{
    if (status_code >= 200L && status_code < 300L) {
        return NETWORK_OK;
    }
    if (status_code == 401L || status_code == 403L) {
        return NETWORK_UNAUTHORIZED;
    }
    if (status_code == 429L) {
        return NETWORK_RATE_LIMITED;
    }
    return status_code >= 500L ? NETWORK_OFFLINE : NETWORK_HTTP_ERROR;
}

static int transfer_progress(
    void *user_data,
    curl_off_t download_total,
    curl_off_t download_now,
    curl_off_t upload_total,
    curl_off_t upload_now
)
{
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    const NetworkClient *const client = user_data;
    return client != NULL
        && atomic_load_explicit(&client->cancel_requested, memory_order_acquire)
        ? 1
        : 0;
}

static bool configure_curl_common(
    NetworkClient *client,
    CURL *curl,
    const char *url,
    long timeout_milliseconds
)
{
    return curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_milliseconds) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https") == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_XFERINFODATA, client) == CURLE_OK
        && curl_easy_setopt(curl, CURLOPT_USERAGENT, "PingMote/0.2") == CURLE_OK;
}

bool network_init(NetworkClient *client, char *error, size_t error_capacity)
{
    if (client == NULL) {
        set_error(error, error_capacity, "network client is required");
        return false;
    }

    atomic_init(&client->initialized, false);
    atomic_init(&client->cancel_requested, false);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        set_error(error, error_capacity, "failed to initialize libcurl");
        return false;
    }

    atomic_store_explicit(&client->initialized, true, memory_order_release);
    set_error(error, error_capacity, "");
    return true;
}

NetworkStatus send_chat(
    NetworkClient *client,
    const char *groq_api_key,
    const char *message,
    char *reply,
    size_t reply_capacity,
    char *error,
    size_t error_capacity
)
{
    char authorization_header[PINGMOTE_HEADER_CAPACITY];
    char response_data[PINGMOTE_HTTP_BUFFER_CAPACITY] = {0};
    WriteBuffer response = {response_data, 0U, sizeof(response_data)};
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
    cJSON *request_json = NULL;
    cJSON *messages_json = NULL;
    cJSON *response_json = NULL;
    char *request_body = NULL;
    NetworkStatus status = NETWORK_INTERNAL_ERROR;

    if (client == NULL
        || !atomic_load_explicit(&client->initialized, memory_order_acquire)
        || groq_api_key == NULL || groq_api_key[0] == '\0'
        || message == NULL || message[0] == '\0'
        || reply == NULL || reply_capacity == 0U) {
        set_error(error, error_capacity, "valid client, API key, message, and reply buffer are required");
        return NETWORK_INVALID_ARGUMENT;
    }

    reply[0] = '\0';
    if (strlen(message) > 2000U || strlen(groq_api_key) > 512U) {
        set_error(error, error_capacity, "message or API key exceeds the supported length");
        return NETWORK_INVALID_ARGUMENT;
    }

    const int header_length = snprintf(
        authorization_header,
        sizeof(authorization_header),
        "Authorization: Bearer %s",
        groq_api_key
    );
    if (header_length < 0 || (size_t)header_length >= sizeof(authorization_header)) {
        set_error(error, error_capacity, "API key is too long");
        return NETWORK_INVALID_ARGUMENT;
    }

    request_json = cJSON_CreateObject();
    messages_json = cJSON_CreateArray();
    cJSON *const system_message = cJSON_CreateObject();
    cJSON *const user_message = cJSON_CreateObject();
    if (request_json == NULL || messages_json == NULL
        || system_message == NULL || user_message == NULL) {
        cJSON_Delete(system_message);
        cJSON_Delete(user_message);
        set_error(error, error_capacity, "failed to allocate chat request");
        goto cleanup;
    }

    if (cJSON_AddStringToObject(system_message, "role", "system") == NULL
        || cJSON_AddStringToObject(system_message, "content", MOTE_SYSTEM_PROMPT) == NULL
        || cJSON_AddStringToObject(user_message, "role", "user") == NULL
        || cJSON_AddStringToObject(user_message, "content", message) == NULL) {
        cJSON_Delete(system_message);
        cJSON_Delete(user_message);
        set_error(error, error_capacity, "failed to create chat messages");
        goto cleanup;
    }

    cJSON_AddItemToArray(messages_json, system_message);
    cJSON_AddItemToArray(messages_json, user_message);
    cJSON_AddItemToObject(request_json, "messages", messages_json);
    messages_json = NULL;

    if (cJSON_AddStringToObject(request_json, "model", "llama-3.1-8b-instant") == NULL
        || cJSON_AddNumberToObject(request_json, "max_completion_tokens", 48.0) == NULL
        || cJSON_AddNumberToObject(request_json, "temperature", 0.85) == NULL
        || cJSON_AddBoolToObject(request_json, "stream", false) == NULL
        || cJSON_AddStringToObject(request_json, "citation_options", "disabled") == NULL) {
        set_error(error, error_capacity, "failed to create chat request");
        goto cleanup;
    }

    request_body = cJSON_PrintUnformatted(request_json);
    curl = curl_easy_init();
    if (request_body == NULL || curl == NULL) {
        set_error(error, error_capacity, "failed to allocate chat transport");
        goto cleanup;
    }

    if (!append_header(&headers, "Content-Type: application/json")
        || !append_header(&headers, "Accept: application/json")
        || !append_header(&headers, authorization_header)
        || !configure_curl_common(client, curl, GROQ_ENDPOINT, 20000L)
        || curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(request_body)) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response) != CURLE_OK) {
        set_error(error, error_capacity, "failed to configure chat transport");
        goto cleanup;
    }

    const CURLcode curl_result = curl_easy_perform(curl);
    status = status_from_curl(curl_result);
    if (status != NETWORK_OK) {
        set_error(error, error_capacity, "could not reach Groq");
        goto cleanup;
    }

    long status_code = 0L;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK) {
        set_error(error, error_capacity, "failed to read Groq status");
        status = NETWORK_HTTP_ERROR;
        goto cleanup;
    }
    status = status_from_http(status_code);
    if (status != NETWORK_OK) {
        set_error(
            error,
            error_capacity,
            status == NETWORK_UNAUTHORIZED ? "Groq API key was rejected" : "Groq request failed"
        );
        goto cleanup;
    }

    response_json = cJSON_ParseWithLength(response.data, response.length);
    const cJSON *const choices = response_json == NULL
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(response_json, "choices");
    const cJSON *const first_choice = cJSON_IsArray(choices)
        ? cJSON_GetArrayItem(choices, 0)
        : NULL;
    const cJSON *const response_message = cJSON_IsObject(first_choice)
        ? cJSON_GetObjectItemCaseSensitive(first_choice, "message")
        : NULL;
    const cJSON *const content = cJSON_IsObject(response_message)
        ? cJSON_GetObjectItemCaseSensitive(response_message, "content")
        : NULL;
    if (!cJSON_IsString(content) || content->valuestring == NULL
        || !reply_sanitize(content->valuestring, reply, reply_capacity)) {
        set_error(error, error_capacity, "Groq returned an invalid reply");
        status = NETWORK_INVALID_RESPONSE;
        goto cleanup;
    }

    set_error(error, error_capacity, "");
    status = NETWORK_OK;

cleanup:
    cJSON_Delete(response_json);
    if (request_body != NULL) {
        wipe_bytes(request_body, strlen(request_body));
    }
    cJSON_free(request_body);
    cJSON_Delete(messages_json);
    wipe_user_message(request_json);
    cJSON_Delete(request_json);
    curl_slist_free_all(headers);
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }
    wipe_bytes(authorization_header, sizeof(authorization_header));
    wipe_bytes(response_data, sizeof(response_data));
    return status;
}

NetworkStatus network_download_file(
    NetworkClient *client,
    const char *url,
    const char *output_path,
    size_t maximum_bytes,
    char *error,
    size_t error_capacity
)
{
    if (client == NULL
        || !atomic_load_explicit(&client->initialized, memory_order_acquire)
        || !is_https_url(url)
        || output_path == NULL || output_path[0] == '\0' || maximum_bytes == 0U) {
        set_error(error, error_capacity, "valid client, HTTPS URL, output path, and size limit are required");
        return NETWORK_INVALID_ARGUMENT;
    }

    FILE *const file = fopen(output_path, "wb");
    if (file == NULL) {
        set_error(error, error_capacity, "failed to open download output file");
        return NETWORK_IO_ERROR;
    }

    FileSink sink = {file, 0U, maximum_bytes, false};
    CURL *const curl = curl_easy_init();
    if (curl == NULL) {
        (void)fclose(file);
        (void)remove(output_path);
        set_error(error, error_capacity, "failed to allocate download transport");
        return NETWORK_INTERNAL_ERROR;
    }

    NetworkStatus status = NETWORK_INTERNAL_ERROR;
    if (!configure_curl_common(client, curl, url, 300000L)
        || curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_file) != CURLE_OK
        || curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink) != CURLE_OK) {
        set_error(error, error_capacity, "failed to configure download transport");
        goto cleanup;
    }

    const CURLcode result = curl_easy_perform(curl);
    status = status_from_curl(result);
    if (sink.limit_exceeded) {
        status = NETWORK_INVALID_RESPONSE;
        set_error(error, error_capacity, "download exceeded its size limit");
        goto cleanup;
    }
    if (status != NETWORK_OK) {
        set_error(error, error_capacity, "download failed");
        goto cleanup;
    }

    long status_code = 0L;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code) != CURLE_OK) {
        status = NETWORK_HTTP_ERROR;
        set_error(error, error_capacity, "failed to read download status");
        goto cleanup;
    }
    status = status_from_http(status_code);
    if (status != NETWORK_OK) {
        set_error(error, error_capacity, "download server rejected the request");
        goto cleanup;
    }

    set_error(error, error_capacity, "");

cleanup:
    curl_easy_cleanup(curl);
    const int close_result = fclose(file);
    if (close_result != 0 && status == NETWORK_OK) {
        status = NETWORK_IO_ERROR;
        set_error(error, error_capacity, "failed to finish writing the download");
    }
    if (status != NETWORK_OK) {
        (void)remove(output_path);
    }
    return status;
}

static bool file_has_wave_header(const char *path)
{
    unsigned char header[12];
    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    const size_t bytes_read = fread(header, 1U, sizeof(header), file);
    const int close_result = fclose(file);
    return bytes_read == sizeof(header)
        && close_result == 0
        && memcmp(header, "RIFF", 4U) == 0
        && memcmp(header + 8U, "WAVE", 4U) == 0;
}

NetworkStatus download_audio(
    NetworkClient *client,
    const char *text,
    const char *output_path,
    char *error,
    size_t error_capacity
)
{
    if (client == NULL || !client->initialized || text == NULL || text[0] == '\0'
        || output_path == NULL || output_path[0] == '\0' || strlen(text) > 500U) {
        set_error(error, error_capacity, "valid client, short text, and output path are required");
        return NETWORK_INVALID_ARGUMENT;
    }

    CURL *const escape_handle = curl_easy_init();
    if (escape_handle == NULL) {
        set_error(error, error_capacity, "failed to allocate speech request");
        return NETWORK_INTERNAL_ERROR;
    }

    char *const encoded_text = curl_easy_escape(escape_handle, text, 0);
    char *const encoded_voice = curl_easy_escape(escape_handle, "Mike (for Telephone)", 0);
    if (encoded_text == NULL || encoded_voice == NULL) {
        curl_free(encoded_text);
        curl_free(encoded_voice);
        curl_easy_cleanup(escape_handle);
        set_error(error, error_capacity, "failed to encode speech request");
        return NETWORK_INTERNAL_ERROR;
    }

    char url[PINGMOTE_URL_CAPACITY];
    const int url_length = snprintf(
        url,
        sizeof(url),
        "%s?text=%s&voice=%s",
        TETYYYS_ENDPOINT,
        encoded_text,
        encoded_voice
    );
    curl_free(encoded_text);
    curl_free(encoded_voice);
    curl_easy_cleanup(escape_handle);

    if (url_length < 0 || (size_t)url_length >= sizeof(url)) {
        set_error(error, error_capacity, "speech request is too long");
        return NETWORK_INVALID_ARGUMENT;
    }

    NetworkStatus status = network_download_file(
        client,
        url,
        output_path,
        PINGMOTE_TTS_MAXIMUM_BYTES,
        error,
        error_capacity
    );
    if (status == NETWORK_OK && !file_has_wave_header(output_path)) {
        (void)remove(output_path);
        set_error(error, error_capacity, "speech service returned malformed audio");
        status = NETWORK_INVALID_RESPONSE;
    }
    return status;
}

const char *network_status_name(NetworkStatus status)
{
    static const char *const names[] = {
        "ok",
        "invalid_argument",
        "offline",
        "unauthorized",
        "rate_limited",
        "http_error",
        "invalid_response",
        "io_error",
        "internal_error"
    };
    if (status < NETWORK_OK || status > NETWORK_INTERNAL_ERROR) {
        return "unknown";
    }
    return names[status];
}

void network_cleanup(NetworkClient *client)
{
    if (client == NULL
        || !atomic_load_explicit(&client->initialized, memory_order_acquire)) {
        return;
    }
    curl_global_cleanup();
    atomic_store_explicit(&client->initialized, false, memory_order_release);
}

void network_cancel(NetworkClient *client)
{
    if (client != NULL) {
        atomic_store_explicit(&client->cancel_requested, true, memory_order_release);
    }
}
