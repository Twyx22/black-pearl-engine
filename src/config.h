#ifndef BPE_CONFIG_H
#define BPE_CONFIG_H

/* ================================================================
 * Black Pearl Engine - Configuration
 * 
 * All memory addresses and offsets for LEGO Pirates of the Caribbean.
 * Update these values if the game is patched or you're targeting
 * a different version.
 *
 * To find new addresses:
 *   - Use Cheat Engine to scan for changed values
 *   - Use a disassembler (IDA/Ghidra) to find instruction patterns
 *   - Check game modding communities for shared offsets
 * ================================================================ */

/* --- Studs --- */

/* Address where the stud display value is written (Cheat Engine found) */
#define STUD_DISPLAY_ADDR       0x0369B660

/* Instruction that subtracts studs: sub ebx,eax at _LEGOPirates.exe+2B1BC5 */
#define STUD_SUB_ADDR           0x006B1BC5
/* Also sbb esi,edx at _LEGOPirates.exe+2B1BC7 (2 bytes) */
#define STUD_SBB_ADDR           0x006B1BC7

/* --- Health --- */

/* Offset from entity base pointer to health value (DEC [reg+0xE26]) */
#define HEALTH_OFFSET           0xE26

/* Invincibility binary patches (offsets from module base, CE scripts verified) */
/* _LEGOPirates.exe+3D3B0B: DEC [ebp+00000E26] - enemy damage instruction */
#define DAMAGE_PATCH_OFFSET     0x3D3B0B
#define DAMAGE_PATCH_SIZE       6

/* _LEGOPirates.exe+4A3D35: MOV [esi+00000E26],cl - health reset on death */
#define DEATH_PATCH_OFFSET      0x4A3D35
#define DEATH_PATCH_SIZE        6

/* --- Underwater Breath --- */

/* _LEGOPirates.exe+37B910: DEC [esi+00000336] - oxygen timer decrement */
#define BREATH_PATCH_OFFSET    0x37B910
#define BREATH_PATCH_SIZE      6

/* --- Movement Speed --- */

/* Float addresses for walk/run speed constants */
#define SPEED_WALK_ADDR         0x00E24C50
#define SPEED_RUN_ADDR          0x00E24C54

/* Default speed values (used to restore when cheat is disabled) */
#define SPEED_WALK_DEFAULT      3.0f
#define SPEED_RUN_DEFAULT       3.5f

/* Speed multiplier when cheat is enabled */
#define SPEED_WALK_CHEAT        300.0f
#define SPEED_RUN_CHEAT         350.0f

/* --- Jump Physics --- */

/* The game calculates new Y position with:
     fld [esi+0xFB0]    (character scale)
     fmul [esi+0xD78]   (gravity factor)
     fadd [esi+0x28]    (current Y position)
     fstp [esi+0x2FC]   (store new Y position)

    super_jump dynamically scans the .text section for ALL
    FMUL [reg+0xD78] instances and redirects them to our own
    gravity float, acting as a Y-velocity modifier. */

/* --- Entity Scanner --- */

/* Offset to the game's entity string table */
#define ENTITY_TABLE_OFFSET     0x00C8F400

/* --- Golden Bricks --- */

/* Offset from module base for golden bricks count (Cheat Engine: _LEGOPirates.exe+B776E4) */
#define GOLDEN_BRICK_OFFSET     0xB776E4

/* Default golden bricks value when forced */
#define GOLDEN_BRICK_DEFAULT    85

/* --- Custom Studs --- */

/* Default custom stud value when forced */
#define CUSTOM_STUD_DEFAULT     999999

/* --- Native Cheat System --- */

/* Pointer table to cheat name strings in .rdata */
#define CHEAT_STRING_TABLE          0xC8F480

/* cheats available in the game, lowercase Squirrel-style names */
#define CHEAT_STR_SCOREX10          0xA6ED18
#define CHEAT_STR_SCOREX8           0xA6ED28
#define CHEAT_STR_SCOREX6           0xA6ED38
#define CHEAT_STR_SCOREX4           0xA6ED48
#define CHEAT_STR_SCOREX2           0xA6ED58
#define CHEAT_STR_INVINCIBILITY     0xA6ED68
#define CHEAT_STR_ALWAYS_SCORE_MULT 0xA6ED7C
#define CHEAT_STR_EXTRAHEARTS       0xA6ED98
#define CHEAT_STR_MINIKIT_DETECTOR  0xA6EDAC
#define CHEAT_STR_POWERBRICK_DET    0xA6EDC4
#define CHEAT_STR_REGENERATE_HEARTS 0xA6EDE0
#define CHEAT_STR_BREATHEUNDERWATER 0xA6EDF8
#define CHEAT_STR_STUD_MAGNET       0xA6EE10
#define CHEAT_STR_DOOMEDRECOVERY    0xA6EE24
#define CHEAT_STR_CHARACTER_STUDS   0xA6EE3C
#define CHEAT_STR_FASTBUILD         0xA6EE54
#define CHEAT_STR_EXTRATOGGLE       0xA6EE64
#define CHEAT_STR_FASTFIX           0xA6EE78
#define CHEAT_STR_FASTDIG           0xA6EE88
#define CHEAT_STR_DISGUISES         0xA6EE98

