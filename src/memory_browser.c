#include "memory_browser.h"
#include "utils.h"
#include "imgui.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Constants
 * ================================================================ */
#define MB_ROWS_PER_PAGE  256   /* number of rows per loaded page */
#define MB_BYTES_PER_ROW  16    /* bytes displayed per row        */
#define MB_VIEW_SIZE      (MB_ROWS_PER_PAGE * MB_BYTES_PER_ROW)  /* 4096 */
#define MB_MAX_SEARCH     512    /* max AOB search results         */
#define MB_MAX_FREEZE     32     /* max simultaneous freeze slots  */

/* ================================================================
 * Internal types
 * ================================================================ */
typedef struct {
    unsigned char bytes[256];
    unsigned char mask[256];  /* 1=exact match, 0=wildcard */
    int len;
} AobPattern;

typedef struct {
    DWORD addr;
    unsigned char val;
    bool enabled;
} FreezeSlot;

typedef struct {
    DWORD start;
    DWORD end;
    char name[16];
} SectionInfo;

/* ================================================================
 * State
 * ================================================================ */
static int          g_open            = 0;

/* --- Current view --- */
static DWORD        g_base            = 0x00000000;  /* first addr in view */
static unsigned char g_buf[MB_VIEW_SIZE];
static int          g_row_valid[MB_ROWS_PER_PAGE];  /* 1=ok, -1=err, 0=unread */

/* --- Sections cache --- */
static SectionInfo  g_secs[16];
static int          g_sec_count = 0;

/* --- Edit state --- */
static int          g_edit_active = 0;
static DWORD        g_edit_addr  = 0;
static char         g_edit_hex[8] = {0};

/* --- Search state --- */
static char         g_search_pat[256]   = {0};
static DWORD        g_search_results[MB_MAX_SEARCH];
static int          g_search_count      = 0;
static int          g_search_idx        = -1;  /* index into results */
static int          g_search_dirty      = 0;   /* re-scan needed */

/* --- Freeze state --- */
static FreezeSlot   g_freeze[MB_MAX_FREEZE];
static int          g_freeze_count = 0;

/* ================================================================
 * Section discovery (PE header parse)
 * ================================================================ */
static void discover_sections(void) {
    g_sec_count = 0;
    DWORD base = (DWORD)GetModuleHandleA(NULL);
    if (!base) return;

    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    PIMAGE_NT_HEADERS nt  = (PIMAGE_NT_HEADERS)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    PIMAGE_SECTION_HEADER sec = IMAGE_FIRST_SECTION(nt);
    int n = (nt->FileHeader.NumberOfSections > 16) ? 16 : nt->FileHeader.NumberOfSections;

    for (int i = 0; i < n; i++) {
        char sec_name[9] = {0};
        memcpy(sec_name, sec[i].Name, 8);
        g_secs[g_sec_count].start = base + sec[i].VirtualAddress;
        g_secs[g_sec_count].end   = g_secs[g_sec_count].start + sec[i].Misc.VirtualSize;
        memcpy(g_secs[g_sec_count].name, sec_name, 9);
        g_sec_count++;
    }
}

/* ================================================================
 * Address validation helpers
 * ================================================================ */
static int addr_is_readable(DWORD addr) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0)
        return 0;
    DWORD p = mbi.Protect;
    if ((p & PAGE_NOACCESS) || (p & PAGE_GUARD))
        return 0;
    if (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
             PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY))
        return 1;
    return 0;
}

static int addr_is_code(DWORD addr) {
    for (int i = 0; i < g_sec_count; i++)
        if (addr >= g_secs[i].start && addr < g_secs[i].end)
            return (strcmp(g_secs[i].name, ".text") == 0) ? 1 : 0;
    return 0;
}

static const char *addr_section_name(DWORD addr) {
    for (int i = 0; i < g_sec_count; i++)
        if (addr >= g_secs[i].start && addr < g_secs[i].end)
            return g_secs[i].name;
    return NULL;
}

