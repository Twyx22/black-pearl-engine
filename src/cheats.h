#ifndef BPE_CHEATS_H
#define BPE_CHEATS_H

#include <windows.h>

typedef struct {
    int invincible, infinite_studs, super_speed, super_jump;
    int moon_jump, time_freeze, noclip, show_debug, show_fps;
    int score_mult;
    int force_custom_studs;
    int custom_stud_value;
    int force_golden_bricks;
    int golden_brick_value;
    int uw_enabled;
    int uw_ratio;
} CheatsState;

extern CheatsState g_cheats;

void apply_stud_patch(void);
void remove_stud_patch(void);

void apply_health_patch(void);
void remove_health_patch(void);

void apply_super_speed(void);
void remove_super_speed(void);

void apply_super_jump(void);
void remove_super_jump(void);

void apply_moon_jump(void);
void remove_moon_jump(void);

void force_custom_studs(void);
void force_golden_bricks(void);

extern const char *g_entities[64];
extern int g_entity_count;
void scan_entities(void);

void update_cheats(void);

#endif
