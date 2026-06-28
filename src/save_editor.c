#include "save_editor.h"
#include "utils.h"
#include "imgui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <shlobj.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define SE_MAX_SAVES        32
#define SE_MAX_PATH         260
#define SE_HEADER_SIZE      128     /* max header we try to parse */

/* Known field offsets in LEGO Pirates save header (heuristic) */
#define SE_OFFSET_STUDS     0x04    /* 4 bytes, int32 */
#define SE_OFFSET_BRICKS    0x10    /* 4 bytes, int32 */
#define SE_OFFSET_LIVES     0x18    /* 4 bytes, int32 */
#define SE_OFFSET_LEVEL     0x20    /* 4 bytes, int32 */
#define SE_OFFSET_PERCENT   0x2C    /* 4 bytes, int32 — completion % */

/* ================================================================
 * Save file entry
 * ================================================================ */
typedef struct {
    char path[SE_MAX_PATH];         /* full path */
    char name[64];                  /* display name (filename) */
    unsigned char header[SE_HEADER_SIZE];
    int  header_valid;              /* 1 = successfully read header */
    int  dirty;                     /* 1 = has been modified */
    int  backup_made;               /* 1 = .bak created */
    /* Editable fields (cached copies) */
    int  studs;
    int  bricks;
    int  lives;
    int  current_level;
    int  completion_pct;
} SaveEntry;

/* ================================================================
 * State
 * ================================================================ */
static int       g_open = 0;
static SaveEntry g_saves[SE_MAX_SAVES];
static int       g_save_count = 0;
static int       g_selected   = -1;
static int       g_editing_field = -1;  /* which field index we are editing */

/* Field descriptors for the editor UI */
typedef struct {
    const char *label;
    int  offset;     /* byte offset in header */
    int  size;       /* bytes */
    int *value;      /* pointer to cached int */
} SaveField;

static SaveField g_fields[] = {
    {"Studs",           SE_OFFSET_STUDS,    4, NULL},
    {"Golden Bricks",   SE_OFFSET_BRICKS,   4, NULL},
    {"Lives",           SE_OFFSET_LIVES,    4, NULL},
    {"Current Level",   SE_OFFSET_LEVEL,    4, NULL},
    {"Completion %",    SE_OFFSET_PERCENT,  4, NULL},
};
#define FIELD_COUNT (sizeof(g_fields) / sizeof(g_fields[0]))

/* ================================================================
 * Helpers
 * ================================================================ */

/* Read int32 from a byte buffer at given offset (little-endian) */
static int read_le32(const unsigned char *buf, int off) {
    if (off < 0 || off + 4 > SE_HEADER_SIZE) return 0;
    return buf[off] | (buf[off+1] << 8) | (buf[off+2] << 16) | (buf[off+3] << 24);
}

/* Write int32 to a byte buffer at given offset (little-endian) */
static void write_le32(unsigned char *buf, int off, int val) {
    if (off < 0 || off + 4 > SE_HEADER_SIZE) return;
    buf[off]     = (unsigned char)(val & 0xFF);
    buf[off+1]   = (unsigned char)((val >> 8) & 0xFF);
    buf[off+2]   = (unsigned char)((val >> 16) & 0xFF);
    buf[off+3]   = (unsigned char)((val >> 24) & 0xFF);
}

/* Get the game's save directory */
static void get_save_dir(char *out, size_t sz) {
    out[0] = '\0';

    /* Try common LEGO Pirates save paths */
    const char *candidates[] = {
        "LEGO Pirates of the Caribbean\\SaveData",
        "LEGO Pirates\\SaveData",
        "LEGO Pirates of the Caribbean\\Save",
        "LEGO Pirates\\Save",
        NULL
    };

    /* Get %USERPROFILE%\Documents */
    char docs[SE_MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, docs))) {
        for (int i = 0; candidates[i]; i++) {
            snprintf(out, sz, "%s\\%s", docs, candidates[i]);
            if (_access(out, 0) == 0)
                return;
        }
    }

    /* Fallback: try relative to executable */
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (base) {
        char exe_path[SE_MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, SE_MAX_PATH);
        char *last_slash = strrchr(exe_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            snprintf(out, sz, "%s\\SaveData", exe_path);
            if (_access(out, 0) == 0)
                return;
            snprintf(out, sz, "%s\\Save", exe_path);
            if (_access(out, 0) == 0)
                return;
        }
    }

    out[0] = '\0';
}

/* ================================================================
 * Save file I/O
 * ================================================================ */

static int read_save_header(SaveEntry *entry) {
    FILE *f = fopen(entry->path, "rb");
    if (!f) return 0;

    size_t n = fread(entry->header, 1, SE_HEADER_SIZE, f);
    fclose(f);

    if (n < 32) {
        entry->header_valid = 0;
        return 0;
    }

    entry->header_valid = 1;
    entry->studs         = read_le32(entry->header, SE_OFFSET_STUDS);
    entry->bricks        = read_le32(entry->header, SE_OFFSET_BRICKS);
    entry->lives         = read_le32(entry->header, SE_OFFSET_LIVES);
    entry->current_level = read_le32(entry->header, SE_OFFSET_LEVEL);
    entry->completion_pct = read_le32(entry->header, SE_OFFSET_PERCENT);
    entry->dirty         = 0;
    entry->backup_made   = 0;

    return 1;
}

