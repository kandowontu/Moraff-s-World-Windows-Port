#ifndef MR_INPUT_H
#define MR_INPUT_H

#include <stddef.h>

#include "mr_world.h"

typedef enum MRMovementMode {
    /* Original default: Up advances; Left/Right/Down rotate the view. */
    MR_MOVEMENT_RELATIVE = 0,
    /* Original Escape toggle: every arrow moves in an absolute direction. */
    MR_MOVEMENT_CARDINAL = 1
} MRMovementMode;

typedef enum MRArrowKey {
    MR_ARROW_UP = 0x48,
    MR_ARROW_RIGHT = 0x4D,
    MR_ARROW_DOWN = 0x50,
    MR_ARROW_LEFT = 0x4B
} MRArrowKey;

typedef enum MRInputAction {
    MR_INPUT_NONE = 0,
    MR_INPUT_MOVE = 1,
    MR_INPUT_TURN = 2
} MRInputAction;

/* DUNSMALL 0CD2-1052 compares the uppercased INKEY$ result against this
 * exact command set.  The enum describes dispatch only; context-dependent
 * rules (for example D drinking at the fountain instead of descending) are
 * resolved by the game-state layer. */
typedef enum MRMainCommand {
    MR_COMMAND_NONE = 0,
    MR_COMMAND_DESCEND_OR_DRINK,
    MR_COMMAND_VIEW_STATISTICS,
    MR_COMMAND_QUIT_TO_CHARACTER_MENU,
    MR_COMMAND_ASCEND_OR_USE_ROPE,
    MR_COMMAND_CAST_PREPARATION_SPELL,
    MR_COMMAND_MAGIC_ITEM_INVENTORY,
    MR_COMMAND_USE_PREPARATION_ITEM,
    MR_COMMAND_DROP_CARRIED_TREASURE,
    MR_COMMAND_HELP,
    MR_COMMAND_PAUSE,
    MR_COMMAND_SET_DELAY,
    MR_COMMAND_TAKE_PILL,
    MR_COMMAND_USE_WAND,
    MR_COMMAND_TOGGLE_SOUND,
    MR_COMMAND_NEXT_PRIMARY_COLOR,
    MR_COMMAND_NEXT_SECONDARY_COLOR,
    MR_COMMAND_TOGGLE_MOVEMENT_MODE
} MRMainCommand;

typedef enum MRCombatCommand {
    MR_COMBAT_COMMAND_NONE = 0,
    MR_COMBAT_COMMAND_HELP,
    MR_COMBAT_COMMAND_SWORD,
    MR_COMBAT_COMMAND_MACE,
    MR_COMBAT_COMMAND_KNIFE,
    MR_COMBAT_COMMAND_PAUSE,
    MR_COMBAT_COMMAND_FISTS,
    MR_COMBAT_COMMAND_CAST,
    MR_COMBAT_COMMAND_USE_ITEM,
    MR_COMBAT_COMMAND_TAKE_PILL,
    MR_COMBAT_COMMAND_USE_WAND,
    MR_COMBAT_COMMAND_BREATHE_FIRE
} MRCombatCommand;

typedef struct MRInputState {
    MRDirection facing;
    MRMovementMode movement_mode;
    unsigned long movement_turn;
} MRInputState;

typedef struct MRInputResult {
    MRInputAction action;
    MRDirection direction;
} MRInputResult;

enum {
    MR_KEYBOARD_DRAIN_POLLS = 18,
    MR_TOP_MESSAGE_SECONDS = 2,
    MR_MAX_CONFIGURED_DELAY = 3000
};

void mr_input_init(MRInputState *state);
void mr_input_toggle_movement_mode(MRInputState *state);
int mr_input_original_uppercase(int first_byte);
MRMainCommand mr_input_decode_main_command(int first_byte);
MRCombatCommand mr_input_decode_combat_command(int first_byte);
MRInputResult mr_input_dispatch_arrow(MRInputState *state, MRArrowKey key);
void mr_input_record_successful_step(MRInputState *state);
int mr_input_clamp_configured_delay(int delay);
int mr_input_uses_software_key_repeat(void);
int mr_input_self_test(char *error, size_t error_size);

#endif
