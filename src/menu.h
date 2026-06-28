#ifndef BPE_MENU_H
#define BPE_MENU_H

#include <d3d9.h>
#include <windows.h>

extern int g_menu_open;
extern int g_sw, g_sh;
extern IDirect3DDevice9 *g_dev;

/* -----------------------------------------------------------------
 * Item: a single entry in a tab's item list
 *
 * type: 0=toggle, 1=value edit, 2=action button, 3=separator,
 *       4=hotkey_bind indicator
 *
 * favorite: 1 if the user has marked this item as a favourite
 * hotkey_vk: VK code bound to this item (0=unbound)
 * tag: arbitrary integer for custom UI actions
 * ----------------------------------------------------------------- */
typedef struct {
    const char *name;
    int type;
    void *val;           /* pointer to the int backing (for toggles/values) */
    int min, max;
    void (*action)(void);
    int favorite;        /* 1 if favourited */
    int hotkey_vk;       /* bound VK code (0 = unbound) */
    int tag;             /* arbitrary tag for custom UI handling */
} Item;

typedef struct {
    const char *name;
    Item *items;
    int count;
    int (*on_select)(int sel);
    int (*on_draw_extra)(int sel, int draw_y, int mx, int my); /* optional extra rendering */
} Tab;

#define MENU_W 480
#define MENU_H 480
#define ITEM_H 24
#define TAB_COUNT 12

extern Tab tabs[];
extern int g_sel, g_tab;

/* Get an Item pointer by tab/item indices (validates bounds) */
Item* menu_get_item(int tab, int item_idx);

void menu_init_imgui(IDirect3DDevice9 *d, HWND hwnd);
void menu_release_imgui(void);
void menu_toggle(void);
void menu_update_input(void);
void menu_render(void);
void menu_render_overlay(void);
void menu_render_debug(void);

int menu_should_be_ready(void);
int menu_get_frame(void);
void menu_increment_frame(void);

#endif
extern int g_prev_f3;
