#ifndef PINGMOTE_SPRITE_H
#define PINGMOTE_SPRITE_H

#include "pingmote/animation.h"

#include <raylib.h>

#include <stdbool.h>
#include <stddef.h>

#define PINGMOTE_SPRITE_CACHE_CAPACITY 8
#define PINGMOTE_SPRITE_PATH_CAPACITY 1024

typedef struct SpriteTextureEntry {
    Texture2D texture;
    char path[PINGMOTE_SPRITE_PATH_CAPACITY];
    bool loaded;
} SpriteTextureEntry;

typedef struct SpriteBinding {
    int texture_index;
    int frame_width;
    int frame_height;
    int frame_count;
    float frames_per_second;
} SpriteBinding;

typedef struct SpriteSystem {
    SpriteTextureEntry textures[PINGMOTE_SPRITE_CACHE_CAPACITY];
    SpriteBinding bindings[ANIMATION_STATE_COUNT];
    size_t texture_count;
    bool initialized;
} SpriteSystem;

bool sprite_system_init(
    SpriteSystem *system,
    const char *asset_directory,
    char *error,
    size_t error_capacity
);
bool sprite_system_has_state(const SpriteSystem *system, AnimationState state);
void sprite_system_draw(
    const SpriteSystem *system,
    AnimationState state,
    Vector2 center,
    float size,
    float elapsed_seconds,
    Color tint
);
void sprite_system_cleanup(SpriteSystem *system);

#endif
