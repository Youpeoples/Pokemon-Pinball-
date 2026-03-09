/*
 * PNG-to-2bpp tile loader and binary file loader.
 *
 * Converts PNG images to GBC 2bpp tile format.
 * GBC 2bpp format: each tile row is 2 bytes (lo plane, hi plane).
 * Pixel 0 (leftmost) is in bit 7 of each byte.
 * Color 0 = white (lightest), color 3 = black (darkest).
 */

#include "renderer/tile_loader.h"
#include "stb_image.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

uint8_t *tiles_from_png(const char *path, size_t *out_size) {
    int width, height, channels;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 1);
    if (!pixels) {
        fprintf(stderr, "tiles_from_png: failed to load '%s': %s\n",
                path, stbi_failure_reason());
        *out_size = 0;
        return NULL;
    }

    if (width % 8 != 0 || height % 8 != 0) {
        fprintf(stderr, "tiles_from_png: '%s' dimensions %dx%d not multiple of 8\n",
                path, width, height);
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    int tiles_per_row = width / 8;
    int tiles_per_col = height / 8;
    int total_tiles = tiles_per_row * tiles_per_col;
    size_t data_size = (size_t)total_tiles * 16; /* 16 bytes per tile */

    uint8_t *tile_data = malloc(data_size);
    if (!tile_data) {
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    /* Convert each tile */
    for (int ty = 0; ty < tiles_per_col; ty++) {
        for (int tx = 0; tx < tiles_per_row; tx++) {
            int tile_idx = ty * tiles_per_row + tx;
            uint8_t *dst = tile_data + tile_idx * 16;

            for (int row = 0; row < 8; row++) {
                uint8_t lo = 0, hi = 0;
                for (int col = 0; col < 8; col++) {
                    int px = tx * 8 + col;
                    int py = ty * 8 + row;
                    uint8_t gray = pixels[py * width + px];

                    /* N10: Quantize 8-bit grayscale to 2-bit color index.
                     * GBC convention: 0=white(lightest), 3=black(darkest).
                     * Uses >>6 thresholds matching rgbgfx behavior. */
                    uint8_t idx = 3 - (gray >> 6);

                    int bit = 7 - col; /* MSB = leftmost pixel */
                    lo |= ((idx & 1) << bit);
                    hi |= (((idx >> 1) & 1) << bit);
                }
                dst[row * 2] = lo;
                dst[row * 2 + 1] = hi;
            }
        }
    }

    stbi_image_free(pixels);
    *out_size = data_size;
    return tile_data;
}

uint8_t *masks_from_png(const char *path, size_t *out_size) {
    int width, height, channels;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 1);
    if (!pixels) {
        fprintf(stderr, "masks_from_png: failed to load '%s': %s\n",
                path, stbi_failure_reason());
        *out_size = 0;
        return NULL;
    }

    if (width % 8 != 0 || height % 8 != 0) {
        fprintf(stderr, "masks_from_png: '%s' dimensions %dx%d not multiple of 8\n",
                path, width, height);
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    int tiles_per_row = width / 8;
    int tiles_per_col = height / 8;
    int total_tiles = tiles_per_row * tiles_per_col;
    size_t data_size = (size_t)total_tiles * 8; /* 8 bytes per 1bpp tile */

    uint8_t *mask_data = malloc(data_size);
    if (!mask_data) {
        stbi_image_free(pixels);
        *out_size = 0;
        return NULL;
    }

    for (int ty = 0; ty < tiles_per_col; ty++) {
        for (int tx = 0; tx < tiles_per_row; tx++) {
            int tile_idx = ty * tiles_per_row + tx;
            uint8_t *dst = mask_data + tile_idx * 8;

            for (int row = 0; row < 8; row++) {
                uint8_t byte = 0;
                for (int col = 0; col < 8; col++) {
                    int px = tx * 8 + col;
                    int py = ty * 8 + row;
                    uint8_t gray = pixels[py * width + px];
                    /* Dark pixels (< 128) = solid = bit set */
                    if (gray < 128) {
                        byte |= (0x80 >> col);
                    }
                }
                dst[row] = byte;
            }
        }
    }

    stbi_image_free(pixels);
    *out_size = data_size;
    return mask_data;
}

uint8_t *load_binary_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "load_binary_file: failed to open '%s'\n", path);
        *out_size = 0;
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        *out_size = 0;
        return NULL;
    }

    uint8_t *data = malloc((size_t)len);
    if (!data) {
        fclose(f);
        *out_size = 0;
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);

    *out_size = read;
    return data;
}
