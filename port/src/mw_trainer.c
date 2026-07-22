#include "mw_trainer.h"
#include "mw_combat.h"
#include "mw_model_viewer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MW_EXTENSION: native Ctrl+F12 recreation of the separately supplied DOS
 * trainer. It is not a WORLD.EXE routine and therefore is not counted as
 * original-game function coverage in PORT_STATUS.md. */

enum {
    TRAINER_STATS,
    TRAINER_GRID,
    TRAINER_EQUIPMENT,
    TRAINER_EFFECTS
};

enum {
    GRID_SPELLS,
    GRID_SCROLLS,
    GRID_WANDS,
    GRID_PAPERS
};

typedef enum TrainerFieldType {
    TF_U8,
    TF_S8,
    TF_U16,
    TF_U32,
    TF_FLOAT,
    TF_RACE,
    TF_CLASS,
    TF_SEX,
    TF_RAISE
} TrainerFieldType;

typedef struct TrainerField {
    const char *name;
    size_t offset;
    TrainerFieldType type;
} TrainerField;

typedef struct TrainerState {
    int page;
    int selection;
    int scroll_top;
    int grid_set;
    int grid_row;
    int grid_col;
    int equip_selection;
    int effect_selection;
    int effect_top;
    int input_mode;
    int input_len;
    char input[11];
} TrainerState;

#define FIELD(member, kind, label) { label, offsetof(Character, member), kind }

/* Exact 45-field order from TRAINER.ASM. */
static const TrainerField trainer_fields[] = {
    FIELD(race, TF_RACE, "Race"),
    FIELD(class_id, TF_CLASS, "Class"),
    FIELD(sex, TF_SEX, "Sex"),
    FIELD(level, TF_U16, "Level"),
    FIELD(hp_cur, TF_U16, "HP Current"),
    FIELD(hp_max, TF_U16, "HP Maximum"),
    FIELD(sp_cur, TF_FLOAT, "SP Current"),
    FIELD(sp_max, TF_FLOAT, "SP Maximum"),
    FIELD(age, TF_U32, "Age"),
    FIELD(x_pos, TF_U16, "X Position"),
    FIELD(y_pos, TF_U16, "Y Position"),
    FIELD(floor_depth, TF_U16, "Floor Depth"),
    FIELD(stat_str, TF_U16, "Strength"),
    FIELD(stat_int, TF_U16, "Intelligence"),
    FIELD(stat_wis, TF_U16, "Wisdom"),
    FIELD(stat_con, TF_U16, "Constitution"),
    FIELD(stat_agi, TF_U16, "Agility"),
    FIELD(stat_luck, TF_U16, "Luck"),
    FIELD(jewels_pocket, TF_U32, "Jewels Pocket"),
    FIELD(jewels_bank, TF_U32, "Jewels Bank"),
    FIELD(copper_stones, TF_U32, "Copper Stones"),
    FIELD(silver_stones, TF_U32, "Silver Stones"),
    FIELD(ivory_stones, TF_U32, "Ivory Stones"),
    FIELD(gold_stones, TF_U32, "Gold Stones"),
    FIELD(platinum_stones, TF_U32, "Platinum Stones"),
    FIELD(jewel_stones, TF_U32, "Jewel Stones"),
    FIELD(raise_x, TF_RAISE, "Raise Contract"),
    FIELD(holy_grenade, TF_U8, "Holy H Grenade"),
    FIELD(stone_teleport, TF_U8, "Stone of Telep"),
    FIELD(stone_see, TF_U8, "Stone of Seeing"),
    FIELD(floor_slosher, TF_U8, "Floor Slosher"),
    FIELD(potion_heal, TF_U8, "Potion Healing"),
    FIELD(green_pill, TF_S8, "Green Pill"),
    FIELD(orange_pill, TF_S8, "Orange Pill"),
    FIELD(blue_pill, TF_S8, "Blue Pill"),
    FIELD(red_pill, TF_S8, "Red Pill"),
    FIELD(white_pill, TF_S8, "White Pill"),
    FIELD(yellow_pill, TF_S8, "Yellow Pill"),
    FIELD(ring_regen, TF_U8, "Ring of Regen"),
    FIELD(native.ring_prot_plus, TF_U16, "Ring of Prot+"),
    FIELD(antimagic_ring, TF_U8, "AntiMagic Ring"),
    FIELD(native.body_armor_plus, TF_U16, "Body Armor Lv"),
    FIELD(native.gauntlet, TF_U16, "Gauntlet"),
    FIELD(diseased_turns, TF_U16, "Diseased Turns"),
    FIELD(poisoned_turns, TF_U16, "Poisoned Turns")
};

