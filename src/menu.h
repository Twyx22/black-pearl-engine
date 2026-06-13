#ifndef BPE_MENU_H
#define BPE_MENU_H

#include <d3d9.h>

extern int g_menu_open;
extern int g_sw, g_sh;
extern IDirect3DDevice9 *g_dev;

typedef struct { const char *name; int type; void *val; int min, max; void (*action)(void); } Item;
typedef struct { const char *name; Item *items; int count; int (*on_select)(int sel); } Tab;

#define MENU_W 480
#define MENU_H 440
#define ITEM_H 24
#define TAB_COUNT 6

extern Tab tabs[];
extern int g_sel, g_tab;

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
