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
    TF_U16,
    TF_U32,
    TF_HP_CURRENT,
    TF_HP_MAXIMUM,
    TF_FLOAT,
    TF_AGE_YEARS,
    TF_X_POSITION,
    TF_Y_POSITION,
    TF_FLOOR,
    TF_RACE,
    TF_CLASS,
    TF_SEX,
    TF_RAISE
} TrainerFieldType;

typedef struct TrainerField {
    const char *name;
    size_t offset;
    TrainerFieldType type;
    uint64_t classic_maximum;
    uint64_t maximum;
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

#define FIELD_MODE(member, kind, label, classic_cap, enhanced_cap) \
    { label, offsetof(Character, member), kind, classic_cap, enhanced_cap }
#define FIELD(member, kind, label, cap) \
    FIELD_MODE(member, kind, label, cap, cap)
#define FIELD_U8(member, label)  FIELD(member, TF_U8, label, UINT8_MAX)
#define FIELD_U16(member, label) FIELD(member, TF_U16, label, UINT16_MAX)
#define FIELD_U32(member, label) FIELD(member, TF_U32, label, UINT32_MAX)

/* Exact 45-field order from TRAINER.ASM, followed by Enhanced-only native
 * rows that are omitted entirely for Classic characters. */
static const TrainerField trainer_fields[] = {
    FIELD_MODE(race, TF_RACE, "Race",
               MW_CLASSIC_RACE_COUNT - 1, RACE_COUNT - 1),
    FIELD_MODE(class_id, TF_CLASS, "Class",
               MW_CLASSIC_CLASS_COUNT - 1, CLASS_COUNT - 1),
    FIELD(sex, TF_SEX, "Sex", 1),
    FIELD(level, TF_U16, "Level", MW_PLAYER_LEVEL_MAX),
    FIELD_MODE(hp_cur, TF_HP_CURRENT, "HP Current",
               INT16_MAX, MW_PLAYER_HP_MAX),
    FIELD_MODE(hp_max, TF_HP_MAXIMUM, "HP Maximum",
               INT16_MAX, MW_PLAYER_HP_MAX),
    FIELD(sp_cur, TF_FLOAT, "SP Current", MW_PLAYER_SP_MAX_U32),
    FIELD(sp_max, TF_FLOAT, "SP Maximum", MW_PLAYER_SP_MAX_U32),
    FIELD(age, TF_AGE_YEARS, "Age (Years)",
          UINT32_MAX / MW_AGE_YEAR_UNITS),
    FIELD(x_pos, TF_X_POSITION, "X Position", MAP_W - 1),
    FIELD(y_pos, TF_Y_POSITION, "Y Position", MAP_H - 1),
    FIELD_MODE(floor_depth, TF_FLOOR, "Floor Depth",
               CLASSIC_DUNGEON_FLOOR, MAX_DUNGEON_FLOOR),
    FIELD(stat_str, TF_U16, "Strength", INT16_MAX),
    FIELD(stat_int, TF_U16, "Intelligence", INT16_MAX),
    FIELD(stat_wis, TF_U16, "Wisdom", INT16_MAX),
    FIELD(stat_con, TF_U16, "Constitution", INT16_MAX),
    FIELD(stat_agi, TF_U16, "Agility", INT16_MAX),
    FIELD(stat_luck, TF_U16, "Luck", INT16_MAX),
    FIELD_U32(jewels_pocket, "Jewels Pocket"),
    FIELD_U32(jewels_bank, "Jewels Bank"),
    FIELD_U32(copper_stones, "Copper Stones"),
    FIELD_U32(silver_stones, "Silver Stones"),
    FIELD_U32(ivory_stones, "Ivory Stones"),
    FIELD_U32(gold_stones, "Gold Stones"),
    FIELD_U32(platinum_stones, "Platinum Stones"),
    FIELD_U32(jewel_stones, "Jewel Stones"),
    FIELD(raise_x, TF_RAISE, "Raise Contract", 1),
    FIELD_U8(holy_grenade, "Holy H Grenade"),
    FIELD_U8(stone_teleport, "Stone of Telep"),
    FIELD_U8(stone_see, "Stone of Seeing"),
    FIELD(floor_slosher, TF_U8, "Floor Slosher", 1),
    FIELD_U8(potion_heal, "Potion Healing"),
    FIELD_MODE(green_pill, TF_U8, "Green Pill", INT8_MAX, UINT8_MAX),
    FIELD_MODE(orange_pill, TF_U8, "Orange Pill", INT8_MAX, UINT8_MAX),
    FIELD_MODE(blue_pill, TF_U8, "Blue Pill", INT8_MAX, UINT8_MAX),
    FIELD_MODE(red_pill, TF_U8, "Red Pill", INT8_MAX, UINT8_MAX),
    FIELD_MODE(white_pill, TF_U8, "White Pill", INT8_MAX, UINT8_MAX),
    FIELD_MODE(yellow_pill, TF_U8, "Yellow Pill", INT8_MAX, UINT8_MAX),
    FIELD_U8(ring_regen, "Ring of Regen"),
    FIELD_MODE(native.ring_prot_plus, TF_U16, "Ring of Prot+",
               UINT8_MAX, INT16_MAX),
    FIELD(antimagic_ring, TF_U8, "AntiMagic Ring", 5),
    FIELD_MODE(native.body_armor_plus, TF_U16, "Body Armor Lv",
               UINT8_MAX, INT16_MAX),
    FIELD_MODE(native.gauntlet, TF_U16, "Gauntlet",
               INT8_MAX, INT16_MAX),
    FIELD_MODE(diseased_turns, TF_U16, "Diseased Turns",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(poisoned_turns, TF_U16, "Poisoned Turns",
               INT16_MAX, UINT16_MAX),
    /* Native V4 Enhanced-only relic ownership.  Classic stops before these
       rows, so its trainer remains the original 45-field layout. */
    FIELD(native.relic_arcane_ring, TF_U8, "Arcane Renew Ring", 1),
    FIELD(native.relic_bloodstone_signet, TF_U8, "Bloodstone Signet", 1),
    FIELD(native.relic_deepward_amulet, TF_U8, "Deepward Amulet", 1),
    FIELD(native.relic_sage_prism, TF_U8, "Sage's Prism", 1),
    FIELD(native.relic_phoenix_seal, TF_U8, "Phoenix Seal", 1)
};

static const TrainerField effect_fields[] = {
    FIELD_MODE(native.enchant_wpn_spell, TF_U16, "Weapon Plus",
               UINT8_MAX, INT16_MAX),
    FIELD_MODE(native.armor_plus, TF_U16, "Armor Plus",
               UINT8_MAX, INT16_MAX),
    FIELD(eff_feather, TF_U8, "Feather", 100),
    FIELD(eff_fast_move, TF_U8, "Fast Move", 60),
    FIELD(eff_invisible, TF_U8, "Invisibility", 100),
    FIELD(eff_str_bonus, TF_U8, "Str Bonus", 60),
    FIELD(eff_agi_bonus, TF_U8, "Agi Bonus", 60),
    FIELD(eff_super_str, TF_U8, "Super Strength", 60),
    FIELD(eff_super_agi, TF_U8, "Super Agility", 60),
    FIELD_MODE(eff_battle_str, TF_U16, "Battle Str",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_battle_spd, TF_U16, "Battle Speed",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_slow_mon, TF_U16, "Slow Monster",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_pwr_weapon, TF_U8, "Power Weapon", 3, 6),
    FIELD_MODE(eff_pwr_wpn_turns, TF_U16, "Pwr Wpn Turns",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_protect_lv, TF_U8, "Protect Level", 5, 10),
    FIELD_MODE(eff_protect_turns, TF_U16, "Protect Turns",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_resist_poison, TF_U16, "Resist Poison",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_resist_disease, TF_U16, "Resist Disease",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_anti_cold, TF_U16, "Anti Cold",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_anti_fire, TF_U16, "Anti Fire",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_resist_drain, TF_U16, "Resist Drain",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_stop_monster, TF_U16, "Stop Monster",
               INT16_MAX, UINT16_MAX),
    FIELD_MODE(eff_hold_monster, TF_U16, "Hold Monster",
               INT16_MAX, UINT16_MAX),
    FIELD(native.relic_phoenix_cooldown, TF_U16,
          "Phoenix Cooldown", UINT16_MAX)
};

enum {
    TRAINER_CLASSIC_FIELD_COUNT = 45,
    TRAINER_CLASSIC_EFFECT_COUNT = 23
};

static int trainer_field_count(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
           (int)(sizeof(trainer_fields) / sizeof(trainer_fields[0])) :
           TRAINER_CLASSIC_FIELD_COUNT;
}

static int trainer_effect_count(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
           (int)(sizeof(effect_fields) / sizeof(effect_fields[0])) :
           TRAINER_CLASSIC_EFFECT_COUNT;
}

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

static void trainer_restrict_state(const Character *p, TrainerState *state);

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
    char page_line[64];
    int race = p->race < RACE_COUNT ? p->race : 0;
    int cls = p->class_id < CLASS_COUNT ? p->class_id : 0;
    snprintf(line, sizeof(line), "%-14.14s %-8s %c %-11s",
             p->name, race_names[race], p->sex ? 'F' : 'M', class_names[cls]);
    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_BOX_Y + 44, line, 15);
    snprintf(page_line, sizeof(page_line), "%s %s", page,
             mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ?
             "ENHANCED" : "CLASSIC");
    trainer_text(v, TRAINER_BOX_X + 480, TRAINER_BOX_Y + 44, page_line, 14);
    video_hline(v, TRAINER_BOX_X, TRAINER_BOX_Y + 80, TRAINER_BOX_W, 11);
}

