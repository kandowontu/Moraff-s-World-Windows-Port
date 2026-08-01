#include "mw_battle_simulator.h"
#include "mw_combat.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* MW_EXTENSION: native interactive counterpart to the melee and spell
   simulators embedded in mw_reference_guide.html.  The formulas and all
   original controls are intentionally kept together here so this diagnostic
   tool cannot mutate the active encounter, character, or game RNG. */

#define SIM_TRIALS 10000
#define SIM_HIST_BINS 20

typedef struct SimRng { u32 state; } SimRng;

typedef struct MeleeResult {
    double average_damage;
    double hit_rate;
    double average_swings;
    int maximum_damage;
    int hits_to_kill;
    int average_hp;
    int base_score;
    int score_min;
    int score_max;
    int histogram[SIM_HIST_BINS];
} MeleeResult;

typedef enum SpellSimType {
    SS_FIXED, SS_SCALE, SS_MULTI, SS_RANGE, SS_EFFECT, SS_DRAIN, SS_AUTOKILL,
    SS_PERCENT_MAX, SS_PERCENT_CURRENT, SS_LEVEL_DRAIN_SCALE
} SpellSimType;

typedef struct SpellSimDef {
    const char *name;
    SpellSimType type;
    int a, b;
    const char *formula;
} SpellSimDef;

typedef struct SpellResult {
    double average_damage;
    double success_rate;
    int minimum_damage;
    int maximum_damage;
    int casts_to_kill;
    int average_hp;
    int immune;
    int one_shot_kills;
    int histogram[SIM_HIST_BINS];
} SpellResult;

typedef struct BattleSimulator {
    int enhanced;
    int tab;
    int selected;
    int level;
    int strength;
    int luck;
    int weapon;
    int power_weapon;
    int strength_spell;
    int permanent_enchant;
    int temporary_enchant;
    int gauntlet;
    int monster;
    int monster_level;
    int spell_level;
    int wisdom;
    int intelligence;
    int spell;
    int spell_monster;
    int spell_monster_level;
    MeleeResult melee;
    SpellResult spell_result;
    u32 simulation_serial;
    int input_mode;
    int input_len;
    char input[12];
} BattleSimulator;

static const SpellSimDef spell_defs[] = {
    {"MAGIC ZAP", SS_SCALE, 2, 2, "LEVEL X 2 + 2"},
    {"MINOR SHOCK", SS_FIXED, 25, 0, "25 FIXED DAMAGE"},
    {"LIGHTNING", SS_SCALE, 4, 4, "LEVEL X 4 + 4"},
    {"MAGIC MISSILE", SS_FIXED, 50, 0, "50 FIXED DAMAGE"},
    {"SLEEP", SS_EFFECT, 10, 0, "INCAPACITATES FOR 10 TURNS"},
    {"MAGIC ZOT", SS_MULTI, 4, 8, "LEVEL+1 MISSILES, EACH 4-8"},
    {"MAJOR SHOCK", SS_FIXED, 125, 0, "125 FIXED DAMAGE"},
    {"HOLD MONSTER", SS_EFFECT, 15, 0, "IMMOBILIZES FOR 15 TURNS"},
    {"MAGIC BOLT", SS_MULTI, 7, 11, "LEVEL+1 BOLTS, EACH 7-11"},
    {"MINOR EXPLOSION", SS_RANGE, 75, 175, "RANDOM DAMAGE 75-175"},
    {"MAJOR SHOCK II", SS_FIXED, 300, 0, "300 FIXED DAMAGE"},
    {"MAJOR EXPLOSION", SS_RANGE, 125, 225, "RANDOM DAMAGE 125-225"},
    {"HUGE EXPLOSION", SS_RANGE, 200, 500, "RANDOM DAMAGE 200-500"},
    {"GO AWAY", SS_EFFECT, 0, 0, "TELEPORTS THE MONSTER AWAY"},
    {"DRAIN MONSTER", SS_DRAIN, 0, 0, "LEVEL<WIS KILLS; ELSE HPF/2 X WIS"},
    {"AUTOKILL", SS_AUTOKILL, 0, 0, "MENTAL SAVE CONTEST FOR INSTANT KILL"},
    /* MW_EXTENSION: every Enhanced offensive spell from the level 11-15
       wizard and priest battle pages.  Non-damaging deep spells and Power
       Weapon IV-VI belong to their respective effect/melee simulators. */
    {"ABYSSAL LANCE", SS_SCALE, 25, 500, "LEVEL X 25 + 500"},
    {"VOID NOVA", SS_RANGE, 5000, 12000, "RANDOM DAMAGE 5000-12000"},
    {"SOUL REND", SS_LEVEL_DRAIN_SCALE, 4, 0,
     "DRAINS WIS X 4 + LEVEL/2 LEVELS"},
    {"OBLIVION", SS_PERCENT_MAX, 25, 5000, "25% MAX HP + 5000"},
    {"STARFIRE", SS_RANGE, 15000, 30000, "RANDOM DAMAGE 15000-30000"},
    {"REALITY RUPTURE", SS_PERCENT_MAX, 40, 25000,
     "40% MAX HP + 25000"},
    {"MANA TEMPEST", SS_SCALE, 50, 2000, "LEVEL X 50 + 2000"},
    {"ANNIHILATION", SS_RANGE, 60000, 120000,
     "RANDOM DAMAGE 60000-120000"},
    {"HOLY CATACLYSM", SS_RANGE, 3500, 9000,
     "RANDOM DAMAGE 3500-9000"},
    {"FINAL JUDGMENT", SS_PERCENT_MAX, 20, 4000, "20% MAX HP + 4000"},
    {"LIFE CONVERGENCE", SS_PERCENT_CURRENT, 10, 2000,
     "CURRENT HP / 10 + 2000"},
    {"WRATH OF HEAVEN", SS_RANGE, 12000, 26000,
     "RANDOM DAMAGE 12000-26000"},
    {"DIVINE VERDICT", SS_PERCENT_MAX, 50, 30000,
     "50% MAX HP + 30000"},
    {"COSMIC IMPLOSION", SS_PERCENT_MAX, 60, 50000,
     "60% MAX HP + 50000"},
    {"END OF AGES", SS_RANGE, 200000, 400000,
     "RANDOM DAMAGE 200000-400000"},
    {"CREATION'S WRATH", SS_PERCENT_MAX, 75, 100000,
     "75% MAX HP + 100000"},
};