static const TrainerField effect_fields[] = {
    FIELD(native.enchant_wpn_spell, TF_U16, "Weapon Plus"),
    FIELD(native.armor_plus, TF_U16, "Armor Plus"),
    FIELD(eff_feather, TF_U8, "Feather"),
    FIELD(eff_fast_move, TF_U8, "Fast Move"),
    FIELD(eff_invisible, TF_U8, "Invisibility"),
    FIELD(eff_str_bonus, TF_U8, "Str Bonus"),
    FIELD(eff_agi_bonus, TF_U8, "Agi Bonus"),
    FIELD(eff_super_str, TF_U8, "Super Strength"),
    FIELD(eff_super_agi, TF_U8, "Super Agility"),
    FIELD(eff_battle_str, TF_U16, "Battle Str"),
    FIELD(eff_battle_spd, TF_U16, "Battle Speed"),
    FIELD(eff_slow_mon, TF_U16, "Slow Monster"),
    FIELD(eff_pwr_weapon, TF_U8, "Power Weapon"),
    FIELD(eff_pwr_wpn_turns, TF_U16, "Pwr Wpn Turns"),
    FIELD(eff_protect_lv, TF_U8, "Protect Level"),
    FIELD(eff_protect_turns, TF_U16, "Protect Turns"),
    FIELD(eff_resist_poison, TF_U16, "Resist Poison"),
    FIELD(eff_resist_disease, TF_U16, "Resist Disease"),
    FIELD(eff_anti_cold, TF_U16, "Anti Cold"),
    FIELD(eff_anti_fire, TF_U16, "Anti Fire"),
    FIELD(eff_resist_drain, TF_U16, "Resist Drain"),
    FIELD(eff_stop_monster, TF_U16, "Stop Monster"),
    FIELD(eff_hold_monster, TF_U16, "Hold Monster")
};

static const char *const trainer_weapon_names[8] = {
    "Fist", "Stick", "Club", "Mace", "Knife", "Shortsword",
    "Long Sword", "Great Sword"
};

static const char *const trainer_armor_names[8] = {
    "Skin", "Leather", "Chain", "Scale", "Plate", "Field Plate",
    "Titanium", "Ogre"
};

static const char *const trainer_grid_names[4] = {
    "SPELLS", "SCROLLS", "WANDS", "PAPERS"
};

static const char *const trainer_type_names[4] = {
    "E PERMANENT", "H PREPARATION", "A WIZARD BATTLE", "P PRIEST BATTLE"
};

static const char *const trainer_type_long_names[4] = {
    "PERMANENT", "PREPARATION", "WIZARD BATTLE", "PRIEST BATTLE"
};

enum {
    TRAINER_VISIBLE_FIELDS = 20,
    TRAINER_BOX_X = 142,
    TRAINER_BOX_Y = 15,
    TRAINER_BOX_W = 740,
    TRAINER_BOX_H = 738,
    TRAINER_CONTENT_Y = 108,
    TRAINER_ROW_H = 27
};

static int trainer_extended_key(Input *input) {
    int key = input_getch(input);
    if (key == 0) return 0x10000 | input_getch(input);
    return key;
}

static void trainer_text(Video *v, int x, int y, const char *text, u8 color) {
    video_draw_text_scaled(v, x, y, text, color, 3, 4);
}

static void trainer_box(Video *v) {
    video_clear(v, 0);
    video_hline(v, TRAINER_BOX_X, TRAINER_BOX_Y, TRAINER_BOX_W, 11);
    video_hline(v, TRAINER_BOX_X, TRAINER_BOX_Y + TRAINER_BOX_H - 1,
                TRAINER_BOX_W, 11);
    video_vline(v, TRAINER_BOX_X, TRAINER_BOX_Y, TRAINER_BOX_H, 11);
    video_vline(v, TRAINER_BOX_X + TRAINER_BOX_W - 1, TRAINER_BOX_Y,
                TRAINER_BOX_H, 11);
    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_BOX_Y + 12,
                 "MORAFF'S WORLD TRAINER", 11);
    trainer_text(v, TRAINER_BOX_X + 492, TRAINER_BOX_Y + 12,
                 "CTRL-F12", 8);
}

static void trainer_identity(Video *v, const Character *p, const char *page) {
    char line[160];
    int race = p->race < RACE_COUNT ? p->race : 0;
    int cls = p->class_id < CLASS_COUNT ? p->class_id : 0;
    snprintf(line, sizeof(line), "%-14.14s %-8s %c %-11s",
             p->name, race_names[race], p->sex ? 'F' : 'M', class_names[cls]);
    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_BOX_Y + 44, line, 15);
    trainer_text(v, TRAINER_BOX_X + 505, TRAINER_BOX_Y + 44, page, 14);
    video_hline(v, TRAINER_BOX_X, TRAINER_BOX_Y + 80, TRAINER_BOX_W, 11);
}

static uint64_t trainer_field_max(TrainerFieldType type) {
    switch (type) {
    case TF_U8: return 255;
    case TF_S8: return 127;
    case TF_U16: return 32767;
    case TF_RACE: return RACE_COUNT - 1;
    case TF_CLASS: return CLASS_COUNT - 1;
    case TF_SEX: return 1;
    case TF_RAISE: return 1;
    case TF_U32:
    case TF_FLOAT:
    default: return UINT32_MAX;
    }
}

static uint64_t trainer_field_get(const Character *p, const TrainerField *f) {
    const u8 *base = (const u8 *)p + f->offset;
    u16 v16;
    u32 v32;
    float vf;
    switch (f->type) {
    case TF_U8:
    case TF_S8:
    case TF_RACE:
    case TF_CLASS:
    case TF_SEX:
        return *base;
    case TF_U16:
        memcpy(&v16, base, sizeof(v16));
        return v16;
    case TF_U32:
        memcpy(&v32, base, sizeof(v32));
        return v32;
    case TF_FLOAT:
        memcpy(&vf, base, sizeof(vf));
        return vf > 0.0f ? (uint64_t)vf : 0;
    case TF_RAISE:
        memcpy(&v16, base, sizeof(v16));
        return v16 == 0xFFFF ? 0 : 1;
    }
    return 0;
}