static uint64_t trainer_field_max(const Character *p,
                                  const TrainerField *field) {
    return mw_experience_mode(p) == MW_EXPERIENCE_CLASSIC ?
           field->classic_maximum : field->maximum;
}

static uint64_t trainer_field_get(const Character *p, const TrainerField *f) {
    const u8 *base = (const u8 *)p + f->offset;
    u16 v16;
    u32 v32;
    float vf;
    switch (f->type) {
    case TF_U8:
    case TF_RACE:
    case TF_CLASS:
    case TF_SEX:
        return *base;
    case TF_U16:
    case TF_X_POSITION:
    case TF_Y_POSITION:
    case TF_FLOOR:
        memcpy(&v16, base, sizeof(v16));
        return v16;
    case TF_U32:
        memcpy(&v32, base, sizeof(v32));
        return v32;
    case TF_HP_CURRENT:
        return mw_hp_cur(p);
    case TF_HP_MAXIMUM:
        return mw_hp_max(p);
    case TF_FLOAT:
        memcpy(&vf, base, sizeof(vf));
        return vf > 0.0f ? (uint64_t)vf : 0;
    case TF_RAISE:
        memcpy(&v16, base, sizeof(v16));
        return v16 == 0xFFFF ? 0 : 1;
    case TF_AGE_YEARS:
        memcpy(&v32, base, sizeof(v32));
        return v32 / MW_AGE_YEAR_UNITS;
    }
    return 0;
}

static void trainer_field_set(Character *p, const TrainerField *f, uint64_t value) {
    u8 *base = (u8 *)p + f->offset;
    uint64_t maximum = trainer_field_max(p, f);
    if (value > maximum) value = maximum;
    u8 v8 = (u8)value;
    u16 v16 = (u16)value;
    u32 v32 = (u32)value;
    float vf = (float)v32;
    switch (f->type) {
    case TF_U8:
    case TF_RACE:
    case TF_CLASS:
    case TF_SEX:
        *base = v8;
        break;
    case TF_U16:
    case TF_X_POSITION:
    case TF_Y_POSITION:
    case TF_FLOOR:
        memcpy(base, &v16, sizeof(v16));
        break;
    case TF_U32:
        memcpy(base, &v32, sizeof(v32));
        break;
    case TF_HP_CURRENT:
        mw_set_hp_cur(p, value);
        break;
    case TF_HP_MAXIMUM:
        mw_set_hp_max(p, value);
        break;
    case TF_FLOAT:
        memcpy(base, &vf, sizeof(vf));
        break;
    case TF_RAISE:
        v16 = value ? 1 : 0xFFFF;
        memcpy(base, &v16, sizeof(v16));
        break;
    case TF_AGE_YEARS:
        v32 = (u32)(value * MW_AGE_YEAR_UNITS);
        memcpy(base, &v32, sizeof(v32));
        break;
    }
}

