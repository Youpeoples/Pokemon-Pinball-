/*
 * Tile-Based Collision Detection
 *
 * Translated from CheckStageCollision in home.asm (0x22b5-0x2972).
 * Also translates LoadStageCollisionAttributes from
 * engine/pinball_game/stage_collision_attributes.asm (0xe578).
 *
 * The collision system works in two steps:
 * 1. Ball position -> tile coordinate -> look up 2x2 tile collision attributes
 * 2. For each of 16 test points around the ball, test against the collision
 *    mask bitmap for the corresponding tile. Find longest consecutive run
 *    of collisions to determine surface normal.
 */

#include "game/collision.h"
#include "renderer/tile_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Data Tables from collision_deltas.asm (0x8817-0x8d17)
 *===========================================================================*/

/* CollisionForceAngles (0x8817): 256 bytes - collision normal angle lookup.
 * Indexed by packed byte: (first_collision_point << 4) | last_collision_point */
static const uint8_t CollisionForceAngles[256] = {
    0xC0, 0xC5, 0xD0, 0xDB, 0xE0, 0xE5, 0xF0, 0xFB,
    0x00, 0x05, 0x10, 0x1B, 0x20, 0x25, 0x30, 0x3B,
    0x45, 0xCA, 0xD5, 0xE0, 0xE5, 0xEA, 0xF5, 0x00,
    0x05, 0x0A, 0x15, 0x20, 0x25, 0x2A, 0x35, 0x40,
    0x50, 0x55, 0xE0, 0xEB, 0xF0, 0xF5, 0x00, 0x0B,
    0x10, 0x15, 0x20, 0x2B, 0x30, 0x35, 0x40, 0x4B,
    0x5B, 0x60, 0x6B, 0xF6, 0xFB, 0x00, 0x0B, 0x16,
    0x1B, 0x20, 0x2B, 0x36, 0x3B, 0x40, 0x4B, 0x56,
    0x60, 0x65, 0x70, 0x7B, 0x00, 0x05, 0x10, 0x1B,
    0x20, 0x25, 0x30, 0x3B, 0x40, 0x45, 0x50, 0x5B,
    0x65, 0x6A, 0x75, 0x80, 0x85, 0x0A, 0x15, 0x20,
    0x25, 0x2A, 0x35, 0x40, 0x45, 0x4A, 0x55, 0x60,
    0x70, 0x75, 0x80, 0x8B, 0x90, 0x95, 0x20, 0x2B,
    0x30, 0x35, 0x40, 0x4B, 0x50, 0x55, 0x60, 0x6B,
    0x7B, 0x80, 0x8B, 0x96, 0x9B, 0xA0, 0xAB, 0x36,
    0x3B, 0x40, 0x4B, 0x56, 0x5B, 0x60, 0x6B, 0x76,
    0x80, 0x85, 0x90, 0x9B, 0xA0, 0xA5, 0xB0, 0xBB,
    0x40, 0x45, 0x50, 0x5B, 0x60, 0x65, 0x70, 0x7B,
    0x85, 0x8A, 0x95, 0xA0, 0xA5, 0xAA, 0xB5, 0xC0,
    0xC5, 0x4A, 0x55, 0x60, 0x65, 0x6A, 0x75, 0x80,
    0x90, 0x95, 0xA0, 0xAB, 0xB0, 0xB5, 0xC0, 0xCB,
    0xD0, 0xD5, 0x60, 0x6B, 0x70, 0x75, 0x80, 0x8B,
    0x9B, 0xA0, 0xAB, 0xB6, 0xBB, 0xC0, 0xCB, 0xD6,
    0xDB, 0xE0, 0xEB, 0x76, 0x7B, 0x80, 0x8B, 0x96,
    0xA0, 0xA5, 0xB0, 0xBB, 0xC0, 0xC5, 0xD0, 0xDB,
    0xE0, 0xE5, 0xF0, 0xFB, 0x80, 0x85, 0x90, 0x9B,
    0xA5, 0xAA, 0xB5, 0xC0, 0xC5, 0xCA, 0xD5, 0xE0,
    0xE5, 0xEA, 0xF5, 0x00, 0x05, 0x8A, 0x95, 0xA0,
    0xB0, 0xB5, 0xC0, 0xCB, 0xD0, 0xD5, 0xE0, 0xEB,
    0xF0, 0xF5, 0x00, 0x0B, 0x10, 0x15, 0xA0, 0xAB,
    0xBB, 0xC0, 0xCB, 0xD6, 0xDB, 0xE0, 0xEB, 0xF6,
    0xFB, 0x00, 0x0B, 0x16, 0x1B, 0x20, 0x2B, 0xB6,
};

