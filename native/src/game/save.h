#ifndef SAVE_H
#define SAVE_H

#include <stddef.h>
#include "game_state.h"

/*
 * Save data to a file with signature and checksum.
 * Translated from SaveData in home/save.asm (0xF1A).
 * Data format: [data][signature 2 bytes][checksum 2 bytes][backup copy]
 */
bool save_data(const uint8_t *data, size_t length, const char *filename);

/*
 * Load saved data from a file, validating signature and checksum.
 * Translated from LoadSavedData in home/save.asm (0xF0C).
 * Falls back to backup copy if primary is corrupt.
 * Returns true on success, false if both copies are invalid.
 */
bool load_saved_data(uint8_t *data, size_t length, const char *filename);

/*
 * Save all game persistent data (high scores, pokedex, key configs, game state).
 */
bool save_game(GameState *state);

/*
 * Load all game persistent data.
 */
bool load_game(GameState *state);

/*
 * Load saved in-game state (sSaveGame block) into GameState.
 * Called when player selects CONTINUE from title screen.
 */
bool load_saved_game_state(GameState *state);

/*
 * Delete the saved game file (matches ASM clearing SRAM after load).
 */
void delete_saved_game(void);

#endif /* SAVE_H */