static int write_save(SaveEntry *entry) {
    if (!entry->header_valid) return 0;

    /* Write cached values back to header buffer */
    write_le32(entry->header, SE_OFFSET_STUDS,    entry->studs);
    write_le32(entry->header, SE_OFFSET_BRICKS,   entry->bricks);
    write_le32(entry->header, SE_OFFSET_LIVES,    entry->lives);
    write_le32(entry->header, SE_OFFSET_LEVEL,    entry->current_level);
    write_le32(entry->header, SE_OFFSET_PERCENT,  entry->completion_pct);

    /* Create .bak if not done yet */
    if (!entry->backup_made) {
        char bak_path[SE_MAX_PATH];
        snprintf(bak_path, sizeof(bak_path), "%s.bak", entry->path);
        if (CopyFileA(entry->path, bak_path, FALSE)) {
            entry->backup_made = 1;
            LOG("Save Editor: backup created: %s", bak_path);
        } else {
            LOG("Save Editor: failed to create backup for %s", entry->path);
        }
    }

    /* Write modified header back — we only rewrite the header portion
     * to avoid corrupting the full save. For full safety, users should
     * use the .bak. */
    FILE *f = fopen(entry->path, "r+b");
    if (!f) return 0;

    size_t n = fwrite(entry->header, 1, SE_HEADER_SIZE, f);
    fclose(f);

    if (n == SE_HEADER_SIZE) {
        entry->dirty = 0;
        LOG("Save Editor: wrote header to %s", entry->path);
        return 1;
    }
    return 0;
}

/* ================================================================
 * Browse for save files
 * ================================================================ */
void save_editor_browse(void) {
    g_save_count = 0;
    g_selected   = -1;

    char save_dir[SE_MAX_PATH];
    get_save_dir(save_dir, sizeof(save_dir));

    if (save_dir[0] == '\0') {
        LOG("Save Editor: could not find save directory");
        return;
    }

    /* Scan for *.sav files */
    char pattern[SE_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.sav", save_dir);

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        LOG("Save Editor: no .sav files found in %s", save_dir);
        return;
    }

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (g_save_count < SE_MAX_SAVES) {
                SaveEntry *e = &g_saves[g_save_count];
                snprintf(e->path, sizeof(e->path), "%s\\%s", save_dir, ffd.cFileName);
                snprintf(e->name, sizeof(e->name), "%s", ffd.cFileName);
                if (read_save_header(e)) {
                    g_save_count++;
                }
            }
        }
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);
    LOG("Save Editor: found %d save file(s) in %s", g_save_count, save_dir);
}

/* ================================================================
 * Public API
 * ================================================================ */

void save_editor_init(void) {
    LOG("Save Editor: initialized");
    /* Lazy browse on first render */
}

/* ================================================================
 * Render (ImGui window)
 * ================================================================ */
