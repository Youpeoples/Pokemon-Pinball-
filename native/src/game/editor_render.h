/*
 * Stage Builder - Rendering
 *
 * Handles the editor viewport rendering including:
 * - Tilemap preview with zoom/pan
 * - Grid overlay
 * - Object bounding boxes
 * - Component palette sidebar
 * - Property inspector panel
 * - Collision overlay
 */

#ifndef EDITOR_RENDER_H
#define EDITOR_RENDER_H

#include "game/editor.h"

/* Forward declarations */
typedef struct Renderer Renderer;
typedef struct Platform Platform;

/* Main editor viewport render - called each frame when editor is active */
void editor_render_viewport(EditorState *editor, Renderer *renderer, Platform *platform);

/* Import tilemap from VRAM into editor table */
void editor_import_tilemap(EditorState *editor, VirtualVRAM *vram);

#endif /* EDITOR_RENDER_H */