#define SIM_CLASSIC_SPELL_COUNT 16
#define SIM_SPELL_COUNT ((int)(sizeof(spell_defs) / sizeof(spell_defs[0])))

enum EnhancedSimulatorSpell {
    SIM_SPELL_ABYSSAL_LANCE = SIM_CLASSIC_SPELL_COUNT,
    SIM_SPELL_VOID_NOVA,
    SIM_SPELL_SOUL_REND,
    SIM_SPELL_OBLIVION,
    SIM_SPELL_STARFIRE,
    SIM_SPELL_REALITY_RUPTURE,
    SIM_SPELL_MANA_TEMPEST,
    SIM_SPELL_ANNIHILATION,
    SIM_SPELL_HOLY_CATACLYSM,
    SIM_SPELL_FINAL_JUDGMENT,
    SIM_SPELL_LIFE_CONVERGENCE,
    SIM_SPELL_WRATH_OF_HEAVEN,
    SIM_SPELL_DIVINE_VERDICT,
    SIM_SPELL_COSMIC_IMPLOSION,
    SIM_SPELL_END_OF_AGES,
    SIM_SPELL_CREATIONS_WRATH
};

static int simulator_spell_count(const BattleSimulator *sim) {
    return sim->enhanced ? SIM_SPELL_COUNT : SIM_CLASSIC_SPELL_COUNT;
}

static const int strength_spell_values[] = {0, 5, 7, 10};
static const char *const strength_spell_names[] = {
    "NONE", "STRENGTH +5", "BATTLE STRENGTH +7", "SUPER STRENGTH +10"
};
static const int gauntlet_values[] = {0, 12, 50};
static const char *const gauntlet_names[] = {
    "NONE", "+12 SHADOW GAUNTLET", "+50 RED GAUNTLET"
};
static const char *const power_weapon_names[] = {
    "NONE", "POWER WEAPON I", "POWER WEAPON II", "POWER WEAPON III",
    "POWER WEAPON IV", "POWER WEAPON V", "POWER WEAPON VI"
};

static u32 sim_next(SimRng *rng) {
    rng->state = rng->state * 1664525u + 1013904223u;
    return rng->state;
}

static int sim_rand(SimRng *rng, int maximum) {
    return maximum > 0 ? (int)(sim_next(rng) % (u32)maximum) : 0;
}