static void trainer_field_set(Character *p, const TrainerField *f, uint64_t value) {
    u8 *base = (u8 *)p + f->offset;
    uint64_t maximum = trainer_field_max(f->type);
    if (value > maximum) value = maximum;
    u8 v8 = (u8)value;
    u16 v16 = (u16)value;
    u32 v32 = (u32)value;
    float vf = (float)v32;
    switch (f->type) {
    case TF_U8:
    case TF_S8:
    case TF_RACE:
    case TF_CLASS:
    case TF_SEX:
        *base = v8;
        break;
    case TF_U16:
        memcpy(base, &v16, sizeof(v16));
        break;
    case TF_U32:
        memcpy(base, &v32, sizeof(v32));
        break;
    case TF_FLOAT:
        memcpy(base, &vf, sizeof(vf));
        break;
    case TF_RAISE:
        v16 = value ? 1 : 0xFFFF;
        memcpy(base, &v16, sizeof(v16));
        break;
    }
}

static void trainer_field_adjust(Character *p, const TrainerField *f, int delta) {
    uint64_t value = trainer_field_get(p, f);
    uint64_t maximum = trainer_field_max(f->type);
    if (f->type == TF_RAISE || f->type == TF_SEX) {
        value = !value;
    } else if (f->type == TF_RACE || f->type == TF_CLASS ||
               f->type == TF_U8 || f->type == TF_S8 || f->type == TF_U16) {
        uint64_t modulus = maximum + 1;
        int64_t moved = (int64_t)value + delta;
        while (moved < 0) moved += (int64_t)modulus;
        value = (uint64_t)moved % modulus;
    } else if (delta < 0) {
        uint64_t amount = (uint64_t)(-delta);
        value = value > amount ? value - amount : 0;
    } else if (value > maximum - (uint64_t)delta) {
        value = maximum;
    } else {
        value += (uint64_t)delta;
    }
    trainer_field_set(p, f, value);
}

static void trainer_format_field(char *out, size_t out_size,
                                 const Character *p, const TrainerField *f) {
    uint64_t value = trainer_field_get(p, f);
    if (f->type == TF_RACE) {
        snprintf(out, out_size, "%s", value < RACE_COUNT ? race_names[value] : "?");
    } else if (f->type == TF_CLASS) {
        snprintf(out, out_size, "%s", value < CLASS_COUNT ? class_names[value] : "?");
    } else if (f->type == TF_SEX) {
        snprintf(out, out_size, "%s", value ? "FEMALE" : "MALE");
    } else if (f->type == TF_RAISE) {
        snprintf(out, out_size, "%s", value ? "ACTIVE" : "NONE");
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)value);
    }
}

static void trainer_draw_field_rows(Video *v, Character *p,
                                    const TrainerField *fields, int count,
                                    int selected, int top,
                                    const TrainerState *state) {
    for (int row = 0; row < TRAINER_VISIBLE_FIELDS; row++) {
        int index = top + row;
        int y = TRAINER_CONTENT_Y + row * TRAINER_ROW_H;
        if (index >= count) continue;
        int active = index == selected;
        if (active)
            video_fill_rect(v, TRAINER_BOX_X + 2, y - 3,
                            TRAINER_BOX_W - 4, TRAINER_ROW_H, 1);
        trainer_text(v, TRAINER_BOX_X + 16, y, active ? ">" : " ",
                     active ? 14 : 7);
        trainer_text(v, TRAINER_BOX_X + 42, y, fields[index].name,
                     active ? 15 : 7);
        char value[48];
        if (active && state->input_mode) {
            snprintf(value, sizeof(value), "%.*s_", state->input_len, state->input);
        } else {
            trainer_format_field(value, sizeof(value), p, &fields[index]);
        }
        trainer_text(v, TRAINER_BOX_X + 515, y, value, active ? 15 : 10);
    }
}

static void trainer_draw_footer(Video *v, const char *line1, const char *line2) {
    video_hline(v, TRAINER_BOX_X, TRAINER_BOX_Y + TRAINER_BOX_H - 66,
                TRAINER_BOX_W, 11);
    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_BOX_Y + TRAINER_BOX_H - 53,
                 line1, 7);
    if (line2)
        trainer_text(v, TRAINER_BOX_X + 18, TRAINER_BOX_Y + TRAINER_BOX_H - 28,
                     line2, 8);
}

static void trainer_draw_stats(Game *g, Character *p, const TrainerState *state) {
    trainer_box(&g->video);
    trainer_identity(&g->video, p, "CHARACTER / STATS");
    trainer_draw_field_rows(&g->video, p, trainer_fields,
                            (int)(sizeof(trainer_fields) / sizeof(trainer_fields[0])),
                            state->selection, state->scroll_top, state);
    trainer_draw_footer(&g->video,
        "UP/DOWN SELECT  LEFT/RIGHT +/-1  PGUP/PGDN +/-100  # ENTER=SET",
        "S SPELLS  R SCROLLS  W WANDS  P PAPERS  E EQUIP  F EFFECTS  ESC EXIT");
}

static u8 (*trainer_grid(Character *p, int set))[45] {
    switch (set) {
    case GRID_SCROLLS: return p->scrolls;
    case GRID_WANDS: return p->wands;
    case GRID_PAPERS: return p->papers;
    case GRID_SPELLS:
    default: return p->spells;
    }
}

static void trainer_outline_rect(Video *v, int x, int y, int w, int h,
                                 u8 color) {
    video_hline(v, x, y, w, color);
    video_hline(v, x, y + h - 1, w, color);
    video_vline(v, x, y, h, color);
    video_vline(v, x + w - 1, y, h, color);
}

