# Third-party dependencies

Dependencies are intentionally not vendored in Git. CMake first uses compatible system packages and otherwise fetches pinned, SHA-256-verified raylib, cJSON, and miniaudio release archives. libcurl comes from the platform package manager.

whisper.cpp is integrated separately behind the transcription boundary so its model lifecycle never leaks into rendering or audio playback code.