static int sim_clamp(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static int sim_wrap(int value, int count) {
    if (count <= 0) return 0;
    value %= count;
    return value < 0 ? value + count : value;
}

static int simulator_numeric_limits(const BattleSimulator *sim,
                                    int *low, int *high) {
    if (sim->tab == 0) {
        switch (sim->selected) {
        case 0: *low = 1; *high = MW_PLAYER_LEVEL_MAX; return 1;
        case 1: *low = 1; *high = MW_PLAYER_STAT_MAX; return 1;
        case 2: *low = 0; *high = MW_PLAYER_STAT_MAX; return 1;
        case 6: *low = 0; *high = INT16_MAX; return 1;
        case 7: *low = 0; *high = 5; return 1;
        case 10: *low = 1; *high = MAX_DUNGEON_FLOOR; return 1;
        default: return 0;
        }
    }
    switch (sim->selected) {
    case 0: *low = 1; *high = MW_PLAYER_LEVEL_MAX; return 1;
    case 1: *low = 1; *high = MW_PLAYER_STAT_MAX; return 1;
    case 2: *low = 1; *high = MW_PLAYER_STAT_MAX; return 1;
    case 5: *low = 1; *high = MAX_DUNGEON_FLOOR; return 1;
    default: return 0;
    }
}

static void simulator_set_numeric_value(BattleSimulator *sim, int value) {
    int low, high;
    if (!simulator_numeric_limits(sim, &low, &high)) return;
    value = sim_clamp(value, low, high);
    if (sim->tab == 0) {
        switch (sim->selected) {
        case 0: sim->level = value; break;
        case 1: sim->strength = value; break;
        case 2: sim->luck = value; break;
        case 6: sim->permanent_enchant = value; break;
        case 7: sim->temporary_enchant = value; break;
        case 10: sim->monster_level = value; break;
        }
    } else {
        switch (sim->selected) {
        case 0: sim->spell_level = value; break;
        case 1: sim->wisdom = value; break;
        case 2: sim->intelligence = value; break;
        case 5: sim->spell_monster_level = value; break;
        }
    }
}

static void simulator_cancel_input(BattleSimulator *sim) {
    sim->input_mode = 0;
    sim->input_len = 0;
    sim->input[0] = '\0';
}

static int simulator_begin_input(BattleSimulator *sim, int digit) {
    int low, high;
    if (!simulator_numeric_limits(sim, &low, &high)) return 0;
    (void)low;
    (void)high;
    sim->input_mode = 1;
    sim->input_len = 1;
    sim->input[0] = (char)digit;
    sim->input[1] = '\0';
    return 1;
}

/* Returns 1 when consumed and 2 when Enter committed a new value. */
static int simulator_edit_key(BattleSimulator *sim, int key) {
    if (!sim->input_mode) {
        if (key >= '0' && key <= '9')
            return simulator_begin_input(sim, key);
        return 0;
    }
    if (key == 0x1B) {
        simulator_cancel_input(sim);
        return 1;
    }
    if (key == 8 || key == 127) {
        if (sim->input_len > 0) sim->input[--sim->input_len] = '\0';
        return 1;
    }
    if (key >= '0' && key <= '9') {
        if (sim->input_len < (int)sizeof(sim->input) - 1) {
            sim->input[sim->input_len++] = (char)key;
            sim->input[sim->input_len] = '\0';
        }
        return 1;
    }
    if (key == '\r' || key == '\n') {
        unsigned long long value = 0;
        for (int i = 0; i < sim->input_len; i++) {
            value = value * 10u + (unsigned)(sim->input[i] - '0');
            if (value > (unsigned long long)INT_MAX) {
                value = INT_MAX;
                break;
            }
        }
        simulator_set_numeric_value(sim, (int)value);
        simulator_cancel_input(sim);
        return 2;
    }
    return 1;
}

static int sim_next_weapon(int current, int direction, int enhanced) {
    do {
        current = sim_wrap(current + direction, WEAPON_STAT_COUNT);
    } while ((current >= 8 && current <= 11) ||
             (!enhanced && current >= 12));
    return current;
}

static int sim_average_hp(const MonsterType *monster, int level) {
    long long average = ((long long)monster->hpF * level + 2) / 2;
    if (monster->boss) average += (long long)level * 20;
    if (average < 1) average = 1;
    if (average > INT_MAX) average = INT_MAX;
    return (int)average;
}

static void histogram_add(int histogram[SIM_HIST_BINS], int value,
                          int maximum) {
    int bin = maximum > 0 ? (int)((long long)value * SIM_HIST_BINS /
                                  (maximum + 1LL)) : 0;
    if (bin < 0) bin = 0;
    if (bin >= SIM_HIST_BINS) bin = SIM_HIST_BINS - 1;
    histogram[bin]++;
}

static void run_melee_simulation(BattleSimulator *sim) {
    const MonsterType *monster = &monster_types[sim->monster];
    const WeaponStats *weapon = &weapon_stats[sim->weapon];
    int damage_max = combat_effective_damage_max(
        sim->weapon, sim->power_weapon, sim->enhanced);
    MeleeResult *out = &sim->melee;
    SimRng rng = {0x4D575349u ^ ++sim->simulation_serial};
    long long total_damage = 0, total_swings = 0;
    int hits = 0;
    memset(out, 0, sizeof(*out));
    out->average_hp = sim_average_hp(monster, sim->monster_level);
    out->base_score = sim->level * 2 + sim->strength +
        strength_spell_values[sim->strength_spell] + sim->luck + weapon->hit +
        sim->gauntlet + sim->permanent_enchant + sim->temporary_enchant -
        sim->monster_level * 2 - monster->def - monster->defMod - monster->agi;
    out->score_min = out->base_score;
    out->score_max = out->base_score + 79;

    int damages[SIM_TRIALS];
    for (int trial = 0; trial < SIM_TRIALS; trial++) {
        int strength = sim->strength +
                       strength_spell_values[sim->strength_spell];
        int score = sim_rand(&rng, 80) + out->base_score;
        if (sim->monster_level > 75 && sim_rand(&rng, 30) == 1) score += 40;
        int damage = 0, swings = 0;
        while (score > 40) {
            damage += sim_rand(&rng, damage_max);
            score -= 40;
            swings++;
        }
        if (damage > 0) {
            if (sim_rand(&rng, 20) < sim->level)
                damage += sim_rand(&rng, strength);
            else
                damage += sim_rand(&rng, strength / 3);
            if (sim->level < 5)
                damage += sim_rand(&rng, 5 - sim->level);
            damage += sim_rand(&rng, sim->level);
            hits++;
            total_swings += swings;
            if (damage > out->maximum_damage) out->maximum_damage = damage;
        }
        damages[trial] = damage;
        total_damage += damage;
    }
    out->average_damage = (double)total_damage / SIM_TRIALS;
    out->hit_rate = (double)hits / SIM_TRIALS;
    out->average_swings = hits ? (double)total_swings / hits : 0.0;
    out->hits_to_kill = out->average_damage > 0.0 ?
        (int)((out->average_hp + out->average_damage - 0.000001) /
              out->average_damage) : INT_MAX;
    for (int i = 0; i < SIM_TRIALS; i++)
        histogram_add(out->histogram, damages[i], out->maximum_damage);
}

static int simulate_spell_cast(SimRng *rng, const SpellSimDef *spell,
                               int level, int wisdom, int intelligence,
                               const MonsterType *monster, int monster_level) {
    switch (spell->type) {
    case SS_FIXED: return spell->a;
    case SS_SCALE: return level * spell->a + spell->b;
    case SS_MULTI: {
        int total = 0;
        for (int i = 0; i <= level; i++)
            total += sim_rand(rng, spell->b - spell->a + 1) + spell->a;
        return total;
    }
    case SS_RANGE: return sim_rand(rng, spell->b - spell->a + 1) + spell->a;
    case SS_DRAIN:
        if (monster->imm >= 100) return -2;
        if (monster_level < wisdom) return -1;
        return (monster->hpF / 2) * wisdom;
    case SS_AUTOKILL: {
        if (monster->imm >= 100) return -2;
        int monster_save = sim_rand(rng, monster->saveA + monster->saveB + 1);
        int monster_score = sim_rand(rng, monster_level + monster_save + 1);
        int mental = sim_rand(rng, wisdom + intelligence + 1);
        int player_score = sim_rand(rng, level + mental + 1);
        return player_score >= monster_score ? -1 : 0;
    }
    case SS_PERCENT_MAX: {
        long long damage =
            (long long)sim_average_hp(monster, monster_level) * spell->a / 100 +
            spell->b;
        return damage > INT_MAX ? INT_MAX : (damage < 1 ? 1 : (int)damage);
    }
    case SS_PERCENT_CURRENT: {
        int divisor = spell->a > 0 ? spell->a : 10;
        long long damage =
            (long long)sim_average_hp(monster, monster_level) / divisor +
            spell->b;
        return damage > INT_MAX ? INT_MAX : (damage < 1 ? 1 : (int)damage);
    }
    case SS_LEVEL_DRAIN_SCALE: {
        long long drain = (long long)wisdom * spell->a + level / 2;
        if (drain < 1) drain = 1;
        return drain >= monster_level ? -1 : 0;
    }
    case SS_EFFECT:
        return monster->imm >= 100 ? -2 : 0;
    }
    return 0;
}

static void run_spell_simulation(BattleSimulator *sim) {
    const SpellSimDef *spell = &spell_defs[sim->spell];
    const MonsterType *monster = &monster_types[sim->spell_monster];
    SpellResult *out = &sim->spell_result;
    SimRng rng = {0x5350454Cu ^ ++sim->simulation_serial};
    long long total = 0;
    int damages[SIM_TRIALS];
    int successes = 0;
    memset(out, 0, sizeof(*out));
    out->minimum_damage = INT_MAX;
    out->average_hp = sim_average_hp(monster, sim->spell_monster_level);
    out->immune = monster->imm >= 100 &&
        (spell->type == SS_EFFECT || spell->type == SS_DRAIN ||
         spell->type == SS_AUTOKILL);

    for (int trial = 0; trial < SIM_TRIALS; trial++) {
        int raw = simulate_spell_cast(&rng, spell, sim->spell_level,
                                      sim->wisdom, sim->intelligence,
                                      monster, sim->spell_monster_level);
        int damage = raw == -1 ? out->average_hp : (raw < 0 ? 0 : raw);
        if (raw == -1) successes++;
        if (damage >= out->average_hp && damage > 0) out->one_shot_kills++;
        if (damage < out->minimum_damage) out->minimum_damage = damage;
        if (damage > out->maximum_damage) out->maximum_damage = damage;
        damages[trial] = damage;
        total += damage;
    }
    if (out->minimum_damage == INT_MAX) out->minimum_damage = 0;
    out->average_damage = (double)total / SIM_TRIALS;
    if (spell->type == SS_AUTOKILL ||
        spell->type == SS_LEVEL_DRAIN_SCALE)
        out->success_rate = (double)successes / SIM_TRIALS;
    else if (spell->type == SS_EFFECT)
        out->success_rate = out->immune ? 0.0 : 1.0;
    else
        out->success_rate = (double)out->one_shot_kills / SIM_TRIALS;
    if (spell->type == SS_AUTOKILL)
        out->casts_to_kill = out->success_rate > 0.0 ?
            (int)(1.0 / out->success_rate + 0.999999) : INT_MAX;
    else if (spell->type == SS_LEVEL_DRAIN_SCALE) {
        long long drain = (long long)sim->wisdom * spell->a +
                          sim->spell_level / 2;
        if (drain < 1) drain = 1;
        out->casts_to_kill = drain >= sim->spell_monster_level ? 1 :
            (int)(((long long)sim->spell_monster_level + drain - 1) / drain);
    }
    else if (spell->type == SS_PERCENT_CURRENT) {
        long long remaining = out->average_hp;
        int casts = 0;
        int divisor = spell->a > 0 ? spell->a : 10;
        while (remaining > 0 && casts < INT_MAX) {
            long long damage = remaining / divisor + spell->b;
            if (damage < 1) damage = 1;
            remaining -= damage;
            casts++;
        }
        out->casts_to_kill = casts;
    }
    else
        out->casts_to_kill = out->average_damage > 0.0 ?
            (int)((out->average_hp + out->average_damage - 0.000001) /
                  out->average_damage) : INT_MAX;
    if (spell->type != SS_EFFECT)
        for (int i = 0; i < SIM_TRIALS; i++)
            histogram_add(out->histogram, damages[i], out->maximum_damage);
}

static void simulator_palette(Video *v) {
    video_load_vga_default_palette(v);
    video_set_palette(v, 1, 18, 28, 72);
    video_set_palette(v, 3, 70, 210, 255);
    video_set_palette(v, 4, 255, 196, 35);
    video_set_palette(v, 5, 235, 82, 40);
    video_set_palette(v, 8, 30, 245, 55);
    video_set_palette(v, 12, 245, 45, 45);
    video_set_palette(v, 15, 225, 225, 225);
}

static void sim_field(Video *v, int row, int selected,
                      const char *label, const char *value) {
    char line[96];
    int y = 92 + row * 37;
    if (selected) video_fill_rect(v, 12, y - 4, 492, 32, 1);
    snprintf(line, sizeof(line), "%c %-20s %s", selected ? '>' : ' ',
             label, value);
    video_draw_text_scaled(v, 18, y, line, selected ? 4 : 15, 3, 4);
}

static void draw_histogram(Video *v, const int histogram[SIM_HIST_BINS],
                           int x, int y, int w, int h) {
    int maximum = 1;
    for (int i = 0; i < SIM_HIST_BINS; i++)
        if (histogram[i] > maximum) maximum = histogram[i];
    video_hline(v, x, y + h, w, 15);
    int bw = w / SIM_HIST_BINS;
    for (int i = 0; i < SIM_HIST_BINS; i++) {
        int bh = histogram[i] * (h - 2) / maximum;
        if (bh > 0)
            video_fill_rect(v, x + i * bw + 1, y + h - bh,
                            bw > 2 ? bw - 2 : 1, bh, i & 1 ? 3 : 8);
    }
}

static void draw_melee_results(Video *v, const BattleSimulator *sim) {
    const MeleeResult *r = &sim->melee;
    const MonsterType *m = &monster_types[sim->monster];
    int damage_max = combat_effective_damage_max(
        sim->weapon, sim->power_weapon, sim->enhanced);
    char line[128];
    int y = 100;
#define RESULT_LINE(...) do { snprintf(line, sizeof(line), __VA_ARGS__); \
    video_draw_text_scaled(v, 530, y, line, 15, 2, 3); y += 28; } while (0)
    video_draw_text(v, 530, 70, "10,000-ATTACK RESULTS", 8);
    RESULT_LINE("AVERAGE DAMAGE: %.1f", r->average_damage);
    RESULT_LINE("HIT RATE: %.1f%%", r->hit_rate * 100.0);
    RESULT_LINE("AVERAGE SWINGS ON HIT: %.1f", r->average_swings);
    RESULT_LINE("MAXIMUM HIT: %d", r->maximum_damage);
    if (r->hits_to_kill == INT_MAX) RESULT_LINE("HITS TO KILL: INF");
    else RESULT_LINE("HITS TO KILL: %d", r->hits_to_kill);
    RESULT_LINE("DAMAGE PER ROUND: %.1f", r->average_damage);
    RESULT_LINE("MONSTER AVERAGE HP: %d", r->average_hp);
    y += 10;
    video_draw_text(v, 530, y, "DAMAGE BREAKDOWN", 4); y += 30;
    RESULT_LINE("SCORE BASE (NO RAND): %d", r->base_score);
    RESULT_LINE("SCORE RANGE: %d TO %d", r->score_min, r->score_max);
    RESULT_LINE("MONSTER DEFENSE: %d+%d+%d",
                m->def, m->defMod, m->agi);
    RESULT_LINE("MONSTER LEVEL X 2: %d", sim->monster_level * 2);
    RESULT_LINE("WEAPON DAMAGE DIE: 0 TO %d", damage_max - 1);
    if (sim->power_weapon && sim->enhanced &&
        combat_power_weapon_max_damage(sim->power_weapon) <
            weapon_stats[sim->weapon].maxDmg)
        RESULT_LINE("PW DIE 0-%d LOWER; BASE RETAINED",
                    combat_power_weapon_max_damage(sim->power_weapon) - 1);
    video_draw_text(v, 530, 498, "DAMAGE DISTRIBUTION", 4);
    draw_histogram(v, r->histogram, 530, 530, 460, 145);
#undef RESULT_LINE
}