static void trainer_draw_grid_cell(Video *v, int x, int y, u8 value,
                                   int wand, int selected) {
    char glyph[2] = {0, 0};
    if (wand)
        glyph[0] = value <= 9 ? (char)('0' + value) : '+';
    else
        glyph[0] = value ? 'Y' : 'N';

    if (selected)
        video_fill_rect(v, x - 2, y - 3, 19, 34, 1);
    video_fill_rect(v, x, y, 15, 29, 0);
    trainer_outline_rect(v, x, y, 15, 29,
                         selected ? 15 : (value ? 10 : 8));
    trainer_text(v, x + 3, y + 2, glyph,
                 selected ? 15 : (value ? 10 : 8));
}

static void trainer_draw_grid(Game *g, Character *p, const TrainerState *state) {
    Video *v = &g->video;
    enum {
        GRID_X = TRAINER_BOX_X + 194,
        GRID_CELL_W = 17,
        GRID_ROW_Y = TRAINER_CONTENT_Y + 58,
        GRID_ROW_H = 54
    };
    trainer_box(v);
    trainer_identity(v, p, trainer_grid_names[state->grid_set]);
    u8 (*grid)[45] = trainer_grid(p, state->grid_set);

    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_CONTENT_Y + 12,
                 "TYPE", 8);
    trainer_text(v, TRAINER_BOX_X + 126, TRAINER_CONTENT_Y + 12,
                 "LEVEL", 8);
    for (int level = 0; level < 10; level++) {
        char level_text[4];
        snprintf(level_text, sizeof(level_text), "%d", level + 1);
        int level_x = GRID_X + level * GRID_CELL_W * 3 +
                      (level == 9 ? 17 : 21);
        trainer_text(v, level_x, TRAINER_CONTENT_Y + 12, level_text, 14);
    }

    for (int row = 0; row < 4; row++) {
        int y = GRID_ROW_Y + row * GRID_ROW_H;
        trainer_text(v, TRAINER_BOX_X + 18, y + 3, trainer_type_names[row],
                     row == state->grid_row ? 15 : 14);
        for (int col = 0; col < 30; col++) {
            u8 value = grid[row][col];
            int x = GRID_X + col * GRID_CELL_W;
            int selected = row == state->grid_row && col == state->grid_col;
            trainer_draw_grid_cell(v, x, y, value,
                                   state->grid_set == GRID_WANDS, selected);
        }
    }

    int selected_row = state->grid_row;
    int selected_col = state->grid_col;
    u8 selected_value = grid[selected_row][selected_col];
    const char *spell_name = combat_spell_name(selected_row, selected_col);
    int level = selected_col / 3 + 1;
    int slot = selected_col % 3 + 1;
    int detail_y = TRAINER_BOX_Y + 389;
    video_hline(v, TRAINER_BOX_X + 18, detail_y,
                TRAINER_BOX_W - 36, 11);
    trainer_text(v, TRAINER_BOX_X + 18, detail_y + 15,
                 "SELECTED SPELL", 11);
    char info[128];
    snprintf(info, sizeof(info), "%c-%02d  LEVEL %d / SLOT %d  %s",
             "EHAP"[selected_row], selected_col + 1, level, slot, spell_name);
    trainer_text(v, TRAINER_BOX_X + 18, detail_y + 48, info, 15);

    const char *state_name;
    if (state->grid_set == GRID_WANDS) {
        snprintf(info, sizeof(info), "%s WAND  CHARGES: %u",
                 trainer_type_long_names[selected_row], selected_value);
        state_name = info;
    } else {
        static const char *const item_names[4] = {
            "SPELLBOOK", "SCROLL", "WAND", "PAPER"
        };
        snprintf(info, sizeof(info), "%s %s: %s",
                 trainer_type_long_names[selected_row],
                 item_names[state->grid_set],
                 selected_value ?
                    (state->grid_set == GRID_SPELLS ? "LEARNED" : "OWNED") :
                    (state->grid_set == GRID_SPELLS ? "NOT LEARNED" : "NONE"));
        state_name = info;
    }
    trainer_text(v, TRAINER_BOX_X + 18, detail_y + 81, state_name,
                 selected_value ? 10 : 8);

    char hint[128];
    snprintf(hint, sizeof(hint), "%s %02d: %s",
             trainer_type_long_names[selected_row], selected_col + 1,
             spell_name);
    trainer_draw_footer(v,
        state->grid_set == GRID_WANDS ?
          "ARROWS NAV  SPACE +1 CHARGE  A +1 ALL  N CLEAR  ESC/S BACK" :
          "ARROWS NAV  SPACE TOGGLE  A ALL ON  N ALL OFF  ESC/S BACK",
        hint);
}

