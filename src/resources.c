#include "pingmote/resources.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

static bool directory_exists(const char *path)
{
    struct stat info;
    return path != NULL && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool copy_fallback(const char *fallback, char *path, size_t capacity)
{
    if (fallback == NULL || path == NULL || capacity == 0U
        || strlen(fallback) >= capacity || !directory_exists(fallback)) {
        return false;
    }
    (void)snprintf(path, capacity, "%s", fallback);
    return true;
}

bool resource_find_sprite_directory(
    const char *development_fallback,
    char *path,
    size_t path_capacity
)
{
    if (path == NULL || path_capacity == 0U) {
        return false;
    }
    path[0] = '\0';

#if defined(__APPLE__)
    const CFBundleRef bundle = CFBundleGetMainBundle();
    CFURLRef resources = bundle == NULL ? NULL : CFBundleCopyResourcesDirectoryURL(bundle);
    if (resources != NULL) {
        UInt8 base[1024];
        const Boolean converted = CFURLGetFileSystemRepresentation(
            resources,
            true,
            base,
            (CFIndex)sizeof(base)
        );
        CFRelease(resources);
        if (converted) {
            const int length = snprintf(
                path,
                path_capacity,
                "%s/assets/sprites",
                (const char *)base
            );
            if (length >= 0 && (size_t)length < path_capacity && directory_exists(path)) {
                return true;
            }
        }
    }
#endif

    path[0] = '\0';
    return copy_fallback(development_fallback, path, path_capacity);
}
