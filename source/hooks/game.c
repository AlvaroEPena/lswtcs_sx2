/* game.c -- engine-level patches for libTTapp.so
 *
 * The game needs a few small binary-level fixes on Switch: the stock
 * NuInputDevicePS::GetGamePadButtonIndex only recognises a subset of Android
 * keycodes, and SetLevelSfxBits can run before its level-spline table is ready
 * on the Android lifecycle path we emulate here. Threading/TLS is handled in
 * pthr.c.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <switch.h>
#include "../util.h"
#include "../so_util.h"
#include "../hooks.h"
#include "../keycodes.h"
#include "../pthr.h"

extern so_module game_mod;

typedef void (*WorldInfoFunc)(void *world_info);
typedef void (*VoidFunc)(void);
typedef void (*MapPlayerToPortFunc)(int player, int port);

static WorldInfoFunc orig_SetLevelSfxBits;
static WorldInfoFunc LevelSplines_InitForLevel;
static VoidFunc orig_GamePads_Init;
static VoidFunc orig_Players_Init;
static uint8_t *game_player_progress;
static int32_t *game_use_player_list;
static int restore_players_pending;
static MapPlayerToPortFunc NuPadMapPlayerToPort;
static volatile uint8_t *game_is_pressed_start;
static volatile uint8_t *game_not_touch_start_by_joystick;
static int (*orig_saveloadLoadSlot)(int,void*,int);

static uintptr_t *game_active_player;
static uintptr_t hub_update;
static float (*orig_NuVecXZDistSqr)(const void *,const void *,void *);

// Hub_Update forms &player->position before checking whether SetPlayer found
// an active human. On a restored game's first update this is NULL+0x6c even
// though Player[0] exists. Treat just this proximity test as "not near" until
// a player is active. Preserve native selection flags and all other callers.
static __attribute__((noinline)) float NuVecXZDistSqr_hub_safe(
    const void *a,const void *b,void *difference) {
  const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
  float result;
  if (caller==hub_update+0x12b8 &&
      a==(const void*)0x6c && !difference && game_active_player && !*game_active_player) {
    result=INFINITY;
  } else {
    result=orig_NuVecXZDistSqr(a,b,difference);
  }
  return result;
}

// MenuExitNewGame and RestoreNewGame both apply this native fallback, but
// MenuExitLoad does not. Players_Init's restored path copies the transient
// progress byte into each object's active-human bit; SetPlayer then selects
// NULL if both are zero. Apply the same fallback once after a full game load,
// before the native initializer consumes these bytes. PlayerProgress is BSS,
// separate from serialized Game data; no save/unlock data is edited here.
static __attribute__((noinline)) void Players_Init_restore_control(void) {
  const int restoring=__atomic_exchange_n(&restore_players_pending,0,__ATOMIC_ACQ_REL);
  if (restoring && game_use_player_list && game_player_progress) {
    debugPrintf("restored player setup: UsePlayerList=%d active=%u,%u\n",
      *game_use_player_list,game_player_progress[6],game_player_progress[22]);
    if (*game_use_player_list && !game_player_progress[6] && !game_player_progress[22]) {
      game_player_progress[6]=1;
      debugPrintf("restored player fix: applied native New Game fallback to activate player 1\n");
    }
  }
  orig_Players_Init();
}

static int saveloadLoadSlot_restore_control(int slot, void *buffer, int bytes) {
  const int result=orig_saveloadLoadSlot(slot,buffer,bytes);
  if (result && slot>=0 && slot<3 && bytes==32348)
    __atomic_store_n(&restore_players_pending,1,__ATOMIC_RELEASE);
  return result;
}

enum {
  WORLDINFO_HEAP_CURSOR_OFF = 0x108,
  WORLDINFO_LEVEL_SPLINES_OFF = 0x2b48,
};

// If the Android binary asks for level SFX before its level-spline table is
// present, keep the original routine on a valid all-null table instead of
// letting it index through NULL. The real initializer overwrites this later if
// the level load path reaches it.
static void *empty_level_spline_table[64];

static int patch_relocation_slot(so_module *mod, const char *symbol, uintptr_t replacement) {
  int patched = 0;

  for (int i = 0; i < mod->elf_hdr->e_shnum; i++) {
    const char *sh_name = mod->shstrtab + mod->sec_hdr[i].sh_name;
    if (strcmp(sh_name, ".rela.plt") != 0 && strcmp(sh_name, ".rela.dyn") != 0)
      continue;

    Elf64_Rela *rels = (Elf64_Rela *)((uintptr_t)mod->load_base + mod->sec_hdr[i].sh_addr);
    const size_t count = mod->sec_hdr[i].sh_size / sizeof(*rels);
    for (size_t j = 0; j < count; j++) {
      Elf64_Sym *sym = &mod->syms[ELF64_R_SYM(rels[j].r_info)];
      const char *name = mod->dynstrtab + sym->st_name;
      const unsigned type = ELF64_R_TYPE(rels[j].r_info);
      // Selected virtual callbacks live in C++ vtables as ABS64 relocations.
      // Patch their writable staging slots before publication. An interior
      // function address (nonzero addend) must not become wrapper+addend.
      if ((type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_GLOB_DAT ||
           (type == R_AARCH64_ABS64 && rels[j].r_addend == 0)) &&
          strcmp(name, symbol) == 0) {
        uintptr_t *slot = so_relocation_target(mod, rels[j].r_offset);
        *slot = replacement + rels[j].r_addend;
        patched++;
      }
    }
  }

  return patched;
}

static void SetLevelSfxBits_safe(void *world_info) {
  debugPrintf("load stage enter: SetLevelSfxBits world=%p\n",world_info);
  if (!orig_SetLevelSfxBits || !world_info)
    return;

  void **level_splines = (void **)((uint8_t *)world_info + WORLDINFO_LEVEL_SPLINES_OFF);
  if (*level_splines == NULL) {
    const void *heap_cursor = *(void **)((uint8_t *)world_info + WORLDINFO_HEAP_CURSOR_OFF);
    if (LevelSplines_InitForLevel && heap_cursor)
      LevelSplines_InitForLevel(world_info);

    if (*level_splines == NULL) {
      *level_splines = empty_level_spline_table;
      debugPrintf("SetLevelSfxBits: using empty level-spline table for world %p\n", world_info);
    } else {
      debugPrintf("SetLevelSfxBits: initialized missing level-spline table for world %p\n", world_info);
    }
  }

  orig_SetLevelSfxBits(world_info);
  debugPrintf("load stage leave: SetLevelSfxBits world=%p\n",world_info);
}

// The Android build exposes touch as input port 0 and a physical gamepad as
// port 1. Its automatic player assignment chooses the first connected port,
// so touch becomes player 1 and the controller is ignored until START asks the
// engine to remap it. Make the Switch controller player 1 before the first
// input update; touch data remains available to the game's UI subsystem.
static void GamePads_Init_controller_first(void) {
  orig_GamePads_Init();
  NuPadMapPlayerToPort(0, 1);

  // The stock auto-mapper sets these when START assigns the controller. Our
  // eager assignment intentionally bypasses that branch, so mirror its UI
  // state as well or DrawPanel keeps rendering "Press the Start button" at
  // the top of every in-game frame.
  if (game_is_pressed_start)
    *game_is_pressed_start = 1;
  if (game_not_touch_start_by_joystick)
    *game_not_touch_start_by_joystick = 0;

  debugPrintf("mapped controller port 1 to player 1\n");
}

// libTTapp.so's internal gamepad button bitmask (from the game's own enum,
// verified against the lswtcs-vita port for build 20202)
enum {
  GAMEPAD_ACTION  = 0x00000080,
  GAMEPAD_JUMP    = 0x00000040,
  GAMEPAD_SPECIAL = 0x00000020,
  GAMEPAD_TAG     = 0x00000010,
  GAMEPAD_L1      = 0x00000004,
  GAMEPAD_R1      = 0x00000008,
  GAMEPAD_L2      = 0x00000001,
  GAMEPAD_R2      = 0x00000002,
  GAMEPAD_L3      = 0x00000200,
  GAMEPAD_R3      = 0x00000400,
  GAMEPAD_START   = 0x00000800,
};

// NuInputDevicePS::GetGamePadButtonIndex(int androidKeyCode, int *isSuccess)
static unsigned int GetGamePadButtonIndex(int android_code, int *is_success) {
  *is_success = 1;
  switch (android_code) {
    case AKEYCODE_HOME:
    case AKEYCODE_BUTTON_START:  return GAMEPAD_START;
    case AKEYCODE_DPAD_UP:
    case AKEYCODE_DPAD_DOWN:
    case AKEYCODE_DPAD_LEFT:
    case AKEYCODE_DPAD_RIGHT:    return 0; // d-pad is read as an axis
    case AKEYCODE_BUTTON_A:      return GAMEPAD_JUMP;
    case AKEYCODE_BUTTON_B:      return GAMEPAD_SPECIAL;
    case AKEYCODE_BUTTON_X:      return GAMEPAD_ACTION;
    case AKEYCODE_BUTTON_Y:      return GAMEPAD_TAG;
    case AKEYCODE_BUTTON_L1:     return GAMEPAD_L1;
    case AKEYCODE_BUTTON_R1:     return GAMEPAD_R1;
    case AKEYCODE_BUTTON_L2:     return GAMEPAD_L2;
    case AKEYCODE_BUTTON_R2:     return GAMEPAD_R2;
    case AKEYCODE_BUTTON_THUMBL: return GAMEPAD_L3;
    case AKEYCODE_BUTTON_THUMBR: return GAMEPAD_R3;
    default:
      *is_success = 0;
      return 0;
  }
}

void patch_game(void) {
  game_active_player=(void*)so_try_find_addr_rx(&game_mod,"player");
  hub_update=so_try_find_addr_rx(&game_mod,"_Z10Hub_UpdateP11WORLDINFO_s");
  orig_NuVecXZDistSqr=(void*)so_try_find_addr_rx(&game_mod,"NuVecXZDistSqr");
  if (game_active_player && hub_update && orig_NuVecXZDistSqr) {
    const int slots=patch_relocation_slot(&game_mod,"NuVecXZDistSqr",(uintptr_t)NuVecXZDistSqr_hub_safe);
    debugPrintf("patched Hub null-player proximity guard: slots=%d active_player=%p caller=%p\n",
      slots,(void*)game_active_player,(void*)(hub_update+0x12b8));
  }
  orig_Players_Init=(VoidFunc)so_try_find_addr_rx(&game_mod,"_Z12Players_Initv");
  game_player_progress=(void*)so_try_find_addr_rx(&game_mod,"PlayerProgress");
  game_use_player_list=(void*)so_try_find_addr_rx(&game_mod,"UsePlayerList");
  if (orig_Players_Init && game_player_progress && game_use_player_list) {
    const int slots=patch_relocation_slot(&game_mod,"_Z12Players_Initv",(uintptr_t)Players_Init_restore_control);
    debugPrintf("patched restored player selection: slots=%d progress=%p use_list=%p\n",
      slots,game_player_progress,game_use_player_list);
  }
  orig_saveloadLoadSlot=(void*)so_try_find_addr_rx(&game_mod,"saveloadLoadSlot");
  if (orig_saveloadLoadSlot) {
    const int count=patch_relocation_slot(&game_mod,"saveloadLoadSlot",(uintptr_t)saveloadLoadSlot_restore_control);
    debugPrintf("save load diagnostic slots: %d\n",count);
  }
  pthr_set_role_symbols(
    so_try_find_addr_rx(&game_mod, "_Z11AndroidMainPv"),
    so_try_find_addr_rx(&game_mod, "_Z17renderThread_mainPv"),
    so_try_find_addr_rx(&game_mod, "_ZN8NuThread10ThreadMainEPv"));

  orig_SetLevelSfxBits =
    (WorldInfoFunc)so_try_find_addr_rx(&game_mod, "_Z15SetLevelSfxBitsP11WORLDINFO_s");
  LevelSplines_InitForLevel =
    (WorldInfoFunc)so_try_find_addr_rx(&game_mod, "_Z25LevelSplines_InitForLevelP11WORLDINFO_s");
  if (orig_SetLevelSfxBits) {
    const int patched = patch_relocation_slot(&game_mod, "_Z15SetLevelSfxBitsP11WORLDINFO_s",
                                              (uintptr_t)SetLevelSfxBits_safe);
    debugPrintf("patched SetLevelSfxBits relocation slots: %d\n", patched);
  } else {
    debugPrintf("SetLevelSfxBits not found; cannot install level-spline crash guard\n");
  }

  orig_GamePads_Init =
    (VoidFunc)so_try_find_addr_rx(&game_mod, "_Z13GamePads_Initv");
  NuPadMapPlayerToPort =
    (MapPlayerToPortFunc)so_try_find_addr_rx(&game_mod, "NuPadMapPlayerToPort");
  game_is_pressed_start =
    (volatile uint8_t *)so_try_find_addr_rx(&game_mod, "isPressedStart");
  game_not_touch_start_by_joystick =
    (volatile uint8_t *)so_try_find_addr_rx(&game_mod, "m_IsNotTouchStartByJoystick");
  if (orig_GamePads_Init && NuPadMapPlayerToPort) {
    const int patched = patch_relocation_slot(&game_mod, "_Z13GamePads_Initv",
                                               (uintptr_t)GamePads_Init_controller_first);
    debugPrintf("patched GamePads_Init relocation slots: %d\n", patched);
  } else {
    debugPrintf("controller-first mapping symbols missing; using stock player assignment\n");
  }

}

void patch_game_code(void) {
  // Patch code in staging before Switch 2 RO makes its copy executable.
  // patch through the writable load_base mirror: patch_game runs before
  // so_finalize, when the executable-side mapping doesn't exist yet (writing
  // to the _rx address here is what caused the first on-hardware crash)
  uintptr_t addr = so_try_find_addr(&game_mod, "_ZN15NuInputDevicePS21GetGamePadButtonIndexEiPi");
  if (addr) {
    hook_arm64(addr, (uintptr_t)GetGamePadButtonIndex);
    debugPrintf("patched GetGamePadButtonIndex @ %p\n", (void *)addr);
  } else {
    debugPrintf("GetGamePadButtonIndex not found; using stock mapping\n");
  }
}
