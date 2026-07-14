#include "pingmote/sprite.h"

#include <stdio.h>
#include <string.h>

typedef struct SpriteDefinition {
    AnimationState state;
    const char *filename;
    float frames_per_second;
} SpriteDefinition;

static const SpriteDefinition SPRITE_DEFINITIONS[] = {
    {ANIMATION_IDLE, "Idle.png", 4.0F},
    {ANIMATION_TALKING, "Talking.png", 10.0F},
    {ANIMATION_THINKING, "Thinking.png", 6.0F}
};

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0U) {
        (void)snprintf(error, capacity, "%s", message);
    }
}

static bool state_is_valid(AnimationState state)
{
    return state >= ANIMATION_IDLE && state < ANIMATION_STATE_COUNT;
}

static int find_cached_texture(const SpriteSystem *system, const char *path)
{
    for (size_t index = 0U; index < system->texture_count; ++index) {
        if (system->textures[index].loaded
            && strcmp(system->textures[index].path, path) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int load_texture_once(SpriteSystem *system, const char *path)
{
    const int cached_index = find_cached_texture(system, path);
    if (cached_index >= 0) {
        return cached_index;
    }

    if (system->texture_count >= PINGMOTE_SPRITE_CACHE_CAPACITY) {
        TraceLog(LOG_WARNING, "SPRITE: cache capacity reached for %s", path);
        return -1;
    }

    Texture2D texture = LoadTexture(path);
    if (!IsTextureValid(texture)) {
        TraceLog(LOG_WARNING, "SPRITE: failed to load %s", path);
        return -1;
    }

    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    SpriteTextureEntry *const entry = &system->textures[system->texture_count];
    const int copied = snprintf(entry->path, sizeof(entry->path), "%s", path);
    if (copied < 0 || (size_t)copied >= sizeof(entry->path)) {
        TraceLog(LOG_WARNING, "SPRITE: path is too long: %s", path);
        UnloadTexture(texture);
        return -1;
    }

    entry->texture = texture;
    entry->loaded = true;
    const int loaded_index = (int)system->texture_count;
    system->texture_count += 1U;
    return loaded_index;
}

static void bind_texture(
    SpriteSystem *system,
    AnimationState state,
    int texture_index,
    float frames_per_second
)
{
    if (!state_is_valid(state) || texture_index < 0) {
        return;
    }

    const Texture2D texture = system->textures[texture_index].texture;
    const int frame_width = texture.height > 0 && texture.width % texture.height == 0
        ? texture.height
        : texture.width;
    const int frame_height = texture.height;
    const int frame_count = frame_width > 0 ? texture.width / frame_width : 1;

    system->bindings[state] = (SpriteBinding){
        .texture_index = texture_index,
        .frame_width = frame_width,
        .frame_height = frame_height,
        .frame_count = frame_count > 0 ? frame_count : 1,
        .frames_per_second = frames_per_second
    };
}

static int first_available_texture(const SpriteSystem *system)
{
    static const AnimationState preferred[] = {
        ANIMATION_IDLE,
        ANIMATION_THINKING,
        ANIMATION_TALKING
    };

    for (size_t index = 0U; index < sizeof(preferred) / sizeof(preferred[0]); ++index) {
        const int texture_index = system->bindings[preferred[index]].texture_index;
        if (texture_index >= 0) {
            return texture_index;
        }
    }
    return -1;
}

static void bind_fallback(
    SpriteSystem *system,
    AnimationState destination,
    AnimationState preferred,
    AnimationState secondary
)
{
    if (system->bindings[destination].texture_index >= 0) {
        return;
    }

    int texture_index = system->bindings[preferred].texture_index;
    if (texture_index < 0) {
        texture_index = system->bindings[secondary].texture_index;
    }
    if (texture_index < 0) {
        texture_index = first_available_texture(system);
    }
    bind_texture(system, destination, texture_index, 4.0F);
}

bool sprite_system_init(
    SpriteSystem *system,
    const char *asset_directory,
    char *error,
    size_t error_capacity
)
{
    if (system == NULL || asset_directory == NULL || asset_directory[0] == '\0') {
        set_error(error, error_capacity, "sprite system and asset directory are required");
        return false;
    }

    *system = (SpriteSystem){0};
    for (int state = 0; state < (int)ANIMATION_STATE_COUNT; ++state) {
        system->bindings[state].texture_index = -1;
    }

    for (size_t index = 0U;
         index < sizeof(SPRITE_DEFINITIONS) / sizeof(SPRITE_DEFINITIONS[0]);
         ++index) {
        char path[PINGMOTE_SPRITE_PATH_CAPACITY];
        const SpriteDefinition definition = SPRITE_DEFINITIONS[index];
        const int path_length = snprintf(
            path,
            sizeof(path),
            "%s/%s",
            asset_directory,
            definition.filename
        );
        if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
            TraceLog(LOG_WARNING, "SPRITE: asset path is too long for %s", definition.filename);
            continue;
        }

        const int texture_index = load_texture_once(system, path);
        bind_texture(system, definition.state, texture_index, definition.frames_per_second);
    }

    bind_fallback(system, ANIMATION_IDLE, ANIMATION_THINKING, ANIMATION_TALKING);
    bind_fallback(system, ANIMATION_TALKING, ANIMATION_THINKING, ANIMATION_IDLE);
    bind_fallback(system, ANIMATION_THINKING, ANIMATION_IDLE, ANIMATION_TALKING);
    bind_fallback(system, ANIMATION_LISTENING, ANIMATION_THINKING, ANIMATION_IDLE);
    bind_fallback(system, ANIMATION_SLEEPING, ANIMATION_IDLE, ANIMATION_THINKING);
    bind_fallback(system, ANIMATION_BOUNCE, ANIMATION_IDLE, ANIMATION_TALKING);

    system->initialized = true;
    if (system->texture_count == 0U) {
        set_error(error, error_capacity, "no sprite textures could be loaded");
        return false;
    }

    set_error(error, error_capacity, "");
    return true;
}

bool sprite_system_has_state(const SpriteSystem *system, AnimationState state)
{
    return system != NULL
        && system->initialized
        && state_is_valid(state)
        && system->bindings[state].texture_index >= 0;
}

void sprite_system_draw(
    const SpriteSystem *system,
    AnimationState state,
    Vector2 center,
    float size,
    float elapsed_seconds,
    Color tint
)
{
    if (!sprite_system_has_state(system, state) || size <= 0.0F) {
        return;
    }

    const SpriteBinding binding = system->bindings[state];
    const Texture2D texture = system->textures[binding.texture_index].texture;
    int frame_index = 0;
    if (binding.frame_count > 1 && binding.frames_per_second > 0.0F) {
        frame_index = (int)(elapsed_seconds * binding.frames_per_second) % binding.frame_count;
    }

    const Rectangle source = {
        (float)(frame_index * binding.frame_width),
        0.0F,
        (float)binding.frame_width,
        (float)binding.frame_height
    };
    const Rectangle destination = {
        center.x,
        center.y,
        size,
        size * ((float)binding.frame_height / (float)binding.frame_width)
    };
    const Vector2 origin = {destination.width * 0.5F, destination.height * 0.5F};
    DrawTexturePro(texture, source, destination, origin, 0.0F, tint);
}

void sprite_system_cleanup(SpriteSystem *system)
{
    if (system == NULL) {
        return;
    }

    for (size_t index = 0U; index < system->texture_count; ++index) {
        if (system->textures[index].loaded) {
            UnloadTexture(system->textures[index].texture);
            system->textures[index].loaded = false;
        }
    }

    system->texture_count = 0U;
    system->initialized = false;
}
