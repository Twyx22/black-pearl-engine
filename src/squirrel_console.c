#include "squirrel_console.h"
#include "utils.h"
#include "imgui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define SQCON_MAX_LINES      512     /* scrollback ring-buffer size */
#define SQCON_MAX_LINE_LEN   256     /* max length per line           */
#define SQCON_HISTORY_SIZE   64      /* command history ring-buffer   */
#define SQCON_INPUT_MAX      256     /* max input length              */

/* ================================================================
 * Scrollback ring-buffer
 * ================================================================ */
static char g_lines[SQCON_MAX_LINES][SQCON_MAX_LINE_LEN];
static int  g_line_count = 0;
static int  g_line_head  = 0;   /* next write index */

static void push_line(const char *text) {
    strncpy(g_lines[g_line_head], text, SQCON_MAX_LINE_LEN - 1);
    g_lines[g_line_head][SQCON_MAX_LINE_LEN - 1] = '\0';
    g_line_head = (g_line_head + 1) % SQCON_MAX_LINES;
    if (g_line_count < SQCON_MAX_LINES)
        g_line_count++;
}

static const char *get_line(int i) {
    /* i = 0 is newest */
    if (i < 0 || i >= g_line_count) return NULL;
    int idx = (g_line_head - 1 - i + SQCON_MAX_LINES) % SQCON_MAX_LINES;
    return g_lines[idx];
}

/* ================================================================
 * Command history ring-buffer
 * ================================================================ */
static char g_history[SQCON_HISTORY_SIZE][SQCON_INPUT_MAX];
static int  g_history_count = 0;
static int  g_history_head  = 0;   /* next write index */
static int  g_history_pos   = -1;  /* -1 = not browsing history */

static void push_history(const char *cmd) {
    /* Skip blank lines and duplicates */
    if (cmd[0] == '\0') return;
    if (g_history_count > 0) {
        int last = (g_history_head - 1 + SQCON_HISTORY_SIZE) % SQCON_HISTORY_SIZE;
        if (strcmp(g_history[last], cmd) == 0) return;
    }
    strncpy(g_history[g_history_head], cmd, SQCON_INPUT_MAX - 1);
    g_history[g_history_head][SQCON_INPUT_MAX - 1] = '\0';
    g_history_head = (g_history_head + 1) % SQCON_HISTORY_SIZE;
    if (g_history_count < SQCON_HISTORY_SIZE)
        g_history_count++;
    g_history_pos = -1;
}

static const char *history_nth(int n) {
    if (n < 0 || n >= g_history_count) return NULL;
    if (n >= SQCON_HISTORY_SIZE) return NULL;
    int idx = (g_history_head - 1 - n + SQCON_HISTORY_SIZE) % SQCON_HISTORY_SIZE;
    return g_history[idx];
}

/* ================================================================
 * Squirrel VM function pointers (discovery targets)
 * ================================================================ */
/* We try to locate the game's sq_call / sq_compile entry points.
 * If found we can call them; otherwise the console works in
 * "log-only" mode — commands are logged but not executed. */

typedef int (__fastcall *SqCall_fn)(void *vm, int nargs, int retval, int null);
static SqCall_fn g_sq_call = NULL;

typedef int (__fastcall *SqCompile_fn)(void *vm, const char *src, int size,
                                        const char *name, int null);
static SqCompile_fn g_sq_compile = NULL;

/* Generic VM pointer — found by scanning for known patterns */
static DWORD g_sq_vm_ptr = 0;
static int  g_sq_found   = 0;   /* 1 = VM entry points discovered */

/* ================================================================
 * Import-based scan for Squirrel functions
 * ================================================================ */
static void find_squirrel_functions(void) {
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;

    /* 1. Try to find sq_call via import table */
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    PIMAGE_NT_HEADERS nt  = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)
        (base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    /* Look for "squirrel.dll" import module, find sq_call and sq_compile */
    for (; imp->Name && imp->FirstThunk; imp++) {
        const char *mod = (const char *)(base + imp->Name);
        if (strstr(mod, "squirrel") || strstr(mod, "sq") || strstr(mod, "Squirrel")) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)(base + imp->FirstThunk);
            PIMAGE_THUNK_DATA name_thunk = (PIMAGE_THUNK_DATA)(base + imp->OriginalFirstThunk);
            for (int i = 0; thunk[i].u1.Function && name_thunk[i].u1.Function; i++) {
                if (!(name_thunk[i].u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME ibn = (PIMAGE_IMPORT_BY_NAME)
                        (base + name_thunk[i].u1.AddressOfData);
                    const char *fname = (const char *)ibn->Name;
                    if (strcmp(fname, "sq_call") == 0) {
                        g_sq_call = (SqCall_fn)thunk[i].u1.Function;
                        LOG("Squirrel Console: sq_call found via imports at %p", g_sq_call);
                    }
                    if (strcmp(fname, "sq_compile") == 0) {
                        g_sq_compile = (SqCompile_fn)thunk[i].u1.Function;
                        LOG("Squirrel Console: sq_compile found via imports at %p", g_sq_compile);
                    }
                }
            }
        }
    }

    /* 2. Pattern scan for sq_call wrapper in .text if import didn't work */
    if (!g_sq_call) {
        /* Common pattern: sq_call thunk in .text from delay-load or static link */
        /* Pattern: call to sq_call — we scan for push args then call pattern */
        DWORD text_start = 0, text_size = 0;
        if (find_text_section(&text_start, &text_size)) {
            /* Look for sq_pushroottable (a distinctive Squirrel API call).
               Pattern for x86 "push 0; push reg; call sq_pushroottable": 6A 00 50/51/52 E8 */
            /* This is heuristic — different builds have different patterns */
            /* We just flag that we searched */
            LOG("Squirrel Console: sq_call not in imports, pattern scan needed");
        }
    }

    if (g_sq_call) {
        push_line("Squirrel VM found — sq_call available");
        g_sq_found = 1;
    } else {
        push_line("Squirrel VM not found — console in log-only mode");
        g_sq_found = 0;
    }
}