static void trainer_draw_equipment(Game *g, Character *p,
                                   const TrainerState *state) {
    Video *v = &g->video;
    trainer_box(v);
    trainer_identity(v, p, "EQUIPMENT");
    trainer_text(v, TRAINER_BOX_X + 52, TRAINER_CONTENT_Y - 3,
                 "ITEM", 14);
    trainer_text(v, TRAINER_BOX_X + 450, TRAINER_CONTENT_Y - 3,
                 "OWN", 14);
    trainer_text(v, TRAINER_BOX_X + 555, TRAINER_CONTENT_Y - 3,
                 "ENCHANT", 14);
    for (int index = 0; index < 16; index++) {
        int armor = index >= 8;
        int slot = index & 7;
        int y = TRAINER_CONTENT_Y + 28 + index * 31;
        int active = index == state->equip_selection;
        u8 owned = armor ? p->armor_inventory[slot] : p->weapon_inventory[slot];
        int enchant = armor ? mw_armor_enchant(p, slot) :
                              mw_weapon_enchant(p, slot);
        if (active)
            video_fill_rect(v, TRAINER_BOX_X + 2, y - 3,
                            TRAINER_BOX_W - 4, 29, 1);
        if (index == 0 || index == 8)
            trainer_text(v, TRAINER_BOX_X + 640, y,
                         armor ? "ARMOR" : "WEAPON", 8);
        trainer_text(v, TRAINER_BOX_X + 16, y, active ? ">" : " ", 14);
        char line[64];
        snprintf(line, sizeof(line), "%d. %s", slot + 1,
                 armor ? trainer_armor_names[slot] : trainer_weapon_names[slot]);
        trainer_text(v, TRAINER_BOX_X + 52, y, line, active ? 15 : 7);
        trainer_text(v, TRAINER_BOX_X + 458, y, owned ? "Y" : "N",
                     active ? 15 : (owned ? 10 : 8));
        snprintf(line, sizeof(line), "%d", enchant);
        trainer_text(v, TRAINER_BOX_X + 585, y, line, active ? 15 : 10);
    }
    trainer_draw_footer(v,
        "UP/DOWN SELECT  SPACE TOGGLES OWNERSHIP  LEFT/RIGHT ENCHANT +/-1",
        "ESC RETURNS TO CHARACTER / STATS");
}

static void trainer_draw_effects(Game *g, Character *p,
                                 const TrainerState *state) {
    trainer_box(&g->video);
    trainer_identity(&g->video, p, "SPELL EFFECTS");
    trainer_draw_field_rows(&g->video, p, effect_fields,
                            (int)(sizeof(effect_fields) / sizeof(effect_fields[0])),
                            state->effect_selection, state->effect_top, state);
    trainer_draw_footer(&g->video,
        "UP/DOWN SELECT  LEFT/RIGHT +/-1  TYPE NUMBER + ENTER TO SET",
        "ESC RETURNS TO CHARACTER / STATS");
}

static void trainer_draw(Game *g, Character *p, const TrainerState *state) {
    switch (state->page) {
    case TRAINER_GRID: trainer_draw_grid(g, p, state); break;
    case TRAINER_EQUIPMENT: trainer_draw_equipment(g, p, state); break;
    case TRAINER_EFFECTS: trainer_draw_effects(g, p, state); break;
    case TRAINER_STATS:
    default: trainer_draw_stats(g, p, state); break;
    }
    video_present(&g->video);
}

static int trainer_text_hotkey(Video *v, int click_x, int click_y,
                               int text_y, const char *line,
                               const char *token, int key) {
    const char *at = strstr(line, token);
    if (!at || click_y < text_y - 4 || click_y >= text_y + TRAINER_ROW_H)
        return -1;
    int adv = (v->font_advance ? v->font_advance : v->font_char_w) * 3 / 4;
    if (adv < 1) adv = 1;
    int x0 = TRAINER_BOX_X + 18 + (int)(at - line) * adv;
    int x1 = x0 + (int)strlen(token) * adv;
    return click_x >= x0 && click_x < x1 ? key : -1;
}

/* The DOS trainer was keyboard-resident; this native recreation also makes
 * its rows, spell grid, ownership cells, and printed footer commands usable
 * with the same mouse layer as the game.  Clicking an already-selected grid
 * or equipment cell performs its Space action. */
