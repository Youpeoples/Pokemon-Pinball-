#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

/*=============================================================================
 * Screen Constants (screen_constants.asm)
 *===========================================================================*/
#define SCREEN_SELECT_GAMEBOY_TARGET  0
#define SCREEN_ERASE_ALL_DATA         1
#define SCREEN_COPYRIGHT              2
#define SCREEN_TITLESCREEN            3
#define SCREEN_PINBALL_GAME           4
#define SCREEN_POKEDEX                5
#define SCREEN_OPTIONS                6
#define SCREEN_HIGH_SCORES            7
#define SCREEN_FIELD_SELECT           8

/*=============================================================================
 * Stage Constants (stage_constants.asm)
 *===========================================================================*/
#define STAGE_RED_FIELD_TOP       0x0
#define STAGE_RED_FIELD_BOTTOM    0x1
/* 0x2, 0x3 unused */
#define STAGE_BLUE_FIELD_TOP      0x4
#define STAGE_BLUE_FIELD_BOTTOM   0x5
#define FIRST_BONUS_STAGE         0x6
/* Note: even-numbered bonus stages are buggy in original */
#define STAGE_GENGAR_BONUS        0x7
#define STAGE_MEWTWO_BONUS        0x9
#define STAGE_MEOWTH_BONUS        0xB
#define STAGE_DIGLETT_BONUS       0xD
#define STAGE_SEEL_BONUS          0xF

/* bit 0 set = stage has flippers (bottom half) */
#define STAGE_HAS_FLIPPERS(stage) ((stage) & 1)

/*=============================================================================
 * Joypad Constants (joy_constants.asm)
 *===========================================================================*/
#define BIT_A_BUTTON  0
#define BIT_B_BUTTON  1
#define BIT_SELECT    2
#define BIT_START     3
#define BIT_D_RIGHT   4
#define BIT_D_LEFT    5
#define BIT_D_UP      6
#define BIT_D_DOWN    7

#define BTN_A         0x01
#define BTN_B         0x02
#define BTN_SELECT    0x04
#define BTN_START     0x08
#define BTN_RIGHT     0x10
#define BTN_LEFT      0x20
#define BTN_UP        0x40
#define BTN_DOWN      0x80

#define R_FLIPPER     BTN_A
#define L_FLIPPER     BTN_LEFT
#define FLIPPERS      (L_FLIPPER | R_FLIPPER)

/*=============================================================================
 * Ball Types (ball_types.asm)
 *===========================================================================*/
#define POKE_BALL    0
#define GREAT_BALL   2
#define ULTRA_BALL   3
#define MASTER_BALL  5

/*=============================================================================
 * Bonus Stage Order (bonus_stage_order_constants.asm)
 *===========================================================================*/
#define BONUS_STAGE_ORDER_GENGAR   0
#define BONUS_STAGE_ORDER_MEWTWO   1
#define BONUS_STAGE_ORDER_MEOWTH   2
#define BONUS_STAGE_ORDER_DIGLETT  3
#define BONUS_STAGE_ORDER_SEEL     4

/*=============================================================================
 * Evolution Type Constants (evolution_type_constants.asm)
 *===========================================================================*/
#define EVO_THUNDER_STONE  0x01
#define EVO_MOON_STONE     0x02
#define EVO_FIRE_STONE     0x03
#define EVO_LEAF_STONE     0x04
#define EVO_WATER_STONE    0x05
#define EVO_LINK_CABLE     0x06
#define EVO_EXPERIENCE     0x07

/*=============================================================================
 * Special Collision Constants (special_collision_constants.asm)
 *===========================================================================*/
