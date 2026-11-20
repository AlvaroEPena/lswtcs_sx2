// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

typedef struct {
  int open, selected, show_fps, fps_size, fps_tenths, save_ok;
  uint64_t saved_at;
} PortMenuSnapshot;

void port_menu_init(void);
// Returns true while game input must be consumed by the port UI.
int port_menu_handle_input(uint64_t held, uint64_t pressed);
int port_menu_blocks_input(void);
void port_menu_snapshot(PortMenuSnapshot *out);
void port_menu_frame_presented(uint64_t now, uint64_t frequency);
// Called only with the game's real GL context owned/current, before present.
void port_overlay_draw(int width, int height);
