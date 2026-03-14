/* embedded_data.c - Binary data loader with embedded fallback
 *
 * Tries to load from external file first (for modding override),
 * falls back to compiled-in embedded data arrays.
 * Always returns a malloc'd copy so callers can free() as before.
 */

#include "embedded_data.h"
#include "../renderer/tile_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Route a relative path to the correct embedded data accessor.
 * Returns pointer to static const data (do NOT free). */
static const uint8_t *embedded_lookup(const char *relative_path, size_t *out_size) {
    const uint8_t *result;

    /* Normalize backslashes to forward slashes for matching */
    char normalized[512];
    size_t len = strlen(relative_path);
    if (len >= sizeof(normalized)) len = sizeof(normalized) - 1;
    for (size_t i = 0; i < len; i++) {
        normalized[i] = (relative_path[i] == '\\') ? '/' : relative_path[i];
    }
    normalized[len] = '\0';

    /* Route based on path patterns */
    if (strstr(normalized, "data/collision/maps/")) {
        result = embedded_collision_map(normalized, out_size);
        if (result) return result;
    }

    if (strstr(normalized, "data/collision/flippers/")) {
        result = embedded_flipper_data(normalized, out_size);
        if (result) return result;
    }

    if (strstr(normalized, "data/tilt/")) {
        result = embedded_tilt_table(normalized, out_size);
        if (result) return result;
    }

    if (strstr(normalized, "ball_physics")) {
        result = embedded_physics_data(normalized, out_size);
        if (result) return result;
    }

    if (strstr(normalized, "collision_angles")) {
        result = embedded_collision_angles(normalized, out_size);
        if (result) return result;
    }

    /* Check file extension for tilemaps and bgattr */
    const char *ext = strrchr(normalized, '.');
    if (ext) {
        if (strcmp(ext, ".map") == 0) {
            result = embedded_tilemap(normalized, out_size);
            if (result) return result;
        }
        if (strcmp(ext, ".bgattr") == 0) {
            result = embedded_bgattr(normalized, out_size);
            if (result) return result;
        }
    }

    *out_size = 0;
    return NULL;
}

uint8_t *load_binary_data(const char *base_path,
                          const char *relative_path,
                          size_t *out_size) {
    /* Step 1: Try external file (modding override) */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", base_path, relative_path);
    uint8_t *data = load_binary_file(path, out_size);
    if (data) return data;

    /* Step 2: Fall back to embedded data */
    size_t embedded_size = 0;
    const uint8_t *embedded = embedded_lookup(relative_path, &embedded_size);
    if (embedded && embedded_size > 0) {
        data = malloc(embedded_size);
        if (data) {
            memcpy(data, embedded, embedded_size);
            *out_size = embedded_size;
            return data;
        }
    }

    *out_size = 0;
    return NULL;
}
