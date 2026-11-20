// SPDX-License-Identifier: MIT
#include <switch.h>
#include "config.h"
#include "util.h"
#include "port_menu.h"

static int menu_open, selected, fps_enabled, fps_size, input_guard;
static int combo_was_held;
static int fps_tenths, save_ok;
static uint64_t saved_at;
static uint64_t fps_started;
static unsigned fps_frames;

void port_menu_init(void) {
  __atomic_store_n(&menu_open, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&selected, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&fps_enabled, config.show_fps != 0, __ATOMIC_RELEASE);
  __atomic_store_n(&fps_size, config.fps_size, __ATOMIC_RELEASE);
  __atomic_store_n(&input_guard, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&fps_tenths, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&saved_at, 0, __ATOMIC_RELEASE);
  combo_was_held = 0;
  fps_started = 0;
  fps_frames = 0;
}

int port_menu_blocks_input(void) {
  return __atomic_load_n(&menu_open, __ATOMIC_ACQUIRE) ||
         __atomic_load_n(&input_guard, __ATOMIC_ACQUIRE);
}

int port_menu_handle_input(uint64_t held, uint64_t pressed) {
  const uint64_t chord = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Down;
  const int combo = (held & chord) == chord;
  if (combo && !combo_was_held) {
    const int opening = !__atomic_load_n(&menu_open, __ATOMIC_ACQUIRE);
    __atomic_store_n(&menu_open, opening, __ATOMIC_RELEASE);
    __atomic_store_n(&input_guard, 1, __ATOMIC_RELEASE);
    if (opening) __atomic_store_n(&selected, 0, __ATOMIC_RELEASE);
    debugPrintf("port menu: %s\n", opening ? "open" : "closed");
  }
  combo_was_held = combo;
  // Do not reuse any held opening/closing buttons as game or menu actions.
  if (!held) __atomic_store_n(&input_guard, 0, __ATOMIC_RELEASE);
  if (combo || __atomic_load_n(&input_guard, __ATOMIC_ACQUIRE)) return 1;
  if (!__atomic_load_n(&menu_open, __ATOMIC_ACQUIRE)) return 0;

  int row = __atomic_load_n(&selected, __ATOMIC_RELAXED);
  if (pressed & (HidNpadButton_Up | HidNpadButton_Down)) {
    row = (row + ((pressed & HidNpadButton_Up) ? 2 : 1)) % 3;
    __atomic_store_n(&selected, row, __ATOMIC_RELEASE);
  }
  int changed = 0;
  if ((pressed & HidNpadButton_B) || (row == 2 && (pressed & HidNpadButton_A))) {
    __atomic_store_n(&menu_open, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&input_guard, 1, __ATOMIC_RELEASE);
  } else if (row == 0 && (pressed & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right))) {
    config.show_fps = !__atomic_load_n(&fps_enabled, __ATOMIC_RELAXED);
    __atomic_store_n(&fps_enabled, config.show_fps, __ATOMIC_RELEASE);
    changed = 1;
  } else if (row == 1 && (pressed & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right))) {
    config.fps_size = (__atomic_load_n(&fps_size, __ATOMIC_RELAXED) +
                       ((pressed & HidNpadButton_Left) ? 2 : 1)) % 3;
    __atomic_store_n(&fps_size, config.fps_size, __ATOMIC_RELEASE);
    changed = 1;
  }
  if (changed) {
    const int ok = write_config(CONFIG_NAME) == 0;
    __atomic_store_n(&save_ok, ok, __ATOMIC_RELAXED);
    __atomic_store_n(&saved_at, armGetSystemTick(), __ATOMIC_RELEASE);
    debugPrintf("port menu: FPS %s size=%d, config %s\n", config.show_fps ? "on" : "off", config.fps_size, ok ? "saved" : "save failed");
  }
  return 1;
}

void port_menu_snapshot(PortMenuSnapshot *out) {
  out->open = __atomic_load_n(&menu_open, __ATOMIC_ACQUIRE);
  out->selected = __atomic_load_n(&selected, __ATOMIC_ACQUIRE);
  out->show_fps = __atomic_load_n(&fps_enabled, __ATOMIC_ACQUIRE);
  out->fps_size = __atomic_load_n(&fps_size, __ATOMIC_ACQUIRE);
  out->fps_tenths = __atomic_load_n(&fps_tenths, __ATOMIC_ACQUIRE);
  out->saved_at = __atomic_load_n(&saved_at, __ATOMIC_ACQUIRE);
  out->save_ok = __atomic_load_n(&save_ok, __ATOMIC_RELAXED);
}

void port_menu_frame_presented(uint64_t now, uint64_t frequency) {
  if (!frequency) return;
  if (!fps_started || now < fps_started) {
    fps_started = now;
    fps_frames = 0;
    return;
  }
  ++fps_frames;
  const uint64_t elapsed = now - fps_started;
  if (elapsed >= frequency / 2 && elapsed) {
    const unsigned tenths = (unsigned)(((uint64_t)fps_frames * frequency * 10 + elapsed / 2) / elapsed);
    __atomic_store_n(&fps_tenths, (int)tenths, __ATOMIC_RELEASE);
    fps_started = now;
    fps_frames = 0;
  }
}
