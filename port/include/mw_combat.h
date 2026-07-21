#ifndef MW_COMBAT_H
#define MW_COMBAT_H

#include "mw_game.h"

/* ── Monster type stats (from DS:0x237, 35 bytes/entry, 112 types) ── */
typedef struct {
    const char *name;
    s16 def;
    s16 dmg;
    s16 atk;
    s8  defMod;
    s8  agi;
    s16 hpF;
    u8  imm;        /* spell immunity threshold (100 = immune to hold/autokill/goaway) */
    u8  minL;       /* minimum dungeon level to appear */
    u8  maxL;       /* maximum dungeon level to appear */
    u8  boss;       /* boss flag: HP gets +level*20 bonus */
    u8  saveA;
    u8  saveB;
} MonsterType;

/* ── Weapon stats (from DS:0x1C0, 7 bytes/entry, 12 weapons) ── */
typedef struct {
    const char *name;
    s16 maxDmg;
    s8  hit;
    s8  speed;
    s8  weight;
} WeaponStats;

#define MONSTER_TYPE_COUNT 112
#define WEAPON_STAT_COUNT  12
#define ARMOR_STAT_COUNT   7

extern const MonsterType monster_types[MONSTER_TYPE_COUNT];
extern const WeaponStats weapon_stats[WEAPON_STAT_COUNT];

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
} CombatState;

/* ── Spell definitions ── */
typedef enum {
    SPELL_CAT_PERMANENT = 0,
    SPELL_CAT_PREPARATION = 1,
    SPELL_CAT_WIZARD = 2,
    SPELL_CAT_PRIEST = 3
} SpellCategory;

/* Combat lifecycle */
void combat_init_encounter(Game *g, CombatState *cs);
void combat_init_entity(Game *g, CombatState *cs, int entity_index);
void combat_run(Game *g, CombatState *cs, Character *player);

/* Individual actions */
int  combat_player_attack(Game *g, CombatState *cs, Character *player);
int  combat_monster_attack(Game *g, CombatState *cs, Character *player);
int  combat_cast_battle_spell(Game *g, CombatState *cs, Character *player);

/* Shared magic UI/engine.  A null combat state means exploration mode. */
int  cmd_cast_spell_menu(Game *g, Character *player, CombatState *combat);
int  cmd_use_item(Game *g, Character *player, CombatState *combat);

/* Original effect lifecycle: one call per player action, and an inn reset. */
void character_tick_effects(Game *g, Character *player);
void character_clear_town_effects(Character *player);
void character_clear_battle_effects(Character *player);

/* Monster utilities */
int  combat_calc_monster_hp(const MonsterType *mt, int level);
int  combat_pick_monster_type(Game *g, int floor_depth);
int  get_monster_pic_index_ext(int type_idx);

/* Equipment selection commands (non-combat) */
void cmd_weapons(Game *g, Character *player);
void cmd_armor(Game *g, Character *player);

/* Preparation spell casting (non-combat) */
void cmd_cast_prep_spell(Game *g, Character *player);

#endif /* MW_COMBAT_H */
