# Black Pearl Engine - Build System
# Auto-detects MinGW on Linux (cross-compile) and Windows (native)

VERSION = 5.1.0

# Detect platform: default MINGW32 path (Windows) vs PATH-based (Linux)
ifeq ($(OS),Windows_NT)
MINGW32 = C:/ProgramData/mingw32/mingw32
CXX = $(MINGW32)/bin/i686-w64-mingw32-g++
export PATH := $(MINGW32)/bin;$(PATH)
RM = -del /f
RMFLAGS = 
else
CXX = i686-w64-mingw32-g++
RM = rm -f
RMFLAGS =
endif

CC = $(CXX)

ifdef DEBUG
CXXFLAGS = -O0 -g -static-libgcc -static-libstdc++ -D_DEBUG -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
else
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++ -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
endif

LDFLAGS = -Wl,--enable-stdcall-fixup -static -ld3dx9 -lwinmm -lpsapi -ld3d9 -ldinput8 -ldxguid -lgdi32 -ldwmapi -lole32
INCLUDES = -Isrc -Ilib/minhook/include -Ilib/imgui -Ilib/imgui/backends

# Core modules (always compiled)
SRCS = \
	src/d3d9_proxy.c \
	src/utils.c \
	src/cheats.c \
	src/menu.c \
	src/hooks.c \
	src/input.c \
	src/config_loader.c \
	src/uw.c \
	src/water.c

# v5 modules (uncomment as each file is created)
SRCS += src/noclip.c
SRCS += src/stud_magnet.c
SRCS += src/score_mult.c
SRCS += src/memory_browser.c
SRCS += src/free_camera.c
SRCS += src/infinite_ammo.c
SRCS += src/super_punch.c
SRCS += src/always_gold.c
SRCS += src/quick_combo.c
SRCS += src/mega_destruct.c
SRCS += src/infinite_cannonballs.c
SRCS += src/teleport.c
SRCS += src/squirrel_console.c
SRCS += src/save_editor.c
SRCS += src/level_editor.c
SRCS += src/native_cheats.c
SRCS += src/presets.c
SRCS += src/favorites.c
SRCS += src/hotkeys.c

MINHOOK_SRCS = lib/minhook/src/buffer.c lib/minhook/src/hook.c lib/minhook/src/trampoline.c lib/minhook/src/hde/hde32.c
IMGUI_SRCS = lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_tables.cpp lib/imgui/imgui_widgets.cpp lib/imgui/backends/imgui_impl_win32.cpp lib/imgui/backends/imgui_impl_dx9.cpp
ALL_SRCS = $(SRCS) $(MINHOOK_SRCS) $(IMGUI_SRCS)
OBJS = $(ALL_SRCS:.c=.o)
OBJS := $(OBJS:.cpp=.o)

.PHONY: all clean install run help

all: d3d9.dll
	@echo "Black Pearl Engine v$(VERSION) — build complete"

d3d9.dll: $(OBJS) src/d3d9_proxy.def
	$(CXX) -shared $(CXXFLAGS) -o $@ $(OBJS) src/d3d9_proxy.def $(LDFLAGS)
	@echo "  -> $@ ready"

.c.o:
	$(CC) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

clean:
	$(RM) $(RMFLAGS) $(OBJS) d3d9.dll bpe.log
	@echo "Black Pearl Engine v$(VERSION) — cleaned"

GAME_DIR = C:\GOG Games\LEGO Pirates of the Caribbean - The Video Game

install: d3d9.dll
	copy /y d3d9.dll "$(GAME_DIR)\"
	@echo "Black Pearl Engine v$(VERSION) — installed to $(GAME_DIR)"

run: install
	"$(GAME_DIR)\LEGOPirates.exe"

help:
	@echo "Black Pearl Engine v$(VERSION) — Build Targets"
	@echo "  all       Build d3d9.dll (release, default)"
	@echo "  clean     Remove build artifacts"
	@echo "  install   Copy d3d9.dll to game directory"
	@echo "  run       Install and launch LEGOPirates.exe"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1   Build with debug symbols (-O0 -g)"
	@echo ""
	@echo "Example:"
	@echo "  make               # release build"
	@echo "  make DEBUG=1       # debug build"