/* CollisionYDeltas (0x8917): 256 signed 16-bit entries */
static const int16_t CollisionYDeltas[256] = {
    0x0000, (int16_t)0xFFE0, (int16_t)0xFF81, (int16_t)0xFEDE,
    (int16_t)0xFE77, (int16_t)0xFE00, (int16_t)0xFCCC, (int16_t)0xFB87,
    (int16_t)0xFB01, (int16_t)0xFA8E, (int16_t)0xF9F8, (int16_t)0xFA1F,
    (int16_t)0xFA77, (int16_t)0xFAFC, (int16_t)0xFCAD, (int16_t)0xFEE7,
    0x0119, (int16_t)0xFFC2, (int16_t)0xFF70, (int16_t)0xFEE4,
    (int16_t)0xFE8C, (int16_t)0xFE24, (int16_t)0xFD15, (int16_t)0xFBF9,
    (int16_t)0xFB87, (int16_t)0xFB27, (int16_t)0xFAB9, (int16_t)0xFB03,
    (int16_t)0xFB69, (int16_t)0xFBFA, (int16_t)0xFDBD, 0x0000,
    0x0353, 0x045D, (int16_t)0xFF4B, (int16_t)0xFF01,
    (int16_t)0xFECC, (int16_t)0xFE8A, (int16_t)0xFDD5, (int16_t)0xFD15,
    (int16_t)0xFCCC, (int16_t)0xFC94, (int16_t)0xFC77, (int16_t)0xFD03,
    (int16_t)0xFD81, (int16_t)0xFE24, 0x0000, 0x0243,
    0x0504, 0x05F6, 0x07B4, (int16_t)0xFF08,
    (int16_t)0xFEFB, (int16_t)0xFEE2, (int16_t)0xFE8A, (int16_t)0xFE24,
    (int16_t)0xFE00, (int16_t)0xFDEC, (int16_t)0xFE10, (int16_t)0xFEC9,
    (int16_t)0xFF54, 0x0000, 0x01DC, 0x0406,
    0x0589, 0x066D, 0x0808, 0x08E7,
    (int16_t)0xFF00, (int16_t)0xFEFB, (int16_t)0xFECC, (int16_t)0xFE8C,
    (int16_t)0xFE77, (int16_t)0xFE71, (int16_t)0xFEAD, (int16_t)0xFF73,
    0x0000, 0x00AC, 0x027F, 0x0497,
    0x05E1, 0x06B6, 0x082C, 0x08E1,
    0x08E7, (int16_t)0xFF08, (int16_t)0xFF01, (int16_t)0xFEE4,
    (int16_t)0xFEDE, (int16_t)0xFEE3, (int16_t)0xFF32, 0x0000,
    0x008D, 0x0137, 0x02FD, 0x04FD,
    0x0608, 0x06B8, 0x07D4, 0x082C,
    0x0808, 0x07B4, (int16_t)0xFF4B, (int16_t)0xFF70,
    (int16_t)0xFF81, (int16_t)0xFF99, 0x0000, 0x00CE,
    0x0153, 0x01F0, 0x0389, 0x0547,
    0x0572, 0x05F8, 0x06B8, 0x06B6,
    0x066D, 0x05F6, 0x045D, (int16_t)0xFFC2,
    (int16_t)0xFFE0, 0x0000, 0x0067, 0x011D,
    0x018F, 0x0214, 0x036C, 0x04D9,
    0x04FF, 0x0572, 0x0608, 0x05E1,
    0x0589, 0x0504, 0x0353, 0x0119,
    0x0000, 0x0020, 0x007F, 0x0122,
    0x0189, 0x0200, 0x0334, 0x0479,
    0x0479, 0x04D9, 0x0547, 0x04FD,
    0x0497, 0x0406, 0x0243, 0x0000,
    (int16_t)0xFEE7, 0x003E, 0x0090, 0x011C,
    0x0174, 0x01DC, 0x02EB, 0x0407,
    0x0334, 0x036C, 0x0389, 0x02FD,
    0x027F, 0x01DC, 0x0000, (int16_t)0xFDBD,
    (int16_t)0xFCAD, (int16_t)0xFBA3, 0x00B5, 0x00FF,
    0x0134, 0x0176, 0x022B, 0x02EB,
    0x0200, 0x0214, 0x01F0, 0x0137,
    0x00AC, 0x0000, (int16_t)0xFE24, (int16_t)0xFBFA,
    (int16_t)0xFAFC, (int16_t)0xFA0A, (int16_t)0xF84C, 0x00F8,
    0x0105, 0x011E, 0x0176, 0x01DC,
    0x0189, 0x018F, 0x0153, 0x008D,
    0x0000, (int16_t)0xFF54, (int16_t)0xFD81, (int16_t)0xFB69,
    (int16_t)0xFA77, (int16_t)0xF993, (int16_t)0xF7F8, (int16_t)0xF719,
    0x0100, 0x0105, 0x0134, 0x0174,
    0x0122, 0x011D, 0x00CE, 0x0000,
    (int16_t)0xFF73, (int16_t)0xFEC9, (int16_t)0xFD03, (int16_t)0xFB03,
    (int16_t)0xFA1F, (int16_t)0xF94A, (int16_t)0xF7D4, (int16_t)0xF71F,
    (int16_t)0xF719, 0x00F8, 0x00FF, 0x011C,
    0x007F, 0x0067, 0x0000, (int16_t)0xFF32,
    (int16_t)0xFEAD, (int16_t)0xFE10, (int16_t)0xFC77, (int16_t)0xFAB9,
    (int16_t)0xF9F8, (int16_t)0xF948, (int16_t)0xF82C, (int16_t)0xF7D4,
    (int16_t)0xF7F8, (int16_t)0xF84C, 0x00B5, 0x0090,
    0x0020, 0x0000, (int16_t)0xFF99, (int16_t)0xFEE3,
    (int16_t)0xFE71, (int16_t)0xFDEC, (int16_t)0xFC94, (int16_t)0xFB27,
    (int16_t)0xFA8E, (int16_t)0xFA08, (int16_t)0xF948, (int16_t)0xF94A,
    (int16_t)0xF993, (int16_t)0xFA0A, (int16_t)0xFBA3, 0x003E,
};