static void trainer_field_adjust(Character *p, const TrainerField *f, int delta) {
    if (f->type == TF_AGE_YEARS) {
        uint64_t amount = (uint64_t)(delta < 0 ? -delta : delta) *
                          MW_AGE_YEAR_UNITS;
        uint64_t raw = p->age;
        if (delta < 0)
            raw = raw > amount ? raw - amount : 0;
        else
            raw = raw > UINT32_MAX - amount ? UINT32_MAX : raw + amount;
        p->age = (u32)raw;
        return;
    }
    uint64_t value = trainer_field_get(p, f);
    uint64_t maximum = trainer_field_max(p, f);
    if (value > maximum) value = maximum;
    if (f->type == TF_RAISE || f->type == TF_SEX) {
        value = !value;
    } else if (f->type == TF_RACE || f->type == TF_CLASS) {
        uint64_t modulus = maximum + 1;
        int64_t moved = (int64_t)value + delta;
        while (moved < 0) moved += (int64_t)modulus;
        value = (uint64_t)moved % modulus;
    } else if (delta < 0) {
        uint64_t amount = (uint64_t)(-delta);
        value = value > amount ? value - amount : 0;
    } else if ((uint64_t)delta > maximum - value) {
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
    } else if (f->type == TF_AGE_YEARS) {
        unsigned days = (p->age % MW_AGE_YEAR_UNITS) / MW_AGE_DAY_UNITS;
        snprintf(out, out_size, "%llu YRS + %u DAYS",
                 (unsigned long long)value, days);
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
                            trainer_field_count(p),
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

static int trainer_grid_is_counted(int set) {
    return set == GRID_SCROLLS || set == GRID_WANDS || set == GRID_PAPERS;
}

static const char *trainer_grid_unit_name(int set) {
    switch (set) {
    case GRID_SCROLLS: return "SCROLLS";
    case GRID_WANDS: return "CHARGES";
    case GRID_PAPERS: return "PAPERS";
    default: return "LEARNED";
    }
}

static void trainer_draw_grid_cell(Video *v, int x, int y, int width,
                                   u8 value, int counted, int selected) {
    char glyph[2] = {0, 0};
    if (counted)
        glyph[0] = value <= 9 ? (char)('0' + value) : '+';
    else
        glyph[0] = value ? 'Y' : 'N';

    if (selected)
        video_fill_rect(v, x - 2, y - 3, width + 4, 34, 1);
    video_fill_rect(v, x, y, width, 29, 0);
    trainer_outline_rect(v, x, y, width, 29,
                         selected ? 15 : (value ? 10 : 8));
    trainer_text(v, x + (width > 11 ? 3 : 1), y + 2, glyph,
                 selected ? 15 : (value ? 10 : 8));
}

static void trainer_draw_grid(Game *g, Character *p, const TrainerState *state) {
    Video *v = &g->video;
    enum {
        GRID_X = TRAINER_BOX_X + 194,
        GRID_ROW_Y = TRAINER_CONTENT_Y + 58,
        GRID_ROW_H = 54
    };
    trainer_box(v);
    trainer_identity(v, p, trainer_grid_names[state->grid_set]);
    u8 (*grid)[45] = trainer_grid(p, state->grid_set);
    int spell_count = mw_spell_catalog_count(p);
    int level_count = (spell_count + 2) / 3;
    int grid_cell_w = spell_count > 40 ? 12 :
                      spell_count > 35 ? 13 :
                      spell_count > MW_ORIGINAL_SPELL_COUNT ? 15 : 17;

    trainer_text(v, TRAINER_BOX_X + 18, TRAINER_CONTENT_Y + 12,
                 "TYPE", 8);
    trainer_text(v, TRAINER_BOX_X + 126, TRAINER_CONTENT_Y + 12,
                 "LEVEL", 8);
    for (int level = 0; level < level_count; level++) {
        char level_text[4];
        snprintf(level_text, sizeof(level_text), "%d", level + 1);
        int level_x = GRID_X + level * grid_cell_w * 3 +
                      (level >= 9 ? 15 : 19);
        trainer_text(v, level_x, TRAINER_CONTENT_Y + 12, level_text, 14);
    }

    for (int row = 0; row < 4; row++) {
        int y = GRID_ROW_Y + row * GRID_ROW_H;
        trainer_text(v, TRAINER_BOX_X + 18, y + 3, trainer_type_names[row],
                     row == state->grid_row ? 15 : 14);
        for (int col = 0; col < spell_count; col++) {
            u8 value = grid[row][col];
            int x = GRID_X + col * grid_cell_w;
            int selected = row == state->grid_row && col == state->grid_col;
            trainer_draw_grid_cell(v, x, y, grid_cell_w - 1, value,
                                   trainer_grid_is_counted(state->grid_set),
                                   selected);
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
    if (trainer_grid_is_counted(state->grid_set)) {
        if (state->input_mode) {
            snprintf(info, sizeof(info), "%s %s  SET COUNT: %.*s_",
                     trainer_type_long_names[selected_row],
                     trainer_grid_unit_name(state->grid_set),
                     state->input_len, state->input);
        } else {
            snprintf(info, sizeof(info), "%s %s: %u / %u",
                     trainer_type_long_names[selected_row],
                     trainer_grid_unit_name(state->grid_set),
                     selected_value, UINT8_MAX);
        }
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
        trainer_grid_is_counted(state->grid_set) ?
          "ARROWS NAV SPACE/+ ADD - SUB M MAX A MAX ALL N CLEAR ESC/S BACK" :
          "ARROWS NAV  SPACE TOGGLE  A ALL ON  N ALL OFF  ESC/S BACK",
        hint);
}

typedef struct TrainerEquipmentEntry {
    int armor;
    int slot;
} TrainerEquipmentEntry;

static int trainer_equipment_count(const Character *p) {
    return mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ? 32 : 16;
}

/* Experience mode is immutable during normal play, but every trainer path
   still clamps its cursor before dereferencing it.  This makes Enhanced-only
   slots inaccessible even if a caller changes modes while a trainer page is
   open or supplies a stale/test cursor. */
static void trainer_restrict_state(const Character *p, TrainerState *state) {
    if (!p || !state) return;
    if (state->grid_set < GRID_SPELLS || state->grid_set > GRID_PAPERS)
        state->grid_set = GRID_SPELLS;
    if (state->grid_row < 0) state->grid_row = 0;
    if (state->grid_row > 3) state->grid_row = 3;
    int field_maximum = trainer_field_count(p) - 1;
    if (state->selection < 0) state->selection = 0;
    if (state->selection > field_maximum)
        state->selection = field_maximum;
    int effect_maximum = trainer_effect_count(p) - 1;
    if (state->effect_selection < 0) state->effect_selection = 0;
    if (state->effect_selection > effect_maximum)
        state->effect_selection = effect_maximum;
    int spell_maximum = mw_spell_catalog_count(p) - 1;
    if (state->grid_col < 0) state->grid_col = 0;
    if (state->grid_col > spell_maximum)
        state->grid_col = spell_maximum;
    int equipment_maximum = trainer_equipment_count(p) - 1;
    if (state->equip_selection < 0) state->equip_selection = 0;
    if (state->equip_selection > equipment_maximum)
        state->equip_selection = equipment_maximum;
}

static TrainerEquipmentEntry trainer_equipment_entry(const Character *p,
                                                       int index) {
    TrainerEquipmentEntry entry = {0, 0};
    int enhanced = mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED;
    if (index < 8) {
        entry.slot = index;
    } else if (enhanced && index < 16) {
        entry.slot = 12 + index - 8;
    } else {
        entry.armor = 1;
        if (enhanced && index >= 24)
            entry.slot = 8 + index - 24;
        else
            entry.slot = index - (enhanced ? 16 : 8);
    }
    return entry;
}

static int trainer_equipment_inventory(const Character *p,
                                       TrainerEquipmentEntry entry) {
    if (entry.slot == 0) return 1; /* fist and skin are implicit */
    return entry.armor ? mw_armor_inventory_count(p, entry.slot) :
                         mw_weapon_inventory_count(p, entry.slot);
}

static void trainer_set_equipment_inventory(Character *p,
                                            TrainerEquipmentEntry entry,
                                            int count) {
    if (entry.slot == 0) return;
    if (entry.armor) {
        mw_set_armor_inventory_count(p, entry.slot, count);
        if (count == 0 && p->equipped_armor == entry.slot)
            p->equipped_armor = 0;
    } else {
        mw_set_weapon_inventory_count(p, entry.slot, count);
        if (count == 0 && p->equipped_weapon == entry.slot)
            p->equipped_weapon = 0;
    }
}

static const char *trainer_equipment_name(TrainerEquipmentEntry entry) {
    return entry.armor ? combat_armor_name(entry.slot) :
                         weapon_stats[entry.slot].name;
}

static int trainer_equipment_enchant(const Character *p,
                                     TrainerEquipmentEntry entry) {
    return entry.armor ? mw_armor_enchant(p, entry.slot) :
                         mw_weapon_enchant(p, entry.slot);
}

static void trainer_set_equipment_enchant(Character *p,
                                          TrainerEquipmentEntry entry,
                                          int enchant) {
    if (entry.armor) mw_set_armor_enchant(p, entry.slot, enchant);
    else mw_set_weapon_enchant(p, entry.slot, enchant);
}

static void trainer_draw_equipment(Game *g, Character *p,
                                   const TrainerState *state) {
    Video *v = &g->video;
    int count = trainer_equipment_count(p);
    int first_armor = mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ? 16 : 8;
    int first = state->equip_selection < first_armor ? 0 : first_armor;
    int end = first + 16;
    if (end > count) end = count;
    trainer_box(v);
    trainer_identity(v, p, "EQUIPMENT");
    trainer_text(v, TRAINER_BOX_X + 52, TRAINER_CONTENT_Y - 3,
                 "ITEM", 14);
    trainer_text(v, TRAINER_BOX_X + 450, TRAINER_CONTENT_Y - 3,
                 "COUNT", 14);
    trainer_text(v, TRAINER_BOX_X + 555, TRAINER_CONTENT_Y - 3,
                 "ENCHANT", 14);
    for (int index = first; index < end; index++) {
        TrainerEquipmentEntry entry = trainer_equipment_entry(p, index);
        int y = TRAINER_CONTENT_Y + 22 + (index - first) * 30;
        int active = index == state->equip_selection;
        int owned = trainer_equipment_inventory(p, entry);
        int enchant = trainer_equipment_enchant(p, entry);
        if (active)
            video_fill_rect(v, TRAINER_BOX_X + 2, y - 3,
                            TRAINER_BOX_W - 4, 26, 1);
        if (index == 0 || index == first_armor)
            trainer_text(v, TRAINER_BOX_X + 640, y,
                         entry.armor ? "ARMOR" : "WEAPON", 8);
        trainer_text(v, TRAINER_BOX_X + 16, y, active ? ">" : " ", 14);
        char line[64];
        snprintf(line, sizeof(line), "%c%02d %s",
                 entry.armor ? 'A' : 'W', entry.slot + 1,
                 trainer_equipment_name(entry));
        trainer_text(v, TRAINER_BOX_X + 52, y, line, active ? 15 : 7);
        if (entry.slot == 0) {
            trainer_text(v, TRAINER_BOX_X + 458, y, "BASE",
                         active ? 15 : 10);
        } else {
            if (active && state->input_mode)
                snprintf(line, sizeof(line), "%.*s_",
                         state->input_len, state->input);
            else
                snprintf(line, sizeof(line), "%d", owned);
            trainer_text(v, TRAINER_BOX_X + 458, y, line,
                         active ? 15 : (owned ? 10 : 8));
        }
        snprintf(line, sizeof(line), "%d", enchant);
        trainer_text(v, TRAINER_BOX_X + 585, y, line, active ? 15 : 10);
    }
    trainer_draw_footer(v,
        first ? "ARMOR PAGE  UP/DOWN SELECT SPACE/+ ADD - SUB M MAX # SET" :
                "WEAPON PAGE UP/DOWN SELECT SPACE/+ ADD - SUB M MAX # SET",
        "LEFT/RIGHT ENCHANT +/-1 PGUP/PGDN +/-100 ESC RETURNS");
}

static void trainer_draw_effects(Game *g, Character *p,
                                 const TrainerState *state) {
    trainer_box(&g->video);
    trainer_identity(&g->video, p, "SPELL EFFECTS");
    trainer_draw_field_rows(&g->video, p, effect_fields,
                            trainer_effect_count(p),
                            state->effect_selection, state->effect_top, state);
    trainer_draw_footer(&g->video,
        "UP/DOWN SELECT  LEFT/RIGHT +/-1  TYPE NUMBER + ENTER TO SET",
        "ESC RETURNS TO CHARACTER / STATS");
}

static void trainer_draw(Game *g, Character *p, TrainerState *state) {
    trainer_restrict_state(p, state);
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
static int trainer_mouse_key(Game *g, Character *p,
                             TrainerState *state, int key) {
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
                int count = trainer_field_count(p);
                int selected = state->scroll_top + row;
                if (selected < count) state->selection = selected;
            } else {
                int count = trainer_effect_count(p);
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
            GRID_ROW_Y = TRAINER_CONTENT_Y + 58,
            GRID_ROW_H_LOCAL = 54
        };
        int spell_count = mw_spell_catalog_count(p);
        int grid_cell_w = spell_count > 40 ? 12 :
                          spell_count > 35 ? 13 :
                          spell_count > MW_ORIGINAL_SPELL_COUNT ? 15 : 17;
        int row = (y - (GRID_ROW_Y - 3)) / GRID_ROW_H_LOCAL;
        if (y >= GRID_ROW_Y - 3 && row >= 0 && row < 4) {
            int col = (x - GRID_X) / grid_cell_w;
            if (x >= GRID_X && col >= 0 && col < spell_count) {
                if (state->grid_row == row && state->grid_col == col)
                    return ' ';
                state->grid_row = row;
                state->grid_col = col;
            } else if (x < GRID_X) {
                state->grid_row = row;
            }
            return -1;
        }
        static const char footer_count[] =
            "ARROWS NAV SPACE/+ ADD - SUB M MAX A MAX ALL N CLEAR ESC/S BACK";
        static const char footer_item[] =
            "ARROWS NAV  SPACE TOGGLE  A ALL ON  N ALL OFF  ESC/S BACK";
        int counted = trainer_grid_is_counted(state->grid_set);
        const char *footer = counted ? footer_count : footer_item;
        int footer_y = TRAINER_BOX_Y + TRAINER_BOX_H - 53;
        int hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                      counted ? "SPACE/+ ADD" :
                                                "SPACE TOGGLE", ' ');
        if (hit >= 0) return hit;
        if (counted) {
            hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                      "- SUB", '-');
            if (hit >= 0) return hit;
        }
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  counted ? "M MAX" : "A ALL ON",
                                  counted ? 'M' : 'A');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  counted ? "A MAX ALL" : "N ALL OFF",
                                  counted ? 'A' : 'N');
        if (hit >= 0) return hit;
        if (counted) {
            hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                      "N CLEAR", 'N');
            if (hit >= 0) return hit;
        }
        hit = trainer_text_hotkey(&g->video, x, y, footer_y, footer,
                                  "ESC/S BACK", 'S');
        return hit;
    }

    if (state->page == TRAINER_EQUIPMENT) {
        int row = (y - (TRAINER_CONTENT_Y + 19)) / 30;
        int count = trainer_equipment_count(p);
        int first_armor =
            mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED ? 16 : 8;
        int first = state->equip_selection < first_armor ? 0 : first_armor;
        int index = first + row;
        if (y >= TRAINER_CONTENT_Y + 19 && row >= 0 && row < 16 &&
            index < count) {
            if (state->equip_selection == index) return ' ';
            state->equip_selection = index;
            return -1;
        }
        int hit = trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 53,
            "UP/DOWN SELECT SPACE/+ ADD - SUB M MAX # SET COUNT",
            "SPACE/+ ADD", ' ');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 53,
            "UP/DOWN SELECT SPACE/+ ADD - SUB M MAX # SET COUNT",
            "- SUB", '-');
        if (hit >= 0) return hit;
        hit = trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 53,
            "UP/DOWN SELECT SPACE/+ ADD - SUB M MAX # SET COUNT",
            "M MAX", 'M');
        if (hit >= 0) return hit;
        return trainer_text_hotkey(&g->video, x, y,
            TRAINER_BOX_Y + TRAINER_BOX_H - 28,
            "LEFT/RIGHT ENCHANT +/-1 PGUP/PGDN +/-100 ESC RETURNS",
            "ESC RETURNS", 0x1B);
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
    int maximum = mw_spell_catalog_count(player) - 1;
    state.grid_col = column < 0 ? 0 : (column > maximum ? maximum : column);
    trainer_draw_grid(g, player, &state);
    video_present(&g->video);
}

