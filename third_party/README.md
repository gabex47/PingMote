# Third-party dependencies

Dependencies are intentionally not vendored in Git. CMake first uses compatible system packages and otherwise fetches pinned raylib and cJSON release tags. libcurl comes from the platform package manager.

miniaudio and whisper.cpp will be integrated behind the existing audio and future transcription boundaries when those features are implemented.
