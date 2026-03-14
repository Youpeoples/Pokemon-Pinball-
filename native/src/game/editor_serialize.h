/*
 * Stage Builder - Lua Serializer
 *
 * Converts an EditorTable into a complete table folder:
 *   manifest.json
 *   scripts/collision.lua
 *   scripts/sprites.lua
 *   scripts/main.lua
 *   scripts/slot.lua (template)
 *   scripts/catchem.lua (template)
 *   scripts/evolution.lua (template)
 *   scripts/map_move.lua (template)
 *   scripts/billboard.lua (template)
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

#endif /* EDITOR_SERIALIZE_H */