void trainer_draw_equipment_test(Game *g, Character *player, int selection) {
    if (!g || !player) return;
    TrainerState state;
    memset(&state, 0, sizeof(state));
    state.page = TRAINER_EQUIPMENT;
    int maximum = trainer_equipment_count(player) - 1;
    state.equip_selection = selection < 0 ? 0 :
                            (selection > maximum ? maximum : selection);
    trainer_draw_equipment(g, player, &state);
    video_present(&g->video);
}

void trainer_draw_stats_test(Game *g, Character *player, int selection) {
    if (!g || !player) return;
    TrainerState state;
    memset(&state, 0, sizeof(state));
    state.page = TRAINER_STATS;
    int maximum = trainer_field_count(player) - 1;
    state.selection = selection < 0 ? 0 :
                      (selection > maximum ? maximum : selection);
    state.scroll_top = state.selection >= TRAINER_VISIBLE_FIELDS ?
                       state.selection - TRAINER_VISIBLE_FIELDS + 1 : 0;
    trainer_draw_stats(g, player, &state);
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
    trainer_restrict_state(p, state);
    state->input[state->input_len] = '\0';
    uint64_t value = state->input_len ? strtoull(state->input, NULL, 10) : 0;
    if (state->page == TRAINER_EFFECTS) {
        trainer_field_set(p, &effect_fields[state->effect_selection], value);
    } else if (state->page == TRAINER_GRID &&
               trainer_grid_is_counted(state->grid_set)) {
        u8 (*grid)[45] = trainer_grid(p, state->grid_set);
        if (value > UINT8_MAX) value = UINT8_MAX;
        grid[state->grid_row][state->grid_col] = (u8)value;
    } else if (state->page == TRAINER_EQUIPMENT) {
        TrainerEquipmentEntry entry =
            trainer_equipment_entry(p, state->equip_selection);
        if (value > UINT8_MAX) value = UINT8_MAX;
        trainer_set_equipment_inventory(p, entry, (int)value);
    } else {
        trainer_field_set(p, &trainer_fields[state->selection], value);
    }
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
    const int count = trainer_field_count(p);
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
    trainer_restrict_state(p, state);
    u8 (*grid)[45] = trainer_grid(p, state->grid_set);
    int spell_count = mw_spell_catalog_count(p);
    int counted = trainer_grid_is_counted(state->grid_set);
    if (key == 0x1B || key == 's' || key == 'S') {
        state->page = TRAINER_STATS;
    } else if (key == (0x10000 | 0x48)) {
        state->grid_row = state->grid_row ? state->grid_row - 1 : 3;
    } else if (key == (0x10000 | 0x50)) {
        state->grid_row = state->grid_row < 3 ? state->grid_row + 1 : 0;
    } else if (key == (0x10000 | 0x4B)) {
        state->grid_col = state->grid_col ?
                          state->grid_col - 1 : spell_count - 1;
    } else if (key == (0x10000 | 0x4D)) {
        state->grid_col = state->grid_col + 1 < spell_count ?
                          state->grid_col + 1 : 0;
    } else if (key == ' ' || (counted && key == '+')) {
        u8 *value = &grid[state->grid_row][state->grid_col];
        if (counted) {
            if (*value < UINT8_MAX) (*value)++;
        } else {
            *value = !*value;
        }
    } else if (counted && key == '-') {
        u8 *value = &grid[state->grid_row][state->grid_col];
        if (*value) (*value)--;
    } else if (counted && (key == 'm' || key == 'M')) {
        grid[state->grid_row][state->grid_col] = UINT8_MAX;
    } else if (counted && key == (0x10000 | 0x49)) {
        u8 *value = &grid[state->grid_row][state->grid_col];
        *value = *value > UINT8_MAX - 10 ? UINT8_MAX : (u8)(*value + 10);
    } else if (counted && key == (0x10000 | 0x51)) {
        u8 *value = &grid[state->grid_row][state->grid_col];
        *value = *value > 10 ? (u8)(*value - 10) : 0;
    } else if (key == 'a' || key == 'A') {
        for (int col = 0; col < spell_count; col++) {
            grid[state->grid_row][col] = counted ? UINT8_MAX : 1;
        }
    } else if (key == 'n' || key == 'N') {
        memset(grid[state->grid_row], 0, (size_t)spell_count);
    } else if (counted && key >= '0' && key <= '9') {
        trainer_start_input(state, key);
    }
}