static int trainer_mouse_key(Game *g, TrainerState *state, int key) {
    if (key != INPUT_MOUSE_CLICK) return key;
    int x, y;
    if (!game_mouse_click_logical(g, &x, &y)) return -1;
    if (x < TRAINER_BOX_X || x >= TRAINER_BOX_X + TRAINER_BOX_W ||
        y < TRAINER_BOX_Y || y >= TRAINER_BOX_Y + TRAINER_BOX_H)
        return -1;
    if (y < TRAINER_BOX_Y + 80 && x > TRAINER_BOX_X + 470)
        return 0x1B;

    if (state->page == TRAINER_STATS || state->page == TRAINER_EFFECTS) {
        int row = (y - (TRAINER_CONTENT_Y - 3)) / TRAINER_ROW_H;
        if (y >= TRAINER_CONTENT_Y - 3 && row >= 0 &&
            row < TRAINER_VISIBLE_FIELDS) {
            if (state->page == TRAINER_STATS) {
                int count = (int)(sizeof(trainer_fields) / sizeof(trainer_fields[0]));
                int selected = state->scroll_top + row;
                if (selected < count) state->selection = selected;
            } else {
                int count = (int)(sizeof(effect_fields) / sizeof(effect_fields[0]));
                int selected = state->effect_top + row;
                if (selected < count) state->effect_selection = selected;
            }
            return -1;
        }
        if (state->page == TRAINER_STATS) {
            static const char footer[] =
                "S SPELLS  R SCROLLS  W WANDS  P PAPERS  E EQUIP  F EFFECTS  ESC EXIT";
            static const char *const token[] = {
                "S SPELLS", "R SCROLLS", "W WANDS", "P PAPERS",
                "E EQUIP", "F EFFECTS", "ESC EXIT"
            };
            static const int hotkey[] = {'S','R','W','P','E','F',0x1B};
            int footer_y = TRAINER_BOX_Y + TRAINER_BOX_H - 28;
            for (int i = 0; i < 7; i++) {
                int hit = trainer_text_hotkey(&g->video, x, y, footer_y,
                                              footer, token[i], hotkey[i]);
                if (hit >= 0) return hit;
            }
        } else {
            static const char footer[] = "ESC RETURNS TO CHARACTER / STATS";
            int hit = trainer_text_hotkey(&g->video, x, y,
                TRAINER_BOX_Y + TRAINER_BOX_H - 28,
                footer, "ESC RETURNS", 0x1B);
            if (hit >= 0) return hit;
        }
        return -1;
    }

    if (state->page == TRAINER_GRID) {
        enum {
            GRID_X = TRAINER_BOX_X + 194,
            GRID_CELL_W = 17,
            GRID_ROW_Y = TRAINER_CONTENT_Y + 58,
            GRID_ROW_H_LOCAL = 54
        };
        int row = (y - (GRID_ROW_Y - 3)) / GRID_ROW_H_LOCAL;
        if (y >= GRID_ROW_Y - 3 && row >= 0 && row < 4) {
            int col = (x - GRID_X) / GRID_CELL_W;
            if (x >= GRID_X && col >= 0 && col < 30) {
                if (state->grid_row == row && state->grid_col == col)
                    return ' ';
                state->grid_row = row;
                state->grid_col = col;
            } else if (x < GRID_X) {
                state->grid_row = row;
            }
            return -1;
        }
        static const char footer_wand[] =
            "ARROWS NAV  SPACE +1 CHARGE  A +1 ALL  N CLEAR  ESC/S BACK";
        static const char footer_item[] =
            "ARROWS NAV  SPACE TOGGLE  A ALL ON  N ALL OFF  ESC/S BACK";
        const char *footer = state->grid_set == GRID_WANDS ?
                             footer_wand : footer_item;
        int footer_y = TRAINER_BOX_Y + TRAINER_BOX_H - 53;
        int hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                      state->grid_set == GRID_WANDS ?
                                      "SPACE +1 CHARGE" : "SPACE TOGGLE", ' ');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  state->grid_set == GRID_WANDS ?
                                  "A +1 ALL" : "A ALL ON", 'A');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  state->grid_set == GRID_WANDS ?
                                  "N CLEAR" : "N ALL OFF", 'N');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  "ESC/S BACK", 'S');
        return hit;
    }

    if (state->page == TRAINER_EQUIPMENT) {
        int row = (y - (TRAINER_CONTENT_Y + 25)) / 31;
        if (y >= TRAINER_CONTENT_Y + 25 && row >= 0 && row < 16) {
            if (state->equip_selection == row) return ' ';
            state->equip_selection = row;
            return -1;
        }
        int hit = trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 53,
            "UP/DOWN SELECT  SPACE TOGGLES OWNERSHIP  LEFT/RIGHT ENCHANT +/-1",
            "SPACE TOGGLES OWNERSHIP", ' ');
        if (hit >= 0) return hit;
        return trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 28,
            "ESC RETURNS TO CHARACTER / STATS", "ESC RETURNS", 0x1B);
    }
    return -1;
}

void trainer_draw_grid_test(Game *g, Character *player, int set,
                            int row, int column) {
    if (!g || !player) return;
    TrainerState state;
    memset(&state, 0, sizeof(state));
    state.page = TRAINER_GRID;
    state.grid_set = set < GRID_SPELLS || set > GRID_PAPERS ?
                     GRID_SPELLS : set;
    state.grid_row = row < 0 ? 0 : (row > 3 ? 3 : row);
    state.grid_col = column < 0 ? 0 : (column > 29 ? 29 : column);
    trainer_draw_grid(g, player, &state);
    video_present(&g->video);
}

static void trainer_keep_visible(int selected, int count, int *top) {
    if (selected < *top) *top = selected;
    if (selected >= *top + TRAINER_VISIBLE_FIELDS)
        *top = selected - TRAINER_VISIBLE_FIELDS + 1;
    if (*top < 0) *top = 0;
    int maximum = count - TRAINER_VISIBLE_FIELDS;
    if (maximum < 0) maximum = 0;
    if (*top > maximum) *top = maximum;
}

static void trainer_start_input(TrainerState *state, int digit) {
    state->input_mode = 1;
    state->input_len = 1;
    state->input[0] = (char)digit;
    state->input[1] = '\0';
}

static void trainer_commit_input(Character *p, TrainerState *state) {
    state->input[state->input_len] = '\0';
    uint64_t value = state->input_len ? strtoull(state->input, NULL, 10) : 0;
    if (state->page == TRAINER_EFFECTS)
        trainer_field_set(p, &effect_fields[state->effect_selection], value);
    else
        trainer_field_set(p, &trainer_fields[state->selection], value);
    state->input_mode = 0;
    state->input_len = 0;
}

static int trainer_handle_input(Character *p, TrainerState *state, int key) {
    if (key == 0x1B) {
        state->input_mode = 0;
        state->input_len = 0;
    } else if (key == 0x0D) {
        trainer_commit_input(p, state);
    } else if (key == 0x08) {
        if (state->input_len) state->input_len--;
        state->input[state->input_len] = '\0';
    } else if (key >= '0' && key <= '9' && state->input_len < 10) {
        state->input[state->input_len++] = (char)key;
        state->input[state->input_len] = '\0';
    }
    return 1;
}

static void trainer_open_grid(TrainerState *state, int set) {
    state->page = TRAINER_GRID;
    state->grid_set = set;
    state->grid_row = 0;
    state->grid_col = 0;
}

