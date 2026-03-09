/*
 * Virtual VRAM implementation
 */

#include "renderer/vram.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

VirtualVRAM *vram_create(void) {
    VirtualVRAM *vram = calloc(1, sizeof(VirtualVRAM));
    return vram;
}

void vram_free(VirtualVRAM *vram) {
    free(vram);
}

bool vram_write(VirtualVRAM *vram, uint8_t bank, uint16_t gbc_addr,
                const uint8_t *src, uint16_t size) {
    if (!vram || !src || bank > 1) return false;

    uint16_t addr = gbc_addr;
    for (uint16_t i = 0; i < size; i++, addr++) {
        if (addr >= 0x8000 && addr <= 0x97FF) {
            /* Tile data region */
            vram->tile_data[bank][addr - 0x8000] = src[i];
        } else if (addr >= 0x9800 && addr <= 0x9BFF) {
            /* BG tilemap region */
            vram->bg_map[bank][addr - 0x9800] = src[i];
        } else if (addr >= 0x9C00 && addr <= 0x9FFF) {
            /* Window tilemap region */
            vram->win_map[bank][addr - 0x9C00] = src[i];
        } else {
            fprintf(stderr, "vram_write: address $%04X out of VRAM range\n", addr);
            return false;
        }
    }
    return true;
}
