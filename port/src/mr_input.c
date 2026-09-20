#include "mr_input.h"

#include <stdio.h>

static MRDirection mr_wrap_direction(int direction) {
    while (direction > MR_DIRECTION_WEST) direction -= 4;
    while (direction < MR_DIRECTION_NORTH) direction += 4;
    return (MRDirection)direction;
}

void mr_input_init(MRInputState *state) {
    if (!state) return;
    /* DUNSMALL 01D1-01EA initializes the movement turn at one and facing at
     * four.  DS:B524 is zero-initialized, selecting relative movement. */
    state->facing = MR_DIRECTION_WEST;
    state->movement_mode = MR_MOVEMENT_RELATIVE;
    state->movement_turn = 1;
}

void mr_input_toggle_movement_mode(MRInputState *state) {
    if (!state) return;
    /* DUNSMALL 10D9-10F9 increments the single and wraps two back to zero. */
    state->movement_mode = state->movement_mode == MR_MOVEMENT_RELATIVE
                               ? MR_MOVEMENT_CARDINAL
                               : MR_MOVEMENT_RELATIVE;
}

int mr_input_original_uppercase(int first_byte) {
    /* DUNSMALL 2F95-2FC7 uses strict comparisons against 60h and 7Ah.
     * Consequently 'a'..'y' are uppercased while lowercase 'z' survives. */
    if (first_byte > 0x60 && first_byte < 0x7A) return first_byte - 0x20;
    return first_byte;
}

MRMainCommand mr_input_decode_main_command(int first_byte) {
    const int key = mr_input_original_uppercase(first_byte);
    switch (key) {
        case 'D': return MR_COMMAND_DESCEND_OR_DRINK;
        case 'V': return MR_COMMAND_VIEW_STATISTICS;
        case 'Q': return MR_COMMAND_QUIT_TO_CHARACTER_MENU;
        case 'U': return MR_COMMAND_ASCEND_OR_USE_ROPE;
        case 'C': return MR_COMMAND_CAST_PREPARATION_SPELL;
        case 'M': return MR_COMMAND_MAGIC_ITEM_INVENTORY;
        case 'I': return MR_COMMAND_USE_PREPARATION_ITEM;
        case 'A': return MR_COMMAND_DROP_CARRIED_TREASURE;
        case 'H':
        case ';': return MR_COMMAND_HELP;
        case 'P': return MR_COMMAND_PAUSE;
        case 'E': return MR_COMMAND_SET_DELAY;
        case 'T': return MR_COMMAND_TAKE_PILL;
        case 'W': return MR_COMMAND_USE_WAND;
        case 'O': return MR_COMMAND_TOGGLE_SOUND;
        case '#': return MR_COMMAND_NEXT_PRIMARY_COLOR;
        case '@': return MR_COMMAND_NEXT_SECONDARY_COLOR;
        case 27: return MR_COMMAND_TOGGLE_MOVEMENT_MODE;
        default: return MR_COMMAND_NONE;
    }
}

MRCombatCommand mr_input_decode_combat_command(int first_byte) {
    const int key = mr_input_original_uppercase(first_byte);
    switch (key) {
        case 'H':
        case ';': return MR_COMBAT_COMMAND_HELP;
        case 'S': return MR_COMBAT_COMMAND_SWORD;
        case 'M': return MR_COMBAT_COMMAND_MACE;
        case 'K': return MR_COMBAT_COMMAND_KNIFE;
        case 'P': return MR_COMBAT_COMMAND_PAUSE;
        case 'F': return MR_COMBAT_COMMAND_FISTS;
        case 'C': return MR_COMBAT_COMMAND_CAST;
        case 'I': return MR_COMBAT_COMMAND_USE_ITEM;
        case 'T': return MR_COMBAT_COMMAND_TAKE_PILL;
        case 'W': return MR_COMBAT_COMMAND_USE_WAND;
        case 'B': return MR_COMBAT_COMMAND_BREATHE_FIRE;
        default: return MR_COMBAT_COMMAND_NONE;
    }
}