/* ================================================================
 * Read a full page into the buffer
 * ================================================================ */
static void load_page(DWORD base) {
    memset(g_row_valid, 0, sizeof(g_row_valid));
    g_base = base;

    for (int r = 0; r < MB_ROWS_PER_PAGE; r++) {
        DWORD addr = base + (DWORD)(r * MB_BYTES_PER_ROW);
        if (addr_is_readable(addr)) {
            if (safe_read(addr, g_buf + r * MB_BYTES_PER_ROW, MB_BYTES_PER_ROW))
                g_row_valid[r] = 1;
            else
                g_row_valid[r] = -1;
        } else {
            g_row_valid[r] = -1;
        }
    }
}

/* ================================================================
 * AOB pattern parsing & scanning
 * ================================================================ */
static int parse_aob(const char *str, AobPattern *pat) {
    int pos = 0, written = 0;
    while (str[pos] && written < 256) {
        /* skip whitespace / dashes */
        while (str[pos] == ' ' || str[pos] == '-' || str[pos] == '\t')
            pos++;
        if (!str[pos]) break;

        if (str[pos] == '?' && str[pos+1] == '?') {
            pat->mask[written] = 0;
            pat->bytes[written] = 0;
            pos += 2;
            written++;
        } else if (str[pos] == '?') {
            pat->mask[written] = 0;
            pat->bytes[written] = 0;
            pos += 1;
            written++;
        } else if (isxdigit((unsigned char)str[pos]) && str[pos+1] &&
                   isxdigit((unsigned char)str[pos+1])) {
            char tmp[3] = {str[pos], str[pos+1], 0};
            pat->bytes[written] = (unsigned char)strtol(tmp, NULL, 16);
            pat->mask[written] = 1;
            pos += 2;
            written++;
        } else {
            /* skip invalid char */
            pos++;
        }
    }
    pat->len = written;
    return written > 0;
}

static int match_aob(const unsigned char *data, const AobPattern *pat) {
    for (int i = 0; i < pat->len; i++) {
        if (pat->mask[i] && data[i] != pat->bytes[i])
            return 0;
    }
    return 1;
}

static void run_search(const AobPattern *pat) {
    g_search_count = 0;
    g_search_idx   = -1;

    /* Scan all readable pages in the process address space via VirtualQuery */
    SYSTEM_INFO si;
    GetSystemInfo(&si);

    unsigned char *addr = (unsigned char *)si.lpMinimumApplicationAddress;
    while ((DWORD)addr < (DWORD)si.lpMaximumApplicationAddress) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) {
            addr += 0x10000;  /* skip 64 KB on failure */
            continue;
        }

        int is_r = (mbi.State == MEM_COMMIT) &&
                   !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                   (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                   PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                   PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY));

        if (is_r && mbi.RegionSize > 0) {
            unsigned char *scan_start = (unsigned char *)mbi.BaseAddress;
            SIZE_T scan_len = mbi.RegionSize;

            for (SIZE_T off = 0; off + pat->len <= scan_len; off++) {
                if (match_aob(scan_start + off, pat)) {
                    if (g_search_count < MB_MAX_SEARCH) {
                        g_search_results[g_search_count++] = (DWORD)(scan_start + off);
                    } else {
                        goto done;
                    }
                }
            }
        }

        addr = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
    }
done:
    if (g_search_count > 0) g_search_idx = 0;
    LOG("Memory Browser: AOB search found %d result(s)", g_search_count);
}

/* ================================================================
 * Public API
 * ================================================================ */
void mem_browser_init(void) {
    discover_sections();
    load_page(g_base);
    LOG("Memory Browser: initialized (%d sections found)", g_sec_count);
}

void mem_browser_toggle(void) {
    g_open = !g_open;
    if (g_open && g_sec_count == 0) {
        discover_sections();
    }
    LOG("Memory Browser: %s", g_open ? "opened" : "closed");
}

int mem_browser_is_open(void) {
    return g_open;
}

