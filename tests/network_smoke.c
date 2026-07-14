#define _POSIX_C_SOURCE 200809L

#include "pingmote/audio.h"
#include "pingmote/network.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    NetworkClient client;
    char error[192];
    if (!network_init(&client, error, sizeof(error))) {
        (void)fprintf(stderr, "network init failed: %s\n", error);
        return 1;
    }

    char path[256];
    const int length = snprintf(
        path,
        sizeof(path),
        "/tmp/pingmote-tts-smoke-%ld.wav",
        (long)getpid()
    );
    if (length < 0 || (size_t)length >= sizeof(path)) {
        network_cleanup(&client);
        return 1;
    }

    const NetworkStatus status = download_audio(
        &client,
        "yo boss",
        path,
        error,
        sizeof(error)
    );
    if (status != NETWORK_OK) {
        (void)fprintf(
            stderr,
            "Tetyys smoke test failed (%s): %s\n",
            network_status_name(status),
            error
        );
        network_cleanup(&client);
        return 1;
    }

    AudioManager audio;
    if (!audio_init(&audio, error, sizeof(error))
        || !play_audio(&audio, path, error, sizeof(error))) {
        (void)fprintf(stderr, "audio smoke test failed: %s\n", error);
        cleanup_audio(&audio);
        network_cleanup(&client);
        (void)remove(path);
        return 1;
    }

    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 10000000L};
    int checks = 0;
    while (audio_is_playing(&audio) && checks < 3000) {
        audio_update(&audio);
        (void)nanosleep(&interval, NULL);
        checks += 1;
    }
    const bool completed = !audio_is_playing(&audio);
    cleanup_audio(&audio);
    network_cleanup(&client);
    (void)remove(path);
    if (!completed) {
        (void)fprintf(stderr, "audio playback did not complete\n");
        return 1;
    }
    return 0;
}
