#ifndef TILE_LOADER_H
#define TILE_LOADER_H

#include <stdint.h>
#include <stddef.h>

/*
 * Load a PNG file and convert its pixels to GBC 2bpp tile data.
 *
 * The PNG is expected to be 2-bit grayscale (4 shades).
 * Tiles are read left-to-right, top-to-bottom (row-major).
 * Each tile is 8x8 pixels = 16 bytes (2 bytes per row: lo plane, hi plane).
 *
 * Returns malloc'd buffer of 2bpp tile data. Caller must free().
 * Sets *out_size to the number of bytes returned.
 * Returns NULL on failure.
 */
uint8_t *tiles_from_png(const char *path, size_t *out_size);

/*
 * Load a binary file (e.g. .map tilemap) into a malloc'd buffer.
 * Returns NULL on failure. Sets *out_size to byte count.
 */
uint8_t *load_binary_file(const char *path, size_t *out_size);

/*
 * Load a PNG file and convert to 1bpp collision mask tile data.
 *
 * Each 8x8 tile = 8 bytes (1 bit per pixel, 1 byte per row).
 * Bit 7 = leftmost pixel. Dark pixels = 1 (solid), light = 0 (empty).
 * Returns malloc'd buffer. Caller must free().
 */
uint8_t *masks_from_png(const char *path, size_t *out_size);

#endif /* TILE_LOADER_H */