MRInputResult mr_input_dispatch_arrow(MRInputState *state, MRArrowKey key) {
    MRInputResult result = {MR_INPUT_NONE, 0};
    if (!state) return result;

    if (state->movement_mode == MR_MOVEMENT_CARDINAL) {
        switch (key) {
            case MR_ARROW_UP: result.direction = MR_DIRECTION_NORTH; break;
            case MR_ARROW_RIGHT: result.direction = MR_DIRECTION_EAST; break;
            case MR_ARROW_DOWN: result.direction = MR_DIRECTION_SOUTH; break;
            case MR_ARROW_LEFT: result.direction = MR_DIRECTION_WEST; break;
            default: return result;
        }
        state->facing = result.direction;
        result.action = MR_INPUT_MOVE;
        return result;
    }

    switch (key) {
        case MR_ARROW_UP:
            result.action = MR_INPUT_MOVE;
            result.direction = state->facing;
            break;
        case MR_ARROW_RIGHT:
            state->facing = mr_wrap_direction((int)state->facing + 1);
            result.action = MR_INPUT_TURN;
            result.direction = state->facing;
            break;
        case MR_ARROW_DOWN:
            state->facing = mr_wrap_direction((int)state->facing + 2);
            result.action = MR_INPUT_TURN;
            result.direction = state->facing;
            break;
        case MR_ARROW_LEFT:
            state->facing = mr_wrap_direction((int)state->facing - 1);
            result.action = MR_INPUT_TURN;
            result.direction = state->facing;
            break;
        default:
            break;
    }
    return result;
}

void mr_input_record_successful_step(MRInputState *state) {
    if (!state) return;
    /* Each of 30D9/3192/3254/3316 increments DS:B474 only after both the
     * monster-occupancy and procedural-wall tests permit the step. */
    ++state->movement_turn;
}

int mr_input_clamp_configured_delay(int delay) {
    /* Numeric input mode four accepts digits, and 0F53-0F63 clamps only the
     * upper end.  Zero is explicitly advertised as the default. */
    if (delay < 0) delay = 0;
    if (delay > MR_MAX_CONFIGURED_DELAY) delay = MR_MAX_CONFIGURED_DELAY;
    return delay;
}

int mr_input_uses_software_key_repeat(void) {
    /* The main loop at 087F performs one INKEY$ poll per pass.  There is no
     * held-key latch or repeat timer: DOS BIOS typematic events reappear as
     * ordinary INKEY$ results and naturally repeat a held movement key. */
    return 0;
}