static void draw_spell_results(Video *v, const BattleSimulator *sim) {
    const SpellResult *r = &sim->spell_result;
    const SpellSimDef *spell = &spell_defs[sim->spell];
    const MonsterType *m = &monster_types[sim->spell_monster];
    char line[128];
    int y = 100;
#define RESULT_LINE(...) do { snprintf(line, sizeof(line), __VA_ARGS__); \
    video_draw_text_scaled(v, 530, y, line, 15, 2, 3); y += 28; } while (0)
    video_draw_text(v, 530, 70, "10,000-CAST RESULTS", 8);
    RESULT_LINE("AVERAGE DAMAGE: %.1f", r->average_damage);
    RESULT_LINE("MIN / MAX: %d / %d", r->minimum_damage, r->maximum_damage);
    if (spell->type == SS_AUTOKILL || spell->type == SS_EFFECT ||
        spell->type == SS_LEVEL_DRAIN_SCALE)
        RESULT_LINE("SUCCESS RATE: %.1f%%", r->success_rate * 100.0);
    else
        RESULT_LINE("ONE-SHOT RATE: %.1f%%", r->success_rate * 100.0);
    if (r->casts_to_kill == INT_MAX) RESULT_LINE("CASTS TO KILL: INF");
    else RESULT_LINE("CASTS TO KILL: %d", r->casts_to_kill);
    RESULT_LINE("IMMUNE: %s", r->immune ? "YES" : "NO");
    RESULT_LINE("MONSTER AVERAGE HP: %d", r->average_hp);
    y += 10;
    video_draw_text(v, 530, y, "SPELL FORMULA", 4); y += 30;
    RESULT_LINE("SPELL: %s", spell->name);
    RESULT_LINE("FORMULA: %s", spell->formula);
    RESULT_LINE("PLAYER LEVEL: %d", sim->spell_level);
    if (spell->type == SS_DRAIN)
        RESULT_LINE("WIS %d; HP FACTOR %d", sim->wisdom, m->hpF);
    if (spell->type == SS_LEVEL_DRAIN_SCALE) {
        long long drain = (long long)sim->wisdom * spell->a +
                          sim->spell_level / 2;
        if (drain > INT_MAX) drain = INT_MAX;
        RESULT_LINE("WIS %d; LEVELS DRAINED %d", sim->wisdom, (int)drain);
    }
    if (spell->type == SS_AUTOKILL) {
        RESULT_LINE("INT+WIS: %d", sim->intelligence + sim->wisdom);
        RESULT_LINE("MONSTER SAVES: %d+%d", m->saveA, m->saveB);
    }
    video_draw_text(v, 530, 498, "DAMAGE DISTRIBUTION", 4);
    draw_histogram(v, r->histogram, 530, 530, 460, 145);