/* CHEAT_ strings (uppercase C++ style) */
#define CHEAT_CAPS_INVINCIBILITY    0xBFF580
#define CHEAT_CAPS_STUD_MAGNET      0xBFF6D4
#define CHEAT_CAPS_SCOREX10         0xBFF48C
#define CHEAT_CAPS_SCOREX8          0xBFF4CC
#define CHEAT_CAPS_SCOREX6          0xBFF500
#define CHEAT_CAPS_SCOREX4          0xBFF540
#define CHEAT_CAPS_SCOREX3          0xBFF560
#define CHEAT_CAPS_SCOREX2          0xBFF570
#define CHEAT_CAPS_ALWAYS_SCORE_MULT 0xBFF3E0
#define CHEAT_CAPS_EXTRAHEARTS       0xBFF3FC
#define CHEAT_CAPS_BREATHEUNDERWATER 0xBFF3A0
#define CHEAT_CAPS_REGENERATE_HEARTS 0xBFF528
#define CHEAT_CAPS_FASTBUILD         0xBFF550
#define CHEAT_CAPS_SUPERSLAP         0xBFF6FC
#define CHEAT_CAPS_SELFDESTRUCT     0xBF89EC
#define CHEAT_CAPS_EXPLODING_BLASTER 0xBFF674
#define CHEAT_CAPS_SUPERBLASTERS    0xBFF5CC
#define CHEAT_CAPS_ROCKETS          0xBFF4DC
#define CHEAT_CAPS_DISGUISES        0xBFF850
#define CHEAT_CAPS_EXTRATOGGLE      0xBFF86C
#define CHEAT_CAPS_FASTFIX          0xBFF46C
#define CHEAT_CAPS_FASTDIG          0xBFF47C
#define CHEAT_CAPS_INFINITE_TORPEDOS 0xBFF49C
#define CHEAT_CAPS_CHARACTER_STUDS   0xBFF6A8
#define CHEAT_CAPS_DOOMEDRECOVERY    0xBFF3B8
#define CHEAT_CAPS_STUD_MAGNET_UPPER 0xBFF6D4
#define CHEAT_CAPS_MINIKIT_DETECTOR  0xBFF510
#define CHEAT_CAPS_POWERBRICK_DET    0xBFF778
#define CHEAT_CAPS_GOLDBRICK_DETECTOR 0xBFF740

/* --- Collision / NoClip --- */

#define NO_CHARACTER_COLLISIONS_STR 0xBE0A38
#define NO_COLLISION_STR            0xBF6820
#define DISABLE_COLLISION_STR       0xBB9EE8
#define COLLISION_FUNC_ADDR         0x6AC280  // sub_6AC280 - collision handling

/* --- Ammo System --- */

#define SET_AMMO_STR                0xC00328
#define MAX_AMMO_STR                0xBEDC7C

/* --- Combo System --- */

#define EASY_COMBOS_STR             0xBF1AE4
#define ALWAYS_START_COMBOS_FIRST   0xBF1AF0
#define COMBO_STR                   0xBE1438

/* --- Punch / Damage --- */

#define PUNCH_ALWAYS_STUN_STR       0xBE333C
#define SWORD_COMBO1_STR            0xBE7010
#define SWORD_COMBO2_STR            0xBE7000

/* --- Cannonballs --- */

#define CANNONBALL2_STR             0xA745D8
#define CANNON_MK_STR_BASE          0xA745E4

/* --- Scoring --- */

#define SCOPE_MULTIPLY_STR          0xC03DBC
#define MULTIPLY_BY_STR             0xC03F0C
#define STATUS_GOLDBRICK_SCORE_STR  0xBE726C
#define DOUBLE_SCORE_STR            0xBF36A0
#define DOUBLE_SCORE_SOCK_STR       0xBF466C

/* --- Level Editor --- */

#define LEVEL_EDITOR_CTOR           0x0058EB50  // Constructor (already defined)
#define LEVEL_EDITOR_CLASS_STR      0xBC108C
#define LEVEL_EDITOR_RTTI_STR       0xDFEE0C
#define LEVELEDITOR_MANAGER_RTTI    0xDFEE28

/* Constructor offset for LevelEditor class (Ghidra: 0x0058eb50, offset = 0x58eb50 - 0x400000) */
#define LEVEL_EDITOR_CTOR_OFFSET    0x18EB50

/* --- DirectInput8 VTable Indices --- */

#define DI8_VT_CREATE_DEVICE    3

#define DIDEV_VT_GET_DEVICE_STATE   9
#define DIDEV_VT_ACQUIRE            7
#define DIDEV_VT_UNACQUIRE          8

/* --- Menu Settings --- */

#define MENU_DEFAULT_WIDTH      480
#define MENU_DEFAULT_HEIGHT     440

/* --- Damage System Mods --- */

/* Reverse Damage: DEC [EBP+0E26] -> INC [EBP+0E26] (1 byte at offset+1) */
#define REVERSE_DAMAGE_OFFSET       0x3D3B0B
#define REVERSE_DAMAGE_SIZE         6

/* One Heart Mode: patch health init values to 1 instead of 4/3 */
#define HEALTH_INIT_4_OFFSET        0x2BC97A
#define HEALTH_INIT_3_OFFSET        0x4877FC
#define HEALTH_INIT_SIZE            7

/* Knockback: NOP the CALL instruction (5 bytes) within TakeDamage */
#define KNOCKBACK_CALL_OFFSET       0x3D3A5F
#define KNOCKBACK_CALL_SIZE         5

/* HitReaction7: NOP the CALL instruction (5 bytes) */
#define HITREACT_CALL_OFFSET        0x052415
#define HITREACT_CALL_SIZE          5

#endif