/* ================================================================
 * Console state
 * ================================================================ */
static int  g_console_open = 0;
static char g_input_buf[SQCON_INPUT_MAX] = {0};

/* ================================================================
 * Built-in commands (log-only fallback)
 * ================================================================ */
static int handle_builtin(const char *cmd) {
    if (_strnicmp(cmd, "help", 4) == 0) {
        push_line("Built-in commands:");
        push_line("  help              — this help");
        push_line("  clear             — clear scrollback");
        push_line("  echo <text>       — print text");
        push_line("  log <text>        — write to bpe.log");
        push_line("All other commands are sent to Squirrel VM (if available).");
        return 1;
    }
    if (_strnicmp(cmd, "clear", 5) == 0) {
        g_line_count = 0;
        g_line_head = 0;
        return 1;
    }
    if (_strnicmp(cmd, "echo ", 5) == 0) {
        push_line(cmd + 5);
        return 1;
    }
    if (_strnicmp(cmd, "log ", 4) == 0) {
        LOG("Squirrel Console: %s", cmd + 4);
        push_line("Logged to bpe.log");
        return 1;
    }
    return 0;  /* not a built-in */
}

/* ================================================================
 * Public API
 * ================================================================ */

void squirrel_console_init(void) {
    push_line("Black Pearl Engine — Squirrel Console");
    push_line("Type 'help' for built-in commands.");
    push_line("---");
    find_squirrel_functions();
    LOG("Squirrel Console: initialized (sq_found=%d)", g_sq_found);
}

void squirrel_console_execute(const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    /* Echo command */
    char echo_buf[SQCON_MAX_LINE_LEN];
    snprintf(echo_buf, sizeof(echo_buf), "> %s", cmd);
    push_line(echo_buf);
    push_history(cmd);

    /* Try built-in commands first */
    if (handle_builtin(cmd))
        return;

    /* If we have sq_call and a VM pointer, try to execute */
    if (g_sq_call && g_sq_vm_ptr) {
        push_line("[Squirrel VM] Executing... (stub)");
        /* TODO: real sq_call invocation once VM pointer is known */
        LOG("Squirrel Console: would call sq_call with: %s", cmd);
    } else {
        /* Log-only mode */
        push_line("[Log-only] Command logged to bpe.log");
        LOG("Squirrel Console: %s", cmd);
    }
}

/* ================================================================
 * Render (ImGui window)
 * ================================================================ */
void squirrel_console_render(void) {
    if (!g_console_open) return;

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(520, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Squirrel Console", &open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    /* --- Scrollback region --- */
    ImGui::BeginChild("##sq_scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8),
                      ImGuiChildFlags_Borders);

    for (int i = g_line_count - 1; i >= 0; i--) {
        const char *line = get_line(i);
        if (!line) continue;

        /* Color the prompt line differently */
        if (line[0] == '>') {
            ImGui::TextColored(ImVec4(0.18f, 0.75f, 0.68f, 1.0f), "%s", line);
        } else if (strstr(line, "Error") || strstr(line, "error")) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line);
        } else {
            ImGui::TextUnformatted(line);
        }
    }

    /* Auto-scroll to bottom */
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();

    /* --- Input line --- */
    ImGui::Separator();

    bool reclaim_focus = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##sq_input", g_input_buf, sizeof(g_input_buf),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                         ImGuiInputTextFlags_CallbackHistory,
                         NULL))
    {
        if (g_input_buf[0] != '\0') {
            squirrel_console_execute(g_input_buf);
            g_input_buf[0] = '\0';
            reclaim_focus = true;
        }
    }

    /* Keep focus on input */
    if (reclaim_focus)
        ImGui::SetKeyboardFocusHere(-1);

    ImGui::End();

    if (!open) g_console_open = 0;
}

/* Module lifecycle helpers (called from menu/hooks when needed) */
int squirrel_console_is_open(void) {
    return g_console_open;
}

void squirrel_console_toggle(void) {
    g_console_open = !g_console_open;
    LOG("Squirrel Console: %s", g_console_open ? "opened" : "closed");
}
