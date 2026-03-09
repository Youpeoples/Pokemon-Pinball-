#ifndef VRAM_H
#define VRAM_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Virtual VRAM - emulates the GBC's dual-bank video memory.
 *
 * GBC VRAM layout ($8000-$9FFF, 8KB per bank, 2 banks):
 *   $8000-$97FF: Tile data (384 tiles x 16 bytes = 6144 bytes)
 *   $9800-$9BFF: BG tilemap (32x32 = 1024 bytes)
 *   $9C00-$9FFF: Window tilemap (32x32 = 1024 bytes)
 *
 * Bank 0 holds tile pixels and tile indices.
 * Bank 1 holds alternate tile pixels and BG attribute bytes.
 */

#define VRAM_TILE_DATA_SIZE  6144  /* $8000-$97FF: 384 tiles x 16 bytes */
#define VRAM_MAP_SIZE        1024  /* 32x32 tilemap */

typedef struct VirtualVRAM {
    uint8_t tile_data[2][VRAM_TILE_DATA_SIZE];  /* 2 banks of tile data */
    uint8_t bg_map[2][VRAM_MAP_SIZE];           /* bank 0=indices, bank 1=attributes */
    uint8_t win_map[2][VRAM_MAP_SIZE];          /* window tilemap */
} VirtualVRAM;

/* Create and zero-initialize a VirtualVRAM */
VirtualVRAM *vram_create(void);

/* Free a VirtualVRAM */
void vram_free(VirtualVRAM *vram);

/* Write data into virtual VRAM at a GBC address.
 * Maps $8000-$97FF to tile_data, $9800-$9BFF to bg_map, $9C00-$9FFF to win_map.
 * Returns true on success. */
bool vram_write(VirtualVRAM *vram, uint8_t bank, uint16_t gbc_addr,
                const uint8_t *src, uint16_t size);

/* Write a single tile index to VRAM (equivalent of ASM PutTileInVRAM, 0x848).
 * Used by RedrawSoundTestID and key config display to update tilemaps. */
static inline void put_tile_in_vram(VirtualVRAM *vram, uint8_t bank,
                                     uint16_t gbc_addr, uint8_t tile) {
    vram_write(vram, bank, gbc_addr, &tile, 1);
}

#endif /* VRAM_H */
