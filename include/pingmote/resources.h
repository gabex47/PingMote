#ifndef PINGMOTE_RESOURCES_H
#define PINGMOTE_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>

bool resource_find_sprite_directory(
    const char *development_fallback,
    char *path,
    size_t path_capacity
);

#endif