static int mr_input_fail(char *error, size_t error_size, const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

int mr_input_self_test(char *error, size_t error_size) {
    MRInputState state;
    MRInputResult result;
    mr_input_init(&state);
    if (state.facing != MR_DIRECTION_WEST ||
        state.movement_mode != MR_MOVEMENT_RELATIVE ||
        state.movement_turn != 1)
        return mr_input_fail(error, error_size, "initial input state mismatch");

    result = mr_input_dispatch_arrow(&state, MR_ARROW_UP);
    if (result.action != MR_INPUT_MOVE || result.direction != MR_DIRECTION_WEST)
        return mr_input_fail(error, error_size, "relative forward mismatch");
    result = mr_input_dispatch_arrow(&state, MR_ARROW_RIGHT);
    if (result.action != MR_INPUT_TURN || state.facing != MR_DIRECTION_NORTH)
        return mr_input_fail(error, error_size, "relative right turn mismatch");
    result = mr_input_dispatch_arrow(&state, MR_ARROW_DOWN);
    if (result.action != MR_INPUT_TURN || state.facing != MR_DIRECTION_SOUTH)
        return mr_input_fail(error, error_size, "relative about-face mismatch");
    result = mr_input_dispatch_arrow(&state, MR_ARROW_LEFT);
    if (result.action != MR_INPUT_TURN || state.facing != MR_DIRECTION_EAST)
        return mr_input_fail(error, error_size, "relative left turn mismatch");

    mr_input_toggle_movement_mode(&state);
    if (state.movement_mode != MR_MOVEMENT_CARDINAL)
        return mr_input_fail(error, error_size, "Escape mode toggle mismatch");
    result = mr_input_dispatch_arrow(&state, MR_ARROW_UP);
    if (result.action != MR_INPUT_MOVE || result.direction != MR_DIRECTION_NORTH ||
        state.facing != MR_DIRECTION_NORTH)
        return mr_input_fail(error, error_size, "cardinal up mismatch");
    result = mr_input_dispatch_arrow(&state, MR_ARROW_LEFT);
    if (result.action != MR_INPUT_MOVE || result.direction != MR_DIRECTION_WEST ||
        state.facing != MR_DIRECTION_WEST)
        return mr_input_fail(error, error_size, "cardinal left mismatch");
    mr_input_toggle_movement_mode(&state);
    if (state.movement_mode != MR_MOVEMENT_RELATIVE)
        return mr_input_fail(error, error_size, "second Escape toggle mismatch");

    if (mr_input_original_uppercase('a') != 'A' ||
        mr_input_original_uppercase('y') != 'Y' ||
        mr_input_original_uppercase('z') != 'z' ||
        mr_input_original_uppercase(0) != 0)
        return mr_input_fail(error, error_size, "original uppercase range mismatch");
    if (mr_input_decode_main_command('d') != MR_COMMAND_DESCEND_OR_DRINK ||
        mr_input_decode_main_command('V') != MR_COMMAND_VIEW_STATISTICS ||
        mr_input_decode_main_command('M') != MR_COMMAND_MAGIC_ITEM_INVENTORY ||
        mr_input_decode_main_command('A') != MR_COMMAND_DROP_CARRIED_TREASURE ||
        mr_input_decode_main_command(';') != MR_COMMAND_HELP ||
        mr_input_decode_main_command('P') != MR_COMMAND_PAUSE ||
        mr_input_decode_main_command(27) != MR_COMMAND_TOGGLE_MOVEMENT_MODE ||
        mr_input_decode_main_command('z') != MR_COMMAND_NONE)
        return mr_input_fail(error, error_size, "main command table mismatch");
    if (mr_input_decode_combat_command('s') != MR_COMBAT_COMMAND_SWORD ||
        mr_input_decode_combat_command('M') != MR_COMBAT_COMMAND_MACE ||
        mr_input_decode_combat_command('k') != MR_COMBAT_COMMAND_KNIFE ||
        mr_input_decode_combat_command('f') != MR_COMBAT_COMMAND_FISTS ||
        mr_input_decode_combat_command('c') != MR_COMBAT_COMMAND_CAST ||
        mr_input_decode_combat_command('i') != MR_COMBAT_COMMAND_USE_ITEM ||
        mr_input_decode_combat_command('t') != MR_COMBAT_COMMAND_TAKE_PILL ||
        mr_input_decode_combat_command('w') != MR_COMBAT_COMMAND_USE_WAND ||
        mr_input_decode_combat_command(';') != MR_COMBAT_COMMAND_HELP ||
        mr_input_decode_combat_command('B') != MR_COMBAT_COMMAND_BREATHE_FIRE ||
        mr_input_decode_combat_command('Q') != MR_COMBAT_COMMAND_NONE)
        return mr_input_fail(error, error_size, "combat command table mismatch");
    mr_input_record_successful_step(&state);
    if (state.movement_turn != 2)
        return mr_input_fail(error, error_size, "movement turn increment mismatch");
    if (mr_input_clamp_configured_delay(-1) != 0 ||
        mr_input_clamp_configured_delay(1234) != 1234 ||
        mr_input_clamp_configured_delay(3001) != 3000 ||
        mr_input_uses_software_key_repeat() != 0)
        return mr_input_fail(error, error_size, "input timing constants mismatch");
    if (error && error_size) error[0] = '\0';
    return 1;
}