#define SPECIAL_COLLISION_NOTHING                0
#define SPECIAL_COLLISION_LEFT_TRIGGER           1
#define SPECIAL_COLLISION_RIGHT_TRIGGER          2
#define SPECIAL_COLLISION_STARYU_ALLEY_TRIGGER   3
#define SPECIAL_COLLISION_VOLTORB                4
#define SPECIAL_COLLISION_SHELLDER               4
#define SPECIAL_COLLISION_BELLSPROUT              5
#define SPECIAL_COLLISION_STARYU                  6
#define SPECIAL_COLLISION_LEFT_DIGLETT            7
#define SPECIAL_COLLISION_POLIWAG                 7
#define SPECIAL_COLLISION_RIGHT_DIGLETT           8
#define SPECIAL_COLLISION_PSYDUCK                 8
#define SPECIAL_COLLISION_LEFT_BONUS_MULTIPLIER   9
#define SPECIAL_COLLISION_RIGHT_BONUS_MULTIPLIER 10
#define SPECIAL_COLLISION_BALL_UPGRADE           11
#define SPECIAL_COLLISION_SPINNER                12
#define SPECIAL_COLLISION_SLOT_HOLE              13
#define SPECIAL_COLLISION_CLOYSTER               14
#define SPECIAL_COLLISION_SLOWPOKE               15

/*=============================================================================
 * Special Mode Constants (pinball_game_constants.asm)
 *===========================================================================*/
#define SPECIAL_MODE_CATCHEM     0
#define SPECIAL_MODE_EVOLUTION   1
#define SPECIAL_MODE_MAP_MOVE    2

#define CATCHEM_MODE_SLOT_REWARD    1
#define EVOLUTION_MODE_SLOT_REWARD  2

#define MAX_PIKACHU_SAVER_CHARGE  15
#define MAX_BONUS_MULTIPLIER      99
#define MAX_EXTRA_BALLS           10

/*=============================================================================
 * Billboard Picture IDs (billboard_pic_pointers.asm)
 *===========================================================================*/
#define BILLBOARD_BALL_SAVER_30          0
#define BILLBOARD_BALL_SAVER_60          1
#define BILLBOARD_BALL_SAVER_90          2
#define BILLBOARD_PIKACHU_SAVER          3
#define BILLBOARD_EXTRA_BALL             4
#define BILLBOARD_SMALL_REWARD           5
#define BILLBOARD_BIG_REWARD             6
#define BILLBOARD_CATCHEM_MODE           7
#define BILLBOARD_EVOLUTION_MODE         8
#define BILLBOARD_GREAT_BALL             9
#define BILLBOARD_ULTRA_BALL            10
#define BILLBOARD_MASTER_BALL           11
#define BILLBOARD_BONUS_MULTIPLIER      12
#define BILLBOARD_GENGAR_BONUS          13
#define BILLBOARD_MEWTWO_BONUS          14
#define BILLBOARD_MEOWTH_BONUS          15
#define BILLBOARD_DIGLETT_BONUS         16
#define BILLBOARD_SEEL_BONUS            17
#define BILLBOARD_SMALL_REWARD_100      18
#define BILLBOARD_BIG_REWARD_1000000    27
#define BILLBOARD_BONUS_MULTIPLIER_X1   36

#define NUM_MEWTWO_COMPLETIONS_FOR_MEW  2

#define MAP_MOVE_FRAMES_COUNTER         480   /* 8 seconds */
#define PINBALL_UPGRADE_FRAMES_COUNTER  3600  /* ~1 minute */

/*=============================================================================
 * Map Constants (map_constants.asm)
 *===========================================================================*/
enum {
    MAP_PALLET_TOWN = 0,
    MAP_VIRIDIAN_CITY,
    MAP_VIRIDIAN_FOREST,
    MAP_PEWTER_CITY,
    MAP_MT_MOON,
    MAP_CERULEAN_CITY,
    MAP_VERMILION_SEASIDE,
    MAP_VERMILION_STREETS,
    MAP_ROCK_MOUNTAIN,
    MAP_LAVENDER_TOWN,
    MAP_CELADON_CITY,
    MAP_CYCLING_ROAD,
    MAP_FUCHSIA_CITY,
    MAP_SAFARI_ZONE,
    MAP_SAFFRON_CITY,
    MAP_SEAFOAM_ISLANDS,
    MAP_CINNABAR_ISLAND,
    MAP_INDIGO_PLATEAU,
    NUM_MAPS
};

