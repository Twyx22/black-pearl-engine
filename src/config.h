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

/* Offset from entity base pointer to health value (DEC [reg+0x864]) */
#define HEALTH_OFFSET           0x864

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

/* Instruction offsets (from module base) for the vertical velocity calculation.
   Pattern: fld [esi+0xFB0]; fmul [esi+0xD78]; fadd [esi+0x28]; fstp [esi+0x2FC]

   First instance (with intervening mov): _LEGOPirates.exe+3BC67B
   Second instance (pure math): _LEGOPirates.exe+3BC818

   We redirect the fmul to our own float for moon jump,
   and the fld to our own float for super jump. */

#define JUMP_FLD_OFFSET1        0x3BC67B   /* fld [esi+0xFB0] - first instance */
#define JUMP_FLD_OFFSET2        0x3BC818   /* fld [esi+0xFB0] - second instance */
#define JUMP_FMUL_OFFSET1       0x3BC688   /* fmul [esi+0xD78] - first instance */
#define JUMP_FMUL_OFFSET2       0x3BC81E   /* fmul [esi+0xD78] - second instance */

/* Moon Jump: low gravity multiplier */
#define GRAVITY_MULT_MOON       0.02f

/* Super Jump: high jump force value */
#define JUMP_FORCE_SUPER        100.0f

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

/* --- DirectInput8 VTable Indices --- */

#define DI8_VT_CREATE_DEVICE    3

#define DIDEV_VT_GET_DEVICE_STATE   9
#define DIDEV_VT_ACQUIRE            7
#define DIDEV_VT_UNACQUIRE          8

/* --- Menu Settings --- */

#define MENU_DEFAULT_WIDTH      480
#define MENU_DEFAULT_HEIGHT     440

#endif
