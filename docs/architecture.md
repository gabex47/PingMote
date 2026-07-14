# Architecture

## Runtime boundaries

`app` owns the raylib window and is the only layer that draws or reads UI input. Its frame loop performs bounded memory operations, polls worker events, updates animation/audio state, and renders at 60 FPS.

`assistant` owns one background worker and fixed-capacity command/event rings. The worker performs all potentially blocking HTTP, Keychain, cache, model, and transcription operations. Shutdown publishes a cancellation flag to libcurl and whisper before joining the worker.

```text
text / push-to-talk
        |
        v
fixed command ring ----> assistant worker
                             |  |  |
                       Groq/TTS cache/whisper
                             |  |  |
        renderer <----- fixed event ring
            |
            v
 animation enum + async miniaudio playback
```

No subsystem calls raylib from another thread. miniaudio callbacks only copy capture samples or set an atomic completion flag; resource destruction and application callbacks happen on their owning thread.

## Modules

- `animation`: deterministic allocation-free state machine.
- `sprite`: one-load texture cache, per-state fallback mapping, frame timing, and horizontal sprite-sheet support.
- `audio`: asynchronous decoded playback and render-thread completion dispatch.
- `microphone`: bounded 16 kHz mono capture buffer with a 30-second ceiling.
- `speech`: whisper.cpp model lifecycle and local transcription.
- `network`: HTTPS-only libcurl transport, bounded responses/downloads, cancellation, Groq JSON, and Tetyys WAV validation.
- `reply`: markdown/emoji stripping, lowercase normalization, hard word limit, and offline phrases.
- `cache`: collision-checked prompt entries, TTS reuse, private permissions, atomic writes, and seven-day pruning.
- `secure_store`: macOS Keychain access with explicit failure when secure storage is unavailable.
- `resources`: bundle-first sprite discovery with a source-tree development fallback.

## Data and credentials

Groq and optional Supabase values are stored as one Keychain generic-password item. Keychain calls run on the worker. Temporary UI and queue copies are overwritten after use. Keys are never logged, cached, embedded, or sent anywhere except their configured provider.

Prompt/reply caches contain no credentials but may contain user text, so their directory is mode `0700` and files are mode `0600`. Conversation history is not persisted. TTS cache artifacts are retained only for reuse and expire automatically.

## Speech path

Push-to-talk initializes and starts miniaudio capture on the worker. Release stops the device before transcription reads the stable preallocated sample buffer. First use downloads `ggml-tiny.en.bin` from whisper.cpp’s official model repository, enforces the exact byte limit, verifies a pinned SHA-256 digest, and atomically renames the partial file. Inference uses Metal/Accelerate on Apple Silicon with a CPU backend available to whisper.cpp.

## Extension points

The command/event boundary accommodates later transcription engines, offline response providers, personality profiles, and memory decisions without changing rendering. Animation states already decouple behavior from available art, and the resource loader accepts future horizontal sprite sheets. Platform credential and bundle adapters are isolated for future Windows and Linux implementations.