void mem_browser_shutdown(void) {
    g_sec_count = 0;
    g_open = 0;
    LOG("Memory Browser: shutdown");
}

/* ================================================================
 * Render helpers
 * ================================================================ */

/* ---- Controls bar ---- */
static void render_controls(void) {
    static char addr_buf[16] = {0};
    ImGui::Text("Address: "); ImGui::SameLine();

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputText("##addr", addr_buf, sizeof(addr_buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue))
    {
        DWORD target;
        if (sscanf(addr_buf, "%x", &target) == 1) {
            g_base = target & ~0xF;  /* align to 16-byte boundary */
            load_page(g_base);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        DWORD target;
        if (sscanf(addr_buf, "%x", &target) == 1) {
            g_base = target & ~0xF;
            load_page(g_base);
        }
    }

    ImGui::SameLine(); ImGui::Text(" | "); ImGui::SameLine();

    /* Section quick-jump buttons */
    for (int i = 0; i < g_sec_count; i++) {
        if (ImGui::Button(g_secs[i].name)) {
            g_base = g_secs[i].start & ~0xF;
            load_page(g_base);
        }
        ImGui::SameLine();
    }

    /* Page navigation */
    ImGui::SameLine(); ImGui::Text(" | "); ImGui::SameLine();
    if (ImGui::Button("<<") || ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        DWORD new_base = g_base - MB_VIEW_SIZE;
        if ((INT_PTR)new_base < 0) new_base = 0;
        load_page(new_base);
    }
    ImGui::SameLine();
    if (ImGui::Button(">>") || ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        load_page(g_base + MB_VIEW_SIZE);
    }

    /* Refresh button */
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        load_page(g_base);
    }
}

