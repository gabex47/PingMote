#ifndef PINGMOTE_SECURE_STORE_H
#define PINGMOTE_SECURE_STORE_H

#include <stdbool.h>
#include <stddef.h>

#define PINGMOTE_API_KEY_CAPACITY 513U
#define PINGMOTE_SUPABASE_URL_CAPACITY 2049U

typedef struct SecureSettings {
    char groq_api_key[PINGMOTE_API_KEY_CAPACITY];
    char supabase_url[PINGMOTE_SUPABASE_URL_CAPACITY];
    char supabase_key[PINGMOTE_API_KEY_CAPACITY];
} SecureSettings;

typedef enum SecureStoreStatus {
    SECURE_STORE_OK = 0,
    SECURE_STORE_NOT_FOUND,
    SECURE_STORE_UNAVAILABLE,
    SECURE_STORE_ACCESS_DENIED,
    SECURE_STORE_INVALID_DATA,
    SECURE_STORE_ERROR
} SecureStoreStatus;

bool secure_store_is_available(void);
SecureStoreStatus secure_store_load(
    SecureSettings *settings,
    char *error,
    size_t error_capacity
);
SecureStoreStatus secure_store_save(
    const SecureSettings *settings,
    char *error,
    size_t error_capacity
);
void secure_settings_clear(SecureSettings *settings);

#endif