/* CollisionXDeltas (0x8b17): 256 signed 16-bit entries */
static const int16_t CollisionXDeltas[256] = {
    (int16_t)0xFF00, (int16_t)0xFEFB, (int16_t)0xFECC, (int16_t)0xFE8C,
    (int16_t)0xFE77, (int16_t)0xFE71, (int16_t)0xFEAD, (int16_t)0xFF73,
    0x0000, 0x00AC, 0x027F, 0x0497,
    0x0589, 0x066D, 0x0808, 0x08E7,
    0x08E7, (int16_t)0xFF08, (int16_t)0xFF01, (int16_t)0xFEE4,
    (int16_t)0xFEDE, (int16_t)0xFEE3, (int16_t)0xFF32, 0x0000,
    0x008D, 0x0137, 0x02FD, 0x04FD,
    0x05E1, 0x06B6, 0x082C, 0x08E1,
    0x0808, 0x07B4, (int16_t)0xFF4B, (int16_t)0xFF70,
    (int16_t)0xFF81, (int16_t)0xFF99, 0x0000, 0x00CE,
    0x0153, 0x01F0, 0x0389, 0x0547,
    0x0608, 0x06B8, 0x07D4, 0x082C,
    0x066D, 0x05F6, 0x045D, (int16_t)0xFFC2,
    (int16_t)0xFFE0, 0x0000, 0x0067, 0x011D,
    0x018F, 0x0214, 0x036C, 0x04D9,
    0x0572, 0x05F8, 0x06B8, 0x06B6,
    0x0589, 0x0504, 0x0353, 0x0119,
    0x0000, 0x0020, 0x007F, 0x0122,
    0x0189, 0x0200, 0x0334, 0x0479,
    0x04FF, 0x0572, 0x0608, 0x05E1,
    0x0497, 0x0406, 0x0243, 0x0000,
    (int16_t)0xFEE7, 0x003E, 0x0090, 0x011C,
    0x0174, 0x01DC, 0x02EB, 0x0407,
    0x0479, 0x04D9, 0x0547, 0x04FD,
    0x027F, 0x01DC, 0x0000, (int16_t)0xFDBD,
    (int16_t)0xFCAD, (int16_t)0xFBA3, 0x00B5, 0x00FF,
    0x0134, 0x0176, 0x022B, 0x02EB,
    0x0334, 0x036C, 0x0389, 0x02FD,
    0x00AC, 0x0000, (int16_t)0xFE24, (int16_t)0xFBFA,
    (int16_t)0xFAFC, (int16_t)0xFA0A, (int16_t)0xF84C, 0x00F8,
    0x0105, 0x011E, 0x0176, 0x01DC,
    0x0200, 0x0214, 0x01F0, 0x0137,
    0x0000, (int16_t)0xFF54, (int16_t)0xFD81, (int16_t)0xFB69,
    (int16_t)0xFA77, (int16_t)0xF993, (int16_t)0xF7F8, (int16_t)0xF719,
    0x0100, 0x0105, 0x0134, 0x0174,
    0x0189, 0x018F, 0x0153, 0x008D,
    (int16_t)0xFF73, (int16_t)0xFEC9, (int16_t)0xFD03, (int16_t)0xFB03,
    (int16_t)0xFA1F, (int16_t)0xF94A, (int16_t)0xF7D4, (int16_t)0xF71F,
    (int16_t)0xF719, 0x00F8, 0x00FF, 0x011C,
    0x0122, 0x011D, 0x00CE, 0x0000,
    (int16_t)0xFEAD, (int16_t)0xFE10, (int16_t)0xFC77, (int16_t)0xFAB9,
    (int16_t)0xF9F8, (int16_t)0xF948, (int16_t)0xF82C, (int16_t)0xF7D4,
    (int16_t)0xF7F8, (int16_t)0xF84C, 0x00B5, 0x0090,
    0x007F, 0x0067, 0x0000, (int16_t)0xFF32,
    (int16_t)0xFE71, (int16_t)0xFDEC, (int16_t)0xFC94, (int16_t)0xFB27,
    (int16_t)0xFA8E, (int16_t)0xFA08, (int16_t)0xF948, (int16_t)0xF94A,
    (int16_t)0xF993, (int16_t)0xFA0A, (int16_t)0xFBA3, 0x003E,
    0x0020, 0x0000, (int16_t)0xFF99, (int16_t)0xFEE3,
    (int16_t)0xFE77, (int16_t)0xFE00, (int16_t)0xFCCC, (int16_t)0xFB87,
    (int16_t)0xFB01, (int16_t)0xFA8E, (int16_t)0xF9F8, (int16_t)0xFA1F,
    (int16_t)0xFA77, (int16_t)0xFAFC, (int16_t)0xFCAD, (int16_t)0xFEE7,
    0x0000, (int16_t)0xFFE0, (int16_t)0xFF81, (int16_t)0xFEDE,
    (int16_t)0xFE8C, (int16_t)0xFE24, (int16_t)0xFD15, (int16_t)0xFBF9,
    (int16_t)0xFB87, (int16_t)0xFB27, (int16_t)0xFAB9, (int16_t)0xFB03,
    (int16_t)0xFB69, (int16_t)0xFBFA, (int16_t)0xFDBD, 0x0000,
    0x0119, (int16_t)0xFFC2, (int16_t)0xFF70, (int16_t)0xFEE4,
    (int16_t)0xFECC, (int16_t)0xFE8A, (int16_t)0xFDD5, (int16_t)0xFD15,
    (int16_t)0xFCCC, (int16_t)0xFC94, (int16_t)0xFC77, (int16_t)0xFD03,
    (int16_t)0xFD81, (int16_t)0xFE24, 0x0000, 0x0243,
    0x0353, 0x045D, (int16_t)0xFF4B, (int16_t)0xFF01,
    (int16_t)0xFEFB, (int16_t)0xFEE2, (int16_t)0xFE8A, (int16_t)0xFE24,
    (int16_t)0xFE00, (int16_t)0xFDEC, (int16_t)0xFE10, (int16_t)0xFEC9,
    (int16_t)0xFF54, 0x0000, 0x01DC, 0x0406,
    0x0504, 0x05F6, 0x07B4, (int16_t)0xFF08,
};

