#include "pingmote/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char path[160];
    const int length = snprintf(path, sizeof(path), "/tmp/pingmote-sha256-%ld", (long)getpid());
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *const file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite("abc", 1U, 3U, file) == 3U);
    assert(fclose(file) == 0);

    char digest[PINGMOTE_SHA256_HEX_CAPACITY];
    char error[160];
    assert(sha256_file(path, digest, error, sizeof(error)));
    assert(strcmp(
        digest,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    ) == 0);
    assert(sha256_file_matches(
        path,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        error,
        sizeof(error)
    ));
    assert(remove(path) == 0);
    return 0;
}