/*=============================================================================
 * Pokemon Constants (pokemon_constants.asm)
 *===========================================================================*/
#define NUM_POKEMON 151

enum PokemonId {
    MON_NONE = 0,
    MON_BULBASAUR = 1, MON_IVYSAUR, MON_VENUSAUR,
    MON_CHARMANDER, MON_CHARMELEON, MON_CHARIZARD,
    MON_SQUIRTLE, MON_WARTORTLE, MON_BLASTOISE,
    MON_CATERPIE, MON_METAPOD, MON_BUTTERFREE,
    MON_WEEDLE, MON_KAKUNA, MON_BEEDRILL,
    MON_PIDGEY, MON_PIDGEOTTO, MON_PIDGEOT,
    MON_RATTATA, MON_RATICATE,
    MON_SPEAROW, MON_FEAROW,
    MON_EKANS, MON_ARBOK,
    MON_PIKACHU, MON_RAICHU,
    MON_SANDSHREW, MON_SANDSLASH,
    MON_NIDORAN_F, MON_NIDORINA, MON_NIDOQUEEN,
    MON_NIDORAN_M, MON_NIDORINO, MON_NIDOKING,
    MON_CLEFAIRY, MON_CLEFABLE,
    MON_VULPIX, MON_NINETALES,
    MON_JIGGLYPUFF, MON_WIGGLYTUFF,
    MON_ZUBAT, MON_GOLBAT,
    MON_ODDISH, MON_GLOOM, MON_VILEPLUME,
    MON_PARAS, MON_PARASECT,
    MON_VENONAT, MON_VENOMOTH,
    MON_DIGLETT, MON_DUGTRIO,
    MON_MEOWTH, MON_PERSIAN,
    MON_PSYDUCK, MON_GOLDUCK,
    MON_MANKEY, MON_PRIMEAPE,
    MON_GROWLITHE, MON_ARCANINE,
    MON_POLIWAG, MON_POLIWHIRL, MON_POLIWRATH,
    MON_ABRA, MON_KADABRA, MON_ALAKAZAM,
    MON_MACHOP, MON_MACHOKE, MON_MACHAMP,
    MON_BELLSPROUT, MON_WEEPINBELL, MON_VICTREEBEL,
    MON_TENTACOOL, MON_TENTACRUEL,
    MON_GEODUDE, MON_GRAVELER, MON_GOLEM,
    MON_PONYTA, MON_RAPIDASH,
    MON_SLOWPOKE, MON_SLOWBRO,
    MON_MAGNEMITE, MON_MAGNETON,
    MON_FARFETCH_D,
    MON_DODUO, MON_DODRIO,
    MON_SEEL, MON_DEWGONG,
    MON_GRIMER, MON_MUK,
    MON_SHELLDER, MON_CLOYSTER,
    MON_GASTLY, MON_HAUNTER, MON_GENGAR,
    MON_ONIX,
    MON_DROWZEE, MON_HYPNO,
    MON_KRABBY, MON_KINGLER,
    MON_VOLTORB, MON_ELECTRODE,
    MON_EXEGGCUTE, MON_EXEGGUTOR,
    MON_CUBONE, MON_MAROWAK,
    MON_HITMONLEE, MON_HITMONCHAN,
    MON_LICKITUNG,
    MON_KOFFING, MON_WEEZING,
    MON_RHYHORN, MON_RHYDON,
    MON_CHANSEY,
    MON_TANGELA,
    MON_KANGASKHAN,
    MON_HORSEA, MON_SEADRA,
    MON_GOLDEEN, MON_SEAKING,
    MON_STARYU, MON_STARMIE,
    MON_MR_MIME,
    MON_SCYTHER,
    MON_JYNX,
    MON_ELECTABUZZ,
    MON_MAGMAR,
    MON_PINSIR,
    MON_TAUROS,
    MON_MAGIKARP, MON_GYARADOS,
    MON_LAPRAS,
    MON_DITTO,
    MON_EEVEE, MON_VAPOREON, MON_JOLTEON, MON_FLAREON,
    MON_PORYGON,
    MON_OMANYTE, MON_OMASTAR,
    MON_KABUTO, MON_KABUTOPS,
    MON_AERODACTYL,
    MON_SNORLAX,
    MON_ARTICUNO, MON_ZAPDOS, MON_MOLTRES,
    MON_DRATINI, MON_DRAGONAIR, MON_DRAGONITE,
    MON_MEWTWO,
    MON_MEW
};