/* BallCollisionTestPointOffsets: 16 signed (x,y) pairs around ball center */
static const int8_t BallCollisionTestPointOffsets[16][2] = {
    { 4,  0}, { 4,  1}, { 3,  3}, { 1,  4},
    { 0,  4}, {-1,  4}, {-3,  3}, {-4,  1},
    {-4,  0}, {-4, -1}, {-3, -3}, {-1, -4},
    { 0, -4}, { 1, -4}, { 3, -3}, { 4, -1},
};

/* BallPositionPointerOffsetDeltas: maps tile quadrant to offset */
static const uint8_t BallPositionOffsetDeltas[4] = { 0x00, 0x20, 0x01, 0x21 };

/*
 * CollisionTests: 8 sub-tables (one per X sub-tile position 0-7).
 * Each has 16 entries of 3 bytes: {y_offset, bitmask, storage_index}.
 * The y_offset high nybble (0x10) indicates the lower tile row.
 */
static const uint8_t CollisionTestData[8][16][3] = {
    /* X0 */
    {
        {0x00, 0x10, 0x0B}, {0x00, 0x08, 0x0C}, {0x00, 0x04, 0x0D},
        {0x01, 0x40, 0x0A}, {0x01, 0x01, 0x0E}, {0x03, 0x80, 0x09},
        {0x13, 0x80, 0x0F}, {0x04, 0x80, 0x08}, {0x14, 0x80, 0x00},
        {0x05, 0x80, 0x07}, {0x15, 0x80, 0x01}, {0x07, 0x40, 0x06},
        {0x07, 0x01, 0x02}, {0x08, 0x10, 0x05}, {0x08, 0x08, 0x04},
        {0x08, 0x04, 0x03},
    },
    /* X1 */
    {
        {0x00, 0x08, 0x0B}, {0x00, 0x04, 0x0C}, {0x00, 0x02, 0x0D},
        {0x01, 0x20, 0x0A}, {0x11, 0x80, 0x0E}, {0x03, 0x40, 0x09},
        {0x13, 0x40, 0x0F}, {0x04, 0x40, 0x08}, {0x14, 0x40, 0x00},
        {0x05, 0x40, 0x07}, {0x15, 0x40, 0x01}, {0x07, 0x20, 0x06},
        {0x17, 0x80, 0x02}, {0x08, 0x08, 0x05}, {0x08, 0x04, 0x04},
        {0x08, 0x02, 0x03},
    },
    /* X2 */
    {
        {0x00, 0x04, 0x0B}, {0x00, 0x02, 0x0C}, {0x00, 0x01, 0x0D},
        {0x01, 0x10, 0x0A}, {0x11, 0x40, 0x0E}, {0x03, 0x20, 0x09},
        {0x13, 0x20, 0x0F}, {0x04, 0x20, 0x08}, {0x14, 0x20, 0x00},
        {0x05, 0x20, 0x07}, {0x15, 0x20, 0x01}, {0x07, 0x10, 0x06},
        {0x17, 0x40, 0x02}, {0x08, 0x04, 0x05}, {0x08, 0x02, 0x04},
        {0x08, 0x01, 0x03},
    },
    /* X3 */
    {
        {0x00, 0x02, 0x0B}, {0x00, 0x01, 0x0C}, {0x10, 0x80, 0x0D},
        {0x01, 0x08, 0x0A}, {0x11, 0x20, 0x0E}, {0x03, 0x10, 0x09},
        {0x13, 0x10, 0x0F}, {0x04, 0x10, 0x08}, {0x14, 0x10, 0x00},
        {0x05, 0x10, 0x07}, {0x15, 0x10, 0x01}, {0x07, 0x08, 0x06},
        {0x17, 0x20, 0x02}, {0x08, 0x02, 0x05}, {0x08, 0x01, 0x04},
        {0x18, 0x80, 0x03},
    },
    /* X4 */
    {
        {0x00, 0x01, 0x0B}, {0x10, 0x80, 0x0C}, {0x10, 0x40, 0x0D},
        {0x01, 0x04, 0x0A}, {0x11, 0x10, 0x0E}, {0x03, 0x08, 0x09},
        {0x13, 0x08, 0x0F}, {0x04, 0x08, 0x08}, {0x14, 0x08, 0x00},
        {0x05, 0x08, 0x07}, {0x15, 0x08, 0x01}, {0x07, 0x04, 0x06},
        {0x17, 0x10, 0x02}, {0x08, 0x01, 0x05}, {0x18, 0x80, 0x04},
        {0x18, 0x40, 0x03},
    },
    /* X5 */
    {
        {0x10, 0x80, 0x0B}, {0x10, 0x40, 0x0C}, {0x10, 0x20, 0x0D},
        {0x01, 0x02, 0x0A}, {0x11, 0x08, 0x0E}, {0x03, 0x04, 0x09},
        {0x13, 0x04, 0x0F}, {0x04, 0x04, 0x08}, {0x14, 0x04, 0x00},
        {0x05, 0x04, 0x07}, {0x15, 0x04, 0x01}, {0x07, 0x02, 0x06},
        {0x17, 0x08, 0x02}, {0x18, 0x80, 0x05}, {0x18, 0x40, 0x04},
        {0x18, 0x20, 0x03},
    },
    /* X6 */
    {
        {0x10, 0x40, 0x0B}, {0x10, 0x20, 0x0C}, {0x10, 0x10, 0x0D},
        {0x01, 0x01, 0x0A}, {0x11, 0x04, 0x0E}, {0x03, 0x02, 0x09},
        {0x13, 0x02, 0x0F}, {0x04, 0x02, 0x08}, {0x14, 0x02, 0x00},
        {0x05, 0x02, 0x07}, {0x15, 0x02, 0x01}, {0x07, 0x01, 0x06},
        {0x17, 0x04, 0x02}, {0x18, 0x40, 0x05}, {0x18, 0x20, 0x04},
        {0x18, 0x10, 0x03},
    },
    /* X7 */
    {
        {0x10, 0x20, 0x0B}, {0x10, 0x10, 0x0C}, {0x10, 0x08, 0x0D},
        {0x11, 0x80, 0x0A}, {0x11, 0x02, 0x0E}, {0x03, 0x01, 0x09},
        {0x13, 0x01, 0x0F}, {0x04, 0x01, 0x08}, {0x14, 0x01, 0x00},
        {0x05, 0x01, 0x07}, {0x15, 0x01, 0x01}, {0x17, 0x80, 0x06},
        {0x17, 0x02, 0x02}, {0x18, 0x20, 0x05}, {0x18, 0x10, 0x04},
        {0x18, 0x08, 0x03},
    },
};