#undef RESULT_LINE
}

static void simulator_numeric_text(const BattleSimulator *sim, int row,
                                   int value, const char *prefix,
                                   char *out, size_t out_size) {
    if (sim->input_mode && sim->selected == row)
        snprintf(out, out_size, "%s%s_", prefix, sim->input);
    else
        snprintf(out, out_size, "%s%d", prefix, value);
}

static void draw_simulator(Game *g, const BattleSimulator *sim) {
    Video *v = &g->video;
    char value[128];
    simulator_palette(v);
    video_clear(v, 0);
    video_draw_text(v, 18, 12, "MORAFF'S WORLD BATTLE SIMULATOR", 8);
    video_draw_text(v, 690, 12, "CTRL-F2 / ESC RETURNS", 15);
    video_fill_rect(v, 12, 44, 240, 34, sim->tab == 0 ? 1 : 0);
    video_fill_rect(v, 260, 44, 240, 34, sim->tab == 1 ? 1 : 0);
    video_draw_text(v, 55, 51, "MELEE DAMAGE", sim->tab == 0 ? 4 : 15);
    video_draw_text(v, 310, 51, "SPELL DAMAGE", sim->tab == 1 ? 4 : 15);

    if (sim->tab == 0) {
        simulator_numeric_text(sim, 0, sim->level, "", value, sizeof(value));
        sim_field(v, 0, sim->selected == 0, "PLAYER LEVEL", value);
        simulator_numeric_text(sim, 1, sim->strength, "", value, sizeof(value));
        sim_field(v, 1, sim->selected == 1, "STRENGTH", value);
        simulator_numeric_text(sim, 2, sim->luck, "", value, sizeof(value));
        sim_field(v, 2, sim->selected == 2, "LUCK", value);
        sim_field(v, 3, sim->selected == 3, "WEAPON",
                  weapon_stats[sim->weapon].name);
        sim_field(v, 4, sim->selected == 4, "POWER WEAPON",
                  power_weapon_names[sim->power_weapon]);
        sim_field(v, 5, sim->selected == 5, "STRENGTH SPELL",
                  strength_spell_names[sim->strength_spell]);
        simulator_numeric_text(sim, 6, sim->permanent_enchant, "+",
                               value, sizeof(value));
        sim_field(v, 6, sim->selected == 6, "PERMANENT ENCHANT", value);
        simulator_numeric_text(sim, 7, sim->temporary_enchant, "+",
                               value, sizeof(value));
        sim_field(v, 7, sim->selected == 7, "TEMP ENCHANT SPELL", value);
        sim_field(v, 8, sim->selected == 8, "GAUNTLET",
                  gauntlet_names[sim->gauntlet]);
        sim_field(v, 9, sim->selected == 9, "MONSTER",
                  monster_types[sim->monster].name);
        simulator_numeric_text(sim, 10, sim->monster_level, "",
                               value, sizeof(value));
        snprintf(value + strlen(value), sizeof(value) - strlen(value),
                 " (RANGE %u-%u)", monster_types[sim->monster].minL,
                 monster_types[sim->monster].maxL);
        sim_field(v, 10, sim->selected == 10, "MONSTER LEVEL", value);
        draw_melee_results(v, sim);
    } else {
        simulator_numeric_text(sim, 0, sim->spell_level, "",
                               value, sizeof(value));
        sim_field(v, 0, sim->selected == 0, "PLAYER LEVEL", value);
        simulator_numeric_text(sim, 1, sim->wisdom, "", value, sizeof(value));
        sim_field(v, 1, sim->selected == 1, "WISDOM", value);
        simulator_numeric_text(sim, 2, sim->intelligence, "",
                               value, sizeof(value));
        sim_field(v, 2, sim->selected == 2, "INTELLIGENCE", value);
        sim_field(v, 3, sim->selected == 3, "OFFENSIVE SPELL",
                  spell_defs[sim->spell].name);
        sim_field(v, 4, sim->selected == 4, "MONSTER",
                  monster_types[sim->spell_monster].name);
        simulator_numeric_text(sim, 5, sim->spell_monster_level, "",
                               value, sizeof(value));
        snprintf(value + strlen(value), sizeof(value) - strlen(value),
                 " (RANGE %u-%u)",
                 monster_types[sim->spell_monster].minL,
                 monster_types[sim->spell_monster].maxL);
        sim_field(v, 5, sim->selected == 5, "MONSTER LEVEL", value);
        draw_spell_results(v, sim);
    }
    video_fill_rect(v, 12, 690, 492, 38, 1);
    video_draw_text(v, 65, 700, "ENTER / CLICK: SIMULATE 10,000", 4);
    video_draw_text(v, 18, 744,
        sim->input_mode ?
        "TYPE VALUE  BACKSPACE EDITS  ENTER APPLIES  ESC CANCELS" :
        "UP/DOWN SELECT  TYPE VALUE OR LEFT/RIGHT  PGUP/PGDN LARGE  TAB SWITCH",
        8);
    video_present(v);
}

