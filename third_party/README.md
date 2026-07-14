# Third-party dependencies

Dependencies are not vendored in Git. CMake fetches immutable release archives over HTTPS and verifies their SHA-256 digests before extraction:

- raylib 5.5 — windowing and rendering
- cJSON 1.7.18 — bounded request/response JSON handling
- miniaudio 0.11.25 — asynchronous playback and microphone capture
- whisper.cpp 1.8.6 — local transcription with Metal, Accelerate, and CPU backends

libcurl and platform frameworks come from the host toolchain. Dependency examples, servers, tools, and upstream tests are disabled in PingMote builds. The application does not use whisper.cpp’s SDL or curl helpers; capture and model download stay behind PingMote’s existing miniaudio and HTTPS boundaries.

The runtime `tiny.en` model is downloaded from whisper.cpp’s official Hugging Face repository on first use. Its expected size and SHA-256 digest are pinned in `src/assistant.c`; a malformed or partial model is deleted before it can be loaded.