/*=============================================================================
 * Collision mask storage (loaded from PNG files)
 *===========================================================================*/
static uint8_t *collision_masks = NULL;     /* Current stage collision masks (1bpp) */
static size_t collision_masks_size = 0;

/* Special collision masks for bottom stages (flipper-dependent) */
static uint8_t *bottom_left_masks = NULL;
static size_t bottom_left_masks_size = 0;
static uint8_t *bottom_right_masks = NULL;
static size_t bottom_right_masks_size = 0;

/*=============================================================================
 * Lua-driven path overrides
 *===========================================================================*/
static char *collision_mask_override = NULL;
static char *collision_map_override = NULL;

void collision_set_mask_override(const char *path) {
    free(collision_mask_override);
    collision_mask_override = path ? _strdup(path) : NULL;
}

void collision_set_map_override(const char *path) {
    free(collision_map_override);
    collision_map_override = path ? _strdup(path) : NULL;
}

void collision_clear_overrides(void) {
    free(collision_mask_override);
    collision_mask_override = NULL;
    free(collision_map_override);
    collision_map_override = NULL;
}

/*=============================================================================
 * Collision map loading
 *===========================================================================*/

/* Get the collision map filename for a given stage + collision state */
static const char *get_collision_map_filename(uint8_t stage, uint8_t collision_state) {
    switch (stage) {
        case STAGE_RED_FIELD_TOP:
            switch (collision_state) {
                case 0: return "data/collision/maps/red_stage_top_0.collision";
                case 1: return "data/collision/maps/red_stage_top_1.collision";
                case 2: return "data/collision/maps/red_stage_top_2.collision";
                case 3: return "data/collision/maps/red_stage_top_3.collision";
                case 4: return "data/collision/maps/red_stage_top_4.collision";
                case 5: return "data/collision/maps/red_stage_top_5.collision";
                case 6: return "data/collision/maps/red_stage_top_6.collision";
                case 7: return "data/collision/maps/red_stage_top_7.collision";
                default: return "data/collision/maps/red_stage_top_4.collision";
            }
        case STAGE_RED_FIELD_BOTTOM:
            return "data/collision/maps/red_stage_bottom.collision";
        case STAGE_BLUE_FIELD_TOP:
            if (collision_state == 0)
                return "data/collision/maps/blue_stage_top_ball_entrance.collision";
            return "data/collision/maps/blue_stage_top.collision";
        case STAGE_BLUE_FIELD_BOTTOM:
            return "data/collision/maps/blue_stage_bottom.collision";
        case STAGE_GENGAR_BONUS: case 0x06:
            if (collision_state == 0)
                return "data/collision/maps/gengar_bonus_ball_entrance.collision";
            return "data/collision/maps/gengar_bonus.collision";
        case STAGE_MEWTWO_BONUS: case 0x08:
            if (collision_state == 0)
                return "data/collision/maps/mewtwo_bonus_ball_entrance.collision";
            return "data/collision/maps/mewtwo_bonus.collision";
        case STAGE_MEOWTH_BONUS: case 0x0A:
            if (collision_state == 0)
                return "data/collision/maps/meowth_bonus_ball_entrance.collision";
            return "data/collision/maps/meowth_bonus.collision";
        case STAGE_DIGLETT_BONUS: case 0x0C:
            if (collision_state == 0)
                return "data/collision/maps/diglett_bonus_ball_entrance.collision";
            return "data/collision/maps/diglett_bonus.collision";
        case STAGE_SEEL_BONUS: case 0x0E:
            if (collision_state == 0)
                return "data/collision/maps/seel_bonus_ball_entrance.collision";
            return "data/collision/maps/seel_bonus.collision";
        default:
            return "data/collision/maps/red_stage_top_4.collision";
    }
}

