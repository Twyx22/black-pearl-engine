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

/* Address of the stud counter variable in memory */
#define STUD_COUNTER_ADDR       0x00E41868

/* Address where the stud display value is written */
#define STUD_DISPLAY_ADDR       0x0359B660

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

/* --- Entity Scanner --- */

/* Offset to the game's entity string table */
#define ENTITY_TABLE_OFFSET     0x00C8F400

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
