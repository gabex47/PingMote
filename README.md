# PingMote

PingMote is a tiny native desktop creature. It stays out of the way, responds in a few words, and uses motion instead of conversation chrome to feel alive.

## Status

This repository contains the production-oriented foundation:

- C23 desktop application built with CMake and raylib
- transparent, borderless, always-on-top, draggable 60 FPS window
- allocation-free animation state machine with six states
- isolated audio and HTTPS networking modules
- authenticated Supabase Edge Function client
- Supabase migrations with per-user Row Level Security
- a JWT-protected `chat` Edge Function with strict short-reply enforcement

The creature currently uses a lightweight procedural renderer. Sprite and sound assets can be added without changing application or state-management code.

## Prerequisites

- macOS 13 or newer
- Xcode Command Line Tools
- CMake 3.24 or newer
- libcurl development files (included with macOS; Homebrew curl also works)

raylib and cJSON are discovered locally first. If unavailable, CMake fetches pinned releases during configuration.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/pingmote
```

For a release build:

```sh
cmake --preset release
cmake --build --preset release
```

## Controls

- Drag the top bar to move Mote.
- Press `1`–`6` to select Idle, Talking, Listening, Thinking, Sleeping, or Bounce.
- Click Mote to bounce.
- Press `M` to toggle reduced motion.
- Press `Q` or click the close button to quit.

## Backend configuration

The desktop client never contains provider secrets. Supply public project configuration and the signed-in user's short-lived access token at runtime:

```sh
export PINGMOTE_SUPABASE_URL="https://fclgpxemiseqozwwhktd.supabase.co"
export PINGMOTE_SUPABASE_PUBLISHABLE_KEY="sb_publishable_..."
export PINGMOTE_ACCESS_TOKEN="user-jwt"
```

`PINGMOTE_SUPABASE_PUBLISHABLE_KEY` is a client-safe project identifier, not an LLM secret. The language-model key exists only as the Edge Function secret `OPENAI_API_KEY`. See [docs/backend.md](docs/backend.md) for deployment and secret configuration.

## Project layout

```text
assets/                  Sprite and audio asset contracts
docs/                    Architecture and backend operations
include/pingmote/         Public C module interfaces
src/                     Native application implementation
supabase/functions/chat/ Edge Function source
supabase/migrations/     Applied database migrations
tests/                   Fast native unit tests
third_party/             Third-party integration notes
```

## Architecture

The window/application layer owns raylib. Animation state, audio, and networking remain isolated behind small C APIs, so whisper.cpp, miniaudio playback, caching, and OS-specific integrations can be introduced without coupling them to rendering. Network requests use fixed-size response buffers and HTTPS-only libcurl settings; credentials are read at runtime and are never logged.

## License

Copyright © 2026 PingMote contributors. All rights reserved.
