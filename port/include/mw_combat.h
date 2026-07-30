#ifndef MW_COMBAT_H
#define MW_COMBAT_H

#include "mw_game.h"

/* The first 112 rows come from the byte-oriented DOS table.  Native rows and
 * all gameplay-facing fields are 16-bit so deep-floor enemies can exceed
 * level/stat 255 without wrapping. */
typedef struct {
    const char *name;
    s16 def;
    s16 dmg;
    s16 atk;
    s16 defMod;
    s16 agi;
    s16 hpF;
    u16 imm;        /* spell immunity threshold (100 = immune to hold/autokill/goaway) */
    u16 minL;       /* minimum dungeon level to appear */
    u16 maxL;       /* maximum dungeon level to appear */
    u16 boss;       /* boss flag: HP gets +level*20 bonus */
    u16 saveA;
    u16 saveB;
} MonsterType;

/* ── Weapon stats (from DS:0x1C0, 7 bytes/entry, 12 weapons) ── */
typedef struct {
    const char *name;
    s16 maxDmg;
    s8  hit;
    s8  speed;
    s8  weight;
} WeaponStats;

#define QUEST_MONSTER_FIRST 104
#define QUEST_MONSTER_COUNT 10
#define DEEP_MONSTER_FIRST 114
#define DEEP_MONSTER_COUNT 64
#define ASCENDED_BOSS_FIRST 174
#define ASCENDED_BOSS_COUNT 4
#define MONSTER_TYPE_COUNT (DEEP_MONSTER_FIRST + DEEP_MONSTER_COUNT)
#define WEAPON_STAT_COUNT  20
#define ARMOR_STAT_COUNT   16

extern const MonsterType monster_types[MONSTER_TYPE_COUNT];
extern const WeaponStats weapon_stats[WEAPON_STAT_COUNT];
const char *combat_armor_name(int armor);
int combat_armor_defense(int armor);
int combat_armor_weight(int armor);
int combat_weapon_allowed(const Character *player, int weapon);
int combat_armor_allowed(const Character *player, int armor);

/* ── Combat state ── */
typedef struct {
    int active;
    int entity_index;       /* MON.MAP record, -1 for synthetic/test combat */
    int monster_type_idx;
    int monster_level;
    int monster_hp;
    int monster_max_hp;
    int monster_asleep;
    int monster_held;
    int monster_stopped;
    int fled;
    int player_fled;
    int special_used;
    char special_message[96];
} CombatState;

enum {
    COMBAT_ACTION_FIGHT = 0,
    COMBAT_ACTION_CAST,
    COMBAT_ACTION_ITEM,
    COMBAT_ACTION_WAIT
};

/* ── Spell definitions ── */
typedef enum {
    SPELL_CAT_PERMANENT = 0,
    SPELL_CAT_PREPARATION = 1,
    SPELL_CAT_WIZARD = 2,
    SPELL_CAT_PRIEST = 3
} SpellCategory;

/* Canonical display name for one of the four 30-entry spell tables. */
const char *combat_spell_name(int category, int index);

/* Combat lifecycle */
void combat_init_encounter(Game *g, CombatState *cs);
void combat_init_entity(Game *g, CombatState *cs, int entity_index);
int  combat_take_turn(Game *g, CombatState *cs, Character *player, int action);
void combat_run(Game *g, CombatState *cs, Character *player);

/* Individual actions */
int  combat_player_attack(Game *g, CombatState *cs, Character *player);
int  combat_monster_attack(Game *g, CombatState *cs, Character *player);
int  combat_cast_battle_spell(Game *g, CombatState *cs, Character *player);

/* Shared magic UI/engine.  A null combat state means exploration mode. */
int  cmd_cast_spell_menu(Game *g, Character *player, CombatState *combat);
int  cmd_use_item(Game *g, Character *player, CombatState *combat);
void game_draw_use_item_test(Game *g, Character *player,
                             CombatState *combat, int page);

/* Original effect lifecycle: one call per player action, and an inn reset. */
void character_tick_effects(Game *g, Character *player);
void character_clear_town_effects(Character *player);
void character_clear_battle_effects(Character *player);
int  combat_self_test(void);

/* Monster utilities */
int  combat_calc_monster_hp(const MonsterType *mt, int level);
int  combat_monster_type_spawnable(int type_idx);
int  combat_monster_type_valid(int type_idx, int floor_depth);
int  combat_pick_monster_type(Game *g, int floor_depth);
int  combat_monster_max_floor(int type_idx);
int  combat_monster_drain_amount(int type_idx);
const char *combat_monster_spell_name(int type_idx);
int  combat_monster_spell_chance(int type_idx);
int  get_monster_pic_index_ext(int type_idx);
int  get_monster_color_ext(int type_idx);
int  get_monster_tint_ext(int type_idx);
int  combat_remap_monster_color(int color, int replace_color, int tint);

/* Equipment selection commands (non-combat) */
void cmd_weapons(Game *g, Character *player);
void cmd_armor(Game *g, Character *player);

/* Preparation spell casting (non-combat) */
void cmd_cast_prep_spell(Game *g, Character *player);

#endif /* MW_COMBAT_H */
