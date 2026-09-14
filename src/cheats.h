#ifndef BPE_CHEATS_H
#define BPE_CHEATS_H

#include <windows.h>

typedef struct {
    int invincible, infinite_studs, super_speed, super_jump, super_jump_scale, speed_mult;
    int time_freeze, noclip, show_debug, show_fps;
    int score_mult;
    int force_custom_studs;
    int custom_stud_value;
    int force_golden_bricks;
    int golden_brick_value;
    int uw_enabled;
    int uw_ratio;
    int char_scale;
    int char_scale_val;
    int remove_water;
    int underwater_breath;
    int reverse_damage;
    int one_heart;
    int no_knockback;
    int no_hit_reaction;
    int damage_response_only;
    int one_hit_kill;
    int stud_magnet;
    int quick_combo;
    int super_punch;
    int always_gold;
    int mega_destruct;
    int infinite_cannonballs;
    int free_camera;
    int fov;
    int teleport_slot_0;
    int teleport_slot_1;
    int teleport_slot_2;
    int teleport_slot_3;
    int teleport_slot_4;
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

void apply_char_scale(void);
void remove_char_scale(void);

void apply_breath_patch(void);
void remove_breath_patch(void);

void force_custom_studs(void);
void force_golden_bricks(void);

extern const char *g_entities[64];
extern int g_entity_count;
void scan_entities(void);

void apply_reverse_damage(void);
void remove_reverse_damage(void);

void apply_one_heart(void);
void remove_one_heart(void);

void apply_no_knockback(void);
void remove_no_knockback(void);

void apply_no_hit_reaction(void);
void remove_no_hit_reaction(void);

void apply_damage_response_only(void);
void remove_damage_response_only(void);

void apply_one_hit_kill(void);
void remove_one_hit_kill(void);

void stud_magnet_apply(void);
void stud_magnet_remove(void);

void quick_combo_apply(void);
void quick_combo_remove(void);
void super_punch_apply(void);
void super_punch_remove(void);
void always_gold_apply(void);
void always_gold_remove(void);
void mega_destruct_apply(void);
void mega_destruct_remove(void);
void infinite_cannonballs_apply(void);
void infinite_cannonballs_remove(void);
void free_camera_toggle(void);
void free_camera_init(void);
void free_camera_update(void);
int  free_camera_is_active(void);

void update_cheats(void);

#endif
