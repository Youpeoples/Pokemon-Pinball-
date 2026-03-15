/*
 * Stage Builder - Serializer
 *
 * Converts an EditorTable into a complete table folder:
 *   manifest.json
 *   scripts/collision.lua + sprites.lua + main.lua + templates
 *   data/top.collision + bottom.collision (binary collision maps)
 *   data/top.map + top.attr + bottom.map + bottom.attr (binary tilemaps)
 */

#ifndef EDITOR_SERIALIZE_H
#define EDITOR_SERIALIZE_H

#include "game/editor.h"

/* Serialize the editor table to a complete table folder.
 * output_path: directory path where the table folder will be created.
 * Returns true on success. */
bool editor_serialize_table(EditorState *editor, const char *output_path);

/* Serialize only the Lua scripts (for hot-reload during playtest).
 * Returns true on success. */
bool editor_serialize_scripts_only(EditorState *editor, const char *output_path);

/* Serialize collision maps to binary files in data/ subfolder.
 * Returns true on success. */
bool editor_serialize_collision(EditorState *editor, const char *output_path);

/* Serialize tilemaps to binary files in data/ subfolder.
 * Returns true on success. */
bool editor_serialize_tilemaps(EditorState *editor, const char *output_path);

/* Serialize palette data to data/palettes.bin (128 bytes).
 * Returns true on success. */
bool editor_serialize_palettes(EditorState *editor, const char *output_path);

#endif /* EDITOR_SERIALIZE_H */
