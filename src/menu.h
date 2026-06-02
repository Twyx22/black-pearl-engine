#ifndef BPE_MENU_H
#define BPE_MENU_H

#include <d3d9.h>
#include <d3dx9.h>

extern int g_menu_open;
extern int g_sw, g_sh;
extern IDirect3DDevice9 *g_dev;
extern ID3DXFont *g_font_small;

typedef struct { const char *name; int type; void *val; int min, max; void (*action)(void); } Item;
typedef struct { const char *name; Item *items; int count; } Tab;

#define MENU_W 480
#define MENU_H 440
#define ITEM_H 24
#define TAB_COUNT 5

extern Tab tabs[];
extern int g_sel, g_tab;

void menu_init_fonts(IDirect3DDevice9 *d);
void menu_release_fonts(void);
void menu_update_input(void);
void menu_render(IDirect3DDevice9 *d);
void menu_render_overlay(IDirect3DDevice9 *d);
void menu_render_debug(IDirect3DDevice9 *d);

int menu_should_be_ready(void);
int menu_get_frame(void);
void menu_increment_frame(void);

#endif
