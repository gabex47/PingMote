#include "pingmote/secure_store.h"

#include <cJSON.h>

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

void secure_settings_clear(SecureSettings *settings)
{
    if (settings == NULL) {
        return;
    }
    volatile unsigned char *cursor = (volatile unsigned char *)settings;
    for (size_t index = 0U; index < sizeof(*settings); ++index) {
        cursor[index] = 0U;
    }
}

#if defined(__APPLE__)
static const char KEYCHAIN_SERVICE[] = "com.pingmote.desktop";
static const char KEYCHAIN_ACCOUNT[] = "settings-v1";

static CFMutableDictionaryRef create_query(void)
{
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    if (query == NULL) {
        return NULL;
    }

    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault,
        KEYCHAIN_SERVICE,
        kCFStringEncodingUTF8
    );
    CFStringRef account = CFStringCreateWithCString(
        kCFAllocatorDefault,
        KEYCHAIN_ACCOUNT,
        kCFStringEncodingUTF8
    );
    if (service == NULL || account == NULL) {
        if (service != NULL) {
            CFRelease(service);
        }
        if (account != NULL) {
            CFRelease(account);
        }
        CFRelease(query);
        return NULL;
    }

    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

static SecureStoreStatus map_status(OSStatus status)
{
    if (status == errSecSuccess) {
        return SECURE_STORE_OK;
    }
    if (status == errSecItemNotFound) {
        return SECURE_STORE_NOT_FOUND;
    }
    if (status == errSecAuthFailed || status == errSecInteractionNotAllowed
        || status == errSecUserCanceled) {
        return SECURE_STORE_ACCESS_DENIED;
    }
    if (status == errSecNotAvailable) {
        return SECURE_STORE_UNAVAILABLE;
    }
    return SECURE_STORE_ERROR;
}

bool secure_store_is_available(void)
{
    return true;
}

SecureStoreStatus secure_store_load(
    SecureSettings *settings,
    char *error,
    size_t error_capacity
)
{
    if (settings == NULL) {
        set_error(error, error_capacity, "settings output is required");
        return SECURE_STORE_ERROR;
    }
    *settings = (SecureSettings){0};

    CFMutableDictionaryRef query = create_query();
    if (query == NULL) {
        set_error(error, error_capacity, "failed to create Keychain request");
        return SECURE_STORE_ERROR;
    }
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFTypeRef result = NULL;
    const OSStatus os_status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    const SecureStoreStatus status = map_status(os_status);
    if (status == SECURE_STORE_NOT_FOUND) {
        set_error(error, error_capacity, "");
        return status;
    }
    if (status != SECURE_STORE_OK || result == NULL || CFGetTypeID(result) != CFDataGetTypeID()) {
        if (result != NULL) {
            CFRelease(result);
        }
        set_error(error, error_capacity, "could not read credentials from Keychain");
        return status == SECURE_STORE_OK ? SECURE_STORE_INVALID_DATA : status;
    }

    const CFDataRef data = (CFDataRef)result;
    const CFIndex length = CFDataGetLength(data);
    if (length <= 0 || (size_t)length >= 8192U) {
        CFRelease(result);
        set_error(error, error_capacity, "Keychain credentials are malformed");
        return SECURE_STORE_INVALID_DATA;
    }

    char json[8192];
    CFDataGetBytes(data, CFRangeMake(0, length), (UInt8 *)json);
    json[length] = '\0';
    CFRelease(result);

    cJSON *const object = cJSON_ParseWithLength(json, (size_t)length);
    const cJSON *const groq = object == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(object, "groq_api_key");
    const cJSON *const url = object == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(object, "supabase_url");
    const cJSON *const key = object == NULL ? NULL
        : cJSON_GetObjectItemCaseSensitive(object, "supabase_key");
    const bool valid = cJSON_IsString(groq) && groq->valuestring != NULL
        && cJSON_IsString(url) && url->valuestring != NULL
        && cJSON_IsString(key) && key->valuestring != NULL
        && strlen(groq->valuestring) < sizeof(settings->groq_api_key)
        && strlen(url->valuestring) < sizeof(settings->supabase_url)
        && strlen(key->valuestring) < sizeof(settings->supabase_key);
    if (valid) {
        (void)snprintf(settings->groq_api_key, sizeof(settings->groq_api_key), "%s", groq->valuestring);
        (void)snprintf(settings->supabase_url, sizeof(settings->supabase_url), "%s", url->valuestring);
        (void)snprintf(settings->supabase_key, sizeof(settings->supabase_key), "%s", key->valuestring);
    }
    cJSON_Delete(object);
    if (!valid) {
        secure_settings_clear(settings);
        set_error(error, error_capacity, "Keychain credentials are malformed");
        return SECURE_STORE_INVALID_DATA;
    }

    set_error(error, error_capacity, "");
    return SECURE_STORE_OK;
}

SecureStoreStatus secure_store_save(
    const SecureSettings *settings,
    char *error,
    size_t error_capacity
)
{
    if (settings == NULL) {
        set_error(error, error_capacity, "settings are required");
        return SECURE_STORE_ERROR;
    }

    cJSON *const object = cJSON_CreateObject();
    if (object == NULL
        || cJSON_AddStringToObject(object, "groq_api_key", settings->groq_api_key) == NULL
        || cJSON_AddStringToObject(object, "supabase_url", settings->supabase_url) == NULL
        || cJSON_AddStringToObject(object, "supabase_key", settings->supabase_key) == NULL) {
        cJSON_Delete(object);
        set_error(error, error_capacity, "failed to prepare credentials");
        return SECURE_STORE_ERROR;
    }

    char *const json = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    if (json == NULL) {
        set_error(error, error_capacity, "failed to prepare credentials");
        return SECURE_STORE_ERROR;
    }

    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault,
        (const UInt8 *)json,
        (CFIndex)strlen(json)
    );
    cJSON_free(json);
    CFMutableDictionaryRef query = create_query();
    if (data == NULL || query == NULL) {
        if (data != NULL) {
            CFRelease(data);
        }
        if (query != NULL) {
            CFRelease(query);
        }
        set_error(error, error_capacity, "failed to create Keychain request");
        return SECURE_STORE_ERROR;
    }

    CFMutableDictionaryRef changes = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    if (changes == NULL) {
        CFRelease(query);
        CFRelease(data);
        set_error(error, error_capacity, "failed to create Keychain request");
        return SECURE_STORE_ERROR;
    }
    CFDictionarySetValue(changes, kSecValueData, data);
    OSStatus os_status = SecItemUpdate(query, changes);
    if (os_status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        os_status = SecItemAdd(query, NULL);
    }

    CFRelease(changes);
    CFRelease(query);
    CFRelease(data);
    const SecureStoreStatus status = map_status(os_status);
    set_error(
        error,
        error_capacity,
        status == SECURE_STORE_OK ? "" : "could not save credentials to Keychain"
    );
    return status;
}
#else
bool secure_store_is_available(void)
{
    return false;
}

SecureStoreStatus secure_store_load(
    SecureSettings *settings,
    char *error,
    size_t error_capacity
)
{
    if (settings != NULL) {
        *settings = (SecureSettings){0};
    }
    set_error(error, error_capacity, "secure credential storage is unavailable on this platform");
    return SECURE_STORE_UNAVAILABLE;
}

SecureStoreStatus secure_store_save(
    const SecureSettings *settings,
    char *error,
    size_t error_capacity
)
{
    (void)settings;
    set_error(error, error_capacity, "secure credential storage is unavailable on this platform");
    return SECURE_STORE_UNAVAILABLE;
}
#endif
