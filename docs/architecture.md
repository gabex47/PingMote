# Architecture

## Native process

`app` owns the window lifecycle and all raylib calls. `animation` is a deterministic, allocation-free state machine with no graphics dependency. `network` owns libcurl and cJSON, validates all inputs, allows HTTPS only, and writes into caller-provided fixed buffers. `audio` defines the stable playback boundary while the initial build remains silent.

This separation keeps platform work local: future Windows and Linux window adapters can replace `app.c`; miniaudio can replace the audio internals; and whisper.cpp can publish transcription events without changing rendering.

## State and event flow

```text
input / future speech / network
              |
              v
      AnimationState enum
              |
              v
      renderer or sprite pack
```

Transitions reset elapsed state time. Bounce is transient and returns to the state that preceded it. A reduced-motion mode preserves state changes while removing movement.

## Backend boundary

The desktop app calls only the authenticated Supabase `chat` Edge Function. The gateway verifies the user's JWT before the function runs. Provider credentials are stored exclusively as Supabase secrets, and the response is validated again by the native client.

Conversations are not persisted. The database contains only user profiles, preferences, and explicitly selected memories protected by Row Level Security.

## Extension points

- Sprite packs: add an asset loader and renderer behind the current draw function.
- Voice: implement the existing audio boundary with miniaudio; keep synthesis server-side or in an isolated adapter.
- Transcription: add a whisper.cpp worker that emits text events to the app loop.
- Offline mode: add a local response provider behind the same chat request boundary.
- Cache: use the OS application-data directory and bounded LRU metadata.
- Personalities and idle behaviors: drive the existing animation enum through configuration and timed events.