static int trainer_stats_key(Character *p, TrainerState *state, int key) {
    const int count = (int)(sizeof(trainer_fields) / sizeof(trainer_fields[0]));
    if (key == 0x1B || key == INPUT_TRAINER) return 0;
    if (key == (0x10000 | 0x48)) {
        state->selection = state->selection ? state->selection - 1 : count - 1;
    } else if (key == (0x10000 | 0x50)) {
        state->selection = state->selection + 1 < count ? state->selection + 1 : 0;
    } else if (key == (0x10000 | 0x4D)) {
        trainer_field_adjust(p, &trainer_fields[state->selection], 1);
    } else if (key == (0x10000 | 0x4B)) {
        trainer_field_adjust(p, &trainer_fields[state->selection], -1);
    } else if (key == (0x10000 | 0x49)) {
        trainer_field_adjust(p, &trainer_fields[state->selection], 100);
    } else if (key == (0x10000 | 0x51)) {
        trainer_field_adjust(p, &trainer_fields[state->selection], -100);
    } else if (key == 's' || key == 'S') {
        trainer_open_grid(state, GRID_SPELLS);
    } else if (key == 'r' || key == 'R') {
        trainer_open_grid(state, GRID_SCROLLS);
    } else if (key == 'w' || key == 'W') {
        trainer_open_grid(state, GRID_WANDS);
    } else if (key == 'p' || key == 'P') {
        trainer_open_grid(state, GRID_PAPERS);
    } else if (key == 'e' || key == 'E') {
        state->page = TRAINER_EQUIPMENT;
        state->equip_selection = 0;
    } else if (key == 'f' || key == 'F') {
        state->page = TRAINER_EFFECTS;
        state->effect_selection = 0;
        state->effect_top = 0;
    } else if (key >= '0' && key <= '9') {
        trainer_start_input(state, key);
    }
    trainer_keep_visible(state->selection, count, &state->scroll_top);
    return 1;
}

static void trainer_grid_key(Character *p, TrainerState *state, int key) {
    u8 (*grid)[45] = trainer_grid(p, state->grid_set);
    if (key == 0x1B || key == 's' || key == 'S') {
        state->page = TRAINER_STATS;
    } else if (key == (0x10000 | 0x48)) {
        state->grid_row = state->grid_row ? state->grid_row - 1 : 3;
    } else if (key == (0x10000 | 0x50)) {
        state->grid_row = state->grid_row < 3 ? state->grid_row + 1 : 0;
    } else if (key == (0x10000 | 0x4B)) {
        state->grid_col = state->grid_col ? state->grid_col - 1 : 29;
    } else if (key == (0x10000 | 0x4D)) {
        state->grid_col = state->grid_col < 29 ? state->grid_col + 1 : 0;
    } else if (key == ' ') {
        u8 *value = &grid[state->grid_row][state->grid_col];
        if (state->grid_set == GRID_WANDS) *value = (u8)((*value + 1) % 10);
        else *value = !*value;
    } else if (key == 'a' || key == 'A') {
        for (int col = 0; col < 30; col++) {
            if (state->grid_set == GRID_WANDS)
                grid[state->grid_row][col] = (u8)((grid[state->grid_row][col] + 1) % 10);
            else
                grid[state->grid_row][col] = 1;
        }
    } else if (key == 'n' || key == 'N') {
        memset(grid[state->grid_row], 0, 30);
    }
}

static void trainer_equipment_key(Character *p, TrainerState *state, int key) {
    int armor = state->equip_selection >= 8;
    int slot = state->equip_selection & 7;
    u8 *owned = armor ? &p->armor_inventory[slot] : &p->weapon_inventory[slot];
    int enchant = armor ? mw_armor_enchant(p, slot) :
                          mw_weapon_enchant(p, slot);
    if (key == 0x1B) {
        state->page = TRAINER_STATS;
    } else if (key == (0x10000 | 0x48)) {
        state->equip_selection = state->equip_selection ? state->equip_selection - 1 : 15;
    } else if (key == (0x10000 | 0x50)) {
        state->equip_selection = state->equip_selection < 15 ? state->equip_selection + 1 : 0;
    } else if (key == ' ') {
        *owned = !*owned;
    } else if (key == (0x10000 | 0x4D)) {
        if (enchant < INT16_MAX) enchant++;
        if (armor) mw_set_armor_enchant(p, slot, enchant);
        else mw_set_weapon_enchant(p, slot, enchant);
    } else if (key == (0x10000 | 0x4B)) {
        if (enchant > INT16_MIN) enchant--;
        if (armor) mw_set_armor_enchant(p, slot, enchant);
        else mw_set_weapon_enchant(p, slot, enchant);
    }
}

static void trainer_effect_key(Character *p, TrainerState *state, int key) {
    const int count = (int)(sizeof(effect_fields) / sizeof(effect_fields[0]));
    if (key == 0x1B) {
        state->page = TRAINER_STATS;
    } else if (key == (0x10000 | 0x48)) {
        state->effect_selection = state->effect_selection ?
            state->effect_selection - 1 : count - 1;
    } else if (key == (0x10000 | 0x50)) {
        state->effect_selection = state->effect_selection + 1 < count ?
            state->effect_selection + 1 : 0;
    } else if (key == (0x10000 | 0x4D)) {
        trainer_field_adjust(p, &effect_fields[state->effect_selection], 1);
    } else if (key == (0x10000 | 0x4B)) {
        trainer_field_adjust(p, &effect_fields[state->effect_selection], -1);
    } else if (key >= '0' && key <= '9') {
        trainer_start_input(state, key);
    }
    trainer_keep_visible(state->effect_selection, count, &state->effect_top);
}