/*=============================================================================
 * Pokedex Constants (pokedex_constants.asm)
 *===========================================================================*/
#define BIT_POKEDEX_MON_SEEN    0
#define BIT_POKEDEX_MON_CAUGHT  1

/*=============================================================================
 * Diglett Stage Constants (diglett_stage_constants.asm)
 *===========================================================================*/
#define NUM_DIGLETTS              31
#define DIGLETT_INITIALIZE_DELAY  0x88

/*=============================================================================
 * Audio Constants
 *===========================================================================*/
#define NUM_SONGS          0x17
#define NUM_SOUND_EFFECTS  0x4E
#define MUSIC_NOTHING      0x00

/*=============================================================================
 * GBC Hardware Constants
 *===========================================================================*/
#define GBC_SCREEN_WIDTH   160
#define GBC_SCREEN_HEIGHT  144
#define GBC_TILE_SIZE      8
#define GBC_TILES_PER_ROW  20  /* 160 / 8 */
#define GBC_TILES_PER_COL  18  /* 144 / 8 */
#define GBC_TILEMAP_W      32
#define GBC_TILEMAP_H      32
#define GBC_OAM_ENTRIES    64
#define GBC_BYTES_PER_TILE 16  /* 2bpp: 8 rows x 2 bytes */

#define GBC_NUM_BG_PALETTES   8
#define GBC_NUM_OBJ_PALETTES  8
#define GBC_COLORS_PER_PAL    4

/* OAM attribute flags */
#define OAM_PALETTE    0x07
#define OAM_TILE_BANK  (1 << 3)
#define OAM_X_FLIP     (1 << 5)
#define OAM_Y_FLIP     (1 << 6)
#define OAM_PRIORITY   (1 << 7)

/*=============================================================================
 * VRAM addresses (mapped to offsets for our tile arrays)
 *===========================================================================*/
#define VRAM_TILES_OB  0x8000  /* Sprite tiles */
#define VRAM_TILES_SH  0x8800  /* Shared tiles */
#define VRAM_TILES_BG  0x9000  /* BG tiles */
#define VRAM_BG_MAP    0x9800  /* BG tilemap */
#define VRAM_BG_WIN    0x9C00  /* Window tilemap */

/*=============================================================================
 * Save data signature bytes
 *===========================================================================*/
#define SAVE_SIGNATURE_BYTE1  0x4E  /* 'N' */
#define SAVE_SIGNATURE_BYTE2  0x54  /* 'T' */

/*=============================================================================
 * Combined View Constants
 *===========================================================================*/
#define COMBINED_TOP_CLIP           8   /* Pixels to clip from top half bottom (overlap row) */
#define COMBINED_TOP_HEIGHT        (144 - COMBINED_TOP_CLIP)  /* 136 */
#define COMBINED_VIEW_HEIGHT       (COMBINED_TOP_HEIGHT + 144) /* 280 */
#define COMBINED_VIEW_SCOREBOARD_H  10  /* Window layer height for scoreboard */
#define COMBINED_TOTAL_HEIGHT      (COMBINED_VIEW_HEIGHT + COMBINED_VIEW_SCOREBOARD_H)

#endif /* CONSTANTS_H */