/* Get the collision mask PNG filename for a given stage + collision state */
static const char *get_collision_mask_filename(uint8_t stage, uint8_t collision_state) {
    switch (stage) {
        case STAGE_RED_FIELD_TOP:
            /* Red top has 4 mask sets (pairs of collision states share masks) */
            switch (collision_state / 2) {
                case 0: return "data/collision/masks/red_stage_top_0.png";
                case 1: return "data/collision/masks/red_stage_top_1.png";
                case 2: return "data/collision/masks/red_stage_top_2.png";
                case 3: return "data/collision/masks/red_stage_top_3.png";
                default: return "data/collision/masks/red_stage_top_2.png";
            }
        case STAGE_RED_FIELD_BOTTOM:
            return "data/collision/masks/red_stage_bottom.png";
        case STAGE_BLUE_FIELD_TOP:
            return "data/collision/masks/blue_stage_top.png";
        case STAGE_BLUE_FIELD_BOTTOM:
            return "data/collision/masks/blue_stage_bottom.png";
        case STAGE_GENGAR_BONUS: case 0x06:
            return "data/collision/masks/gengar_bonus.png";
        case STAGE_MEWTWO_BONUS: case 0x08:
            return "data/collision/masks/mewtwo_bonus.png";
        case STAGE_MEOWTH_BONUS: case 0x0A:
            return "data/collision/masks/meowth_bonus.png";
        case STAGE_DIGLETT_BONUS: case 0x0C:
            return "data/collision/masks/diglett_bonus.png";
        case STAGE_SEEL_BONUS: case 0x0E:
            return "data/collision/masks/seel_bonus.png";
        default:
            return "data/collision/masks/red_stage_top_2.png";
    }
}

void load_bottom_collision_masks(GameState *state) {
    char path[260];
    if (state->current_stage >= FIRST_BONUS_STAGE) {
        snprintf(path, sizeof(path), "%s/data/collision/masks/bottom_left_bonus_stage_masks.png",
                 state->asset_base_path);
        free(bottom_left_masks);
        bottom_left_masks = masks_from_png(path, &bottom_left_masks_size);
        snprintf(path, sizeof(path), "%s/data/collision/masks/bottom_right_bonus_stage_masks.png",
                 state->asset_base_path);
        free(bottom_right_masks);
        bottom_right_masks = masks_from_png(path, &bottom_right_masks_size);
    } else {
        snprintf(path, sizeof(path), "%s/data/collision/masks/bottom_left_masks.png",
                 state->asset_base_path);
        free(bottom_left_masks);
        bottom_left_masks = masks_from_png(path, &bottom_left_masks_size);
        snprintf(path, sizeof(path), "%s/data/collision/masks/bottom_right_masks.png",
                 state->asset_base_path);
        free(bottom_right_masks);
        bottom_right_masks = masks_from_png(path, &bottom_right_masks_size);
    }
}

void load_stage_collision_attributes(GameState *state) {
    char path[260];

    /* Load collision map (.collision binary file = 0x300 bytes) */
    if (collision_map_override) {
        snprintf(path, sizeof(path), "%s/%s", state->asset_base_path,
                 collision_map_override);
    } else {
        const char *map_file = get_collision_map_filename(
            state->current_stage, state->stage_collision_state);
        snprintf(path, sizeof(path), "%s/%s", state->asset_base_path, map_file);
    }
    size_t map_size = 0;
    uint8_t *map_data = load_binary_file(path, &map_size);
    if (map_data && map_size >= 0x300) {
        memcpy(state->stage_collision_map, map_data, 0x300);
    } else if (map_data) {
        memcpy(state->stage_collision_map, map_data, map_size);
        memset(state->stage_collision_map + map_size, 0, 0x300 - map_size);
    } else {
        memset(state->stage_collision_map, 0, 0x300);
        fprintf(stderr, "collision: failed to load map '%s'\n", path);
    }
    free(map_data);

    /* Load collision masks (1bpp PNG) */
    if (collision_mask_override) {
        snprintf(path, sizeof(path), "%s/%s", state->asset_base_path,
                 collision_mask_override);
    } else {
        const char *mask_file = get_collision_mask_filename(
            state->current_stage, state->stage_collision_state);
        snprintf(path, sizeof(path), "%s/%s", state->asset_base_path, mask_file);
    }
    free(collision_masks);
    collision_masks = masks_from_png(path, &collision_masks_size);
    if (!collision_masks) {
        fprintf(stderr, "collision: failed to load masks '%s'\n", path);
    }

    /* Load special bottom-stage masks if on a bottom stage */
    if (STAGE_HAS_FLIPPERS(state->current_stage)) {
        load_bottom_collision_masks(state);
    }
}

/*=============================================================================
 * TryLoadSpecialCollisionMask (0x248a)
 * For bottom stages, attributes >= 0xD0 use special masks.
 * Returns pointer to mask row, or NULL for static mask.
 *===========================================================================*/