static void trainer_sync_runtime(Game *g, Character *p) {
    int target_floor = p->floor_depth;
    int target_x = p->x_pos;
    int target_y = p->y_pos;
    target_floor = game_clamp_dungeon_floor(g, target_floor);
    if (target_x >= MAP_W) target_x = MAP_W - 1;
    if (target_y >= MAP_H) target_y = MAP_H - 1;

    if (target_floor != g->cur_floor)
        game_change_floor(g, p, target_floor);
    g->cur_floor = target_floor;
    g->cur_x = target_x;
    g->cur_y = target_y;
    p->floor_depth = (u16)target_floor;
    p->x_pos = (u16)target_x;
    p->y_pos = (u16)target_y;
    g->last_move_dir = p->facing_dir <= 3 ? p->facing_dir : 0;
    /* Raw coordinate editing was intentionally unrestricted in the DOS TSR,
       but the native game must not resume inside rock or on an actor and
       recreate the old immobile-save failure.  A cell with no traversable
       edge is solid in the generated dungeon representation. */
    int has_exit = game_can_move(g, target_x, target_y, target_x, target_y - 1) ||
                   game_can_move(g, target_x, target_y, target_x, target_y + 1) ||
                   game_can_move(g, target_x, target_y, target_x - 1, target_y) ||
                   game_can_move(g, target_x, target_y, target_x + 1, target_y);
    if (!has_exit || game_find_monster(g, target_x, target_y) >= 0)
        game_relocate(g, p);
    game_update_visibility(g);
    g->monster_adjacent = game_find_adjacent_monster(g) >= 0;
}

void trainer_run(Game *g, Character *player) {
    if (!g || !player) return;
    mw_character_native_ensure(player);
    TrainerState state;
    memset(&state, 0, sizeof(state));

    int running = 1;
    while (running && !input_poll_quit(&g->input)) {
        trainer_draw(g, player, &state);
        int key = trainer_extended_key(&g->input);
        key = trainer_mouse_key(g, &state, key);
        if (key == INPUT_MODEL_VIEWER) {
            model_viewer_run(g);
            continue;
        }
        if (state.input_mode) {
            trainer_handle_input(player, &state, key);
            continue;
        }
        switch (state.page) {
        case TRAINER_GRID:
            trainer_grid_key(player, &state, key);
            break;
        case TRAINER_EQUIPMENT:
            trainer_equipment_key(player, &state, key);
            break;
        case TRAINER_EFFECTS:
            trainer_effect_key(player, &state, key);
            break;
        case TRAINER_STATS:
        default:
            running = trainer_stats_key(player, &state, key);
            break;
        }
    }
    trainer_sync_runtime(g, player);
}

int trainer_self_test(void) {
    int failures = 0;
    Character p;
    memset(&p, 0, sizeof(p));

    if (sizeof(trainer_fields) / sizeof(trainer_fields[0]) != 45) failures++;
    if (sizeof(effect_fields) / sizeof(effect_fields[0]) != 23) failures++;
    if (input_sdl_to_dos(SDLK_F12, KMOD_CTRL) != INPUT_TRAINER) failures++;
    if (input_sdl_to_dos(SDLK_F12, KMOD_NONE) != 0) failures++;
    if (input_sdl_to_dos(SDLK_F11, KMOD_CTRL) != INPUT_WILDERNESS_TEST)
        failures++;
    if (input_sdl_to_dos(SDLK_F11, KMOD_NONE) != 0) failures++;

    trainer_field_set(&p, &trainer_fields[12], 1234);
    if (p.stat_str != 1234) failures++;
    trainer_field_set(&p, &trainer_fields[12], 99999);
    if (p.stat_str != 32767) failures++;
    trainer_field_set(&p, &trainer_fields[0], 99);
    if (p.race != RACE_COUNT - 1) failures++;
    trainer_field_set(&p, &trainer_fields[26], 0);
    if (p.raise_x != 0xFFFF) failures++;
    trainer_field_adjust(&p, &trainer_fields[26], 1);
    if (p.raise_x == 0xFFFF) failures++;
    trainer_field_set(&p, &effect_fields[16], 4567);
    if (p.eff_resist_poison != 4567) failures++;

    if (strcmp(combat_spell_name(SPELL_CAT_PERMANENT, 0),
               "ENCHANT WEAPON LEVEL 1") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PREPARATION, 11),
               "DESCEND") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_WIZARD, 29),
               "POWER WEAPON III") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 27),
               "ULTRA PROTECTION") != 0) failures++;

    TrainerState grid_state;
    memset(&grid_state, 0, sizeof(grid_state));
    trainer_open_grid(&grid_state, GRID_SCROLLS);
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.scrolls[0][0] != 1) failures++;
    trainer_grid_key(&p, &grid_state, (0x10000 | 0x4B));
    if (grid_state.grid_col != 29) failures++;
    trainer_grid_key(&p, &grid_state, (0x10000 | 0x48));
    if (grid_state.grid_row != 3) failures++;
    p.scrolls[3][30] = 77;
    trainer_grid_key(&p, &grid_state, 'A');
    if (p.scrolls[3][0] != 1 || p.scrolls[3][29] != 1 ||
        p.scrolls[3][30] != 77) failures++;
    trainer_grid_key(&p, &grid_state, 'N');
    if (p.scrolls[3][0] != 0 || p.scrolls[3][29] != 0 ||
        p.scrolls[3][30] != 77) failures++;

    trainer_open_grid(&grid_state, GRID_WANDS);
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.wands[0][0] != 1) failures++;
    for (int i = 0; i < 9; i++) trainer_grid_key(&p, &grid_state, ' ');
    if (p.wands[0][0] != 0) failures++;

    printf("Trainer field/input coverage: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