static int simulator_selected_value(const BattleSimulator *sim) {
    if (sim->tab == 0) {
        switch (sim->selected) {
        case 0: return sim->level;
        case 1: return sim->strength;
        case 2: return sim->luck;
        case 3: return sim->weapon;
        case 4: return sim->power_weapon;
        case 5: return sim->strength_spell;
        case 6: return sim->permanent_enchant;
        case 7: return sim->temporary_enchant;
        case 8: return sim->gauntlet;
        case 9: return sim->monster;
        case 10: return sim->monster_level;
        default: return INT_MIN;
        }
    }
    switch (sim->selected) {
    case 0: return sim->spell_level;
    case 1: return sim->wisdom;
    case 2: return sim->intelligence;
    case 3: return sim->spell;
    case 4: return sim->spell_monster;
    case 5: return sim->spell_monster_level;
    default: return INT_MIN;
    }
}

static void adjust_simulator(BattleSimulator *sim, int direction,
                             int large_step) {
    int amount = direction * (large_step ? 100 : 1);
    int old_value = simulator_selected_value(sim);
    if (sim->tab == 0) {
        switch (sim->selected) {
        case 0: sim->level = sim_clamp(sim->level + amount, 1,
                                       MW_PLAYER_LEVEL_MAX); break;
        case 1: sim->strength = sim_clamp(sim->strength + amount, 1,
                                          MW_PLAYER_STAT_MAX); break;
        case 2: sim->luck = sim_clamp(sim->luck + amount, 0,
                                      MW_PLAYER_STAT_MAX); break;
        case 3: sim->weapon = sim_next_weapon(sim->weapon, direction,
                                              sim->enhanced); break;
        case 4: sim->power_weapon = sim_wrap(
                    sim->power_weapon + direction, sim->enhanced ? 7 : 4); break;
        case 5: sim->strength_spell = sim_wrap(sim->strength_spell + direction, 4); break;
        case 6: sim->permanent_enchant = sim_clamp(
                    sim->permanent_enchant + amount, 0, INT16_MAX); break;
        case 7: sim->temporary_enchant = sim_clamp(
                    sim->temporary_enchant + direction, 0, 5); break;
        case 8: sim->gauntlet = sim_wrap(sim->gauntlet + direction, 3); break;
        case 9: sim->monster = sim_wrap(sim->monster + direction,
                                        MONSTER_TYPE_COUNT); break;
        case 10: sim->monster_level = sim_clamp(
                     sim->monster_level + amount, 1, MAX_DUNGEON_FLOOR); break;
        }
        if (simulator_selected_value(sim) != old_value)
            run_melee_simulation(sim);
    } else {
        switch (sim->selected) {
        case 0: sim->spell_level = sim_clamp(sim->spell_level + amount, 1,
                                             MW_PLAYER_LEVEL_MAX); break;
        case 1: sim->wisdom = sim_clamp(sim->wisdom + amount, 1,
                                        MW_PLAYER_STAT_MAX); break;
        case 2: sim->intelligence = sim_clamp(sim->intelligence + amount, 1,
                                              MW_PLAYER_STAT_MAX); break;
        case 3: sim->spell = sim_wrap(sim->spell + direction,
                                      simulator_spell_count(sim)); break;
        case 4: sim->spell_monster = sim_wrap(sim->spell_monster + direction,
                                               MONSTER_TYPE_COUNT); break;
        case 5: sim->spell_monster_level = sim_clamp(
                    sim->spell_monster_level + amount, 1,
                    MAX_DUNGEON_FLOOR); break;
        }
        if (simulator_selected_value(sim) != old_value)
            run_spell_simulation(sim);
    }
}