static void trainer_equipment_key(Character *p, TrainerState *state, int key) {
    trainer_restrict_state(p, state);
    int count = trainer_equipment_count(p);
    int enhanced = mw_experience_mode(p) == MW_EXPERIENCE_ENHANCED;
    int enchant_minimum = enhanced ? INT16_MIN : 0;
    int enchant_maximum = enhanced ? INT16_MAX : INT8_MAX;
    TrainerEquipmentEntry entry =
        trainer_equipment_entry(p, state->equip_selection);
    int owned = trainer_equipment_inventory(p, entry);
    int enchant = trainer_equipment_enchant(p, entry);
    if (key == 0x1B) {
        state->page = TRAINER_STATS;
    } else if (key == (0x10000 | 0x48)) {
        state->equip_selection = state->equip_selection ?
            state->equip_selection - 1 : count - 1;
    } else if (key == (0x10000 | 0x50)) {
        state->equip_selection = state->equip_selection + 1 < count ?
            state->equip_selection + 1 : 0;
    } else if (key == ' ' || key == '+') {
        trainer_set_equipment_inventory(p, entry,
            owned < UINT8_MAX ? owned + 1 : UINT8_MAX);
    } else if (key == '-') {
        trainer_set_equipment_inventory(p, entry,
            owned > 0 ? owned - 1 : 0);
    } else if (key == 'm' || key == 'M') {
        trainer_set_equipment_inventory(p, entry, UINT8_MAX);
    } else if (key == (0x10000 | 0x4D)) {
        if (enchant < enchant_minimum) enchant = enchant_minimum;
        if (enchant < enchant_maximum) enchant++;
        if (enchant > enchant_maximum) enchant = enchant_maximum;
        trainer_set_equipment_enchant(p, entry, enchant);
    } else if (key == (0x10000 | 0x4B)) {
        if (enchant > enchant_maximum) enchant = enchant_maximum;
        if (enchant > enchant_minimum) enchant--;
        if (enchant < enchant_minimum) enchant = enchant_minimum;
        trainer_set_equipment_enchant(p, entry, enchant);
    } else if (key == (0x10000 | 0x49)) {
        if (enchant < enchant_minimum) enchant = enchant_minimum;
        enchant = enchant > enchant_maximum - 100 ?
                  enchant_maximum : enchant + 100;
        trainer_set_equipment_enchant(p, entry, enchant);
    } else if (key == (0x10000 | 0x51)) {
        if (enchant > enchant_maximum) enchant = enchant_maximum;
        enchant = enchant < enchant_minimum + 100 ?
                  enchant_minimum : enchant - 100;
        trainer_set_equipment_enchant(p, entry, enchant);
    } else if (entry.slot != 0 && key >= '0' && key <= '9') {
        trainer_start_input(state, key);
    }
}