static const uint8_t *try_load_special_collision_mask(
    GameState *state, uint8_t attribute, uint8_t y_sub)
{
    /* Only bottom stages have special masks */
    if (!(state->current_stage & 1)) {
        return NULL;
    }

    if (attribute < 0xD0) {
        return NULL;
    }

    if (attribute < 0xE0) {
        /* Animated mon collision mask: attribute 0xD0-0xDF */
        uint8_t index = (attribute - 0xD0) * 8 + y_sub;
        if (index < sizeof(state->mon_animated_collision_mask)) {
            return &state->mon_animated_collision_mask[index];
        }
        return NULL;
    }

    /* Flipper-dependent masks: attribute 0xE0-0xFF */
    uint8_t mask_index = attribute - 0xE0;
    uint8_t is_right = (mask_index & 0x10) ? 1 : 0;
    mask_index &= 0x0F;

    const uint8_t *mask_base;
    size_t mask_base_size;
    uint8_t flipper_state;

    if (is_right) {
        mask_base = bottom_right_masks;
        mask_base_size = bottom_right_masks_size;
        flipper_state = (uint8_t)(state->right_flipper_state >> 8);
    } else {
        mask_base = bottom_left_masks;
        mask_base_size = bottom_left_masks_size;
        flipper_state = (uint8_t)(state->left_flipper_state >> 8);
    }

    if (!mask_base) return NULL;

    /* Three sets of masks based on flipper position: 0-6, 7-13, 14+ */
    /* Each set is 0x80 bytes (16 tiles * 8 bytes) */
    size_t offset = 0;
    if (flipper_state >= 7) offset += 0x80;
    if (flipper_state >= 14) offset += 0x80;

    offset += (size_t)mask_index * 8 + y_sub;
    if (offset < mask_base_size) {
        return &mask_base[offset];
    }
    return NULL;
}

/*=============================================================================
 * CheckStageCollision (0x22b5)
 *===========================================================================*/