static void simulator_switch_tab(BattleSimulator *sim, int tab) {
    simulator_cancel_input(sim);
    sim->tab = tab ? 1 : 0;
    sim->selected = 0;
}

static void simulator_run_current(BattleSimulator *sim) {
    if (sim->tab) run_spell_simulation(sim);
    else run_melee_simulation(sim);
}

void battle_simulator_run(Game *g, Character *player) {
    BattleSimulator sim;
    memset(&sim, 0, sizeof(sim));
    sim.enhanced = mw_experience_mode(player) == MW_EXPERIENCE_ENHANCED;
    sim.level = player->level ? player->level : 1;
    sim.strength = player->stat_str ? player->stat_str : 1;
    sim.luck = player->stat_luck;
    sim.weapon = player->equipped_weapon;
    if (sim.weapon >= 8 && sim.weapon <= 11) sim.weapon = 6;
    if (sim.weapon < 0 || sim.weapon >= WEAPON_STAT_COUNT) sim.weapon = 0;
    if (!sim.enhanced && sim.weapon >= 12) sim.weapon = 6;
    sim.permanent_enchant = mw_weapon_enchant(player, sim.weapon);
    if (sim.permanent_enchant < 0) sim.permanent_enchant = 0;
    sim.temporary_enchant = sim_clamp(mw_enchant_wpn_spell(player), 0, 5);
    sim.monster_level = g->cur_floor > 0 ? g->cur_floor : 10;
    sim.spell_level = sim.level;
    sim.wisdom = player->stat_wis ? player->stat_wis : 1;
    sim.intelligence = player->stat_int ? player->stat_int : 1;
    sim.spell = 5;
    sim.spell_monster_level = sim.monster_level;
    int adjacent = game_find_adjacent_monster(g);
    if (adjacent >= 0 && g->monster_layer >= 0) {
        MonsterRecord *m = &g->monster_map[g->monster_layer][adjacent];
        sim.monster = sim.spell_monster = m->type;
        sim.monster_level = sim.spell_monster_level = m->level;
    }
    run_melee_simulation(&sim);
    run_spell_simulation(&sim);

    while (!input_poll_quit(&g->input)) {
        draw_simulator(g, &sim);
        int key = input_getch(&g->input);
        if (sim.input_mode) {
            if (key == 0) (void)input_getch(&g->input);
            else if (simulator_edit_key(&sim, key) == 2)
                simulator_run_current(&sim);
            continue;
        }
        if (key == 0) {
            int scan = input_getch(&g->input);
            int count = sim.tab ? 6 : 11;
            if (scan == 0x48) sim.selected = sim_wrap(sim.selected - 1, count);
            else if (scan == 0x50) sim.selected = sim_wrap(sim.selected + 1, count);
            else if (scan == 0x4B) adjust_simulator(&sim, -1, 0);
            else if (scan == 0x4D) adjust_simulator(&sim, 1, 0);
            else if (scan == 0x49) adjust_simulator(&sim, 1, 1);
            else if (scan == 0x51) adjust_simulator(&sim, -1, 1);
            continue;
        }
        if (key == 0x1B || key == INPUT_BATTLE_SIMULATOR) break;
        if (key >= '0' && key <= '9' && simulator_edit_key(&sim, key))
            continue;
        if (key == '\t') { simulator_switch_tab(&sim, !sim.tab); continue; }
        if (key == INPUT_MOUSE_WHEEL_UP) { adjust_simulator(&sim, 1, 0); continue; }
        if (key == INPUT_MOUSE_WHEEL_DOWN) { adjust_simulator(&sim, -1, 0); continue; }
        if (key == INPUT_MOUSE_CLICK) {
            int x, y;
            if (!game_mouse_click_logical(g, &x, &y)) continue;
            if (y >= 44 && y < 80) {
                if (x >= 12 && x < 252) simulator_switch_tab(&sim, 0);
                else if (x >= 260 && x < 500) simulator_switch_tab(&sim, 1);
            } else if (x >= 12 && x < 504 && y >= 88 && y < 510) {
                int row = (y - 88) / 37;
                int count = sim.tab ? 6 : 11;
                if (row >= 0 && row < count) {
                    sim.selected = row;
                    adjust_simulator(&sim, x < 258 ? -1 : 1, 0);
                }
            } else if (x >= 12 && x < 504 && y >= 680 && y < 738) {
                simulator_run_current(&sim);
            }
            continue;
        }
        if (key == '\r' || key == '\n' || key == 'r' || key == 'R') {
            simulator_run_current(&sim);
        }
    }
    game_refresh_world_palette(g);
}