static void trainer_effect_key(Character *p, TrainerState *state, int key) {
    const int count = trainer_effect_count(p);
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
        key = trainer_mouse_key(g, player, &state, key);
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
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);

    if (sizeof(trainer_fields) / sizeof(trainer_fields[0]) != 50) failures++;
    if (sizeof(effect_fields) / sizeof(effect_fields[0]) != 24) failures++;
    if (trainer_field_count(&p) != 50 || trainer_effect_count(&p) != 24)
        failures++;
    if (input_sdl_to_dos(SDLK_F12, KMOD_CTRL) != INPUT_TRAINER) failures++;
    if (input_sdl_to_dos(SDLK_F12, KMOD_NONE) != -0x86) failures++;
    if (input_sdl_to_dos(SDLK_F11, KMOD_CTRL) != INPUT_WILDERNESS_TEST)
        failures++;
    if (input_sdl_to_dos(SDLK_F11, KMOD_NONE) != -0x85) failures++;

    trainer_field_set(&p, &trainer_fields[12], 1234);
    if (p.stat_str != 1234) failures++;
    trainer_field_set(&p, &trainer_fields[12], 99999);
    if (p.stat_str != 32767) failures++;
    trainer_field_set(&p, &trainer_fields[5], MW_PLAYER_HP_MAX);
    trainer_field_set(&p, &trainer_fields[4], MW_PLAYER_HP_MAX);
    if (mw_hp_cur(&p) != MW_PLAYER_HP_MAX) failures++;
    trainer_field_adjust(&p, &trainer_fields[4], 1);
    if (mw_hp_cur(&p) != MW_PLAYER_HP_MAX) failures++;
    trainer_field_set(&p, &trainer_fields[0], 99);
    if (p.race != RACE_COUNT - 1) failures++;
    trainer_field_set(&p, &trainer_fields[1], 99);
    if (p.class_id != CLASS_COUNT - 1) failures++;
    trainer_field_set(&p, &trainer_fields[3], 99999);
    if (p.level != MW_PLAYER_LEVEL_MAX) failures++;
    trainer_field_set(&p, &trainer_fields[8], 42);
    if (p.age != 42u * MW_AGE_YEAR_UNITS ||
        trainer_field_get(&p, &trainer_fields[8]) != 42) failures++;
    p.age += 17u * MW_AGE_DAY_UNITS;
    trainer_field_adjust(&p, &trainer_fields[8], 1);
    if (p.age != 43u * MW_AGE_YEAR_UNITS +
                 17u * MW_AGE_DAY_UNITS) failures++;
    trainer_field_set(&p, &trainer_fields[9], 999);
    if (p.x_pos != MAP_W - 1) failures++;
    trainer_field_set(&p, &trainer_fields[10], 999);
    if (p.y_pos != MAP_H - 1) failures++;
    trainer_field_set(&p, &trainer_fields[11], 9999);
    if (p.floor_depth != MAX_DUNGEON_FLOOR) failures++;
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    trainer_field_set(&p, &trainer_fields[0], 99);
    trainer_field_set(&p, &trainer_fields[1], 99);
    if (p.race != MW_CLASSIC_RACE_COUNT - 1 ||
        p.class_id != MW_CLASSIC_CLASS_COUNT - 1)
        failures++;
    trainer_field_set(&p, &trainer_fields[11], 9999);
    if (p.floor_depth != CLASSIC_DUNGEON_FLOOR) failures++;
    trainer_field_set(&p, &trainer_fields[4], 99999);
    if (mw_hp_cur(&p) != INT16_MAX) failures++;
    trainer_field_set(&p, &trainer_fields[32], 999);
    if (p.green_pill != INT8_MAX) failures++;
    trainer_field_set(&p, &trainer_fields[39], 99999);
    if (mw_ring_prot_plus(&p) != UINT8_MAX) failures++;
    trainer_field_set(&p, &effect_fields[14], 999);
    if (p.eff_protect_lv != 5) failures++;
    trainer_field_set(&p, &effect_fields[16], 999999);
    if (p.eff_resist_poison != INT16_MAX) failures++;
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);
    trainer_field_set(&p, &trainer_fields[26], 0);
    if (p.raise_x != 0xFFFF) failures++;
    trainer_field_adjust(&p, &trainer_fields[26], 1);
    if (p.raise_x == 0xFFFF) failures++;
    trainer_field_set(&p, &trainer_fields[30], 99);
    if (p.floor_slosher != 1) failures++;
    trainer_field_set(&p, &trainer_fields[32], 999);
    if (p.green_pill != UINT8_MAX) failures++;
    trainer_field_set(&p, &trainer_fields[40], 99);
    if (p.antimagic_ring != 5) failures++;
    trainer_field_set(&p, &effect_fields[16], 4567);
    if (p.eff_resist_poison != 4567) failures++;
    trainer_field_set(&p, &effect_fields[16], 999999);
    if (p.eff_resist_poison != UINT16_MAX) failures++;
    trainer_field_set(&p, &effect_fields[2], 999);
    if (p.eff_feather != 100) failures++;
    trainer_field_set(&p, &effect_fields[12], 999);
    if (p.eff_pwr_weapon != 6) failures++;
    trainer_field_set(&p, &effect_fields[14], 999);
    if (p.eff_protect_lv != 10) failures++;
    trainer_field_set(&p, &trainer_fields[45], 1);
    trainer_field_set(&p, &trainer_fields[49], 1);
    trainer_field_set(&p, &effect_fields[23], 287);
    if (!mw_relic_owned(&p, MW_RELIC_ARCANE_RING) ||
        !mw_relic_owned(&p, MW_RELIC_PHOENIX_SEAL) ||
        p.native.relic_phoenix_cooldown != 287)
        failures++;
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    trainer_field_set(&p, &effect_fields[12], 999);
    if (p.eff_pwr_weapon != 3) failures++;
    trainer_field_set(&p, &effect_fields[14], 999);
    if (p.eff_protect_lv != 5) failures++;
    if (trainer_field_count(&p) != TRAINER_CLASSIC_FIELD_COUNT ||
        trainer_effect_count(&p) != TRAINER_CLASSIC_EFFECT_COUNT ||
        mw_relic_count(&p) != 0)
        failures++;
    TrainerState relic_state = {0};
    relic_state.selection = 49;
    relic_state.effect_selection = 23;
    trainer_restrict_state(&p, &relic_state);
    if (relic_state.selection != TRAINER_CLASSIC_FIELD_COUNT - 1 ||
        relic_state.effect_selection != TRAINER_CLASSIC_EFFECT_COUNT - 1)
        failures++;
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);

    if (strcmp(combat_spell_name(SPELL_CAT_PERMANENT, 0),
               "ENCHANT WEAPON LEVEL 1") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PREPARATION, 11),
               "DESCEND") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_WIZARD, 29),
               "POWER WEAPON III") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 27),
               "ULTRA PROTECTION") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PERMANENT, 30),
               "ENCHANT WEAPON LEVEL 150") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PREPARATION, 34),
               "TOWN PORTAL") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_WIZARD, 32),
               "VOID NOVA") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 34),
               "FINAL JUDGMENT") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PERMANENT, 39),
               "CHARGE ASCENDANT WAND") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PREPARATION, 39),
               "SOUL ANCHOR") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 35),
               "LIFE CONVERGENCE") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_WIZARD, 39),
               "ANNIHILATION") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 39),
               "DIVINE VERDICT") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PERMANENT, 44),
               "CHARGE MYTHIC WAND") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PREPARATION, 44),
               "PERFECT VITALITY") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_WIZARD, 44),
               "POWER WEAPON VI") != 0) failures++;
    if (strcmp(combat_spell_name(SPELL_CAT_PRIEST, 43),
               "CREATION'S WRATH") != 0) failures++;

    TrainerState grid_state;
    memset(&grid_state, 0, sizeof(grid_state));
    trainer_open_grid(&grid_state, GRID_SCROLLS);
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.scrolls[0][0] != 1) failures++;
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.scrolls[0][0] != 2) failures++;
    trainer_grid_key(&p, &grid_state, (0x10000 | 0x4B));
    if (grid_state.grid_col != MW_ENHANCED_SPELL_COUNT - 1) failures++;
    trainer_grid_key(&p, &grid_state, (0x10000 | 0x48));
    if (grid_state.grid_row != 3) failures++;
    p.scrolls[3][35] = 77;
    trainer_grid_key(&p, &grid_state, 'A');
    if (p.scrolls[3][0] != UINT8_MAX ||
        p.scrolls[3][MW_ENHANCED_SPELL_COUNT - 1] != UINT8_MAX ||
        p.scrolls[3][35] != UINT8_MAX) failures++;
    trainer_grid_key(&p, &grid_state, 'N');
    if (p.scrolls[3][0] != 0 ||
        p.scrolls[3][MW_ENHANCED_SPELL_COUNT - 1] != 0 ||
        p.scrolls[3][35] != 0) failures++;

    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    trainer_open_grid(&grid_state, GRID_SCROLLS);
    trainer_grid_key(&p, &grid_state, (0x10000 | 0x4B));
    if (grid_state.grid_col != MW_ORIGINAL_SPELL_COUNT - 1) failures++;
    for (int set = GRID_SPELLS; set <= GRID_PAPERS; set++) {
        u8 (*classic_grid)[45] = trainer_grid(&p, set);
        classic_grid[2][MW_DEEP_SPELL_FIRST] = 73;
        classic_grid[2][MW_ENHANCED_SPELL_COUNT - 1] = 91;
        trainer_open_grid(&grid_state, set);
        grid_state.grid_row = 2;
        grid_state.grid_col = MW_ENHANCED_SPELL_COUNT - 1;
        trainer_grid_key(&p, &grid_state, ' ');
        if (grid_state.grid_col != MW_ORIGINAL_SPELL_COUNT - 1 ||
            classic_grid[2][MW_DEEP_SPELL_FIRST] != 73 ||
            classic_grid[2][MW_ENHANCED_SPELL_COUNT - 1] != 91)
            failures++;
        trainer_grid_key(&p, &grid_state, 'A');
        trainer_grid_key(&p, &grid_state, 'N');
        if (classic_grid[2][MW_DEEP_SPELL_FIRST] != 73 ||
            classic_grid[2][MW_ENHANCED_SPELL_COUNT - 1] != 91)
            failures++;
    }
    mw_set_experience_mode(&p, MW_EXPERIENCE_ENHANCED);

    trainer_open_grid(&grid_state, GRID_WANDS);
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.wands[0][0] != 1) failures++;
    for (int i = 0; i < 254; i++) trainer_grid_key(&p, &grid_state, ' ');
    trainer_grid_key(&p, &grid_state, ' ');
    if (p.wands[0][0] != UINT8_MAX) failures++;
    trainer_grid_key(&p, &grid_state, '-');
    if (p.wands[0][0] != UINT8_MAX - 1) failures++;

    TrainerState equip_state;
    memset(&equip_state, 0, sizeof(equip_state));
    equip_state.page = TRAINER_EQUIPMENT;
    if (trainer_equipment_count(&p) != 32) failures++;
    equip_state.equip_selection = 8; /* Worldforged Blade / weapon slot 12 */
    trainer_equipment_key(&p, &equip_state, ' ');
    if (mw_weapon_inventory_count(&p, 12) != 1) failures++;
    trainer_equipment_key(&p, &equip_state, 'M');
    if (mw_weapon_inventory_count(&p, 12) != UINT8_MAX) failures++;
    trainer_equipment_key(&p, &equip_state, (0x10000 | 0x4D));
    if (mw_weapon_enchant(&p, 12) != 1) failures++;
    mw_set_weapon_enchant(&p, 12, INT16_MAX);
    trainer_equipment_key(&p, &equip_state, (0x10000 | 0x4D));
    if (mw_weapon_enchant(&p, 12) != INT16_MAX) failures++;
    equip_state.equip_selection = 31; /* Moraff's Bulwark / armor slot 15 */
    trainer_equipment_key(&p, &equip_state, ' ');
    if (mw_armor_inventory_count(&p, 15) != 1) failures++;
    equip_state.equip_selection = 15; /* Moraff's Legacy / weapon slot 19 */
    trainer_equipment_key(&p, &equip_state, ' ');
    if (mw_weapon_inventory_count(&p, 19) != 1) failures++;
    mw_set_experience_mode(&p, MW_EXPERIENCE_CLASSIC);
    if (trainer_equipment_count(&p) != 16 ||
        !trainer_equipment_entry(&p, 8).armor ||
        trainer_equipment_entry(&p, 8).slot != 0) failures++;
    int deep_weapon_count = mw_weapon_inventory_count(&p, 12);
    int deep_armor_count = mw_armor_inventory_count(&p, 9);
    int final_weapon_count = mw_weapon_inventory_count(&p, 19);
    int final_armor_count = mw_armor_inventory_count(&p, 15);
    equip_state.equip_selection = 31; /* stale Enhanced cursor */
    trainer_equipment_key(&p, &equip_state, 'M');
    if (equip_state.equip_selection != 15 ||
        mw_weapon_inventory_count(&p, 12) != deep_weapon_count ||
        mw_armor_inventory_count(&p, 9) != deep_armor_count ||
        mw_weapon_inventory_count(&p, 19) != final_weapon_count ||
        mw_armor_inventory_count(&p, 15) != final_armor_count) failures++;
    equip_state.equip_selection = 1;
    mw_set_weapon_enchant(&p, 1, INT8_MAX);
    trainer_equipment_key(&p, &equip_state, (0x10000 | 0x4D));
    if (mw_weapon_enchant(&p, 1) != INT8_MAX) failures++;
    mw_set_weapon_enchant(&p, 1, -5);
    trainer_equipment_key(&p, &equip_state, (0x10000 | 0x4B));
    if (mw_weapon_enchant(&p, 1) != 0) failures++;

    printf("Trainer field/input coverage: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures;
}