void check_stage_collision(GameState *state) {
    if (!collision_masks) return;

    /* Calculate tile coordinates from ball position */
    uint8_t ball_x_int = (uint8_t)(state->ball_x_pos >> 8);
    uint8_t ball_y_int = (uint8_t)(state->ball_y_pos >> 8);

    /* Prevent underflow: ball center offset by 4px for radius.
     * If ball_x or ball_y < 4, the ball is outside the playfield -
     * skip collision to avoid wrapping to the opposite side. */
    if (ball_x_int < 4 || ball_y_int < 4) {
        state->upper_left_collision_attr = 0;
        state->upper_right_collision_attr = 0;
        state->lower_left_collision_attr = 0;
        state->lower_right_collision_attr = 0;
        return;
    }

    uint8_t adj_x = ball_x_int - 4;
    uint8_t adj_y = ball_y_int - 4;

    state->sub_tile_ball_x_pos = adj_x & 7;
    state->sub_tile_ball_y_pos = adj_y & 7;

    /* Tile coordinates */
    uint8_t tile_x = (adj_x & 0xF8) >> 3;
    uint8_t tile_y = (adj_y & 0xF8);

    /* Calculate tile offset: (y_tile * 32) + x_tile
     * Note: tile_y is already multiplied by 8, so * 4 gives * 32 */
    uint16_t tile_offset = ((uint16_t)tile_y * 4) + tile_x;
    state->ball_position_tile_offset = tile_offset;

    /* Read 2x2 tile collision attributes from collision map */
    if (tile_offset < 0x300) {
        state->upper_left_collision_attr = state->stage_collision_map[tile_offset];
    } else {
        state->upper_left_collision_attr = 0;
    }
    if (tile_offset + 1 < 0x300) {
        state->upper_right_collision_attr = state->stage_collision_map[tile_offset + 1];
    } else {
        state->upper_right_collision_attr = 0;
    }
    if (tile_offset + 32 < 0x300) {
        state->lower_left_collision_attr = state->stage_collision_map[tile_offset + 32];
    } else {
        state->lower_left_collision_attr = 0;
    }
    if (tile_offset + 33 < 0x300) {
        state->lower_right_collision_attr = state->stage_collision_map[tile_offset + 33];
    } else {
        state->lower_right_collision_attr = 0;
    }

    /* Get the collision test table for this X sub-tile */
    uint8_t sub_x = state->sub_tile_ball_x_pos;
    if (sub_x > 7) sub_x = 7;
    const uint8_t (*test_table)[3] = CollisionTestData[sub_x];
    uint8_t sub_y = state->sub_tile_ball_y_pos;

    /* The 4 collision attributes accessible as array */
    uint8_t attrs[4] = {
        state->upper_left_collision_attr,
        state->lower_left_collision_attr,
        state->upper_right_collision_attr,
        state->lower_right_collision_attr,
    };

    /* Test each of the 16 collision points */
    memset(state->collision_point_tests, 0, sizeof(state->collision_point_tests));

    for (int i = 0; i < 16; i++) {
        uint8_t y_offset_raw = test_table[i][0];
        uint8_t bitmask = test_table[i][1];
        uint8_t store_index = test_table[i][2];

        /* Add sub-tile Y to the test point Y offset */
        uint8_t total_y = y_offset_raw + sub_y;

        /* Determine which of the 4 tiles this falls in:
         * High nybble bit 4 of y_offset_raw selects right tile column,
         * total_y / 8 (after masking) selects lower tile row */
        uint8_t tile_select = (total_y >> 3) & 3;
        /* The y_offset_raw high nybble 0x10 means the right column tile */
        if (y_offset_raw & 0x10) {
            /* Right column: attrs[2] or attrs[3] based on y row */
            tile_select = (total_y >> 3) & 1;
            tile_select = tile_select ? 3 : 2;
        } else {
            /* Left column: attrs[0] or attrs[1] based on y row */
            tile_select = (total_y >> 3) & 1;
            tile_select = tile_select ? 1 : 0;
        }

        uint8_t attr = attrs[tile_select];
        uint8_t y_within_tile = total_y & 7;

        /* Check for special collision masks (bottom stages, attributes >= 0xD0) */
        const uint8_t *special_mask = try_load_special_collision_mask(
            state, attr, y_within_tile);

        uint8_t mask_byte;
        if (special_mask) {
            /* Special mask: the pointer already points to the right row */
            mask_byte = *special_mask;
        } else {
            /* Static mask: look up in collision_masks array */
            /* Each mask tile is 8 bytes (1bpp, 8 rows) */
            size_t mask_offset = (size_t)attr * 8 + y_within_tile;
            if (mask_offset < collision_masks_size) {
                mask_byte = collision_masks[mask_offset];
            } else {
                mask_byte = 0;
            }
        }

        /* Test the bitmask bit against the collision mask row */
        uint8_t result = mask_byte & bitmask;
        if (store_index < 16) {
            state->collision_point_tests[store_index] = result;
        }
    }

    /* Find longest consecutive run of colliding test points.
     * Translated from home.asm lines 2812-2869.
     *
     * Key ASM behavior: after the 16-point array, a 12-byte backup copy
     * of points[0..11] exists at wd7d9. The .findNextFailedCollisionPoint
     * loop reads past index 15 into this backup, enabling natural wrap-around.
     * This means a run starting at e.g. point 10 can wrap through 0-5.
     *
     * We emulate this by extending the array to 32 entries (16 + 16 backup). */
    uint8_t extended[32];
    memcpy(extended, state->collision_point_tests, 16);
    memcpy(extended + 16, state->collision_point_tests, 16); /* full 16-byte backup */

    uint8_t max_consecutive = 0;
    uint8_t max_d = 0;  /* packed (start << 4) | end */
    uint8_t b = 0;

    /* ASM .findNextCollisionPoint2: if point 0 is colliding,
     * skip past the initial colliding run to find a gap first. */
    if (extended[0]) {
        while (b < 16 && extended[b]) {
            b++;
        }
        if (b >= 16) {
            /* All 16 points colliding - ball is completely inside wall. */
            state->is_ball_colliding = 0;
            return;
        }
    }

    /* ASM .findNextCollisionPoint + .handleCollisionPoint loop */
    while (b < 16) {
        /* Skip non-colliding points */
        while (b < 16 && !extended[b]) {
            b++;
        }
        if (b >= 16) break;

        /* Found start of collision run */
        uint8_t run_start = b;
        uint8_t run_length = 0;
        /* ASM: .findNextFailedCollisionPoint reads past index 15 into backup.
         * We use the extended array (32 entries) to allow wrap-around. */
        while (extended[b] && b < 32) {
            run_length++;
            b++;
        }

        /* Check if this is the longest run (ASM: cp e / jr c) */
        if (run_length > max_consecutive) {
            max_consecutive = run_length;
            /* End index: last colliding point, wrapped to 0-15 */
            uint8_t run_end = (b - 1) & 0x0F;
            max_d = (run_start << 4) | run_end;
        }
    }

    /* Store collision result */
    state->is_ball_colliding = max_consecutive;

    if (!max_consecutive) return;

    /* Look up collision normal angle */
    state->collision_normal_angle = CollisionForceAngles[max_d];

    /* Apply position correction deltas.
     * Index into Y/X delta tables by max_d (the packed start/end) */
    uint16_t delta_index = max_d;
    state->ball_y_pos = (ufixed8_8)((uint16_t)state->ball_y_pos +
                                     (uint16_t)CollisionYDeltas[delta_index]);
    state->ball_x_pos = (ufixed8_8)((uint16_t)state->ball_x_pos +
                                     (uint16_t)CollisionXDeltas[delta_index]);

    /* Determine which tile triggered the collision (for object collision dispatch).
     * Use the midpoint of the collision arc to pick a test point. */
    uint8_t start_idx = (max_d >> 4) & 0x0F;
    uint8_t end_idx = max_d & 0x0F;
    uint8_t length = end_idx - start_idx;
    if (end_idx < start_idx) length = (uint8_t)(length + 16);

    uint8_t mid_index = (start_idx + start_idx + length + 1) & 0x1E;
    /* Look up the collision test point offset for the midpoint */
    int8_t test_x = BallCollisionTestPointOffsets[mid_index / 2][0];
    int8_t test_y = BallCollisionTestPointOffsets[mid_index / 2][1];

    /* Determine which quadrant (of the 2x2 tile area) the collision point is in */
    uint8_t qx = (uint8_t)((int)state->sub_tile_ball_x_pos + 4 + test_x);
    uint8_t qy = (uint8_t)((int)state->sub_tile_ball_y_pos + 4 + test_y);
    uint8_t quadrant = 0;
    if (qx & 8) quadrant |= 2;  /* right column */
    if (qy & 8) quadrant |= 1;  /* lower row */

    /* Save which collision attribute tile was hit */
    state->cur_collision_attribute = attrs[quadrant];

    uint16_t collision_tile = state->ball_position_tile_offset +
                              BallPositionOffsetDeltas[quadrant];
    state->cur_collision_tile_offset = collision_tile;
}