int battle_simulator_self_test(void) {
    BattleSimulator sim;
    int failures = 0;
    memset(&sim, 0, sizeof(sim));
    sim.level = 10; sim.strength = 20; sim.luck = 10;
    sim.weapon = 6; sim.monster = 0; sim.monster_level = 10;
    sim.strength_spell = 0; sim.gauntlet = 0;
    run_melee_simulation(&sim);
    if (sim.melee.average_hp != 51 || sim.melee.average_damage <= 0.0 ||
        sim.melee.hit_rate <= 0.0 || sim.melee.hit_rate > 1.0)
        failures++;
    sim.spell = 1; sim.spell_level = 10; sim.wisdom = 20;
    sim.intelligence = 20; sim.spell_monster = 0;
    sim.spell_monster_level = 10;
    run_spell_simulation(&sim);
    if (sim.spell_result.average_damage != 25.0 ||
        sim.spell_result.minimum_damage != 25 ||
        sim.spell_result.maximum_damage != 25)
        failures++;
    if (SIM_SPELL_COUNT != SIM_SPELL_CREATIONS_WRATH + 1 ||
        strcmp(spell_defs[SIM_SPELL_ABYSSAL_LANCE].name, "ABYSSAL LANCE") ||
        strcmp(spell_defs[SIM_SPELL_CREATIONS_WRATH].name,
               "CREATION'S WRATH"))
        failures++;
    sim.enhanced = 0;
    sim.tab = 1;
    sim.selected = 3;
    sim.spell = SIM_CLASSIC_SPELL_COUNT - 1;
    adjust_simulator(&sim, 1, 0);
    if (sim.spell != 0 || simulator_spell_count(&sim) != 16)
        failures++;
    sim.enhanced = 1;
    sim.spell = SIM_CLASSIC_SPELL_COUNT - 1;
    adjust_simulator(&sim, 1, 0);
    if (sim.spell != SIM_SPELL_ABYSSAL_LANCE ||
        simulator_spell_count(&sim) != 32)
        failures++;
    sim.spell = SIM_SPELL_ABYSSAL_LANCE;
    run_spell_simulation(&sim);
    if (sim.spell_result.average_damage != 750.0)
        failures++;
    sim.spell = SIM_SPELL_SOUL_REND;
    run_spell_simulation(&sim);
    if (sim.spell_result.success_rate != 1.0 ||
        sim.spell_result.average_damage != 51.0 ||
        sim.spell_result.casts_to_kill != 1)
        failures++;
    sim.spell_monster_level = 200;
    run_spell_simulation(&sim);
    if (sim.spell_result.success_rate != 0.0 ||
        sim.spell_result.average_damage != 0.0 ||
        sim.spell_result.casts_to_kill != 3)
        failures++;
    sim.spell_monster_level = 10;
    sim.spell = SIM_SPELL_LIFE_CONVERGENCE;
    run_spell_simulation(&sim);
    if (sim.spell_result.average_damage != 2005.0 ||
        sim.spell_result.casts_to_kill != 1)
        failures++;
    sim.spell = SIM_SPELL_COSMIC_IMPLOSION;
    run_spell_simulation(&sim);
    if (sim.spell_result.average_damage != 50030.0)
        failures++;
    sim.spell = SIM_SPELL_END_OF_AGES;
    run_spell_simulation(&sim);
    if (sim.spell_result.minimum_damage < 200000 ||
        sim.spell_result.maximum_damage > 400000 ||
        sim.spell_result.minimum_damage >= sim.spell_result.maximum_damage)
        failures++;
    sim.spell = SIM_SPELL_CREATIONS_WRATH;
    run_spell_simulation(&sim);
    if (sim.spell_result.average_damage != 100038.0)
        failures++;
    if (sim_next_weapon(7, 1, 1) != 12 ||
        sim_next_weapon(12, -1, 1) != 7 ||
        sim_next_weapon(7, 1, 0) != 0 ||
        combat_effective_damage_max(7, 3, 0) != 399 ||
        combat_effective_damage_max(19, 3, 0) != 399 ||
        combat_effective_damage_max(19, 3, 1) != 980 ||
        combat_effective_damage_max(12, 1, 1) != 129 ||
        combat_effective_damage_max(19, 5, 1) != 1300 ||
        combat_effective_damage_max(19, 6, 1) != 2000 ||
        combat_power_weapon_max_damage(4) != 800)
        failures++;
    sim.tab = 0;
    sim.selected = 6;
    sim.permanent_enchant = INT16_MAX;
    run_melee_simulation(&sim);
    u32 serial_at_cap = sim.simulation_serial;
    double damage_at_cap = sim.melee.average_damage;
    int score_at_cap = sim.melee.base_score;
    adjust_simulator(&sim, 1, 0);
    if (sim.permanent_enchant != INT16_MAX ||
        sim.simulation_serial != serial_at_cap ||
        sim.melee.average_damage != damage_at_cap ||
        sim.melee.base_score != score_at_cap)
        failures++;
    adjust_simulator(&sim, -1, 0);
    if (sim.permanent_enchant != INT16_MAX - 1 ||
        sim.simulation_serial != serial_at_cap + 1)
        failures++;
    sim.tab = 0;
    sim.selected = 0;
    simulator_edit_key(&sim, '9');
    simulator_edit_key(&sim, '9');
    simulator_edit_key(&sim, '9');
    simulator_edit_key(&sim, '9');
    if (simulator_edit_key(&sim, '\r') != 2 ||
        sim.level != MW_PLAYER_LEVEL_MAX || sim.input_mode)
        failures++;
    sim.selected = 7;
    simulator_edit_key(&sim, '8');
    simulator_edit_key(&sim, 8);
    simulator_edit_key(&sim, '4');
    if (simulator_edit_key(&sim, '\r') != 2 ||
        sim.temporary_enchant != 4)
        failures++;
    sim.selected = 3;
    if (simulator_edit_key(&sim, '5') != 0 || sim.input_mode)
        failures++;
    sim.tab = 1;
    sim.selected = 1;
    simulator_edit_key(&sim, '1');
    simulator_edit_key(&sim, '2');
    simulator_edit_key(&sim, '3');
    simulator_edit_key(&sim, '4');
    if (simulator_edit_key(&sim, '\n') != 2 || sim.wisdom != 1234)
        failures++;
    sim.selected = 5;
    simulator_edit_key(&sim, '0');
    if (simulator_edit_key(&sim, '\r') != 2 ||
        sim.spell_monster_level != 1)
        failures++;
    sim.selected = 2;
    int old_intelligence = sim.intelligence;
    simulator_edit_key(&sim, '7');
    if (simulator_edit_key(&sim, 0x1B) != 1 || sim.input_mode ||
        sim.intelligence != old_intelligence)
        failures++;
    return failures;
}