void save_editor_render(void) {
    if (!g_open) return;

    /* Lazy browse on first open */
    if (g_save_count == 0) {
        save_editor_browse();
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Save File Editor", &open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    /* --- Toolbar --- */
    if (ImGui::Button("Refresh")) {
        save_editor_browse();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        /* Open a folder picker */
        char folder[SE_MAX_PATH] = {0};
        BROWSEINFOA bi = {0};
        bi.lpszTitle = "Select save file directory";
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl) {
            if (SHGetPathFromIDListA(pidl, folder)) {
                /* Re-scan from selected folder */
                char pattern[SE_MAX_PATH];
                snprintf(pattern, sizeof(pattern), "%s\\*.sav", folder);
                g_save_count = 0;
                WIN32_FIND_DATAA ffd2;
                HANDLE hF2 = FindFirstFileA(pattern, &ffd2);
                if (hF2 != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            if (g_save_count < SE_MAX_SAVES) {
                                SaveEntry *e = &g_saves[g_save_count];
                                snprintf(e->path, sizeof(e->path), "%s\\%s", folder, ffd2.cFileName);
                                snprintf(e->name, sizeof(e->name), "%s", ffd2.cFileName);
                                if (read_save_header(e))
                                    g_save_count++;
                            }
                        }
                    } while (FindNextFileA(hF2, &ffd2));
                    FindClose(hF2);
                }
            }
            CoTaskMemFree(pidl);
        }
    }
    ImGui::SameLine();
    ImGui::Text("  |  Saves: %d", g_save_count);

    ImGui::Separator();

    /* --- Split layout: file list | editor --- */
    ImGui::BeginChild("##se_filelist", ImVec2(180, 0), ImGuiChildFlags_Borders);
    for (int i = 0; i < g_save_count; i++) {
        ImGui::PushID(i);
        ImU32 bg = (i == g_selected) ? IM_COL32(30, 148, 135, 80) : 0;
        if (bg) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, bg);

        if (ImGui::Selectable(g_saves[i].name, i == g_selected)) {
            g_selected = i;
            g_editing_field = -1;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    /* --- Editor panel --- */
    ImGui::BeginChild("##se_editor", ImVec2(0, 0), ImGuiChildFlags_Borders);

    if (g_selected < 0 || g_selected >= g_save_count) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "Select a save file from the list.");
    } else {
        SaveEntry *entry = &g_saves[g_selected];

        ImGui::Text("File: %s", entry->name);
        ImGui::Text("Path: %s", entry->path);
        if (entry->backup_made)
            ImGui::TextColored(ImVec4(0.18f, 0.75f, 0.18f, 1.0f), "Backup: %s.bak", entry->path);
        ImGui::Separator();

        /* Field editors */
        for (int i = 0; i < FIELD_COUNT; i++) {
            g_fields[i].value = NULL;  /* reset — we point per-entry */
        }

        /* Wire up field pointers to the selected entry */
        int vals[] = {
            entry->studs,
            entry->bricks,
            entry->lives,
            entry->current_level,
            entry->completion_pct,
        };

        for (int i = 0; i < FIELD_COUNT; i++) {
            ImGui::PushID(i);
            ImGui::Text("%s: ", g_fields[i].label); ImGui::SameLine();

            char label[32];
            snprintf(label, sizeof(label), "##val_%d", i);
            ImGui::SetNextItemWidth(100.0f);

            int *pval;
            switch (i) {
                case 0: pval = &entry->studs; break;
                case 1: pval = &entry->bricks; break;
                case 2: pval = &entry->lives; break;
                case 3: pval = &entry->current_level; break;
                case 4: pval = &entry->completion_pct; break;
                default: pval = NULL; break;
            }

            if (pval) {
                if (ImGui::InputInt(label, pval, 0, 0)) {
                    entry->dirty = 1;
                }
            }

            ImGui::PopID();
        }

        ImGui::Separator();

        /* Save button */
        if (entry->dirty) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "Modified — click Save to apply changes.");
            if (ImGui::Button("Save Changes")) {
                if (write_save(entry)) {
                    LOG("Save Editor: saved %s", entry->path);
                } else {
                    LOG("Save Editor: failed to save %s", entry->path);
                }
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Make Backup")) {
            char bak_path[SE_MAX_PATH];
            snprintf(bak_path, sizeof(bak_path), "%s.bak", entry->path);
            if (CopyFileA(entry->path, bak_path, FALSE)) {
                entry->backup_made = 1;
                LOG("Save Editor: backup created: %s", bak_path);
            }
        }

        /* Raw hex view */
        if (ImGui::CollapsingHeader("Raw Header (hex)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("##se_hex", ImVec2(0, 120), ImGuiChildFlags_Borders);
            for (int i = 0; i < SE_HEADER_SIZE; i += 16) {
                char addr_str[16];
                snprintf(addr_str, sizeof(addr_str), "%04X: ", i);
                ImGui::TextUnformatted(addr_str); ImGui::SameLine();

                for (int j = 0; j < 16 && (i + j) < SE_HEADER_SIZE; j++) {
                    ImU32 col = IM_COL32(200, 200, 200, 255);
                    /* Highlight known field offsets */
                    for (int k = 0; k < FIELD_COUNT; k++) {
                        if ((i + j) >= g_fields[k].offset &&
                            (i + j) < g_fields[k].offset + g_fields[k].size)
                        {
                            col = IM_COL32(204, 163, 71, 255);
                            break;
                        }
                    }
                    char hex[8];
                    snprintf(hex, sizeof(hex), "%02X ", entry->header[i + j]);
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", hex);
                    ImGui::SameLine();
                }

                /* ASCII sidebar */
                ImGui::Text(" | ");
                ImGui::SameLine();
                for (int j = 0; j < 16 && (i + j) < SE_HEADER_SIZE; j++) {
                    unsigned char c = entry->header[i + j];
                    if (c >= 32 && c < 127) {
                        char ascii[2] = {(char)c, 0};
                        ImGui::Text("%s", ascii);
                    } else {
                        ImGui::TextColored(ImVec4(120/255.0f, 120/255.0f, 120/255.0f, 1.0f), ".");
                    }
                    if (j < 15) ImGui::SameLine();
                }
                ImGui::Text("");
            }
            ImGui::EndChild();
        }
    }

    ImGui::EndChild();
    ImGui::End();

    if (!open) g_open = false;
}

/* Module lifecycle helpers */
int save_editor_is_open(void) {
    return g_open;
}

void save_editor_toggle(void) {
    g_open = !g_open;
    if (g_open && g_save_count == 0) {
        save_editor_browse();
    }
    LOG("Save Editor: %s", g_open ? "opened" : "closed");
}