/* ---- Hex dump ---- */
static void render_hex_dump(void) {
    static int selected_row = -1;
    static int selected_col = -1;
    int set_edit = 0;

    /* Column headers */
    ImGui::Text("        "); ImGui::SameLine();  /* address column spacer */
    for (int c = 0; c < 16; c++) {
        ImGui::Text("%02X ", c);
        ImGui::SameLine();
    }
    ImGui::Text(" | ASCII");
    ImGui::Separator();

    ImGui::BeginChild("##hexdump", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 5),
                      ImGuiChildFlags_Borders);

    for (int r = 0; r < MB_ROWS_PER_PAGE; r++) {
        DWORD row_addr = g_base + (DWORD)(r * MB_BYTES_PER_ROW);

        /* --- Address column --- */
        ImU32 addr_col;
        const char *sn = addr_section_name(row_addr);
        if (sn && strcmp(sn, ".text") == 0)
            addr_col = IM_COL32(204, 163, 71, 255);      /* gold */
        else if (sn && strcmp(sn, ".rdata") == 0)
            addr_col = IM_COL32(140, 180, 220, 255);     /* steel blue */
        else if (sn && strcmp(sn, ".data") == 0)
            addr_col = IM_COL32(130, 200, 130, 255);     /* green */
        else
            addr_col = ImGui::GetColorU32(ImGuiCol_Text);

        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(addr_col), "%08X", row_addr);
        ImGui::SameLine();

        /* --- Hex bytes --- */
        for (int c = 0; c < 16; c++) {
            int idx = r * 16 + c;

            /* Determine color */
            ImU32 col;
            if (g_row_valid[r] < 0) {
                col = IM_COL32(120, 40, 40, 255);         /* dark red = invalid */
            } else if (addr_is_code(row_addr + c)) {
                col = IM_COL32(204, 163, 71, 255);        /* gold = code */
            } else {
                col = ImGui::GetColorU32(ImGuiCol_Text);  /* normal */
            }

            /* Highlight selected byte */
            if (selected_row == r && selected_col == c) {
                col = IM_COL32(30, 148, 135, 255);        /* teal highlight */
            }

            char hex[8];
            if (g_row_valid[r] > 0)
                snprintf(hex, sizeof(hex), "%02X", g_buf[idx]);
            else
                snprintf(hex, sizeof(hex), "??");

            ImGui::PushID(r * 16 + c);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", hex);
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                selected_row = r;
                selected_col = c;
                set_edit = 1;
            }
            ImGui::PopID();

            /* Spacing between groups of 4 for readability */
            if ((c & 3) == 3 && c < 15) {
                ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
            } else {
                ImGui::SameLine();
            }
        }

        /* --- ASCII sidebar --- */
        ImGui::Text(" | ");
        ImGui::SameLine();
        for (int c = 0; c < 16; c++) {
            int idx = r * 16 + c;
            if (g_row_valid[r] > 0 && isprint((unsigned char)g_buf[idx])) {
                char ascii[2] = { (char)g_buf[idx], 0 };
                ImGui::Text("%s", ascii);
            } else {
                ImGui::TextColored(ImVec4(120/255.0f, 120/255.0f, 120/255.0f, 1.0f), ".");
            }
            if (c < 15) ImGui::SameLine();
        }
    }

    ImGui::EndChild();

    /* ---- Edit popup ---- */
    if (set_edit && selected_row >= 0 && selected_col >= 0) {
        g_edit_addr = g_base + (DWORD)(selected_row * 16 + selected_col);
        g_edit_hex[0] = 0;
        g_edit_active = 1;
        ImGui::OpenPopup("Edit Byte");
    }

    if (ImGui::BeginPopup("Edit Byte")) {
        ImGui::Text("Edit byte at 0x%08X", g_edit_addr);
        ImGui::Separator();

        int idx = (int)(g_edit_addr - g_base);
        if (idx >= 0 && idx < MB_VIEW_SIZE) {
            unsigned char cur = g_buf[idx];
            ImGui::Text("Current: %02X (%d)", cur, cur);
        }

        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputText("##hexval", g_edit_hex, sizeof(g_edit_hex),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_AutoSelectAll);

        if (strlen(g_edit_hex) >= 2) {
            unsigned int val;
            if (sscanf(g_edit_hex, "%02x", &val) == 1) {
                unsigned char byte_val = (unsigned char)(val & 0xFF);
                if (safe_write(g_edit_addr, &byte_val, 1)) {
                    LOG("Memory Browser: wrote %02X to 0x%08X", byte_val, g_edit_addr);
                    load_page(g_base);  /* refresh view */
                }
            }
            g_edit_active = 0;
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g_edit_active = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

/* ---- Status bar ---- */
static void render_status(void) {
    ImGui::Separator();

    char buf[256];
    int valid_rows = 0;
    for (int r = 0; r < MB_ROWS_PER_PAGE; r++)
        if (g_row_valid[r] > 0) valid_rows++;

    const char *sn = addr_section_name(g_base);
    snprintf(buf, sizeof(buf),
             "Base: 0x%08X  |  Range: 0x%08X - 0x%08X  |  Valid: %d/%d rows  |  Section: %s",
             g_base, g_base, g_base + MB_VIEW_SIZE - 1,
             valid_rows, MB_ROWS_PER_PAGE, sn ? sn : "?");

    ImGui::TextColored(ImVec4(160/255.0f, 160/255.0f, 160/255.0f, 1.0f), "%s", buf);
}

/* ---- Search panel ---- */
static void render_search(void) {
    if (ImGui::CollapsingHeader("Pattern Search", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(250.0f);
        if (ImGui::InputText("##searchpat", g_search_pat, sizeof(g_search_pat),
                             ImGuiInputTextFlags_EnterReturnsTrue))
        {
            g_search_dirty = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Search")) {
            g_search_dirty = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) {
            g_search_count = 0;
            g_search_idx = -1;
            g_search_dirty = 0;
        }

        ImGui::SameLine();
        ImGui::Text("  (format: 48 65 ?? 6C 6F)");

        /* Run search if dirty */
        if (g_search_dirty && strlen(g_search_pat) > 0) {
            AobPattern pat;
            if (parse_aob(g_search_pat, &pat)) {
                run_search(&pat);
            }
            g_search_dirty = 0;
        }

        /* Results navigation */
        if (g_search_count > 0) {
            ImGui::Text("Results: %d", g_search_count);
            ImGui::SameLine();
            if (ImGui::Button("Prev")) {
                if (g_search_idx > 0) g_search_idx--;
            }
            ImGui::SameLine();
            if (ImGui::Button("Next")) {
                if (g_search_idx < g_search_count - 1) g_search_idx++;
            }
            ImGui::SameLine();
            if (g_search_idx >= 0 && g_search_idx < g_search_count) {
                DWORD result = g_search_results[g_search_idx];
                ImGui::Text(" [%d/%d] 0x%08X", g_search_idx + 1, g_search_count, result);
                ImGui::SameLine();
                if (ImGui::Button("Jump")) {
                    g_base = result & ~0xF;
                    load_page(g_base);
                }
            }
        } else if (g_search_pat[0] && !g_search_dirty) {
            ImGui::TextColored(ImVec4(200/255.0f, 120/255.0f, 120/255.0f, 1.0f), "No results");
        }
    }
}

/* ---- Freeze panel ---- */
static void render_freeze(void) {
    if (ImGui::CollapsingHeader("Freeze Values", ImGuiTreeNodeFlags_DefaultOpen)) {
        /* New freeze entry */
        static char freeze_addr[16] = {0};
        static char freeze_val[8] = {0};

        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputText("##fraddr", freeze_addr, sizeof(freeze_addr),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f);
        ImGui::InputText("##frval", freeze_val, sizeof(freeze_val),
                         ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::SameLine();
        if (ImGui::Button("Freeze") && g_freeze_count < MB_MAX_FREEZE) {
            DWORD addr;
            unsigned int val;
            if (sscanf(freeze_addr, "%x", &addr) == 1 &&
                sscanf(freeze_val, "%02x", &val) == 1)
            {
                g_freeze[g_freeze_count].addr    = addr;
                g_freeze[g_freeze_count].val     = (unsigned char)(val & 0xFF);
                g_freeze[g_freeze_count].enabled = true;
                g_freeze_count++;
                LOG("Memory Browser: freeze slot %d = 0x%08X = %02X",
                    g_freeze_count - 1, addr, (unsigned char)(val & 0xFF));
            }
        }

        /* Existing freeze entries */
        for (int i = 0; i < g_freeze_count; i++) {
            ImGui::PushID(i);
            char label[64];
            snprintf(label, sizeof(label), "0x%08X", g_freeze[i].addr);

            ImGui::Checkbox("##en", &g_freeze[i].enabled); ImGui::SameLine();
            ImGui::Text("%s = %02X", label, g_freeze[i].val); ImGui::SameLine();

            char del_label[32];
            snprintf(del_label, sizeof(del_label), "X##%d", i);
            if (ImGui::SmallButton("X")) {
                /* Remove by shifting */
                for (int j = i; j < g_freeze_count - 1; j++)
                    g_freeze[j] = g_freeze[j + 1];
                g_freeze_count--;
                i--;
            }
            ImGui::PopID();
        }
    }
}

/* ---- Apply freeze values (called every frame) ---- */
static void apply_freeze(void) {
    for (int i = 0; i < g_freeze_count; i++) {
        if (g_freeze[i].enabled) {
            safe_write(g_freeze[i].addr, &g_freeze[i].val, 1);
        }
    }
}

/* ================================================================
 * Main render function
 * ================================================================ */
void mem_browser_render(void) {
    if (!g_open) return;

    /* Lazy init if sections haven't been discovered */
    if (g_sec_count == 0) {
        discover_sections();
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory Browser", &open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    render_controls();
    ImGui::Separator();
    render_hex_dump();
    render_status();
    render_search();
    render_freeze();

    ImGui::End();

    /* Apply freeze values every frame while window is open */
    apply_freeze();

    if (!open) g_open = false;
}
