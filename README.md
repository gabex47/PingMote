# PingMote

PingMote is a tiny native desktop creature written in C23. It stays above other windows, answers in a few words, speaks with a deliberately crusty SAPI4 voice, and uses animation instead of chatbot chrome.

## What works

- transparent, borderless, always-on-top macOS app at 60 FPS
- supplied Idle, Talking, and Thinking PNGs with cached sprite-sheet-ready loading
- compact text input, Enter-to-send, fading speech bubble, and keyboard navigation
- direct Groq chat with `llama-3.1-8b-instant` and locally enforced short plain-text replies
- Tetyys `Mike (for Telephone)` speech, asynchronous miniaudio playback, and expiring audio reuse
- hold-to-talk microphone capture and local whisper.cpp transcription
- macOS Keychain storage for the Groq key and optional Supabase configuration
- seven-day prompt/reply and generated-audio cache
- built-in offline replies; animation, input, and voice recording remain available without a network
- one background worker for network, Keychain, cache, model loading, and transcription work

Conversation history is never stored. Cached prompts and replies expire automatically and credentials never enter the cache.

## Requirements

- macOS 13 or newer
- Xcode Command Line Tools
- CMake 3.24 or newer
- Ninja
- libcurl development files (the macOS or Homebrew installation works)
- an internet connection during the first configure, unless all dependencies are already available locally

CMake uses pinned, SHA-256-verified releases of raylib 5.5, cJSON 1.7.18, miniaudio 0.11.25, and whisper.cpp 1.8.6.

## Build and run

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
open build/dev/PingMote.app
```

For terminal logs, run the bundle executable directly:

```sh
./build/dev/PingMote.app/Contents/MacOS/PingMote
```

Release build:

```sh
cmake --preset release
cmake --build --preset release
open build/release/PingMote.app
```

## First launch

1. Open **settings** in PingMote.
2. Paste a Groq API key and choose **save securely**.
3. Hold the **mic** button to speak, or type a message and press Enter.
4. Approve the macOS microphone prompt when using push-to-talk for the first time.

The first transcription downloads the pinned 74 MB `tiny.en` whisper model, verifies its SHA-256 digest, and keeps it in the user cache. Later transcriptions are local and do not upload microphone audio.

## Controls

- Drag the header to move PingMote.
- Click Mote to bounce.
- Type and press Enter to send.
- Hold **mic** with the pointer, or Tab to it and hold Enter, for push-to-talk.
- Use Tab and Shift-Tab to navigate controls.
- Click **settings** to replace or clear saved credentials.
- Press Command-Q or click the close button to quit.
- Set `PINGMOTE_REDUCED_MOTION=1` before launch to disable idle movement.

## Local data

On macOS, non-secret cache files live under:

```text
~/Library/Caches/com.pingmote.desktop/
```

Prompt/reply and TTS entries expire after seven days. Partial downloads are removed after failure or startup cleanup. The whisper model remains cached until the user removes it. Groq and optional Supabase credentials are stored only in macOS Keychain.

## Optional live tests

Unit tests never require a network or microphone. Maintainers can explicitly enable live provider tests:

```sh
cmake -S . -B build/live -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DPINGMOTE_NETWORK_TESTS=ON
cmake --build build/live
ctest --test-dir build/live -L network --output-on-failure
```

The live tests download a short Tetyys sample, play and remove it, and verify whisper against its official sample. They do not exercise Groq because no API key is read by the test process.

## Project layout

```text
assets/                  Bundled sprite and audio assets
cmake/                   macOS bundle metadata
docs/                    Architecture and backend operations
include/pingmote/         Public C module interfaces
src/                     Native application implementation
supabase/functions/chat/ Optional hosted Edge Function
supabase/migrations/     Applied database migrations
tests/                   Unit and opt-in live smoke tests
third_party/             Dependency policy
```

The optional Supabase backend remains available for future accounts and synced memories. Phase 2 chat calls the user’s Groq account directly; neither Groq nor microphone audio passes through Supabase.

## License

Copyright © 2026 PingMote contributors. All rights reserved.
